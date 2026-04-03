#ifndef PCCDIALOG_H
#define PCCDIALOG_H

/**
 * @file PCCDialog.h
 * @brief Photometric Color Calibration dialog.
 *
 * Provides a user interface for running Photometric Color Calibration (PCC)
 * on plate-solved images. Downloads Gaia DR3 catalog data, performs aperture
 * photometry matching, and applies per-channel correction factors with
 * optional background neutralization.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#include <QDialog>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QPointer>
#include <QFutureWatcher>
#include <atomic>

#include "DialogBase.h"
#include "photometry/CatalogClient.h"
#include "photometry/PCCCalibrator.h"
#include "../ImageBuffer.h"
#include "../ImageViewer.h"

/**
 * @class PCCDialog
 * @brief Dialog for Photometric Color Calibration of astronomical images.
 *
 * Workflow:
 *  1. Verify the image has valid WCS coordinates (plate-solved).
 *  2. Query Gaia DR3 catalog for reference stars in the field.
 *  3. Run aperture-based photometric calibration asynchronously.
 *  4. Apply per-channel correction factors and optional background neutralization.
 */
class PCCDialog : public DialogBase {
    Q_OBJECT

public:
    explicit PCCDialog(ImageViewer* viewer, QWidget* parent = nullptr);

    /** @brief Update the target image viewer. */
    void setViewer(ImageViewer* v);

    /** @brief Retrieve the calibration result after completion. */
    PCCResult result() const { return m_result; }

private slots:
    void onRun();
    void onCatalogReady(const std::vector<CatalogStar>& stars);
    void onCatalogError(const QString& err);
    void onCalibrationFinished();
    void onCancel();

private:
    // -- UI Widgets --
    QPointer<ImageViewer>  m_viewer;
    QLabel*                m_status                 = nullptr;
    QCheckBox*             m_chkNeutralizeBackground = nullptr;
    QPushButton*           m_btnRun                 = nullptr;
    QPushButton*           m_btnCancel              = nullptr;

    // -- Cancellation --
    std::atomic<bool>      m_cancelFlag{false};

    // -- Processing objects --
    CatalogClient*                  m_catalog    = nullptr;
    PCCCalibrator*                  m_calibrator = nullptr;
    PCCResult                       m_result;
    QFutureWatcher<PCCResult>*      m_watcher    = nullptr;
};

#endif // PCCDIALOG_H