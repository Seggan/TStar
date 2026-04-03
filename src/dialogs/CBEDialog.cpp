// =============================================================================
// CBEDialog.cpp
//
// Implementation of the Catalog Background Extraction dialog.
// Coordinates the full pipeline: HiPS survey download, WCS reprojection,
// interactive alignment verification, and gradient extraction/subtraction.
// =============================================================================

#include "CBEDialog.h"
#include "../MainWindowCallbacks.h"
#include "../ImageViewer.h"
#include "../background/CatalogGradientExtractor.h"
#include "../astrometry/WCSUtils.h"
#include "ReferenceAlignDialog.h"

#include <algorithm>
#include <cmath>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QMessageBox>
#include <QApplication>
#include <QProgressDialog>
#include <QtConcurrent>


// =============================================================================
// Construction and Lifecycle
// =============================================================================

CBEDialog::CBEDialog(QWidget* pParent, ImageViewer* viewer,
                     const ImageBuffer& buffer)
    : DialogBase(pParent)
    , m_viewer(viewer)
    , m_originalBuffer(buffer)
{
    setWindowTitle(tr("Catalog Background Extraction"));
    setMinimumWidth(380);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    QFormLayout* form = new QFormLayout;

    // ---- Survey selector ----
    m_comboSurvey = new QComboBox(this);
    m_comboSurvey->addItem(tr("PanSTARRS DR1 (Color)"),
                           HiPSClient::SUR_PANSTARRS_DR1_COLOR);
    m_comboSurvey->addItem(tr("DSS2 Red (Optical)"),
                           HiPSClient::SUR_DSS2_RED);
    m_comboSurvey->addItem(tr("unWISE (Infrared)"),
                           HiPSClient::SUR_UNWISE_COLOR);
    m_comboSurvey->setCurrentIndex(1);
    form->addRow(tr("HiPS Survey:"), m_comboSurvey);

    // ---- Smoothing scale ----
    m_spinScale = new QSpinBox(this);
    m_spinScale->setRange(16, 1024);
    m_spinScale->setValue(128);
    m_spinScale->setSingleStep(16);
    m_spinScale->setSuffix(tr(" px"));
    m_spinScale->setToolTip(
        tr("Gaussian sigma for large-scale background extraction.\n"
           "Larger values capture broader gradients."));
    form->addRow(tr("Smoothing Scale:"), m_spinScale);

    mainLayout->addLayout(form);

    // ---- Processing options ----
    m_checkProtectStars = new QCheckBox(
        tr("Protect Stars (Morphological filter)"), this);
    m_checkProtectStars->setChecked(true);
    m_checkProtectStars->setToolTip(
        tr("Applies morphological opening before blurring\n"
           "to prevent bright stars from biasing the gradient map."));
    mainLayout->addWidget(m_checkProtectStars);

    m_checkGradientMap = new QCheckBox(
        tr("Output Background Map"), this);
    m_checkGradientMap->setToolTip(
        tr("Instead of correcting the image, output the\n"
           "computed gradient map for inspection."));
    mainLayout->addWidget(m_checkGradientMap);

    mainLayout->addStretch();

    // ---- Action buttons ----
    QHBoxLayout* btnLayout = new QHBoxLayout;

    QPushButton* btnClearCache = new QPushButton(tr("Clear Cache"), this);
    btnClearCache->setToolTip(
        tr("Delete all cached HiPS reference images."));
    btnLayout->addWidget(btnClearCache);
    btnLayout->addStretch();

    m_btnCancel = new QPushButton(tr("Cancel"), this);
    m_btnCancel->setEnabled(false);
    btnLayout->addWidget(m_btnCancel);

    QPushButton* btnClose = new QPushButton(tr("Close"), this);
    btnLayout->addWidget(btnClose);

    m_btnApply = new QPushButton(tr("Apply"), this);
    m_btnApply->setDefault(true);
    btnLayout->addWidget(m_btnApply);

    mainLayout->addLayout(btnLayout);

    // ---- Signal connections ----
    connect(m_btnApply,  &QPushButton::clicked, this, &CBEDialog::onApply);
    connect(m_btnCancel, &QPushButton::clicked, this, &CBEDialog::onCancel);
    connect(btnClose,    &QPushButton::clicked, this, &QDialog::close);

    connect(btnClearCache, &QPushButton::clicked, this, [this]() {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        m_hipsClient->clearCache();
        QApplication::restoreOverrideCursor();
        QMessageBox::information(this, tr("Cache Cleared"),
            tr("Reference image cache has been emptied."));
    });

    // Initialize the HiPS download client
    m_hipsClient = new HiPSClient(this);
    connect(m_hipsClient, &HiPSClient::imageReady,
            this, &CBEDialog::onHiPSImageReady);
    connect(m_hipsClient, &HiPSClient::errorOccurred,
            this, &CBEDialog::onHiPSError);

    if (auto cb = getCallbacks())
        cb->logMessage(tr("Catalog Background Extraction tool active."), 0);
}

CBEDialog::~CBEDialog()
{
}

void CBEDialog::setViewer(ImageViewer* viewer)
{
    m_viewer = viewer;
    if (viewer && viewer->getBuffer().isValid())
        m_originalBuffer = viewer->getBuffer();
}

void CBEDialog::closeEvent(QCloseEvent* event)
{
    DialogBase::closeEvent(event);
}


// =============================================================================
// Apply -- Initiate the CBE Pipeline
//
// 1. Validate WCS metadata.
// 2. Compute field-of-view and orientation from the WCS.
// 3. Request reference survey image from HiPS.
// =============================================================================

void CBEDialog::onApply()
{
    const ImageBuffer& currentBuf =
        (m_viewer && m_viewer->getBuffer().isValid())
            ? m_viewer->getBuffer()
            : m_originalBuffer;

    const auto& meta = currentBuf.metadata();

    // Verify that the image has valid astrometric solution
    if (!WCSUtils::hasValidWCS(meta)) {
        QMessageBox::warning(this, tr("No Astrometry"),
            tr("Image must be Plate Solved before using "
               "Catalog Background Extraction."));
        return;
    }

    m_btnApply->setEnabled(false);
    m_btnCancel->setEnabled(true);
    m_cancelFlag.store(false);
    setCursor(Qt::WaitCursor);

    if (auto cb = getCallbacks()) {
        cb->logMessage(tr("Requesting reference survey from HiPS..."), 0);
        cb->startLongProcess();
    }

    // Compute field-of-view from the WCS CD matrix
    double fov_x = 0, fov_y = 0;
    bool hasFov = WCSUtils::getFieldOfView(
        meta, currentBuf.width(), currentBuf.height(), fov_x, fov_y);

    if (!hasFov) {
        QMessageBox::warning(this, tr("No WCS"),
            tr("Image has no valid WCS / CD matrix.\n"
               "Please plate-solve the image before using "
               "Catalog Background Extraction.\n\n"
               "Without WCS the reference image cannot be correctly "
               "oriented and scaled."));
        m_btnApply->setEnabled(true);
        unsetCursor();
        if (auto cb = getCallbacks()) cb->endLongProcess();
        return;
    }

    // Use the larger FoV dimension (hips2fits convention)
    double fov_base = std::max(fov_x, fov_y);

    // Sanity check: realistic FoV is 0.001 - 60 degrees
    if (fov_base < 0.001 || fov_base > 60.0) {
        if (auto cb = getCallbacks())
            cb->logMessage(
                tr("WARNING: computed FoV (%1 deg) is out of range. "
                   "WCS may be stale. Re-plate-solving is recommended. "
                   "Falling back to 1.0 deg.").arg(fov_base, 0, 'f', 4), 2);
        fov_base = 1.0;
        fov_x = fov_base;
        fov_y = fov_base;
    }

    double fov_to_send = fov_base * m_paddingFactor;

    // Extract orientation from WCS
    double rotAngle    = WCSUtils::positionAngle(meta);
    m_parityFlipped    = WCSUtils::isParityFlipped(meta);
    m_targetWidth      = currentBuf.width();
    m_targetHeight     = currentBuf.height();

    // Cap download dimensions for network reliability
    static const int MAX_HIPS_DIM = 2048;
    int reqW = static_cast<int>(std::round(m_targetWidth  * m_paddingFactor));
    int reqH = static_cast<int>(std::round(m_targetHeight * m_paddingFactor));

    if (reqW > MAX_HIPS_DIM || reqH > MAX_HIPS_DIM) {
        double scale = std::min(
            static_cast<double>(MAX_HIPS_DIM) / reqW,
            static_cast<double>(MAX_HIPS_DIM) / reqH);
        reqW = std::max(1, static_cast<int>(std::round(reqW * scale)));
        reqH = std::max(1, static_cast<int>(std::round(reqH * scale)));
    }

    if (auto cb = getCallbacks()) {
        cb->logMessage(
            tr("HiPS request: %1x%2 px, FoV=%3 deg (%4 degx%5 deg), "
               "rot=%6 deg, parity=%7")
                .arg(reqW).arg(reqH)
                .arg(fov_to_send, 0, 'f', 4)
                .arg(fov_x, 0, 'f', 4).arg(fov_y, 0, 'f', 4)
                .arg(rotAngle, 0, 'f', 1)
                .arg(m_parityFlipped ? tr("flipped") : tr("normal")), 0);

        if (m_parityFlipped || std::abs(rotAngle) > 10.0)
            cb->logMessage(
                tr("  (non-standard orientation -- ensure image has been "
                   "recently plate-solved)"), 0);
    }

    QString survey = m_comboSurvey->currentData().toString();
    m_hipsClient->fetchFITS(survey, meta.ra, meta.dec,
                            fov_to_send, reqW, reqH, rotAngle);
}


// =============================================================================
// HiPS Callback -- Reference Image Received
//
// Performs WCS reprojection to align the reference image to the target
// coordinate frame, launches the interactive alignment dialog, then
// runs the gradient extraction algorithm.
// =============================================================================

void CBEDialog::onHiPSImageReady(const ImageBuffer& refImg)
{
    if (auto cb = getCallbacks()) {
        cb->endLongProcess();
        cb->logMessage(
            tr("Reference received (%1x%2, %3ch). "
               "Running gradient extraction...")
                .arg(refImg.width()).arg(refImg.height())
                .arg(refImg.channels()), 0);
    }

    // Capture current extraction options
    Background::CatalogGradientExtractor::Options opts;
    opts.blurScale        = m_spinScale->value();
    opts.protectStars     = m_checkProtectStars->isChecked();
    opts.outputGradientMap = m_checkGradientMap->isChecked();

    ImageBuffer target = m_originalBuffer;
    ImageBuffer ref    = refImg;

    // -------------------------------------------------------------------------
    // WCS Reprojection: align the downloaded reference to the target frame
    // using bilinear interpolation through WCS coordinate transforms.
    // -------------------------------------------------------------------------
    if (ref.isValid() &&
        WCSUtils::hasValidWCS(ref.metadata()) &&
        WCSUtils::hasValidWCS(target.metadata())) {

        if (auto cb = getCallbacks())
            cb->logMessage(
                tr("Aligning reference image exactly to target WCS..."), 0);

        // Cap alignment resolution to save CPU (gradient map is heavily smoothed)
        static const int MAX_ALIGN_DIM = 2048;
        int W = target.width();
        int H = target.height();
        if (W > MAX_ALIGN_DIM || H > MAX_ALIGN_DIM) {
            double scale = std::min(
                static_cast<double>(MAX_ALIGN_DIM) / W,
                static_cast<double>(MAX_ALIGN_DIM) / H);
            W = std::max(1, static_cast<int>(std::round(W * scale)));
            H = std::max(1, static_cast<int>(std::round(H * scale)));
        }

        ImageBuffer aligned(W, H, ref.channels());
        aligned.setMetadata(target.metadata());

        double scaleX = static_cast<double>(target.width())  / W;
        double scaleY = static_cast<double>(target.height()) / H;

        int Ws = ref.width();
        int Hs = ref.height();
        int ch = ref.channels();

        const auto& tgtMeta = target.metadata();
        const auto& refMeta = ref.metadata();
        const float* srcPtr = ref.data().data();
        float*       dstPtr = aligned.data().data();

        #pragma omp parallel for
        for (int y = 0; y < H; ++y) {
            double targetY = y * scaleY;
            for (int x = 0; x < W; ++x) {
                double targetX = x * scaleX;
                double ra = 0, dec = 0;
                int dstIdx = (y * W + x) * ch;

                if (WCSUtils::pixelToWorld(tgtMeta, targetX, targetY,
                                            ra, dec)) {
                    double srcX = 0, srcY = 0;
                    if (WCSUtils::worldToPixel(refMeta, ra, dec,
                                                srcX, srcY)) {
                        int ix = static_cast<int>(std::floor(srcX));
                        int iy = static_cast<int>(std::floor(srcY));

                        if (ix >= 0 && ix < Ws - 1 &&
                            iy >= 0 && iy < Hs - 1) {
                            // Bilinear interpolation
                            double fx = srcX - ix;
                            double fy = srcY - iy;
                            double w00 = (1 - fx) * (1 - fy);
                            double w10 = fx       * (1 - fy);
                            double w01 = (1 - fx) * fy;
                            double w11 = fx       * fy;

                            for (int c = 0; c < ch; ++c) {
                                double v00 = srcPtr[(iy     * Ws + ix)     * ch + c];
                                double v10 = srcPtr[(iy     * Ws + ix + 1) * ch + c];
                                double v01 = srcPtr[((iy+1) * Ws + ix)     * ch + c];
                                double v11 = srcPtr[((iy+1) * Ws + ix + 1) * ch + c];
                                dstPtr[dstIdx + c] = static_cast<float>(
                                    v00 * w00 + v10 * w10 +
                                    v01 * w01 + v11 * w11);
                            }
                            continue;
                        }
                    }
                }

                // Fill unmapped pixels with zero
                for (int c = 0; c < ch; ++c)
                    dstPtr[dstIdx + c] = 0.0f;
            }
        }

        ref = aligned;
    }

    // -------------------------------------------------------------------------
    // Interactive Alignment Verification
    // -------------------------------------------------------------------------
    ReferenceAlignDialog alignDlg(this, ref, target, 1.0);
    if (alignDlg.exec() != QDialog::Accepted) {
        if (auto cb = getCallbacks())
            cb->logMessage(
                tr("Reference alignment cancelled. "
                   "Background extraction aborted."), 2);
        m_btnApply->setEnabled(true);
        unsetCursor();
        if (m_busyDialog) m_busyDialog->hide();
        return;
    }

    ref = alignDlg.getAlignedBuffer();

    // -------------------------------------------------------------------------
    // Background Gradient Extraction (threaded)
    // -------------------------------------------------------------------------
    if (!m_busyDialog) {
        m_busyDialog = new QProgressDialog(
            tr("Running CBE..."), QString(), 0, 0, this);
        m_busyDialog->setWindowTitle(tr("Catalog Background Extraction"));
        m_busyDialog->setWindowModality(Qt::WindowModal);
        m_busyDialog->setCancelButton(nullptr);
        m_busyDialog->setMinimumDuration(0);
    }
    m_busyDialog->setLabelText(tr("Running CBE..."));
    m_busyDialog->show();

    std::atomic<bool>* flagPtr = &m_cancelFlag;

    auto progress = [this](int pct) {
        QMetaObject::invokeMethod(this, [this, pct]() {
            if (m_busyDialog) m_busyDialog->setValue(pct);
        }, Qt::QueuedConnection);
    };

    auto future = QtConcurrent::run(
        [target, ref, opts, flagPtr, progress]() mutable
        -> QPair<bool, ImageBuffer> {
            bool ok = Background::CatalogGradientExtractor::extract(
                target, ref, opts, progress, flagPtr);
            return qMakePair(ok, target);
        });

    auto* watcher = new QFutureWatcher<QPair<bool, ImageBuffer>>(this);
    connect(watcher,
        &QFutureWatcher<QPair<bool, ImageBuffer>>::finished,
        this, [this, watcher]() {
            auto result = watcher->result();
            watcher->deleteLater();

            if (result.first) {
                if (auto cb = getCallbacks())
                    cb->logMessage(
                        tr("Background extracted successfully."), 1);
                emit applyResult(result.second);
            } else {
                if (auto cb = getCallbacks())
                    cb->logMessage(
                        tr("Extraction failed during gradient matching."), 3);
                QMessageBox::warning(this, tr("Error"),
                    tr("Failed to match gradients with the reference catalog."));
            }

            m_btnApply->setEnabled(true);
            m_btnCancel->setEnabled(false);
            unsetCursor();
            if (m_busyDialog) m_busyDialog->hide();
        });

    watcher->setFuture(future);
}


// =============================================================================
// Error and Cancel Handling
// =============================================================================

void CBEDialog::onHiPSError(const QString& err)
{
    if (auto cb = getCallbacks()) {
        cb->endLongProcess();
        cb->logMessage(tr("Catalog download failed: %1").arg(err), 3);
    }

    QMessageBox::critical(this, tr("HiPS Error"), err);
    m_btnApply->setEnabled(true);
    m_btnCancel->setEnabled(false);
    unsetCursor();
    if (m_busyDialog) m_busyDialog->hide();
}

void CBEDialog::onCancel()
{
    m_cancelFlag.store(true);
    if (m_hipsClient) m_hipsClient->cancel();
    if (m_busyDialog) m_busyDialog->setLabelText(tr("Cancelling..."));
}