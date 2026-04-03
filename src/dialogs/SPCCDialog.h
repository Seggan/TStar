#pragma once
#ifndef SPCCDIALOG_H
#define SPCCDIALOG_H

/**
 * @file SPCCDialog.h
 * @brief Spectrophotometric Color Calibration (SPCC) dialog interface.
 *
 * Provides a two-step workflow for spectrophotometric color calibration:
 *
 *   Step 1 - "Fetch Stars":
 *     Plate-solve the active image, query Gaia DR3 for in-field stars,
 *     and build a StarRecord list with Pickles SED matches.
 *     Optionally applies Gaia BP-RP colour fallback for unmatched stars.
 *
 *   Step 2 - "Run Calibration":
 *     Launch SPCC::calibrateWithStarList() on a background thread,
 *     display results, and apply the correction to the viewer.
 *
 *   Reset - Restore the original unmodified image buffer.
 *
 * Equipment combo boxes (white reference, R/G/B filters, sensor QE,
 * LP filters) are populated dynamically from tstar_data.fits via
 * SPCC::loadTStarFits(). User selections are persisted across sessions
 * using QSettings.
 */

#include <QDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
#include <QProgressBar>
#include <QTableWidget>
#include <QFutureWatcher>
#include <QSettings>

#include "ImageBuffer.h"
#include "SPCC.h"
#include "../photometry/CatalogClient.h"

/* Forward declarations */
class ImageViewer;
class MainWindow;
class QProgressDialog;

/**
 * @class SPCCDialog
 * @brief Dialog providing the SPCC user interface and workflow orchestration.
 */
class SPCCDialog : public QDialog {
    Q_OBJECT

public:
    explicit SPCCDialog(ImageViewer* viewer, MainWindow* mw,
                        QWidget* parent = nullptr);
    ~SPCCDialog() override;

    /**
     * @brief Update the associated viewer when the active document changes.
     * @param viewer Pointer to the new active ImageViewer.
     */
    void setViewer(ImageViewer* viewer);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    /* Step 1: Star fetching */
    void onFetchStars();
    void onFetchStarsFinished();
    void onCatalogReady(const std::vector<CatalogStar>& stars);
    void onCatalogError(const QString& err);

    /* Step 2: Calibration */
    void onRun();
    void onCalibrationFinished();

    /* General controls */
    void onReset();
    void onOpenSaspViewer();
    void onCancel();

    /* Settings persistence slots */
    void saveWhiteRefSetting(int);
    void saveRFilterSetting(int);
    void saveGFilterSetting(int);
    void saveBFilterSetting(int);
    void saveSensorSetting(int);
    void saveLpFilter1Setting(int);
    void saveLpFilter2Setting(int);
    void saveGradMethodSetting(const QString&);
    void saveSepThreshSetting(double);
    void saveFullMatrixSetting(bool);
    void saveLinearModeSetting(bool);

private:
    /* UI construction helpers */
    void buildUI();
    void connectSignals();
    void populateCombosFromFits();
    void restoreSettings();
    void showResults(const SPCCResult& r);
    void setControlsEnabled(bool en);

    /* Calibration execution */
    void startCalibration();
    SPCCParams collectParams() const;

    /* Resource cleanup */
    void cleanup();

    /* ----- UI elements: Equipment group ----- */
    QComboBox* m_whiteRefCombo   = nullptr;   ///< SED / white reference selector
    QComboBox* m_rFilterCombo    = nullptr;   ///< Red channel filter curve
    QComboBox* m_gFilterCombo    = nullptr;   ///< Green channel filter curve
    QComboBox* m_bFilterCombo    = nullptr;   ///< Blue channel filter curve
    QComboBox* m_sensorCombo     = nullptr;   ///< Sensor quantum efficiency curve
    QComboBox* m_lpFilter1Combo  = nullptr;   ///< Optional LP / cut filter 1
    QComboBox* m_lpFilter2Combo  = nullptr;   ///< Optional LP / cut filter 2

    /* ----- UI elements: Options group ----- */
    QDoubleSpinBox* m_sepThreshSpin   = nullptr;  ///< SEP detection threshold (sigma)
    QComboBox*      m_bgMethodCombo   = nullptr;  ///< Background subtraction method
    QComboBox*      m_gradMethodCombo = nullptr;  ///< Gradient surface polynomial degree
    QCheckBox*      m_fullMatrixCheck  = nullptr;  ///< Full 3x3 vs diagonal correction
    QCheckBox*      m_linearModeCheck  = nullptr;  ///< Linear vs polynomial application
    QCheckBox*      m_runGradientCheck = nullptr;  ///< Enable chromatic gradient extraction

    /* ----- UI elements: Results group ----- */
    QLabel*        m_starsLabel    = nullptr;  ///< Displays matched star count
    QLabel*        m_residualLabel = nullptr;  ///< Displays RMS residual
    QLabel*        m_scalesLabel   = nullptr;  ///< Displays R, G, B scale factors
    QLabel*        m_modelLabel    = nullptr;  ///< Displays fitted model type
    QTableWidget*  m_matrixTable   = nullptr;  ///< 3x3 correction matrix display

    /* ----- UI elements: Status / progress ----- */
    QProgressBar* m_progressBar = nullptr;
    QLabel*       m_statusLabel = nullptr;

    /* ----- UI elements: Action buttons ----- */
    QPushButton* m_fetchStarsBtn  = nullptr;   ///< Step 1 button
    QPushButton* m_runBtn         = nullptr;   ///< Step 2 button
    QPushButton* m_resetBtn       = nullptr;
    QPushButton* m_cancelBtn      = nullptr;
    QPushButton* m_saspViewerBtn  = nullptr;
    QPushButton* m_closeBtn       = nullptr;

    /* ----- Application state ----- */
    ImageViewer* m_viewer     = nullptr;
    MainWindow*  m_mainWindow = nullptr;
    ImageBuffer  m_originalBuffer;              ///< Snapshot for Reset functionality

    SPCCDataStore m_store;                      ///< In-memory spectral database
    bool          m_storeLoaded = false;        ///< Whether tstar_data.fits loaded OK

    std::vector<StarRecord> m_starList;         ///< Stars built by Step 1, used by Step 2

    /* Background worker infrastructure */
    QFutureWatcher<std::vector<StarRecord>>* m_fetchWatcher = nullptr;
    QFutureWatcher<SPCCResult>*              m_calibWatcher = nullptr;
    CatalogClient*                           m_catalog      = nullptr;
    std::atomic<bool>                        m_cancelFlag{false};

    QString m_dataPath;                         ///< Resolved path to spectral data directory

    static constexpr const char* kSettingsGroup = "SPCC";
};

#endif // SPCCDIALOG_H