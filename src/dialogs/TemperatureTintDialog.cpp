// =============================================================================
// TemperatureTintDialog.cpp
// Implements colour temperature and tint adjustment via per-channel gain
// multiplication. Supports live preview, shadow/highlight protection,
// mask blending, and undo on apply.
// =============================================================================

#include "TemperatureTintDialog.h"
#include "MainWindowCallbacks.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include "../ImageViewer.h"

#include <algorithm>
#include <cmath>

// -----------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------
TemperatureTintDialog::TemperatureTintDialog(QWidget* parent, ImageViewer* viewer)
    : DialogBase(parent, tr("Temperature / Tint"), 420, 120)
    , m_viewer(nullptr)
    , m_buffer(nullptr)
{
    setupUI();
    if (viewer) {
        setViewer(viewer);
    }
}

// -----------------------------------------------------------------------------
// Destructor  --  Restores the original buffer if the dialog is destroyed
// without applying.
// -----------------------------------------------------------------------------
TemperatureTintDialog::~TemperatureTintDialog()
{
    if (!m_applied && m_viewer && m_originalBuffer.isValid()) {
        m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
    }
}

// -----------------------------------------------------------------------------
// setupUI  --  Creates temperature and tint sliders with min/max labels,
// preview and protection checkboxes, and action buttons.
// -----------------------------------------------------------------------------
void TemperatureTintDialog::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(15);

    QGridLayout* grid = new QGridLayout();
    grid->setSpacing(10);

    // Helper lambda: constructs a labelled slider row with min/max value labels
    auto addSlider = [&](int row, const QString& label,
                         QSlider*& sld, QLabel*& val,
                         int min, int max, int def) {
        // Parameter label
        grid->addWidget(new QLabel(label), row, 0);

        // Minimum value label
        QLabel* minLbl = new QLabel(QString::number(min));
        minLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        minLbl->setFixedWidth(30);
        grid->addWidget(minLbl, row, 1);

        // Slider
        sld = new QSlider(Qt::Horizontal);
        sld->setRange(min, max);
        sld->setValue(def);
        grid->addWidget(sld, row, 2);

        // Maximum value label
        QLabel* maxLbl = new QLabel(QString::number(max));
        maxLbl->setFixedWidth(30);
        grid->addWidget(maxLbl, row, 3);

        // Current value label
        val = new QLabel(QString::number(def));
        val->setFixedWidth(40);
        val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(val, row, 4);

        connect(sld, &QSlider::valueChanged,
                this, &TemperatureTintDialog::onSliderChanged);
        connect(sld, &QSlider::valueChanged,
                this, &TemperatureTintDialog::triggerPreview);
    };

    addSlider(0, tr("Temperature:"), m_sldTemperature, m_valTemperature, -100, 100, 0);
    addSlider(1, tr("Tint:"),        m_sldTint,        m_valTint,        -100, 100, 0);

    mainLayout->addLayout(grid);

    // --- Buttons and options ---
    QHBoxLayout* btnLayout = new QHBoxLayout();

    m_chkPreview = new QCheckBox(tr("Preview"));
    m_chkPreview->setChecked(true);
    connect(m_chkPreview, &QCheckBox::toggled, this, [this](bool on) {
        if (on) {
            triggerPreview();
        } else if (m_viewer && m_originalBuffer.isValid()) {
            m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
        }
    });

    m_chkProtect = new QCheckBox(tr("Protect Shadows/Highlights"));
    m_chkProtect->setChecked(true);
    m_chkProtect->setToolTip(
        tr("When enabled, near-black and near-white pixels are protected from colour casts"));
    connect(m_chkProtect, &QCheckBox::toggled,
            this, &TemperatureTintDialog::triggerPreview);

    QPushButton* btnReset  = new QPushButton(tr("Reset"));
    QPushButton* btnApply  = new QPushButton(tr("Apply"));
    btnApply->setDefault(true);
    QPushButton* btnCancel = new QPushButton(tr("Cancel"));

    btnLayout->addWidget(m_chkPreview);
    btnLayout->addWidget(m_chkProtect);
    btnLayout->addStretch();
    btnLayout->addWidget(btnReset);
    btnLayout->addWidget(btnCancel);
    btnLayout->addWidget(btnApply);

    mainLayout->addLayout(btnLayout);

    connect(btnReset,  &QPushButton::clicked, this, &TemperatureTintDialog::resetState);
    connect(btnApply,  &QPushButton::clicked, this, &TemperatureTintDialog::handleApply);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
}

// -----------------------------------------------------------------------------
// onSliderChanged  --  Updates the value labels to reflect current slider state.
// -----------------------------------------------------------------------------
void TemperatureTintDialog::onSliderChanged()
{
    m_valTemperature->setText(QString::number(m_sldTemperature->value()));
    m_valTint->setText(QString::number(m_sldTint->value()));
}

// -----------------------------------------------------------------------------
// computeGain  --  Derives per-channel RGB gain multipliers from the current
// temperature and tint slider positions.
//
// Temperature: positive = warm (boost red, reduce blue)
//              negative = cool (reduce red, boost blue)
// Tint:        positive = magenta (reduce green, slight red/blue boost)
//              negative = green   (boost green, slight red/blue reduction)
// -----------------------------------------------------------------------------
void TemperatureTintDialog::computeGain(float& r, float& g, float& b) const
{
    float T  = m_sldTemperature->value() / 100.0f;  // Normalized: -1.0 to +1.0
    float Ti = m_sldTint->value()        / 100.0f;  // Normalized: -1.0 to +1.0

    r = 1.0f + T  * 0.5f;   // Range: 0.5 to 1.5
    b = 1.0f - T  * 0.5f;   // Range: 0.5 to 1.5
    g = 1.0f - Ti * 0.5f;   // Range: 0.5 to 1.5

    // Slight red/blue boost in the magenta (positive tint) direction
    r *= 1.0f + Ti * 0.15f;
    b *= 1.0f + Ti * 0.15f;

    // Clamp to prevent division-by-zero or sign inversion
    r = std::max(0.01f, r);
    g = std::max(0.01f, g);
    b = std::max(0.01f, b);
}

// -----------------------------------------------------------------------------
// triggerPreview  --  Applies the current gain to a copy of the original
// buffer and refreshes the viewer display.
// -----------------------------------------------------------------------------
void TemperatureTintDialog::triggerPreview()
{
    if (!m_viewer || !m_buffer || !m_originalBuffer.isValid()) return;
    if (m_chkPreview && !m_chkPreview->isChecked()) return;

    // Reset the working buffer to the clean original
    *m_buffer = m_originalBuffer;

    float r, g, b;
    computeGain(r, g, b);
    m_buffer->applyWhiteBalance(r, g, b, m_chkProtect->isChecked());

    // Blend through the mask if one is present
    if (m_originalBuffer.hasMask()) {
        m_buffer->blendResult(m_originalBuffer);
    }

    m_viewer->refreshDisplay(true);
}

// -----------------------------------------------------------------------------
// handleApply  --  Commits the temperature/tint adjustment with undo support.
// -----------------------------------------------------------------------------
void TemperatureTintDialog::handleApply()
{
    if (m_viewer && m_buffer && m_originalBuffer.isValid()) {
        // Restore the clean original
        *m_buffer = m_originalBuffer;

        // Record undo
        m_viewer->pushUndo(tr("Temperature / Tint"));

        // Apply final gain values
        float r, g, b;
        computeGain(r, g, b);
        m_buffer->applyWhiteBalance(r, g, b, m_chkProtect->isChecked());

        // Blend through the mask if present
        if (m_originalBuffer.hasMask()) {
            m_buffer->blendResult(m_originalBuffer);
        }

        m_viewer->refreshDisplay(true);

        if (auto mw = getCallbacks()) {
            mw->logMessage(tr("Temperature / Tint applied."), 1);
        }
        m_applied = true;
    }

    emit applyInternal();
    accept();
}

// -----------------------------------------------------------------------------
// setViewer  --  Switches to a new viewer, restoring the previous one if
// changes were not applied.
// -----------------------------------------------------------------------------
void TemperatureTintDialog::setViewer(ImageViewer* viewer)
{
    if (m_viewer == viewer) return;

    // Restore the previous viewer to its original state
    if (m_viewer) {
        disconnect(m_viewer, &QObject::destroyed, this, nullptr);
        if (!m_applied && m_originalBuffer.isValid()) {
            m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
        }
    }

    m_viewer         = viewer;
    m_applied        = false;
    m_buffer         = nullptr;
    m_originalBuffer = ImageBuffer();

    if (m_viewer) {
        // Invalidate pointers if the viewer is destroyed externally
        connect(m_viewer, &QObject::destroyed, this, [this]() {
            m_viewer  = nullptr;
            m_buffer  = nullptr;
            m_originalBuffer = ImageBuffer();
        });

        if (m_viewer->getBuffer().isValid()) {
            m_buffer         = &m_viewer->getBuffer();
            m_originalBuffer = *m_buffer;
            triggerPreview();
        }
    }
}

// =============================================================================
// State serialization
// =============================================================================

TemperatureTintDialog::State TemperatureTintDialog::getState() const
{
    return { m_sldTemperature->value(), m_sldTint->value() };
}

void TemperatureTintDialog::setState(const State& s)
{
    m_sldTemperature->setValue(s.temperature);
    m_sldTint->setValue(s.tint);
}

void TemperatureTintDialog::resetState()
{
    m_sldTemperature->setValue(0);
    m_sldTint->setValue(0);
}