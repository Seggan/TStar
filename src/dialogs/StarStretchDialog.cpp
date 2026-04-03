// =============================================================================
// StarStretchDialog.cpp
// Implements the star stretch dialog with live preview, colour boost,
// optional SCNR, and undo support on apply.
// =============================================================================

#include "StarStretchDialog.h"
#include "MainWindowCallbacks.h"
#include "../ImageViewer.h"
#include "DialogBase.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QCheckBox>
#include <QPushButton>
#include <QMessageBox>
#include <QIcon>
#include <QGroupBox>

// -----------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------
StarStretchDialog::StarStretchDialog(QWidget* parent, ImageViewer* viewer)
    : DialogBase(parent, tr("Star Stretch"), 450, 200)
    , m_viewer(viewer)
{
    if (m_viewer) {
        m_originalBuffer = m_viewer->getBuffer();
    }
    createUI();
}

// -----------------------------------------------------------------------------
// Destructor  --  Restores the original buffer if the dialog is destroyed
// without applying.
// -----------------------------------------------------------------------------
StarStretchDialog::~StarStretchDialog()
{
    if (!m_applied && m_viewer && m_originalBuffer.isValid()) {
        m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
    }
}

// -----------------------------------------------------------------------------
// setViewer  --  Switches to a new target viewer, restoring the previous one
// if changes were not applied.
// -----------------------------------------------------------------------------
void StarStretchDialog::setViewer(ImageViewer* v)
{
    if (m_viewer == v) return;

    // Restore the previous viewer to its original state
    if (m_viewer && !m_applied) {
        m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
    }

    m_viewer  = v;
    m_applied = false;

    if (m_viewer) {
        m_originalBuffer = m_viewer->getBuffer();
        updatePreview();
    }
}

// -----------------------------------------------------------------------------
// createUI  --  Builds sliders, checkboxes, and action buttons.
// -----------------------------------------------------------------------------
void StarStretchDialog::createUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // --- Stretch amount slider ---
    m_lblStretch = new QLabel(tr("Stretch Amount: 0.00"));
    m_sliderStretch = new QSlider(Qt::Horizontal);
    m_sliderStretch->setRange(0, 800);
    m_sliderStretch->setValue(0);

    connect(m_sliderStretch, &QSlider::valueChanged, this, [this](int val) {
        m_lblStretch->setText(
            tr("Stretch Amount: %1").arg(val / 100.0, 0, 'f', 2));
        onSliderChanged();
    });

    mainLayout->addWidget(m_lblStretch);
    mainLayout->addWidget(m_sliderStretch);

    // --- Colour boost slider ---
    m_lblBoost = new QLabel(tr("Color Boost: 1.00"));
    m_sliderBoost = new QSlider(Qt::Horizontal);
    m_sliderBoost->setRange(0, 200);
    m_sliderBoost->setValue(100);

    connect(m_sliderBoost, &QSlider::valueChanged, this, [this](int val) {
        m_lblBoost->setText(
            tr("Color Boost: %1").arg(val / 100.0, 0, 'f', 2));
        onSliderChanged();
    });

    mainLayout->addWidget(m_lblBoost);
    mainLayout->addWidget(m_sliderBoost);

    // --- SCNR toggle and preview toggle on the same row ---
    QHBoxLayout* scnrRow = new QHBoxLayout();

    m_chkScnr = new QCheckBox(tr("Remove Green via SCNR (Optional)"));
    m_chkPreview = new QCheckBox(tr("Preview"));
    m_chkPreview->setChecked(true);

    connect(m_chkScnr, &QCheckBox::toggled,
            this, &StarStretchDialog::onSliderChanged);
    connect(m_chkPreview, &QCheckBox::toggled, this, [this](bool on) {
        if (on) {
            updatePreview();
        } else if (m_viewer) {
            m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
        }
    });

    scnrRow->addWidget(m_chkScnr);
    scnrRow->addStretch();
    scnrRow->addWidget(m_chkPreview);
    mainLayout->addLayout(scnrRow);

    // --- Action buttons: Reset / Cancel / Apply ---
    QHBoxLayout* btnLayout = new QHBoxLayout();

    QPushButton* btnReset  = new QPushButton(tr("Reset"));
    m_btnApply             = new QPushButton(tr("Apply"));
    QPushButton* btnCancel = new QPushButton(tr("Cancel"));

    connect(btnReset, &QPushButton::clicked, this, [this]() {
        m_sliderStretch->setValue(500);
        m_sliderBoost->setValue(100);
        m_chkScnr->setChecked(false);
    });
    connect(m_btnApply, &QPushButton::clicked, this, &StarStretchDialog::onApply);
    connect(btnCancel,  &QPushButton::clicked, this, &StarStretchDialog::reject);

    btnLayout->addWidget(btnReset);
    btnLayout->addStretch();
    btnLayout->addWidget(btnCancel);
    btnLayout->addStretch();
    btnLayout->addWidget(m_btnApply);

    mainLayout->addLayout(btnLayout);
}

// -----------------------------------------------------------------------------
// onSliderChanged  --  Triggers a preview update whenever any parameter changes.
// -----------------------------------------------------------------------------
void StarStretchDialog::onSliderChanged()
{
    updatePreview();
}

// -----------------------------------------------------------------------------
// updatePreview  --  Runs the star stretch algorithm on the original buffer
// and displays the result in the viewer without committing it.
// -----------------------------------------------------------------------------
void StarStretchDialog::updatePreview()
{
    if (!m_viewer) return;
    if (m_chkPreview && !m_chkPreview->isChecked()) return;

    StarStretchParams params;
    params.stretchAmount = m_sliderStretch->value() / 100.0f;
    params.colorBoost    = m_sliderBoost->value()   / 100.0f;
    params.scnr          = m_chkScnr->isChecked();

    if (m_runner.run(m_originalBuffer, m_previewBuffer, params)) {
        // preserveView = false to maintain current zoom/pan on slider moves
        m_viewer->setBuffer(m_previewBuffer, m_viewer->windowTitle(), false);
    }
}

// -----------------------------------------------------------------------------
// onApply  --  Commits the stretch: restores the clean buffer, pushes undo,
// then applies the final result.
// -----------------------------------------------------------------------------
void StarStretchDialog::onApply()
{
    if (!m_viewer) {
        reject();
        return;
    }

    // Restore the clean original before pushing undo
    m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
    m_viewer->pushUndo(tr("Star Stretch"));

    // Apply the final processing
    updatePreview();

    m_applied = true;

    if (MainWindowCallbacks* mw = getCallbacks()) {
        mw->logMessage(tr("Star Stretch applied."), 1, true);
    }
    accept();
}

// -----------------------------------------------------------------------------
// reject  --  Restores the original image if the dialog is cancelled.
// -----------------------------------------------------------------------------
void StarStretchDialog::reject()
{
    if (!m_applied && m_viewer) {
        m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
    }
    QDialog::reject();
}