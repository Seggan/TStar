/*

#pragma once

#ifndef DECONVOLUTIONDIALOG_H
#define DECONVOLUTIONDIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QGroupBox>
#include <QProgressBar>
#include <QTabWidget>
#include <QSlider>
#include <QListWidget>
#include <QLineEdit>
#include <QRadioButton>
#include <QButtonGroup>
#include <QFutureWatcher>
#include <QThread>
#include <QTimer>
#include "ImageBuffer.h"
#include "Deconvolution.h"

class ImageViewer;
class MainWindow;

// ─── MFDeconvWorker ───────────────────────────────────────────────────────────

// Worker QThread for Multi-Frame Deconvolution.
// Runs Deconvolution::applyMultiFrame in a separate thread and
// reports progress and completion to the UI.
// Emits:
//   - statusMessage(QString) for generic log lines
//   - progressUpdated(int, QString) for __PROGRESS__ xxx.x messages
//   - finished(bool, QString, QString) → success, message, outPath

class MFDeconvWorker : public QThread {
    Q_OBJECT
public:
    explicit MFDeconvWorker(const MFDeconvParams& params, QObject* parent = nullptr);
    ~MFDeconvWorker() override;

    void requestStop();

signals:
    void statusMessage(const QString& msg);
    void progressUpdated(int percent, const QString& msg);
    void finished(bool success, const QString& message, const QString& outPath);

protected:
    void run() override;

private:
    MFDeconvParams m_params;
    bool           m_stopRequested = false;

    /// Parses "__PROGRESS__ 0.xxxx msg" lines emitted by the backend callback
    /// and converts them into progressUpdated signals.
    void handleStatusLine(const QString& line);
};

// ─── DeconvolutionDialog ──────────────────────────────────────────────────────
class DeconvolutionDialog : public QDialog {
    Q_OBJECT

public:
    explicit DeconvolutionDialog(ImageViewer* viewer, MainWindow* mw,
                                  QWidget* parent = nullptr);
    ~DeconvolutionDialog() override;

    void setViewer(ImageViewer* viewer);

protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    // ── Singola immagine ──────────────────────────────────────────────────
    void onPreview();
    void onApply();
    void onUndo();
    void onClose();
    void onFinished();
    void onAlgorithmChanged(const QString& algo);
    void updatePSFPreview();
    void onRunSEP();
    void onUseStellarPSF();

    // ── Multi-Frame Deconvolution ─────────────────────────────────────────
    void onMFAddFrames();           ///< Open file dialog to add FITS frames
    void onMFRemoveFrame();         ///< Remove selected frames from list
    void onMFClearFrames();         ///< Clear frame list
    void onMFMoveUp();              ///< Move selected frame up
    void onMFMoveDown();            ///< Move selected frame down
    void onMFBrowseOutput();        ///< Select output path
    void onMFStart();               ///< Start MFDeconv
    void onMFStop();                ///< Request worker stop
    void onMFWorkerStatus(const QString& msg);         ///< Receive worker log lines
    void onMFWorkerProgress(int percent, const QString& msg); ///< Update progress bar
    void onMFWorkerFinished(bool ok, const QString& msg, const QString& out); ///< Worker completed
    void onMFColorModeChanged(const QString& mode);    ///< luma/perchannel
    void onMFSeedModeChanged();                        ///< robust/median
    void onMFRhoChanged(const QString& rho);           ///< huber/l2
    void onMFSuperResChanged(int r);                   ///< SR factor 1/2/3
    void onMFStarMaskToggled(bool on);                 ///< Enable/disable star mask
    void onMFVarMapToggled(bool on);                   ///< Enable/disable variance map
    void onMFSaveIntermediateToggled(bool on);         ///< Enable intermediate iteration saves
    void onMFLowMemToggled(bool on);                   ///< low-mem mode

private:
    void buildUI();

    // ── Tab builder helpers ────────────────────────────────────────────────
    void buildConvolutionTab();
    void buildDeconvolutionTab();
    void buildPSFEstimatorTab();
    void buildTVDenoiseTab();
    void buildMultiFrameTab();      ///< MFDeconv tab

    void connectSignals();
    DeconvParams   collectParams() const;
    MFDeconvParams collectMFParams() const;
    void setControlsEnabled(bool en);
    void setMFControlsEnabled(bool en);
    void displayPSF(double r, double k, double a, double rot);
    void displayStellarPSF();
    void updateMFFrameCount();      ///< Update "N frames loaded" label

    // ──────────────────────────────────────────────────────────────────────
    // Layout elements — Singola immagine
    // ──────────────────────────────────────────────────────────────────────
    QTabWidget*     m_tabs               = nullptr;
    QLabel*         m_convPSFLabel       = nullptr;
    QDoubleSpinBox* m_strengthSpin       = nullptr;
    QSlider*        m_strengthSlider     = nullptr;
    QPushButton*    m_previewBtn         = nullptr;
    QPushButton*    m_undoBtn            = nullptr;
    QPushButton*    m_pushBtn            = nullptr;
    QPushButton*    m_closeBtn           = nullptr;
    QLabel*         m_statusLabel        = nullptr;

    // Convolution Tab
    QDoubleSpinBox* m_convRadiusSpin     = nullptr;
    QDoubleSpinBox* m_convShapeSpin      = nullptr;
    QDoubleSpinBox* m_convAspectSpin     = nullptr;
    QDoubleSpinBox* m_convRotSpin        = nullptr;

    // Deconvolution Tab
    QComboBox*      m_algoCombo          = nullptr;
    QWidget*        m_psfParamGroup      = nullptr;
    QDoubleSpinBox* m_rlPsfRadiusSpin    = nullptr;
    QDoubleSpinBox* m_rlPsfShapeSpin     = nullptr;
    QDoubleSpinBox* m_rlPsfAspectSpin    = nullptr;
    QDoubleSpinBox* m_rlPsfRotSpin       = nullptr;

    QWidget*        m_customPsfBar       = nullptr;
    QLabel*         m_customPsfLabel     = nullptr;
    QPushButton*    m_disableCustomBtn   = nullptr;

    // RL widget
    QWidget*        m_rlWidget           = nullptr;
    QDoubleSpinBox* m_rlIterSpin         = nullptr;
    QComboBox*      m_rlRegCombo         = nullptr;
    QCheckBox*      m_rlClipCheck        = nullptr;
    QCheckBox*      m_rlLumCheck         = nullptr;

    // Wiener widget
    QWidget*        m_wienerWidget       = nullptr;
    QDoubleSpinBox* m_wienerNsrSpin      = nullptr;
    QComboBox*      m_wienerRegCombo     = nullptr;
    QCheckBox*      m_wienerLumCheck     = nullptr;
    QCheckBox*      m_wienerDeringCheck  = nullptr;

    // Larson-Sekanina widget
    QWidget*        m_lsWidget           = nullptr;
    QDoubleSpinBox* m_lsRadStepSpin      = nullptr;
    QDoubleSpinBox* m_lsAngStepSpin      = nullptr;
    QComboBox*      m_lsOpCombo          = nullptr;
    QComboBox*      m_lsBlendCombo       = nullptr;

    // Van Cittert widget
    QWidget*        m_vcWidget           = nullptr;
    QDoubleSpinBox* m_vcIterSpin         = nullptr;
    QDoubleSpinBox* m_vcRelaxSpin        = nullptr;

    // PSF Estimator Tab
    QDoubleSpinBox* m_sepThreshSpin      = nullptr;
    QSpinBox*       m_sepMinAreaSpin     = nullptr;
    QDoubleSpinBox* m_sepSatSpin         = nullptr;
    QSpinBox*       m_sepMaxStarsSpin    = nullptr;
    QSpinBox*       m_sepStampSpin       = nullptr;
    QPushButton*    m_sepRunBtn          = nullptr;
    QPushButton*    m_sepUseBtn          = nullptr;
    QPushButton*    m_sepSaveBtn         = nullptr;
    QLabel*         m_sepPsfPreview      = nullptr;

    // TV Denoise Tab
    QDoubleSpinBox* m_tvWeightSpin       = nullptr;
    QDoubleSpinBox* m_tvIterSpin         = nullptr;
    QCheckBox*      m_tvMultiCheck       = nullptr;

    // ──────────────────────────────────────────────────────────────────────
    // Layout elements — Multi-Frame Deconvolution tab
    // Matches the parameters exposed by the MFDeconv backend.
    // ──────────────────────────────────────────────────────────────────────

    // Frame list
    QListWidget*    m_mfFrameList        = nullptr;  ///< list of aligned frame paths
    QLabel*         m_mfFrameCountLabel  = nullptr;  ///< "N frames loaded"
    QPushButton*    m_mfAddBtn           = nullptr;
    QPushButton*    m_mfRemoveBtn        = nullptr;
    QPushButton*    m_mfClearBtn         = nullptr;
    QPushButton*    m_mfMoveUpBtn        = nullptr;
    QPushButton*    m_mfMoveDownBtn      = nullptr;

    // Output path
    QLineEdit*      m_mfOutputEdit       = nullptr;
    QPushButton*    m_mfBrowseOutBtn     = nullptr;

    // ── Parametri base (iters, kappa, relax) ─────────────────────────────
    QSpinBox*       m_mfItersSpin        = nullptr;  ///< maxIters (default 20)
    QSpinBox*       m_mfMinItersSpin     = nullptr;  ///< minIters (default 3)
    QDoubleSpinBox* m_mfKappaSpin        = nullptr;  ///< kappa clamping (default 2.0)
    QDoubleSpinBox* m_mfRelaxSpin        = nullptr;  ///< relax (default 0.7)

    // ── Loss function (rho) ───────────────────────────────────────────────
    QComboBox*      m_mfRhoCombo         = nullptr;  ///< "huber" / "l2"
    QDoubleSpinBox* m_mfHuberDeltaSpin   = nullptr;  ///< huber_delta (>0 fisso, <0 auto×RMS, 0=flat)
    QLabel*         m_mfHuberDeltaLabel  = nullptr;

    // ── Color mode ────────────────────────────────────────────────────────
    QComboBox*      m_mfColorModeCombo   = nullptr;  ///< "luma" / "perchannel"

    // ── Seed mode ─────────────────────────────────────────────────────────
    QRadioButton*   m_mfSeedRobustRadio  = nullptr;  ///< seed_mode="robust" (bootstrap+σ-clip)
    QRadioButton*   m_mfSeedMedianRadio  = nullptr;  ///< seed_mode="median" (tiled exact median)
    QButtonGroup*   m_mfSeedBtnGroup     = nullptr;
    QSpinBox*       m_mfBootstrapSpin    = nullptr;  ///< bootstrap_frames (default 20)
    QDoubleSpinBox* m_mfClipSigmaSpin    = nullptr;  ///< clip_sigma (default 5.0)

    // ── Star masks ───────────────────────────────────────────────────────
    QCheckBox*      m_mfStarMaskCheck    = nullptr;  ///< use_star_masks
    QGroupBox*      m_mfStarMaskGroup    = nullptr;  ///< opzioni star mask
    QDoubleSpinBox* m_mfSmThreshSpin     = nullptr;  ///< thresh_sigma (default 2.0)
    QSpinBox*       m_mfSmMaxObjsSpin    = nullptr;  ///< max_objs (default 2000)
    QSpinBox*       m_mfSmGrowPxSpin     = nullptr;  ///< grow_px (default 8)
    QDoubleSpinBox* m_mfSmEllScaleSpin   = nullptr;  ///< ellipse_scale (default 1.2)
    QDoubleSpinBox* m_mfSmSoftSigmaSpin  = nullptr;  ///< soft_sigma (default 2.0)
    QSpinBox*       m_mfSmMaxRadiusSpin  = nullptr;  ///< max_radius_px (default 16)
    QDoubleSpinBox* m_mfSmKeepFloorSpin  = nullptr;  ///< keep_floor (default 0.20)
    QLineEdit*      m_mfSmRefPathEdit    = nullptr;  ///< star_mask_ref_path (optional)
    QPushButton*    m_mfSmRefBrowseBtn   = nullptr;

    // ── Variance maps ─────────────────────────────────────────────────────
    QCheckBox*      m_mfVarMapCheck      = nullptr;  ///< use_variance_maps
    QGroupBox*      m_mfVarMapGroup      = nullptr;  ///< opzioni variance map
    QSpinBox*       m_mfVmBwSpin         = nullptr;  ///< bw (default 64)
    QSpinBox*       m_mfVmBhSpin         = nullptr;  ///< bh (default 64)
    QDoubleSpinBox* m_mfVmSmoothSpin     = nullptr;  ///< smooth_sigma (default 1.0)
    QDoubleSpinBox* m_mfVmFloorSpin      = nullptr;  ///< floor (default 1e-8)

    // ── Early stopping ────────────────────────────────────────────────────
    QGroupBox*      m_mfEarlyStopGroup   = nullptr;
    QDoubleSpinBox* m_mfEsTolUpdSpin     = nullptr;  ///< tol_upd_floor (default 2e-4)
    QDoubleSpinBox* m_mfEsTolRelSpin     = nullptr;  ///< tol_rel_floor (default 5e-4)
    QDoubleSpinBox* m_mfEsEarlyFracSpin  = nullptr;  ///< early_frac (default 0.40)
    QDoubleSpinBox* m_mfEsEmaAlphaSpin   = nullptr;  ///< ema_alpha (default 0.5)
    QSpinBox*       m_mfEsPatienceSpin   = nullptr;  ///< patience (default 2)

    // ── Super-Resolution ──────────────────────────────────────────────────
    QSpinBox*       m_mfSrFactorSpin     = nullptr;  ///< super_res_factor (1/2/3)
    QGroupBox*      m_mfSrGroup          = nullptr;  ///< SR options (visible only if r>1)
    QDoubleSpinBox* m_mfSrSigmaSpin      = nullptr;  ///< sr_sigma (default 1.1)
    QSpinBox*       m_mfSrOptItersSpin   = nullptr;  ///< sr_psf_opt_iters (default 250)
    QDoubleSpinBox* m_mfSrOptLrSpin      = nullptr;  ///< sr_psf_opt_lr (default 0.1)

    // ── Salvataggio intermedie ────────────────────────────────────────────
    QCheckBox*      m_mfSaveIntermCheck  = nullptr;  ///< save_intermediate
    QSpinBox*       m_mfSaveEverySpin    = nullptr;  ///< save_every (default 1)

    // ── Low-memory mode ───────────────────────────────────────────────────
    QCheckBox*      m_mfLowMemCheck      = nullptr;  ///< low_mem

    // ── Controlli run/stop ────────────────────────────────────────────────
    QPushButton*    m_mfStartBtn         = nullptr;
    QPushButton*    m_mfStopBtn          = nullptr;
    QProgressBar*   m_mfProgressBar      = nullptr;
    QLabel*         m_mfStatusLabel      = nullptr;  ///< ultima riga di log

    // ── Log area ──────────────────────────────────────────────────────────
    // Use QPlainTextEdit for multi-line logs.
    class QPlainTextEdit* m_mfLogEdit    = nullptr;

    // ──────────────────────────────────────────────────────────────────────
    // State
    // ──────────────────────────────────────────────────────────────────────
    ImageViewer*    m_viewer             = nullptr;
    MainWindow*     m_mainWindow         = nullptr;
    ImageBuffer     m_originalBuffer;
    ImageBuffer     m_previewBuffer;
    bool            m_isPreview          = false;
    cv::Mat         m_customKernel;
    bool            m_useCustom          = false;

    // MFDeconv worker
    MFDeconvWorker* m_mfWorker           = nullptr;

    QFutureWatcher<DeconvResult>* m_watcher = nullptr;
};

#endif // DECONVOLUTIONDIALOG_H

*/