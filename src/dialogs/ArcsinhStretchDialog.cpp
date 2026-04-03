#include "ArcsinhStretchDialog.h"
#include "MainWindowCallbacks.h"
#include "ImageViewer.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>

#include <cmath>

#ifdef _OPENMP
#include <omp.h>
#endif

// =============================================================================
// Constructor / Destructor
// =============================================================================

ArcsinhStretchDialog::ArcsinhStretchDialog(ImageViewer* viewer, QWidget* parent)
    : DialogBase(parent, tr("Arcsinh Stretch"), 500, 250)
    , m_viewer(nullptr)
    , m_applied(false)
{
    setMinimumWidth(400);
    setupUI();
    setViewer(viewer);
}

ArcsinhStretchDialog::~ArcsinhStretchDialog()
{
    // Restore the original image if the dialog is destroyed without applying.
    if (!m_applied && m_viewer && m_originalBuffer.isValid())
        m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
}

// =============================================================================
// Public interface
// =============================================================================

void ArcsinhStretchDialog::setViewer(ImageViewer* v)
{
    if (m_viewer == v)
        return;

    // Restore the outgoing viewer before switching to prevent leaving it with
    // a partially previewed (uncommitted) result.
    if (m_viewer && !m_applied && m_originalBuffer.isValid())
        m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);

    m_viewer         = v;
    m_applied        = false;
    m_originalBuffer = ImageBuffer();

    if (m_viewer && m_viewer->getBuffer().isValid())
    {
        m_originalBuffer = m_viewer->getBuffer();

        if (m_previewCheck->isChecked())
            updatePreview();
    }
}

// =============================================================================
// Protected overrides
// =============================================================================

void ArcsinhStretchDialog::reject()
{
    if (!m_applied && m_viewer && m_originalBuffer.isValid())
        m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);

    QDialog::reject();
}

// =============================================================================
// Private helpers - UI construction
// =============================================================================

void ArcsinhStretchDialog::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);

    // -------------------------------------------------------------------------
    // Stretch factor row
    // -------------------------------------------------------------------------
    {
        QHBoxLayout* row = new QHBoxLayout();
        row->addWidget(new QLabel(tr("Stretch factor:")));
        row->addStretch();

        m_stretchSpin = new QDoubleSpinBox();
        m_stretchSpin->setRange(0, 1000);
        m_stretchSpin->setDecimals(1);
        m_stretchSpin->setSingleStep(1.0);
        m_stretchSpin->setValue(0);
        row->addWidget(m_stretchSpin);

        mainLayout->addLayout(row);
    }

    m_stretchSlider = new QSlider(Qt::Horizontal);
    m_stretchSlider->setRange(0, 10000); // 0..1000 with 0.1 precision
    m_stretchSlider->setValue(0);
    m_stretchSlider->setToolTip(tr("Controls the degree of non-linearity applied."));
    mainLayout->addWidget(m_stretchSlider);

    // -------------------------------------------------------------------------
    // Black point row
    // -------------------------------------------------------------------------
    {
        QHBoxLayout* row = new QHBoxLayout();
        row->addWidget(new QLabel(tr("Black Point:")));
        row->addStretch();

        m_blackPointSpin = new QDoubleSpinBox();
        m_blackPointSpin->setRange(0, 0.20);
        m_blackPointSpin->setDecimals(5);
        m_blackPointSpin->setSingleStep(0.001);
        m_blackPointSpin->setValue(0);
        row->addWidget(m_blackPointSpin);

        mainLayout->addLayout(row);
    }

    m_blackPointSlider = new QSlider(Qt::Horizontal);
    m_blackPointSlider->setRange(0, 20000); // 0..0.2 with 0.00001 precision
    m_blackPointSlider->setValue(0);
    m_blackPointSlider->setToolTip(tr("Constant offset subtracted from the image before stretching."));
    mainLayout->addWidget(m_blackPointSlider);

    // -------------------------------------------------------------------------
    // Clipping statistics display
    // -------------------------------------------------------------------------
    {
        QHBoxLayout* row = new QHBoxLayout();

        m_lowClipLabel = new QLabel(tr("Low: 0.00%"));
        m_lowClipLabel->setStyleSheet("color: #ff8888; margin-right: 10px;");

        m_highClipLabel = new QLabel(tr("High: 0.00%"));
        m_highClipLabel->setStyleSheet("color: #8888ff;");

        row->addWidget(m_lowClipLabel);
        row->addWidget(m_highClipLabel);
        row->addStretch();

        mainLayout->addLayout(row);
    }

    // -------------------------------------------------------------------------
    // Option checkboxes
    // -------------------------------------------------------------------------
    m_humanLuminanceCheck = new QCheckBox(tr("Human-weighted luminance"));
    m_humanLuminanceCheck->setChecked(true);
    m_humanLuminanceCheck->setToolTip(
        tr("For colour images, weight the luminance channel using human eye "
           "luminous efficiency (CIE photopic curve) before computing the stretch."));
    mainLayout->addWidget(m_humanLuminanceCheck);

    m_previewCheck = new QCheckBox(tr("Preview"));
    m_previewCheck->setChecked(true);
    mainLayout->addWidget(m_previewCheck);

    // -------------------------------------------------------------------------
    // Action buttons
    // -------------------------------------------------------------------------
    {
        QHBoxLayout* row    = new QHBoxLayout();
        QPushButton* reset  = new QPushButton(tr("Reset"));
        QPushButton* cancel = new QPushButton(tr("Cancel"));
        QPushButton* apply  = new QPushButton(tr("Apply"));
        apply->setDefault(true);

        row->addWidget(reset);
        row->addStretch();
        row->addWidget(cancel);
        row->addWidget(apply);

        mainLayout->addLayout(row);

        connect(reset,  &QPushButton::clicked, this, &ArcsinhStretchDialog::onReset);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(apply,  &QPushButton::clicked, this, &ArcsinhStretchDialog::onApply);
    }

    // -------------------------------------------------------------------------
    // Signal connections
    // Slider drag: update the spin box and clipping stats only (no preview)
    // to avoid per-frame preview renders during continuous drag.
    // Slider release: trigger a full preview update.
    // Spin box change: sync the slider and trigger a preview if enabled.
    // -------------------------------------------------------------------------

    connect(m_stretchSlider, &QSlider::valueChanged, [this](int val)
    {
        const double dval = val / 10.0;
        m_stretchSpin->blockSignals(true);
        m_stretchSpin->setValue(dval);
        m_stretchSpin->blockSignals(false);
        m_stretch = static_cast<float>(dval);
        updateClippingStatsOnly();
    });

    connect(m_stretchSlider, &QSlider::sliderReleased,
            this, &ArcsinhStretchDialog::updatePreview);

    connect(m_stretchSpin,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val)
    {
        m_stretchSlider->blockSignals(true);
        m_stretchSlider->setValue(static_cast<int>(val * 10));
        m_stretchSlider->blockSignals(false);
        m_stretch = static_cast<float>(val);
        if (m_previewCheck->isChecked())
            updatePreview();
    });

    connect(m_blackPointSlider, &QSlider::valueChanged, [this](int val)
    {
        const double dval = val / 100000.0;
        m_blackPointSpin->blockSignals(true);
        m_blackPointSpin->setValue(dval);
        m_blackPointSpin->blockSignals(false);
        m_blackPoint = static_cast<float>(dval);
        updateClippingStatsOnly();
    });

    connect(m_blackPointSlider, &QSlider::sliderReleased,
            this, &ArcsinhStretchDialog::updatePreview);

    connect(m_blackPointSpin,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ArcsinhStretchDialog::onBlackPointChanged);

    connect(m_humanLuminanceCheck, &QCheckBox::toggled,
            this, &ArcsinhStretchDialog::onHumanLuminanceToggled);

    connect(m_previewCheck, &QCheckBox::toggled,
            this, &ArcsinhStretchDialog::onPreviewToggled);
}

// =============================================================================
// Private slots
// =============================================================================

void ArcsinhStretchDialog::onStretchChanged(int value)
{
    const double dval = value / 10.0;
    m_stretchSpin->blockSignals(true);
    m_stretchSpin->setValue(dval);
    m_stretchSpin->blockSignals(false);
    m_stretch = static_cast<float>(dval);
}

void ArcsinhStretchDialog::onBlackPointChanged(double value)
{
    m_blackPointSlider->blockSignals(true);
    m_blackPointSlider->setValue(static_cast<int>(value * 100000));
    m_blackPointSlider->blockSignals(false);
    m_blackPoint = static_cast<float>(value);

    if (m_previewCheck->isChecked())
        updatePreview();
}

void ArcsinhStretchDialog::onHumanLuminanceToggled(bool checked)
{
    m_humanLuminance = checked;
    if (m_previewCheck->isChecked())
        updatePreview();
}

void ArcsinhStretchDialog::onPreviewToggled(bool checked)
{
    if (checked)
    {
        updatePreview();
    }
    else
    {
        // Restore the original image when preview is switched off.
        if (m_viewer && m_originalBuffer.isValid())
            m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);

        m_lowClipLabel->setText(tr("Low: 0.00%"));
        m_highClipLabel->setText(tr("High: 0.00%"));
    }
}

void ArcsinhStretchDialog::onReset()
{
    m_stretchSlider->setValue(0);
    m_stretchSpin->setValue(0);
    m_blackPointSlider->setValue(0);
    m_blackPointSpin->setValue(0);
    m_humanLuminanceCheck->setChecked(true);

    m_stretch        = 0.0f;
    m_blackPoint     = 0.0f;
    m_humanLuminance = true;

    if (m_viewer && m_originalBuffer.isValid())
        m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);

    m_lowClipLabel->setText(tr("Low: 0.00%"));
    m_highClipLabel->setText(tr("High: 0.00%"));
}

void ArcsinhStretchDialog::onApply()
{
    if (!m_viewer || !m_originalBuffer.isValid())
        return;

    // Step 1: Restore the viewer to the unmodified state so that pushUndo
    //         captures the correct baseline (not the preview result).
    m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);

    // Step 2: Push an undo snapshot of the clean state.
    m_viewer->pushUndo(tr("Arcsinh Stretch"));

    // Step 3: Compute the stretched result from the original data.
    ImageBuffer result = m_originalBuffer;
    result.applyArcSinh(m_stretch, m_blackPoint, m_humanLuminance);

    // Step 4: Commit the result to the viewer.
    m_viewer->setBuffer(result, m_viewer->windowTitle(), true);

    if (auto* mw = getCallbacks())
        mw->logMessage(tr("Arcsinh Stretch applied."), 1);

    m_applied = true;
    accept();
}

// =============================================================================
// Private helpers - preview and statistics
// =============================================================================

void ArcsinhStretchDialog::updatePreview()
{
    if (!m_viewer || !m_previewCheck->isChecked() || !m_originalBuffer.isValid())
        return;

    ImageBuffer preview = m_originalBuffer;
    preview.applyArcSinh(m_stretch, m_blackPoint, m_humanLuminance);
    m_viewer->setBuffer(preview, m_viewer->windowTitle(), true);

    updateClippingStats(preview);
}

void ArcsinhStretchDialog::updateClippingStats(const ImageBuffer& buffer)
{
    long lowClip  = 0;
    long highClip = 0;
    buffer.computeClippingStats(lowClip, highClip);

    const long total = static_cast<long>(buffer.width()) *
                       buffer.height() *
                       buffer.channels();

    if (total > 0)
    {
        const float lowPct  = (100.0f * lowClip)  / total;
        const float highPct = (100.0f * highClip) / total;
        m_lowClipLabel->setText(tr("Low: %1%").arg(lowPct,  0, 'f', 4));
        m_highClipLabel->setText(tr("High: %1%").arg(highPct, 0, 'f', 4));
    }
}

void ArcsinhStretchDialog::updateClippingStatsOnly()
{
    // Compute statistics on a temporary buffer without updating the viewer,
    // providing live feedback during slider drag with minimal overhead.
    if (!m_originalBuffer.isValid())
        return;

    ImageBuffer temp = m_originalBuffer;
    temp.applyArcSinh(m_stretch, m_blackPoint, m_humanLuminance);
    updateClippingStats(temp);
}