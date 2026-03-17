#pragma once
/*
 * TGVDenoiseDialog.h  —  Qt6 dialog for TGV² denoising
 *
 * Matches TStar's existing dialog style (non-modal, live preview,
 * undo/redo via ImageBuffer snapshot).
 */

#ifndef TGVDENOISEDIALOG_H
#define TGVDENOISEDIALOG_H

#include <QDialog>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QSlider>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
#include <QRadioButton>
#include <QComboBox>
#include <QProgressBar>
#include <QFutureWatcher>
#include "ImageBuffer.h"
#include "TGVDenoise.h"

class ImageViewer;
class MainWindow;

class TGVDenoiseDialog : public QDialog {
    Q_OBJECT

public:
    explicit TGVDenoiseDialog(ImageViewer* viewer, MainWindow* mw, QWidget* parent = nullptr);
    ~TGVDenoiseDialog() override;

    void setViewer(ImageViewer* viewer);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onPreview();
    void onApply();
    void onReset();
    void onPreviewFinished();
    void onApplyFinished();
    void onParameterChanged();

private:
    void buildUI();
    void connectSignals();
    TGVParams collectParams() const;
    void setControlsEnabled(bool en);
    void updateStrengthLabel(int val);

    // ── Widgets ────────────────────────────────────────────────────────────
    // Strength (maps to alpha0/alpha1 ratio via preset + fine tune)
    QSlider*        m_strengthSlider   = nullptr;
    QLabel*         m_strengthLabel    = nullptr;

    // Fine controls
    QDoubleSpinBox* m_alpha0Spin       = nullptr;   // order-0 weight (detail)
    QDoubleSpinBox* m_alpha1Spin       = nullptr;   // order-1 weight (edges)
    QDoubleSpinBox* m_lambdaSpin       = nullptr;   // data fidelity
    QSpinBox*       m_iterSpin         = nullptr;   // max iterations
    QDoubleSpinBox* m_tolSpin          = nullptr;   // convergence tol

    // Noise estimation
    QCheckBox*      m_autoLambdaCheck  = nullptr;
    QLabel*         m_noiseEstLabel    = nullptr;

    // Channel
    QCheckBox*      m_perChannelCheck  = nullptr;

    // Buttons
    QPushButton*    m_previewBtn       = nullptr;
    QPushButton*    m_applyBtn         = nullptr;
    QPushButton*    m_resetBtn         = nullptr;
    QPushButton*    m_closeBtn         = nullptr;

    // Progress
    QProgressBar*   m_progressBar      = nullptr;
    QLabel*         m_statusLabel      = nullptr;

    // State
    ImageViewer*    m_viewer           = nullptr;
    MainWindow*     m_mainWindow       = nullptr;
    ImageBuffer     m_originalBuffer;   // snapshot for reset
    bool            m_previewActive    = false;
    bool            m_dirty            = false;

    QFutureWatcher<TGVResult>* m_watcher = nullptr;
};

#endif // TGVDENOISEDIALOG_H
