/**
 * @file PCCDialog.cpp
 * @brief Implementation of the Photometric Color Calibration dialog.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#include "PCCDialog.h"
#include "../MainWindowCallbacks.h"
#include "../ImageViewer.h"
#include "../astrometry/WCSUtils.h"
#include "../photometry/StarDetector.h"
#include "PCCDistributionDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QCoreApplication>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>

// ============================================================================
// Construction
// ============================================================================

PCCDialog::PCCDialog(ImageViewer* viewer, QWidget* parent)
    : DialogBase(parent, tr("Photometric Color Calibration"), 0, 0)
    , m_viewer(viewer)
{
    setMinimumWidth(400);
    resize(400, 150);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(6);

    // -- Status label --
    m_status = new QLabel(tr("Ready. Image must be plate solved."), this);
    m_status->setWordWrap(false);
    m_status->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    mainLayout->addWidget(m_status);

    // -- Background neutralization option --
    m_chkNeutralizeBackground = new QCheckBox(tr("Background Neutralization"), this);
    m_chkNeutralizeBackground->setChecked(true);
    mainLayout->addWidget(m_chkNeutralizeBackground);

    // -- Action buttons --
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    m_btnCancel = new QPushButton(tr("Cancel"), this);
    m_btnCancel->setEnabled(false);
    m_btnRun = new QPushButton(tr("Run PCC"), this);
    m_btnRun->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    buttonLayout->addWidget(m_btnCancel);
    buttonLayout->addWidget(m_btnRun);
    mainLayout->addLayout(buttonLayout);

    // -- Initialize status based on current image state --
    if (m_viewer && m_viewer->getBuffer().isValid()) {
        const ImageBuffer::Metadata& meta = m_viewer->getBuffer().metadata();

        if (!WCSUtils::hasValidWCS(meta)) {
            m_status->setText(tr("Warning: No WCS coordinates found!"));
        } else if (!meta.catalogStars.empty()) {
            m_status->setText(tr("Ready (Catalog stars cached)."));
        } else if (!meta.catalogStars.empty()) {
            m_status->setText(tr("Ready. Cached stars: %1").arg(meta.catalogStars.size()));
        }
    } else {
        m_status->setText(tr("Error: No valid image."));
        m_btnRun->setEnabled(false);
    }

    // -- Signal connections --
    connect(m_btnRun,    &QPushButton::clicked, this, &PCCDialog::onRun);
    connect(m_btnCancel, &QPushButton::clicked, this, &PCCDialog::onCancel);

    // -- Catalog client --
    m_catalog = new CatalogClient(this);
    connect(m_catalog, &CatalogClient::catalogReady,  this, &PCCDialog::onCatalogReady);
    connect(m_catalog, &CatalogClient::errorOccurred,  this, &PCCDialog::onCatalogError);
    connect(m_catalog, &CatalogClient::mirrorStatus,   this, [this](const QString& msg) {
        m_status->setText(msg);
    });

    // -- Calibrator and async watcher --
    m_calibrator = new PCCCalibrator();
    m_watcher    = new QFutureWatcher<PCCResult>(this);
    connect(m_watcher, &QFutureWatcher<PCCResult>::finished,
            this,      &PCCDialog::onCalibrationFinished);
}

// ============================================================================
// Viewer management
// ============================================================================

void PCCDialog::setViewer(ImageViewer* v)
{
    if (m_viewer == v)
        return;
    m_viewer = v;

    if (m_viewer && m_viewer->getBuffer().isValid()) {
        const ImageBuffer::Metadata& meta = m_viewer->getBuffer().metadata();

        if (!meta.catalogStars.empty()) {
            m_status->setText(tr("Ready. Cached stars: %1").arg(meta.catalogStars.size()));
        } else if (!WCSUtils::hasValidWCS(meta)) {
            m_status->setText(tr("Warning: No WCS coordinates found!"));
        } else {
            m_status->setText(tr("Ready (RA: %1, Dec: %2)").arg(meta.ra).arg(meta.dec));
        }
        m_btnRun->setEnabled(true);
    } else {
        m_status->setText(tr("Error: No valid image."));
        m_btnRun->setEnabled(false);
    }
}

// ============================================================================
// Run PCC
// ============================================================================

void PCCDialog::onRun()
{
    if (!m_viewer || !m_viewer->getBuffer().isValid()) {
        m_status->setText(tr("Error: Image closed."));
        return;
    }

    const ImageBuffer::Metadata& meta = m_viewer->getBuffer().metadata();

    // Use cached catalog stars if available
    if (!meta.catalogStars.empty()) {
        onCatalogReady(meta.catalogStars);
        return;
    }

    // Validate WCS before querying the catalog
    if (!WCSUtils::hasValidWCS(meta)) {
        QMessageBox::critical(this, tr("Error"),
            tr("Image must be plate solved before running PCC.\n"
               "After stacking, run the ASTAP solver first."));
        return;
    }

    // Start catalog download
    m_status->setText(tr("Downloading Gaia DR3 Catalog..."));
    m_btnRun->setEnabled(false);
    m_btnCancel->setEnabled(true);
    m_cancelFlag.store(false);
    m_catalog->queryGaiaDR3(meta.ra, meta.dec, 1.0);
}

// ============================================================================
// Catalog ready -- launch asynchronous calibration
// ============================================================================

void PCCDialog::onCatalogReady(const std::vector<CatalogStar>& stars)
{
    if (!m_viewer || !m_viewer->getBuffer().isValid())
        return;

    m_status->setText(tr("Catalog loaded (%1 stars). Running Calibration (Async)...")
                      .arg(stars.size()));
    m_btnRun->setEnabled(false);

    // Notify the main window of the long-running operation
    if (MainWindowCallbacks* mw = getCallbacks()) {
        mw->startLongProcess();
        mw->logMessage(tr("Starting PCC with %1 catalog stars...").arg(stars.size()), 0, false);
    }

    // Deep-copy the buffer for thread-safe background processing
    ImageBuffer bufferCopy = m_viewer->getBuffer();

    // Determine detection threshold: use higher sigma for stacked images
    float detectionThreshold = 2.0f;
    {
        const ImageBuffer::Metadata& m = m_viewer->getBuffer().metadata();
        if (m.stackCount > 1)
            detectionThreshold = 3.0f;
    }

    // Launch the calibration in a background thread
    std::atomic<bool>* flagPtr = &m_cancelFlag;
    QFuture<PCCResult> future = QtConcurrent::run(
        [bufferCopy, stars, flagPtr, detectionThreshold]()
    {
        // Step 1: Compute robust background median per channel
        //         Using sigma clipping bounds: -2.8 / +2.0
        float bg_r = bufferCopy.getRobustMedian(0, -2.8f, +2.0f);
        float bg_g = bufferCopy.getRobustMedian(1, -2.8f, +2.0f);
        float bg_b = bufferCopy.getRobustMedian(2, -2.8f, +2.0f);

        // Step 2: Detect stars per channel
        StarDetector detector;
        detector.setMaxStars(2000);
        detector.setThresholdSigma(detectionThreshold);

        std::vector<DetectedStar> starsR = detector.detect(bufferCopy, 0);
        std::vector<DetectedStar> starsG = detector.detect(bufferCopy, 1);
        std::vector<DetectedStar> starsB = detector.detect(bufferCopy, 2);

        // Step 3: Configure calibrator with WCS parameters
        ImageBuffer::Metadata meta = bufferCopy.metadata();
        PCCCalibrator calibrator;
        calibrator.setCancelFlag(flagPtr);
        calibrator.setWCS(meta.ra, meta.dec, meta.crpix1, meta.crpix2,
                          meta.cd1_1, meta.cd1_2, meta.cd2_1, meta.cd2_2);

        // Step 4: Run aperture-based photometric calibration
        PCCResult result = calibrator.calibrateWithAperture(bufferCopy, stars);

        // Step 5: Attach background statistics to the result
        result.bg_r = bg_r;
        result.bg_g = bg_g;
        result.bg_b = bg_b;

        return result;
    });

    // Guard against concurrent executions (button should be disabled)
    if (m_watcher->isRunning())
        return;

    m_watcher->setFuture(future);
}

// ============================================================================
// Calibration finished -- apply correction to the image
// ============================================================================

void PCCDialog::onCalibrationFinished()
{
    if (!m_viewer)
        return;

    PCCResult res = m_watcher->result();
    m_result = res;

    // Persist result in image metadata for downstream tools
    ImageBuffer::Metadata meta = m_viewer->getBuffer().metadata();
    meta.pccResult = res;
    m_viewer->getBuffer().setMetadata(meta);

    m_btnRun->setEnabled(true);
    m_btnCancel->setEnabled(false);

    MainWindowCallbacks* mw = getCallbacks();
    if (mw)
        mw->endLongProcess();

    if (!res.valid) {
        // Calibration failed
        m_status->setText(tr("Calibration Failed."));
        QMessageBox::warning(this, tr("PCC Failed"),
            tr("Could not match enough stars. Check WCS and Image Quality."));
    } else {
        // Calibration succeeded -- apply correction
        m_status->setText(tr("Success. Applying correction..."));

        const bool neutralize = m_chkNeutralizeBackground->isChecked();

        // Compute per-channel background levels using robust median
        constexpr float kSigmaLow  = -2.8f;
        constexpr float kSigmaHigh = +2.0f;

        float bg[3];
        bg[0] = m_viewer->getBuffer().getRobustMedian(0, kSigmaLow, kSigmaHigh);
        bg[1] = m_viewer->getBuffer().getRobustMedian(1, kSigmaLow, kSigmaHigh);
        bg[2] = m_viewer->getBuffer().getRobustMedian(2, kSigmaLow, kSigmaHigh);

        // Zero out background if neutralization is disabled
        if (!neutralize) {
            bg[0] = 0.0f;
            bg[1] = 0.0f;
            bg[2] = 0.0f;
        }

        // Build correction factors and offsets
        float kw[3] = {
            static_cast<float>(res.R_factor),
            static_cast<float>(res.G_factor),
            static_cast<float>(res.B_factor)
        };
        float bg_mean = (bg[0] + bg[1] + bg[2]) / 3.0f;

        float offset[3];
        for (int c = 0; c < 3; ++c)
            offset[c] = -bg[c] * kw[c] + bg_mean;

        // Format result message
        QString msg = tr(
            "Factors (K):\nR: %1\nG: %2\nB: %3\n\n"
            "Background Ref (Detected):\nR: %4\nG: %5\nB: %6\n\n"
            "Computed Offsets:\nR: %7\nG: %8\nB: %9")
            .arg(kw[0], 0, 'f', 6).arg(kw[1], 0, 'f', 6).arg(kw[2], 0, 'f', 6)
            .arg(bg[0], 0, 'e', 5).arg(bg[1], 0, 'e', 5).arg(bg[2], 0, 'e', 5)
            .arg(offset[0], 0, 'e', 5).arg(offset[1], 0, 'e', 5).arg(offset[2], 0, 'e', 5);

        // Save undo state and apply correction
        m_viewer->pushUndo(tr("PCC"));
        m_viewer->getBuffer().applyPCC(kw[0], kw[1], kw[2], bg[0], bg[1], bg[2], bg_mean);
        m_viewer->refreshDisplay();

        if (mw) {
            mw->logMessage(msg, 1, true);
            mw->logMessage(tr("PCC applied."), 1);
        }

        QMessageBox::information(this, tr("PCC Result"), msg);
        accept();
    }
}

// ============================================================================
// Error and cancellation handlers
// ============================================================================

void PCCDialog::onCatalogError(const QString& err)
{
    m_status->setText(tr("Catalog Error: %1").arg(err));
    m_btnRun->setEnabled(true);
    m_btnCancel->setEnabled(false);
}

void PCCDialog::onCancel()
{
    m_cancelFlag.store(true);
    m_status->setText(tr("Cancelling..."));
    // CatalogClient does not support direct cancellation;
    // the result will be handled by catalogReady or errorOccurred callbacks.
}