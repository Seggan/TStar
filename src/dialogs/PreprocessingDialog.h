#ifndef PREPROCESSING_DIALOG_H
#define PREPROCESSING_DIALOG_H

/**
 * @file PreprocessingDialog.h
 * @brief Image preprocessing and calibration dialog.
 *
 * Provides a comprehensive UI for:
 *  - Selecting or creating master calibration frames (bias, dark, flat).
 *  - Configuring calibration options (dark optimization, debayer, cosmetic).
 *  - Selecting light frames for batch calibration.
 *  - Executing the calibration pipeline with progress reporting.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#include <QDialog>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTextEdit>
#include <QListWidget>
#include <memory>

#include "../preprocessing/PreprocessingTypes.h"
#include "../preprocessing/Preprocessing.h"

class MainWindow;

/**
 * @class PreprocessingDialog
 * @brief Dialog for configuring and executing image calibration workflows.
 */
class PreprocessingDialog : public QDialog {
    Q_OBJECT

public:
    explicit PreprocessingDialog(MainWindow* parent = nullptr);
    ~PreprocessingDialog() override;

private slots:
    // -- Master frame selection and creation --
    void onSelectBias();
    void onSelectDark();
    void onSelectFlat();
    void onSelectDarkFlat();
    void onCreateMasterBias();
    void onCreateMasterDark();
    void onCreateMasterFlat();

    // -- Light frame management --
    void onAddLights();
    void onRemoveLights();
    void onClearLights();

    // -- Calibration execution --
    void onStartCalibration();
    void onCancel();

    // -- Progress reporting --
    void onProgressChanged(const QString& message, double progress);
    void onLogMessage(const QString& message, const QString& color);
    void onImageProcessed(const QString& path, bool success);
    void onFinished(bool success);

private:
    // -- UI construction helpers --
    void setupUI();
    void setupMastersGroup();
    void setupLightsGroup();
    void setupOptionsGroup();
    void setupProgressGroup();

    /** @brief Collect all UI settings into a PreprocessParams struct. */
    Preprocessing::PreprocessParams gatherParams() const;

    // -- Master frame widgets --
    QGroupBox*   m_mastersGroup       = nullptr;
    QLineEdit*   m_biasPath           = nullptr;
    QLineEdit*   m_darkPath           = nullptr;
    QLineEdit*   m_flatPath           = nullptr;
    QLineEdit*   m_darkFlatPath       = nullptr;
    QPushButton* m_selectBiasBtn      = nullptr;
    QPushButton* m_selectDarkBtn      = nullptr;
    QPushButton* m_selectFlatBtn      = nullptr;
    QPushButton* m_selectDarkFlatBtn  = nullptr;
    QPushButton* m_createBiasBtn      = nullptr;
    QPushButton* m_createDarkBtn      = nullptr;
    QPushButton* m_createFlatBtn      = nullptr;

    // -- Light frame widgets --
    QGroupBox*   m_lightsGroup        = nullptr;
    QListWidget* m_lightsList         = nullptr;
    QPushButton* m_addLightsBtn       = nullptr;
    QPushButton* m_removeLightsBtn    = nullptr;
    QPushButton* m_clearLightsBtn     = nullptr;

    // -- Calibration option widgets --
    QGroupBox*      m_optionsGroup       = nullptr;
    QCheckBox*      m_useBiasCheck       = nullptr;
    QCheckBox*      m_useDarkCheck       = nullptr;
    QCheckBox*      m_useFlatCheck       = nullptr;
    QCheckBox*      m_darkOptimCheck     = nullptr;
    QCheckBox*      m_fixBandingCheck    = nullptr;
    QCheckBox*      m_fixBadLinesCheck   = nullptr;
    QCheckBox*      m_fixXTransCheck     = nullptr;
    QCheckBox*      m_debayerCheck       = nullptr;
    QComboBox*      m_bayerPatternCombo  = nullptr;
    QComboBox*      m_debayerAlgoCombo   = nullptr;
    QCheckBox*      m_cfaEqualizeCheck   = nullptr;
    QCheckBox*      m_cosmeticCheck      = nullptr;
    QComboBox*      m_cosmeticModeCombo  = nullptr;
    QDoubleSpinBox* m_hotSigmaSpin       = nullptr;
    QDoubleSpinBox* m_coldSigmaSpin      = nullptr;
    QDoubleSpinBox* m_biasLevelSpin      = nullptr;
    QDoubleSpinBox* m_pedestalSpin       = nullptr;
    QLineEdit*      m_outputPrefix       = nullptr;
    QLineEdit*      m_outputDir          = nullptr;

    // -- Progress widgets --
    QGroupBox*    m_progressGroup = nullptr;
    QProgressBar* m_progressBar   = nullptr;
    QTextEdit*    m_logText        = nullptr;
    QPushButton*  m_startBtn       = nullptr;
    QPushButton*  m_cancelBtn      = nullptr;

    // -- Runtime state --
    QStringList m_lightFiles;
    std::unique_ptr<Preprocessing::PreprocessingWorker> m_worker;
    MainWindow* m_mainWindow = nullptr;
    bool        m_isRunning  = false;
};

#endif // PREPROCESSING_DIALOG_H