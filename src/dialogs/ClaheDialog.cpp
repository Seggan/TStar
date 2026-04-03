// =============================================================================
// ClaheDialog.cpp
//
// Implementation of the CLAHE dialog. Uses OpenCV's CLAHE algorithm
// operating in Lab color space (L channel only) for color images, or
// directly on 16-bit grayscale for monochrome images. Provides real-time
// debounced preview and supports mask-aware blending.
// =============================================================================

#include "ClaheDialog.h"
#include "MainWindowCallbacks.h"
#include "DialogBase.h"
#include "../ImageViewer.h"
#include "../ImageBuffer.h"

#include <QGraphicsView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStatusBar>
#include <QGridLayout>
#include <QGroupBox>
#include <QSlider>
#include <QLabel>
#include <QCheckBox>
#include <QPushButton>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QMessageBox>
#include <QTimer>
#include <QWheelEvent>

#include <opencv2/opencv.hpp>


// =============================================================================
// Construction and Destruction
// =============================================================================

ClaheDialog::ClaheDialog(QWidget* parent)
    : DialogBase(parent, tr("CLAHE"))
{
    m_mainWindow = getCallbacks();
    setWindowTitle(
        tr("CLAHE (Contrast Limited Adaptive Histogram Equalization)"));
    resize(800, 600);

    // Capture the current image from the active viewer
    if (m_mainWindow &&
        m_mainWindow->getCurrentViewer() &&
        m_mainWindow->getCurrentViewer()->getBuffer().isValid()) {
        m_sourceImage = m_mainWindow->getCurrentViewer()->getBuffer();
    }

    // Debounce timer: triggers preview update 200 ms after last slider change
    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(200);
    connect(m_previewTimer, &QTimer::timeout,
            this, &ClaheDialog::updatePreview);

    setupUi();

    // Defer initial preview until the dialog is fully laid out
    QTimer::singleShot(300, this, &ClaheDialog::updatePreview);
}

ClaheDialog::~ClaheDialog()
{
}


// =============================================================================
// Event Filter -- Wheel Zoom on Preview
// =============================================================================

bool ClaheDialog::eventFilter(QObject* obj, QEvent* ev)
{
    if (obj == m_view->viewport() && ev->type() == QEvent::Wheel) {
        QWheelEvent* we = static_cast<QWheelEvent*>(ev);
        float factor  = (we->angleDelta().y() > 0) ? 1.15f : (1.0f / 1.15f);
        float newZoom = std::clamp(m_zoom * factor, 0.05f, 32.0f);
        float actual  = newZoom / m_zoom;
        m_zoom = newZoom;
        m_view->scale(actual, actual);
        return true;
    }
    return DialogBase::eventFilter(obj, ev);
}


// =============================================================================
// Source Image Management
// =============================================================================

void ClaheDialog::setSource(const ImageBuffer& img)
{
    m_sourceImage = img;
    updatePreview();
}


// =============================================================================
// UI Construction
// =============================================================================

void ClaheDialog::setupUi()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // ---- Parameter controls ----
    QGroupBox* grp    = new QGroupBox(tr("Parameters"));
    QGridLayout* grid = new QGridLayout(grp);

    // Clip limit slider (internal range: 1..5000, display: 0.01..50.00)
    m_clipSlider = new QSlider(Qt::Horizontal);
    m_clipSlider->setRange(1, 5000);
    m_clipSlider->setValue(200);
    m_clipSlider->setPageStep(10);
    m_clipLabel = new QLabel("2.00");
    m_clipLabel->setMinimumWidth(40);

    connect(m_clipSlider, &QSlider::valueChanged, [this](int val) {
        m_clipLabel->setText(QString::number(val / 100.0, 'f', 2));
        if (m_chkPreview && m_chkPreview->isChecked())
            m_previewTimer->start();
    });

    grid->addWidget(new QLabel(tr("Clip Limit:")), 0, 0);
    grid->addWidget(m_clipSlider,                  0, 1);
    grid->addWidget(m_clipLabel,                   0, 2);

    // Tile grid size slider (1..128)
    m_tileSlider = new QSlider(Qt::Horizontal);
    m_tileSlider->setRange(1, 128);
    m_tileSlider->setValue(8);
    m_tileSlider->setPageStep(10);
    m_tileLabel = new QLabel("8x8");
    m_tileLabel->setMinimumWidth(40);

    connect(m_tileSlider, &QSlider::valueChanged, [this](int val) {
        m_tileLabel->setText(QString("%1x%1").arg(val));
        if (m_chkPreview && m_chkPreview->isChecked())
            m_previewTimer->start();
    });

    grid->addWidget(new QLabel(tr("Grid Size:")), 1, 0);
    grid->addWidget(m_tileSlider,                 1, 1);
    grid->addWidget(m_tileLabel,                  1, 2);

    // Opacity slider (0..100%)
    m_opacitySlider = new QSlider(Qt::Horizontal);
    m_opacitySlider->setRange(0, 100);
    m_opacitySlider->setValue(100);
    m_opacityLabel = new QLabel("100%");
    m_opacityLabel->setMinimumWidth(40);

    connect(m_opacitySlider, &QSlider::valueChanged, [this](int val) {
        m_opacityLabel->setText(QString("%1%").arg(val));
        if (m_chkPreview && m_chkPreview->isChecked())
            m_previewTimer->start();
    });

    grid->addWidget(new QLabel(tr("Opacity:")), 2, 0);
    grid->addWidget(m_opacitySlider,            2, 1);
    grid->addWidget(m_opacityLabel,             2, 2);

    mainLayout->addWidget(grp);

    // ---- Preview viewport ----
    m_scene = new QGraphicsScene(this);
    m_view  = new QGraphicsView(m_scene);
    m_view->setBackgroundBrush(Qt::black);
    m_view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    m_view->setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    m_view->viewport()->installEventFilter(this);

    m_pixmapItem = new QGraphicsPixmapItem();
    m_scene->addItem(m_pixmapItem);
    mainLayout->addWidget(m_view, 1);

    // ---- Preview toggle ----
    m_chkPreview = new QCheckBox(tr("Preview"), this);
    m_chkPreview->setChecked(true);

    connect(m_chkPreview, &QCheckBox::toggled, [this](bool on) {
        if (on) {
            updatePreview();
        } else {
            // Show the original unprocessed image
            if (m_sourceImage.isValid()) {
                QImage qimg = m_sourceImage.getDisplayImage();
                m_pixmapItem->setPixmap(QPixmap::fromImage(qimg));
                m_scene->setSceneRect(m_pixmapItem->boundingRect());
            }
        }
    });

    // ---- Action buttons ----
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* btnReset  = new QPushButton(tr("Reset"));
    QPushButton* btnApply  = new QPushButton(tr("Apply"));
    QPushButton* btnClose  = new QPushButton(tr("Close"));

    connect(btnReset, &QPushButton::clicked, this, &ClaheDialog::onReset);
    connect(btnApply, &QPushButton::clicked, this, &ClaheDialog::onApply);
    connect(btnClose, &QPushButton::clicked, this, &QDialog::reject);

    btnLayout->addWidget(m_chkPreview);
    btnLayout->addStretch();
    btnLayout->addWidget(btnReset);
    btnLayout->addWidget(btnClose);
    btnLayout->addWidget(btnApply);
    mainLayout->addLayout(btnLayout);
}


// =============================================================================
// Reset to Default Parameters
// =============================================================================

void ClaheDialog::onReset()
{
    // Block signals to prevent multiple preview updates during reset
    m_clipSlider->blockSignals(true);
    m_tileSlider->blockSignals(true);

    m_clipSlider->setValue(200);
    m_clipLabel->setText("2.00");

    m_tileSlider->setValue(8);
    m_tileLabel->setText("8x8");

    if (m_opacitySlider) {
        m_opacitySlider->blockSignals(true);
        m_opacitySlider->setValue(100);
        m_opacityLabel->setText("100%");
        m_opacitySlider->blockSignals(false);
    }

    m_clipSlider->blockSignals(false);
    m_tileSlider->blockSignals(false);

    if (m_chkPreview)
        m_chkPreview->setChecked(true);

    updatePreview();
}


// =============================================================================
// Preview Update
// =============================================================================

void ClaheDialog::updatePreview()
{
    if (!m_sourceImage.isValid())
        return;
    if (m_chkPreview && !m_chkPreview->isChecked())
        return;

    float clip = m_clipSlider->value() / 100.0f;
    int   grid = m_tileSlider->value();

    // Run CLAHE on the source image
    createPreview(m_sourceImage, clip, grid);

    // Apply opacity blending and/or mask blending
    float opacity = m_opacitySlider
                    ? m_opacitySlider->value() / 100.0f
                    : 1.0f;

    ImageBuffer displayBuf = m_previewImage;

    if (m_sourceImage.hasMask()) {
        MaskLayer ml = *m_sourceImage.getMask();
        ml.opacity *= opacity;
        displayBuf.setMask(ml);
        displayBuf.blendResult(m_sourceImage);
    } else if (opacity < 1.0f) {
        const auto& orig = m_sourceImage.data();
        auto&       bd   = displayBuf.data();
        for (size_t i = 0; i < bd.size(); ++i)
            bd[i] = bd[i] * opacity + orig[i] * (1.0f - opacity);
    }

    // Update the graphics view
    QImage qimg = displayBuf.getDisplayImage();
    m_pixmapItem->setPixmap(QPixmap::fromImage(qimg));
    m_scene->setSceneRect(m_pixmapItem->boundingRect());

    // Fit to view only on the very first display
    if (m_firstDisplay) {
        m_view->fitInView(m_pixmapItem, Qt::KeepAspectRatio);
        m_zoom = static_cast<float>(m_view->transform().m11());
        m_firstDisplay = false;
    }
}


// =============================================================================
// CLAHE Processing
//
// For color images: converts to Lab space, applies CLAHE on the L channel
// at 16-bit precision, then converts back to RGB.
// For grayscale: applies CLAHE directly at 16-bit precision.
// =============================================================================

void ClaheDialog::createPreview(const ImageBuffer& src, float clipLimit,
                                 int tileGridSize)
{
    const int w  = src.width();
    const int h  = src.height();
    const int c  = src.channels();
    const float* data = src.data().data();

    auto clahe = cv::createCLAHE(clipLimit,
        cv::Size(tileGridSize, tileGridSize));

    ImageBuffer out = src;
    float* outData  = out.data().data();

    if (c == 3) {
        // ---- Color path: CLAHE on L channel in Lab space ----

        // Convert float [0,1] RGB to 32F BGR for OpenCV
        cv::Mat mat32f(h, w, CV_32FC3);

        #pragma omp parallel for
        for (int i = 0; i < w * h; ++i) {
            int y = i / w, x = i % w;
            mat32f.at<cv::Vec3f>(y, x)[0] = std::clamp(data[i*3 + 2], 0.0f, 1.0f);  // B
            mat32f.at<cv::Vec3f>(y, x)[1] = std::clamp(data[i*3 + 1], 0.0f, 1.0f);  // G
            mat32f.at<cv::Vec3f>(y, x)[2] = std::clamp(data[i*3 + 0], 0.0f, 1.0f);  // R
        }

        // Convert to Lab color space (L range: [0, 100])
        cv::Mat lab;
        cv::cvtColor(mat32f, lab, cv::COLOR_BGR2Lab);
        std::vector<cv::Mat> lab_planes;
        cv::split(lab, lab_planes);

        // Scale L channel to 16-bit for CLAHE (0..100 -> 0..65535)
        lab_planes[0].convertTo(lab_planes[0], CV_16U, 65535.0 / 100.0);
        clahe->apply(lab_planes[0], lab_planes[0]);
        lab_planes[0].convertTo(lab_planes[0], CV_32F, 100.0 / 65535.0);

        // Merge and convert back to BGR
        cv::merge(lab_planes, lab);
        cv::Mat res;
        cv::cvtColor(lab, res, cv::COLOR_Lab2BGR);

        // Convert 32F BGR back to float [0,1] RGB
        #pragma omp parallel for
        for (int i = 0; i < w * h; ++i) {
            int y = i / w, x = i % w;
            outData[i*3 + 0] = std::clamp(res.at<cv::Vec3f>(y, x)[2], 0.0f, 1.0f);  // R
            outData[i*3 + 1] = std::clamp(res.at<cv::Vec3f>(y, x)[1], 0.0f, 1.0f);  // G
            outData[i*3 + 2] = std::clamp(res.at<cv::Vec3f>(y, x)[0], 0.0f, 1.0f);  // B
        }

    } else {
        // ---- Grayscale path: CLAHE on 16-bit data ----

        cv::Mat mat16(h, w, CV_16UC1);

        #pragma omp parallel for
        for (int i = 0; i < w * h; ++i) {
            mat16.at<uint16_t>(i / w, i % w) =
                static_cast<uint16_t>(
                    std::clamp(data[i], 0.0f, 1.0f) * 65535.0f);
        }

        cv::Mat res;
        clahe->apply(mat16, res);

        #pragma omp parallel for
        for (int i = 0; i < w * h; ++i) {
            outData[i] = res.at<uint16_t>(i / w, i % w) / 65535.0f;
        }
    }

    m_previewImage = out;
}


// =============================================================================
// Apply to Document
// =============================================================================

void ClaheDialog::onApply()
{
    float clip = m_clipSlider->value() / 100.0f;
    int   grid = m_tileSlider->value();

    if (!m_mainWindow)
        return;

    ImageViewer* viewer = m_mainWindow->getCurrentViewer();
    if (!viewer || !viewer->getBuffer().isValid())
        return;

    ImageBuffer& buffer = viewer->getBuffer();

    // Preserve original for mask blending or partial opacity
    ImageBuffer original;
    float opacity = m_opacitySlider
                    ? m_opacitySlider->value() / 100.0f
                    : 1.0f;

    if (buffer.hasMask() || opacity < 1.0f)
        original = buffer;

    viewer->pushUndo(tr("CLAHE"));

    // Apply CLAHE to the full image
    createPreview(buffer, clip, grid);
    buffer = m_previewImage;

    // Blend with mask and/or opacity
    if (original.isValid() && original.hasMask()) {
        MaskLayer ml = *original.getMask();
        ml.opacity *= opacity;
        buffer.setMask(ml);
        buffer.blendResult(original);
    } else if (original.isValid() && opacity < 1.0f) {
        const auto& orig = original.data();
        auto&       bd   = buffer.data();
        for (size_t i = 0; i < bd.size(); ++i)
            bd[i] = bd[i] * opacity + orig[i] * (1.0f - opacity);
    }

    viewer->refreshDisplay();
    m_mainWindow->logMessage(tr("CLAHE applied."), 1, true);

    accept();
}