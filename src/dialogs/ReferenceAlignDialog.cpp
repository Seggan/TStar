#include "ReferenceAlignDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QLabel>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QPainter>
#include <QIcon>
#include <QApplication>
#include <QResizeEvent>
#include <QSize>
#include <opencv2/opencv.hpp>
#include <cmath>

ReferenceAlignDialog::ReferenceAlignDialog(QWidget* parent, const ImageBuffer& refBuffer, const ImageBuffer& targetBuffer, double paddingFactor)
    : DialogBase(parent, tr("Align Reference Image"), 800, 600), m_originalRefBuffer(refBuffer), m_targetBuffer(targetBuffer), m_currentBuffer(refBuffer), m_paddingFactor(paddingFactor)
{
    // Removing manual centering and sizing as DialogBase handles this now.
    // resize(700, 600) and centering are done by base class.

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    QLabel* infoLabel = new QLabel(tr("Check if the reference image pattern matches your image. Use the buttons below to flip or rotate it if necessary."), this);
    infoLabel->setWordWrap(false);
    mainLayout->addWidget(infoLabel);

    // Preview area
    m_previewLabel = new QLabel(this);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setStyleSheet("QLabel { background-color: #1e1e1e; border: 1px solid #444; }");
    m_previewLabel->setMinimumSize(400, 400);
    m_previewLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    mainLayout->addWidget(m_previewLabel, 1);

    // Toolbar for transformations
    QHBoxLayout* toolLayout = new QHBoxLayout();
    
    QPushButton* btnFlipH = new QPushButton(tr("Flip Horizontal"), this);
    QPushButton* btnFlipV = new QPushButton(tr("Flip Vertical"), this);
    QPushButton* btnRotCW = new QPushButton(tr("Rotate 90° CW"), this);
    QPushButton* btnRotCCW = new QPushButton(tr("Rotate 90° CCW"), this);

    toolLayout->addWidget(btnFlipH);
    toolLayout->addWidget(btnFlipV);
    toolLayout->addWidget(btnRotCCW);
    toolLayout->addWidget(btnRotCW);
    toolLayout->addStretch();

    mainLayout->addLayout(toolLayout);

    QFormLayout* formOptions = new QFormLayout();
    
    QHBoxLayout* rotLayout = new QHBoxLayout();
    m_sliderRotation = new QSlider(Qt::Horizontal, this);
    m_sliderRotation->setRange(-2700, 2700); // -270.0 to 270.0
    m_sliderRotation->setValue(0);
    
    m_spinRotation = new QDoubleSpinBox(this);
    m_spinRotation->setRange(-270.0, 270.0);
    m_spinRotation->setSingleStep(0.1);
    m_spinRotation->setSuffix("°");
    m_spinRotation->setValue(0.0);

    rotLayout->addWidget(m_sliderRotation);
    rotLayout->addWidget(m_spinRotation);
    
    formOptions->addRow(tr("Fine Rotation:"), rotLayout);

    QHBoxLayout* overLayout = new QHBoxLayout();
    QCheckBox* chkOverlay = new QCheckBox(tr("Show Original Overlay"), this);
    chkOverlay->setChecked(false);
    
    m_chkOverlayStretch = new QCheckBox(tr("Auto-stretch Overlay"), this);
    m_chkOverlayStretch->setChecked(true);
    m_chkOverlayStretch->setEnabled(false);
    
    m_sliderOpacity = new QSlider(Qt::Horizontal, this);
    m_sliderOpacity->setRange(0, 100);
    m_sliderOpacity->setValue(35);
    m_sliderOpacity->setEnabled(false);

    overLayout->addWidget(chkOverlay);
    overLayout->addWidget(m_chkOverlayStretch);
    overLayout->addWidget(new QLabel(tr("Opacity:")));
    overLayout->addWidget(m_sliderOpacity);

    formOptions->addRow(tr("Overlay (with Target):"), overLayout);

    mainLayout->addLayout(formOptions);

    // Dialog buttons
    QHBoxLayout* buttonBox = new QHBoxLayout();
    buttonBox->addStretch();
    QPushButton* cancelBtn = new QPushButton(tr("Cancel"));
    QPushButton* okBtn = new QPushButton(tr("OK"));
    okBtn->setDefault(true);
    buttonBox->addWidget(cancelBtn);
    buttonBox->addWidget(okBtn);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
    mainLayout->addLayout(buttonBox);

    connect(btnFlipH, &QPushButton::clicked, this, &ReferenceAlignDialog::onFlipHorizontal);
    connect(btnFlipV, &QPushButton::clicked, this, &ReferenceAlignDialog::onFlipVertical);
    connect(btnRotCW, &QPushButton::clicked, this, &ReferenceAlignDialog::onRotateCW);
    connect(btnRotCCW, &QPushButton::clicked, this, &ReferenceAlignDialog::onRotateCCW);

    connect(m_sliderRotation, &QSlider::valueChanged, this, [this](int value) {
        m_spinRotation->blockSignals(true);
        m_spinRotation->setValue(value / 10.0);
        m_spinRotation->blockSignals(false);
        onRotationChanged(value / 10.0);
    });

    connect(m_spinRotation, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        m_sliderRotation->blockSignals(true);
        m_sliderRotation->setValue(std::round(value * 10.0));
        m_sliderRotation->blockSignals(false);
        onRotationChanged(value);
    });

    connect(chkOverlay, &QCheckBox::toggled, this, [this](bool checked) {
        m_sliderOpacity->setEnabled(checked);
        m_chkOverlayStretch->setEnabled(checked);
        onOverlayToggled(checked);
    });

    connect(m_chkOverlayStretch, &QCheckBox::toggled, this, &ReferenceAlignDialog::onOverlayStretchToggled);

    connect(m_sliderOpacity, &QSlider::valueChanged, this, &ReferenceAlignDialog::onOpacityChanged);

    // Extraneous connects removed

    rebuildBuffer();
}

ReferenceAlignDialog::~ReferenceAlignDialog() {}

ImageBuffer ReferenceAlignDialog::getAlignedBuffer() const {
    return m_currentBuffer;
}

void ReferenceAlignDialog::resizeEvent(QResizeEvent* event) {
    DialogBase::resizeEvent(event);
    if (m_previewLabel->size() != m_lastPreviewSize) {
        m_lastPreviewSize = m_previewLabel->size();
        updatePreview();
    }
}

void ReferenceAlignDialog::onFlipHorizontal() {
    m_flipH = !m_flipH;
    rebuildBuffer();
}

void ReferenceAlignDialog::onFlipVertical() {
    m_flipV = !m_flipV;
    rebuildBuffer();
}

void ReferenceAlignDialog::onRotateCW() {
    onRotationChanged(m_rotationAngle - 90.0);
    
    m_spinRotation->blockSignals(true);
    m_spinRotation->setValue(m_rotationAngle);
    m_spinRotation->blockSignals(false);
    
    m_sliderRotation->blockSignals(true);
    m_sliderRotation->setValue(std::round(m_rotationAngle * 10.0));
    m_sliderRotation->blockSignals(false);
}

void ReferenceAlignDialog::onRotateCCW() {
    onRotationChanged(m_rotationAngle + 90.0);
    
    m_spinRotation->blockSignals(true);
    m_spinRotation->setValue(m_rotationAngle);
    m_spinRotation->blockSignals(false);

    m_sliderRotation->blockSignals(true);
    m_sliderRotation->setValue(std::round(m_rotationAngle * 10.0));
    m_sliderRotation->blockSignals(false);
}

void ReferenceAlignDialog::onRotationChanged(double value) {
    m_rotationAngle = value;
    while (m_rotationAngle > 270.0)  m_rotationAngle -= 360.0;
    while (m_rotationAngle < -270.0) m_rotationAngle += 360.0;
    rebuildBuffer();
}

void ReferenceAlignDialog::onOverlayToggled(bool checked) {
    m_showOverlay = checked;
    updatePreview();
}

void ReferenceAlignDialog::onOverlayStretchToggled(bool checked) {
    m_overlayAutoStretch = checked;
    updatePreview();
}

void ReferenceAlignDialog::onOpacityChanged(int value) {
    m_overlayOpacity = value;
    updatePreview();
}

void ReferenceAlignDialog::rebuildBuffer() {
    if (!m_originalRefBuffer.isValid()) return;

    int origW = m_originalRefBuffer.width();
    int origH = m_originalRefBuffer.height();
    int c = m_originalRefBuffer.channels();
    int cvType = CV_MAKETYPE(CV_32F, c);

    cv::Mat mat(origH, origW, cvType, m_originalRefBuffer.data().data());
    cv::Mat transformed;
    
    if (c <= 4) {
        mat.copyTo(transformed);
        if (m_flipH) cv::flip(transformed, transformed, 1);
        if (m_flipV) cv::flip(transformed, transformed, 0);

        if (m_rotationAngle != 0.0) {
            cv::Point2f center(transformed.cols / 2.0f, transformed.rows / 2.0f);
            cv::Mat rot = cv::getRotationMatrix2D(center, m_rotationAngle, 1.0);
            cv::warpAffine(transformed, transformed, rot, transformed.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar::all(0));
        }
    } else {
        std::vector<cv::Mat> channels;
        cv::split(mat, channels);
        
        cv::Mat rot;
        if (m_rotationAngle != 0.0) {
            cv::Point2f center(origW / 2.0f, origH / 2.0f);
            rot = cv::getRotationMatrix2D(center, m_rotationAngle, 1.0);
        }

        for (int i = 0; i < c; ++i) {
            if (m_flipH) cv::flip(channels[i], channels[i], 1);
            if (m_flipV) cv::flip(channels[i], channels[i], 0);

            if (m_rotationAngle != 0.0) {
                cv::warpAffine(channels[i], channels[i], rot, channels[i].size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0));
            }
        }
        cv::merge(channels, transformed);
    }

    int cropW = std::round(origW / m_paddingFactor);
    int cropH = std::round(origH / m_paddingFactor);
    
    cropW = std::min(cropW, transformed.cols);
    cropH = std::min(cropH, transformed.rows);

    int startX = (transformed.cols - cropW) / 2;
    int startY = (transformed.rows - cropH) / 2;

    cv::Rect roi(startX, startY, cropW, cropH);
    cv::Mat cropped = transformed(roi).clone();

    m_currentBuffer.resize(cropW, cropH, c);
    std::memcpy(m_currentBuffer.data().data(), cropped.ptr<float>(), m_currentBuffer.data().size() * sizeof(float));

    updatePreview();
}

QImage ReferenceAlignDialog::bufferToQImageScaled(const ImageBuffer& buf, bool autoStretch) {
    if (!buf.isValid()) return QImage();

    int previewW = m_previewLabel->width();
    int previewH = m_previewLabel->height();
    
    if (previewW <= 0 || previewH <= 0) {
        previewW = 400; previewH = 400;
    }
    
    ImageBuffer::DisplayMode mode = autoStretch ? ImageBuffer::Display_AutoStretch : ImageBuffer::Display_Linear;
    
    // We pass true for 'linked' to match main window behavior typically, or false if it's a mono image.
    // The main display defaults to linked=false for mono and linked=true for RGB.
    bool isLinked = (buf.channels() == 3); 
    
    // getDisplayImage natively handles 24-bit STF and high-quality downscaling
    // Use KeepAspectRatio to ensure we don't distort the astronomical field
    QImage img = buf.getDisplayImage(mode, isLinked, nullptr, previewW, previewH, false, false, false, 0.25f, ImageBuffer::ChannelRGB);
    
    // Ensure the image fits the space while preserving aspect ratio
    if (img.width() > previewW || img.height() > previewH) {
        img = img.scaled(previewW, previewH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    // Ensure format is RGB888 for QPainter blending later
    if (img.format() != QImage::Format_RGB888) {
        return img.convertToFormat(QImage::Format_RGB888);
    }
    
    return img;
}

void ReferenceAlignDialog::updatePreview() {
    int pw = m_previewLabel->width();
    int ph = m_previewLabel->height();
    if (pw <= 0 || ph <= 0) return;

    QImage refImg = bufferToQImageScaled(m_currentBuffer, true);
    if (refImg.isNull()) return;

    QImage finalPreview;

    if (m_showOverlay && m_targetBuffer.isValid()) {
        QImage targetImg = bufferToQImageScaled(m_targetBuffer, m_overlayAutoStretch);
        
        // Ensure both images have the same scale for a perfect overlay.
        // Since both were generated from bufferToQImageScaled with the same constraints, 
        // they should already be very close, but aspect ratios of buffers might differ slightly 
        // if cropping wasn't exact. Let's force them to match.
        if (targetImg.size() != refImg.size()) {
            targetImg = targetImg.scaled(refImg.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
        
        finalPreview = QImage(refImg.size(), QImage::Format_RGB888);
        finalPreview.fill(Qt::black);
        QPainter p(&finalPreview);
        
        // Base: Target (Original)
        p.drawImage(0, 0, targetImg);
        
        // Overlay: Reference (Catalog)
        p.setOpacity(m_overlayOpacity / 100.0);
        p.drawImage(0, 0, refImg);
        p.end();
    } else {
        finalPreview = refImg;
    }

    // Centering in the label
    QPixmap pix(pw, ph);
    pix.fill(palette().color(QPalette::Window)); // Use theme background color
    QPainter p(&pix);
    int x = (pw - finalPreview.width()) / 2;
    int y = (ph - finalPreview.height()) / 2;
    p.drawImage(x, y, finalPreview);
    p.end();

    m_previewLabel->setPixmap(pix);
}
