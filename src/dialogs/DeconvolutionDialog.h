#pragma once
/*
 * DeconvolutionDialog.h  —  Qt6 dialog for image deconvolution
 *
 * Supports RL, RLTV, and Wiener algorithms with full PSF control,
 * live PSF preview, and star-protection mask.
 */

#ifndef DECONVOLUTIONDIALOG_H
#define DECONVOLUTIONDIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
#include <QRadioButton>
#include <QProgressBar>
#include <QTabWidget>
#include <QSlider>
#include <QFutureWatcher>
#include "ImageBuffer.h"
#include "Deconvolution.h"

class ImageViewer;
class MainWindow;

class DeconvolutionDialog : public QDialog {
    Q_OBJECT

public:
    explicit DeconvolutionDialog(ImageViewer* viewer, MainWindow* mw,
                                  QWidget* parent = nullptr);
    ~DeconvolutionDialog() override;

    void setViewer(ImageViewer* viewer);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onPreview();
    void onApply();
    void onReset();
    void onFinished();
    void onAlgorithmChanged(int index);
    void onPSFSourceChanged(int index);
    void onEstimateFWHM();
    void onFWHMChanged(double);

private:
    void buildUI();
    void connectSignals();
    DeconvParams collectParams() const;
    void setControlsEnabled(bool en);
    void setAlgorithmWidgets(DeconvAlgorithm algo);
    void renderPSFThumbnail();

    // ── Algorithm ──────────────────────────────────────────────────────────
    QComboBox*      m_algoCombo          = nullptr;

    // ── PSF ────────────────────────────────────────────────────────────────
    QComboBox*      m_psfSourceCombo     = nullptr;
    QDoubleSpinBox* m_fwhmSpin           = nullptr;
    QDoubleSpinBox* m_betaSpin           = nullptr;
    QDoubleSpinBox* m_angleSpin          = nullptr;
    QDoubleSpinBox* m_roundnessSpin      = nullptr;
    QDoubleSpinBox* m_airyWavelengthSpin = nullptr;
    QDoubleSpinBox* m_airyApertureSpin   = nullptr;
    QDoubleSpinBox* m_airyFocalLenSpin   = nullptr;
    QDoubleSpinBox* m_airyPixelSizeSpin  = nullptr;
    QDoubleSpinBox* m_airyObstructionSpin= nullptr;
    QPushButton*    m_estimateFWHMBtn    = nullptr;
    QLabel*         m_psfPreview         = nullptr;   // 64×64 thumbnail of PSF
    QSpinBox*       m_kernelSizeSpin     = nullptr;

    // ── RL / RLTV iterations ───────────────────────────────────────────────
    QGroupBox*      m_grpIter            = nullptr;
    QSpinBox*       m_iterSpin           = nullptr;
    QDoubleSpinBox* m_tolSpin            = nullptr;

    // ── RLTV regularisation ────────────────────────────────────────────────
    QGroupBox*      m_grpTV              = nullptr;
    QDoubleSpinBox* m_tvWeightSpin       = nullptr;

    // ── Wiener ────────────────────────────────────────────────────────────
    QGroupBox*      m_grpWiener          = nullptr;
    QDoubleSpinBox* m_wienerKSpin        = nullptr;

    // ── Star mask ──────────────────────────────────────────────────────────
    QGroupBox*      m_grpStarMask        = nullptr;
    QCheckBox*      m_starMaskCheck      = nullptr;
    QDoubleSpinBox* m_starMaskThreshSpin = nullptr;
    QDoubleSpinBox* m_starMaskRadiusSpin = nullptr;
    QDoubleSpinBox* m_starMaskBlendSpin  = nullptr;

    // ── Border ────────────────────────────────────────────────────────────
    QSpinBox*       m_borderPadSpin      = nullptr;

    // ── Progress ──────────────────────────────────────────────────────────
    QProgressBar*   m_progressBar        = nullptr;
    QLabel*         m_statusLabel        = nullptr;

    // ── Buttons ───────────────────────────────────────────────────────────
    QPushButton*    m_previewBtn         = nullptr;
    QPushButton*    m_applyBtn           = nullptr;
    QPushButton*    m_resetBtn           = nullptr;
    QPushButton*    m_closeBtn           = nullptr;

    // ── State ─────────────────────────────────────────────────────────────
    ImageViewer*    m_viewer             = nullptr;
    MainWindow*     m_mainWindow         = nullptr;
    ImageBuffer     m_originalBuffer;
    bool            m_isPreview          = false;

    QFutureWatcher<DeconvResult>* m_watcher = nullptr;
};

#endif // DECONVOLUTIONDIALOG_H
