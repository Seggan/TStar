#include "SaturationDialog.h"

#include <QHBoxLayout>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QMessageBox>

#include "../MainWindowCallbacks.h"
#include "../ImageViewer.h"

#include <algorithm>
#include <cmath>

// ----------------------------------------------------------------------------
// Constructor / Destructor
// ----------------------------------------------------------------------------

SaturationDialog::SaturationDialog(QWidget* parent, ImageViewer* viewer)
    : DialogBase(parent, tr("Color Saturation"), 500, 250)
    , m_viewer(nullptr)
    , m_buffer(nullptr)
{
    setupUI();

    if (viewer)
        setViewer(viewer);
}

SaturationDialog::~SaturationDialog()
{
    // If the dialog is closed without applying, restore the original buffer
    if (!m_applied && m_viewer && m_originalBuffer.isValid())
        m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
}

// ----------------------------------------------------------------------------
// Private Methods - UI Setup
// ----------------------------------------------------------------------------

void SaturationDialog::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(15);

    QGridLayout* grid = new QGridLayout();
    grid->setSpacing(10);
    int row = 0;

    // Helper lambda to create a labeled slider row with a value display label
    auto addSlider = [&](const QString& label, QSlider*& sld, QLabel*& val,
                         int min, int max, int def)
    {
        grid->addWidget(new QLabel(label), row, 0);

        sld = new QSlider(Qt::Horizontal);
        sld->setRange(min, max);
        sld->setValue(def);
        grid->addWidget(sld, row, 1);

        val = new QLabel(QString::number(def / 100.0, 'f', 2));
        val->setFixedWidth(40);
        grid->addWidget(val, row, 2);

        connect(sld, &QSlider::valueChanged, this, &SaturationDialog::onSliderChanged);
        connect(sld, &QSlider::valueChanged, this, &SaturationDialog::triggerPreview);
        ++row;
    };

    // Amount: range 0.0 to 5.0, default 1.0 (neutral)
    addSlider(tr("Amount:"),    m_sldAmount,   m_valAmount,   0, 500, 100);
    // Background factor: range 0.0 to 5.0, default 1.0
    addSlider(tr("BG Factor:"), m_sldBgFactor, m_valBgFactor, 0, 500, 100);

    // Hue preset selection
    grid->addWidget(new QLabel(tr("Hue Presets:")), row, 0);
    m_cmbPresets = new QComboBox();
    m_cmbPresets->addItem(tr("All Colors"));
    m_cmbPresets->addItem(tr("Reds"));
    m_cmbPresets->addItem(tr("Yellows"));
    m_cmbPresets->addItem(tr("Greens"));
    m_cmbPresets->addItem(tr("Cyans"));
    m_cmbPresets->addItem(tr("Blues"));
    m_cmbPresets->addItem(tr("Magentas"));
    m_cmbPresets->setStyleSheet(
        "QComboBox { color: white; background-color: #2a2a2a; border: 1px solid #555; padding: 2px; border-radius: 3px; }"
        "QComboBox:focus { border: 2px solid #4a9eff; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox::down-arrow { image: url(:/images/dropdown.png); }"
        "QComboBox QAbstractItemView { color: white; background-color: #2a2a2a; outline: none; }"
        "QComboBox QAbstractItemView::item { padding: 3px; margin: 0px; }"
        "QComboBox QAbstractItemView::item:hover { background-color: #4a7ba7 !important; color: white; }"
        "QComboBox QAbstractItemView::item:selected { background-color: #4a7ba7; color: white; }"
    );
    grid->addWidget(m_cmbPresets, row, 1, 1, 2);
    connect(m_cmbPresets, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SaturationDialog::onPresetChanged);
    ++row;

    // Helper lambda to create a hue parameter slider row (displays integer degrees)
    auto addHueSlider = [&](const QString& label, QSlider*& sld, QLabel*& val,
                            int min, int max, int def, bool deg = true)
    {
        grid->addWidget(new QLabel(label), row, 0);

        sld = new QSlider(Qt::Horizontal);
        sld->setRange(min, max);
        sld->setValue(def);
        grid->addWidget(sld, row, 1);

        val = new QLabel(QString::number(def) + (deg ? " deg" : ""));
        val->setFixedWidth(40);
        grid->addWidget(val, row, 2);

        connect(sld, &QSlider::valueChanged, this, &SaturationDialog::onSliderChanged);
        connect(sld, &QSlider::valueChanged, this, &SaturationDialog::triggerPreview);
        ++row;
    };

    addHueSlider(tr("Hue Center:"), m_sldHueCenter, m_valHueCenter, 0, 360, 0);
    addHueSlider(tr("Hue Width:"),  m_sldHueWidth,  m_valHueWidth,  0, 360, 360);
    addHueSlider(tr("Hue Smooth:"), m_sldHueSmooth, m_valHueSmooth, 0, 180, 30);

    mainLayout->addLayout(grid);

    // Action buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();

    m_chkPreview = new QCheckBox(tr("Preview"));
    m_chkPreview->setChecked(true);
    connect(m_chkPreview, &QCheckBox::toggled, this, [this](bool on) {
        if (on)
            triggerPreview();
        else if (m_viewer && m_originalBuffer.isValid())
            m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
    });

    QPushButton* btnReset  = new QPushButton(tr("Reset"));
    QPushButton* btnApply  = new QPushButton(tr("Apply"));
    QPushButton* btnCancel = new QPushButton(tr("Cancel"));
    btnApply->setDefault(true);

    btnLayout->addWidget(m_chkPreview);
    btnLayout->addStretch();
    btnLayout->addWidget(btnReset);
    btnLayout->addWidget(btnCancel);
    btnLayout->addWidget(btnApply);
    mainLayout->addLayout(btnLayout);

    connect(btnReset,  &QPushButton::clicked, this, &SaturationDialog::resetState);
    connect(btnApply,  &QPushButton::clicked, this, &SaturationDialog::handleApply);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
}

// ----------------------------------------------------------------------------
// Public Methods
// ----------------------------------------------------------------------------

void SaturationDialog::setViewer(ImageViewer* viewer)
{
    if (m_viewer == viewer)
        return;

    // Restore the previous viewer's buffer if a preview was not committed
    if (m_viewer)
    {
        disconnect(m_viewer, &QObject::destroyed, this, nullptr);
        if (!m_applied && m_originalBuffer.isValid())
            m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
    }

    m_viewer   = viewer;
    m_applied  = false;
    m_buffer   = nullptr;
    m_originalBuffer = ImageBuffer();

    if (m_viewer)
    {
        // Guard against the viewer being destroyed while the dialog is open
        connect(m_viewer, &QObject::destroyed, this, [this]() {
            m_viewer = nullptr;
            m_buffer = nullptr;
            m_originalBuffer = ImageBuffer();
        });

        if (m_viewer->getBuffer().isValid())
        {
            m_buffer         = &m_viewer->getBuffer();
            m_originalBuffer = *m_buffer; // Deep-copy backup
            triggerPreview();
        }
    }
}

void SaturationDialog::setBuffer(ImageBuffer* buffer)
{
    m_buffer = buffer;
    if (m_buffer)
    {
        m_originalBuffer = *m_buffer;
        triggerPreview();
    }
}

// ----------------------------------------------------------------------------
// Public Accessors - State
// ----------------------------------------------------------------------------

ImageBuffer::SaturationParams SaturationDialog::getParams() const
{
    ImageBuffer::SaturationParams params;
    params.amount    = m_sldAmount->value()    / 100.0f;
    params.bgFactor  = m_sldBgFactor->value()  / 100.0f;
    params.hueCenter = static_cast<float>(m_sldHueCenter->value());
    params.hueWidth  = static_cast<float>(m_sldHueWidth->value());
    params.hueSmooth = static_cast<float>(m_sldHueSmooth->value());
    return params;
}

SaturationDialog::State SaturationDialog::getState() const
{
    State s;
    s.amount      = m_sldAmount->value();
    s.bgFactor    = m_sldBgFactor->value();
    s.hueCenter   = m_sldHueCenter->value();
    s.hueWidth    = m_sldHueWidth->value();
    s.hueSmooth   = m_sldHueSmooth->value();
    s.presetIndex = m_cmbPresets->currentIndex();
    return s;
}

void SaturationDialog::setState(const State& s)
{
    m_sldAmount->setValue(s.amount);
    m_sldBgFactor->setValue(s.bgFactor);
    m_sldHueCenter->setValue(s.hueCenter);
    m_sldHueWidth->setValue(s.hueWidth);
    m_sldHueSmooth->setValue(s.hueSmooth);

    if (s.presetIndex >= 0 && s.presetIndex < m_cmbPresets->count())
        m_cmbPresets->setCurrentIndex(s.presetIndex);
}

void SaturationDialog::resetState()
{
    m_sldAmount->setValue(150);
    m_sldBgFactor->setValue(100);
    m_cmbPresets->setCurrentIndex(0); // All Colors
    m_sldHueSmooth->setValue(30);
}

// ----------------------------------------------------------------------------
// Private Slots
// ----------------------------------------------------------------------------

void SaturationDialog::onSliderChanged()
{
    m_valAmount->setText(QString::number(m_sldAmount->value()    / 100.0, 'f', 2));
    m_valBgFactor->setText(QString::number(m_sldBgFactor->value() / 100.0, 'f', 2));
    m_valHueCenter->setText(QString::number(m_sldHueCenter->value()) + " deg");
    m_valHueWidth->setText(QString::number(m_sldHueWidth->value())   + " deg");
    m_valHueSmooth->setText(QString::number(m_sldHueSmooth->value()) + " deg");
}

void SaturationDialog::onPresetChanged(int index)
{
    // Preset index mapping:
    // 0: All Colors, 1: Reds (0 deg), 2: Yellows (60 deg),
    // 3: Greens (120 deg), 4: Cyans (180 deg), 5: Blues (240 deg), 6: Magentas (300 deg)

    // Temporarily disconnect to prevent recursive signals during bulk value changes
    disconnect(m_sldHueCenter, &QSlider::valueChanged, this, &SaturationDialog::onSliderChanged);
    disconnect(m_sldHueWidth,  &QSlider::valueChanged, this, &SaturationDialog::onSliderChanged);

    if (index == 0)
    {
        m_sldHueCenter->setValue(0);
        m_sldHueWidth->setValue(360);
    }
    else
    {
        m_sldHueCenter->setValue((index - 1) * 60);
        m_sldHueWidth->setValue(60);
    }

    connect(m_sldHueCenter, &QSlider::valueChanged, this, &SaturationDialog::onSliderChanged);
    connect(m_sldHueWidth,  &QSlider::valueChanged, this, &SaturationDialog::onSliderChanged);

    onSliderChanged();
    emit preview(getParams());
    triggerPreview();
}

void SaturationDialog::handleApply()
{
    if (m_viewer && m_buffer && m_originalBuffer.isValid())
    {
        // Restore the clean state before pushing an undo entry
        *m_buffer = m_originalBuffer;
        m_viewer->pushUndo(tr("Saturation"));

        // Re-apply the final parameters to the now-clean buffer
        m_buffer->applySaturation(getParams());
        m_viewer->refreshDisplay(true);

        if (auto mw = getCallbacks())
            mw->logMessage(tr("Saturation applied."), 1);

        m_applied        = true;
        m_originalBuffer = *m_buffer;
    }

    emit applyInternal(getParams());
    accept();
}

void SaturationDialog::triggerPreview()
{
    if (!m_viewer || !m_buffer || !m_originalBuffer.isValid())
        return;
    if (m_chkPreview && !m_chkPreview->isChecked())
        return;

    // Reset to the clean backup before applying the current parameters
    *m_buffer = m_originalBuffer;
    m_buffer->applySaturation(getParams());
    m_viewer->refreshDisplay(true);
}