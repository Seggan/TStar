// =============================================================================
// UpscaleDialog.cpp
// Implements the image upscale/resample dialog. Maintains the original aspect
// ratio between width and height, and supports nearest-neighbor, bilinear,
// bicubic, and Lanczos4 interpolation.
// =============================================================================

#include "UpscaleDialog.h"
#include "MainWindowCallbacks.h"
#include "../ImageViewer.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>

// -----------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------
UpscaleDialog::UpscaleDialog(QWidget* parent)
    : DialogBase(parent, tr("Image Upscale"), 300, 120)
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // --- Width input ---
    QHBoxLayout* wLayout = new QHBoxLayout();
    wLayout->addWidget(new QLabel(tr("New Width:")));

    m_widthSpin = new QSpinBox(this);
    m_widthSpin->setRange(1, 32768);
    wLayout->addWidget(m_widthSpin);
    mainLayout->addLayout(wLayout);

    // --- Height input ---
    QHBoxLayout* hLayout = new QHBoxLayout();
    hLayout->addWidget(new QLabel(tr("New Height:")));

    m_heightSpin = new QSpinBox(this);
    m_heightSpin->setRange(1, 32768);
    hLayout->addWidget(m_heightSpin);
    mainLayout->addLayout(hLayout);

    // --- Interpolation method selector ---
    QHBoxLayout* mLayout = new QHBoxLayout();
    mLayout->addWidget(new QLabel(tr("Interpolation:")));

    m_methodCombo = new QComboBox(this);
    m_methodCombo->addItem(tr("Nearest Neighbor"),      ImageBuffer::Interpolation_Nearest);
    m_methodCombo->addItem(tr("Bilinear (fast)"),       ImageBuffer::Interpolation_Linear);
    m_methodCombo->addItem(tr("Bicubic (precise)"),     ImageBuffer::Interpolation_Cubic);
    m_methodCombo->addItem(tr("Lanczos4 (best quality)"), ImageBuffer::Interpolation_Lanczos);
    m_methodCombo->setCurrentIndex(3);  // Default to Lanczos4
    mLayout->addWidget(m_methodCombo);
    mainLayout->addLayout(mLayout);

    mainLayout->addStretch();

    // --- Action buttons ---
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* closeBtn  = new QPushButton(tr("Close"));
    QPushButton* applyBtn  = new QPushButton(tr("Apply"));

    btnLayout->addWidget(closeBtn);
    btnLayout->addWidget(applyBtn);
    mainLayout->addLayout(btnLayout);

    // --- Aspect-ratio linking ---
    // Changing width recalculates height, and vice versa
    connect(m_widthSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int val) {
        m_heightSpin->blockSignals(true);
        m_heightSpin->setValue(
            static_cast<int>(val / m_aspectRatio + 0.5f));
        m_heightSpin->blockSignals(false);
    });

    connect(m_heightSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int val) {
        m_widthSpin->blockSignals(true);
        m_widthSpin->setValue(
            static_cast<int>(val * m_aspectRatio + 0.5f));
        m_widthSpin->blockSignals(false);
    });

    connect(applyBtn, &QPushButton::clicked, this, &UpscaleDialog::onApply);
    connect(closeBtn, &QPushButton::clicked, this, &UpscaleDialog::reject);
}

// -----------------------------------------------------------------------------
// setViewer  --  Initialises spin boxes from the viewer's current image
// dimensions and computes the aspect ratio.
// -----------------------------------------------------------------------------
void UpscaleDialog::setViewer(ImageViewer* v)
{
    m_viewer = v;
    if (!m_viewer) return;

    int w = m_viewer->getBuffer().width();
    int h = m_viewer->getBuffer().height();
    m_aspectRatio = static_cast<float>(w) / h;

    m_widthSpin->blockSignals(true);
    m_heightSpin->blockSignals(true);
    m_widthSpin->setValue(w);
    m_heightSpin->setValue(h);
    m_widthSpin->blockSignals(false);
    m_heightSpin->blockSignals(false);
}

// -----------------------------------------------------------------------------
// onApply  --  Resamples the active image to the specified dimensions.
// -----------------------------------------------------------------------------
void UpscaleDialog::onApply()
{
    // Use the currently active viewer to follow selection changes
    MainWindowCallbacks* cb = getCallbacks();
    ImageViewer* v = cb ? cb->getCurrentViewer() : m_viewer.data();

    if (!v) {
        QMessageBox::warning(this, tr("No Image"), tr("Select image."));
        return;
    }

    int newW = m_widthSpin->value();
    int newH = m_heightSpin->value();
    ImageBuffer::InterpolationMethod method =
        static_cast<ImageBuffer::InterpolationMethod>(
            m_methodCombo->currentData().toInt());

    v->pushUndo(tr("Upscale"));
    v->getBuffer().resample(newW, newH, method);
    v->refreshDisplay(false);
    v->fitToWindow();

    if (cb) cb->logMessage(tr("Upscale applied."), 1);

    accept();
}