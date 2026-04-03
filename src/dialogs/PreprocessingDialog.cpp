/**
 * @file PreprocessingDialog.cpp
 * @brief Image preprocessing and calibration dialog implementation.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#include "PreprocessingDialog.h"
#include "../MainWindow.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QLabel>
#include <QApplication>
#include <QSettings>

// ============================================================================
// Construction / Destruction
// ============================================================================

PreprocessingDialog::PreprocessingDialog(MainWindow* parent)
    : QDialog(parent)
    , m_mainWindow(parent)
{
    setWindowTitle(tr("Image Calibration"));
    setMinimumSize(700, 600);
    resize(800, 700);

    setupUI();

    // Center dialog on parent window
    if (parent)
        move(parent->geometry().center() - rect().center());
}

PreprocessingDialog::~PreprocessingDialog()
{
    if (m_worker && m_worker->isRunning()) {
        m_worker->requestCancel();
        m_worker->wait();
    }
}

// ============================================================================
// UI Setup -- Top-level layout
// ============================================================================

void PreprocessingDialog::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);

    // Master calibration frames section
    setupMastersGroup();
    mainLayout->addWidget(m_mastersGroup);

    // Calibration options section
    setupOptionsGroup();
    mainLayout->addWidget(m_optionsGroup);

    // Light frames and progress side by side
    QHBoxLayout* bottomLayout = new QHBoxLayout();
    setupLightsGroup();
    setupProgressGroup();
    bottomLayout->addWidget(m_lightsGroup,   1);
    bottomLayout->addWidget(m_progressGroup, 1);
    mainLayout->addLayout(bottomLayout);
}

// ============================================================================
// Master Calibration Frames section
// ============================================================================

void PreprocessingDialog::setupMastersGroup()
{
    m_mastersGroup = new QGroupBox(tr("Master Calibration Frames"), this);
    QHBoxLayout* mainLayout = new QHBoxLayout(m_mastersGroup);

    // Helper to create a column for a single calibration frame type
    auto addFrameColumn = [&](const QString& label,
                              QLineEdit** pathEdit,
                              QPushButton** selectBtn,
                              QPushButton** createBtn,
                              const char* selectSlot,
                              const char* createSlot,
                              const QString& placeholder,
                              const QString& tooltip = QString())
    {
        QVBoxLayout* col = new QVBoxLayout();

        QLabel* lbl = new QLabel(label, this);
        if (!tooltip.isEmpty())
            lbl->setToolTip(tooltip);
        col->addWidget(lbl);

        *pathEdit = new QLineEdit(this);
        (*pathEdit)->setPlaceholderText(placeholder);
        if (!tooltip.isEmpty())
            (*pathEdit)->setToolTip(tooltip);
        col->addWidget(*pathEdit);

        QHBoxLayout* btnRow = new QHBoxLayout();
        *selectBtn = new QPushButton(tr("..."), this);
        (*selectBtn)->setMaximumWidth(30);
        connect(*selectBtn, SIGNAL(clicked()), this, selectSlot);
        btnRow->addWidget(*selectBtn);

        if (createBtn && createSlot) {
            *createBtn = new QPushButton(tr("Create"), this);
            connect(*createBtn, SIGNAL(clicked()), this, createSlot);
            btnRow->addWidget(*createBtn);
        }

        col->addLayout(btnRow);
        mainLayout->addLayout(col);
    };

    // Bias column
    addFrameColumn(tr("Bias:"), &m_biasPath, &m_selectBiasBtn, &m_createBiasBtn,
                   SLOT(onSelectBias()), SLOT(onCreateMasterBias()),
                   tr("Select or create..."));

    // Dark column
    addFrameColumn(tr("Dark:"), &m_darkPath, &m_selectDarkBtn, &m_createDarkBtn,
                   SLOT(onSelectDark()), SLOT(onCreateMasterDark()),
                   tr("Select or create..."));

    // Flat column
    addFrameColumn(tr("Flat:"), &m_flatPath, &m_selectFlatBtn, &m_createFlatBtn,
                   SLOT(onSelectFlat()), SLOT(onCreateMasterFlat()),
                   tr("Select or create..."));

    // Dark for Flat column (optional, no Create button)
    {
        QVBoxLayout* col = new QVBoxLayout();
        QString darkFlatTooltip = tr(
            "(Optional: A Master Dark frame taken at the SAME exposure time "
            "and temperature as your FLAT frames. Used to subtract thermal "
            "noise from the flats before flat-fielding. If left empty, the "
            "main DARK is used instead (or flats are not dark-subtracted if "
            "no dark is set). Cosmetic correction uses the DARK tab file "
            "-- not this Dark for Flat.)");

        QLabel* darkFlatLabel = new QLabel(tr("Dark for Flat:"), this);
        darkFlatLabel->setToolTip(darkFlatTooltip);
        col->addWidget(darkFlatLabel);

        m_darkFlatPath = new QLineEdit(this);
        m_darkFlatPath->setPlaceholderText(tr("(Optional)"));
        m_darkFlatPath->setToolTip(darkFlatTooltip);
        col->addWidget(m_darkFlatPath);

        QHBoxLayout* btnRow = new QHBoxLayout();
        m_selectDarkFlatBtn = new QPushButton(tr("..."), this);
        m_selectDarkFlatBtn->setMaximumWidth(30);
        connect(m_selectDarkFlatBtn, &QPushButton::clicked,
                this,                &PreprocessingDialog::onSelectDarkFlat);
        btnRow->addWidget(m_selectDarkFlatBtn);
        col->addLayout(btnRow);

        mainLayout->addLayout(col);
    }
}

// ============================================================================
// Calibration Options section
// ============================================================================

void PreprocessingDialog::setupOptionsGroup()
{
    m_optionsGroup = new QGroupBox(tr("Calibration Options"), this);
    QVBoxLayout* layout = new QVBoxLayout(m_optionsGroup);

    // -- Row 1: Enable/disable calibration stages --
    QHBoxLayout* row1 = new QHBoxLayout();

    m_useBiasCheck = new QCheckBox(tr("Use Bias calibration"), this);
    m_useBiasCheck->setChecked(true);
    row1->addWidget(m_useBiasCheck);

    m_useDarkCheck = new QCheckBox(tr("Use Dark calibration"), this);
    m_useDarkCheck->setChecked(true);
    row1->addWidget(m_useDarkCheck);

    m_useFlatCheck = new QCheckBox(tr("Use Flat calibration"), this);
    m_useFlatCheck->setChecked(true);
    row1->addWidget(m_useFlatCheck);

    m_darkOptimCheck = new QCheckBox(tr("Optimize dark scaling"), this);
    row1->addWidget(m_darkOptimCheck);
    layout->addLayout(row1);

    // -- Row 2: Sensor-specific fixes --
    QHBoxLayout* fixRow = new QHBoxLayout();

    m_fixBandingCheck = new QCheckBox(tr("Fix banding"), this);
    m_fixBandingCheck->setToolTip(tr(
        "Repairs horizontal readout banding noise common in CMOS sensors. "
        "Works by estimating and subtracting the per-row/column offset. "
        "Enable only if you see horizontal stripe patterns in dark or bias frames."));

    m_fixBadLinesCheck = new QCheckBox(tr("Fix bad lines"), this);
    m_fixBadLinesCheck->setToolTip(tr(
        "Identifies and repairs individual defective rows or columns "
        "(stuck pixels, hot lines). A 'bad line' is a row or column whose "
        "mean value deviates significantly from its neighbours. Cosmetic "
        "correction (sigma-clipping or from Master Dark) provides "
        "complementary hot/cold pixel repair."));

    m_fixXTransCheck = new QCheckBox(tr("Fix X-Trans"), this);

    fixRow->addWidget(m_fixBandingCheck);
    fixRow->addWidget(m_fixBadLinesCheck);
    fixRow->addWidget(m_fixXTransCheck);
    layout->addLayout(fixRow);

    // -- Row 3: Advanced numerical parameters --
    QHBoxLayout* advRow = new QHBoxLayout();

    advRow->addWidget(new QLabel(tr("Bias value:"), this));
    m_biasLevelSpin = new QDoubleSpinBox(this);
    m_biasLevelSpin->setRange(0, 65535);
    m_biasLevelSpin->setValue(0);
    m_biasLevelSpin->setSpecialValueText(tr("Auto"));
    m_biasLevelSpin->setMaximumWidth(80);
    advRow->addWidget(m_biasLevelSpin);

    advRow->addWidget(new QLabel(tr("Pedestal:"), this));
    m_pedestalSpin = new QDoubleSpinBox(this);
    m_pedestalSpin->setRange(0, 10000);
    m_pedestalSpin->setValue(0);
    m_pedestalSpin->setMaximumWidth(80);
    advRow->addWidget(m_pedestalSpin);

    advRow->addWidget(new QLabel(tr("Prefix:"), this));
    m_outputPrefix = new QLineEdit(this);
    m_outputPrefix->setText("pp_");
    m_outputPrefix->setMaximumWidth(60);
    advRow->addWidget(m_outputPrefix);
    advRow->addStretch();
    layout->addLayout(advRow);

    // -- Row 4: Debayer settings --
    QHBoxLayout* debayerRow = new QHBoxLayout();

    m_debayerCheck = new QCheckBox(tr("Debayer:"), this);
    debayerRow->addWidget(m_debayerCheck);

    debayerRow->addWidget(new QLabel(tr("Pattern:"), this));
    m_bayerPatternCombo = new QComboBox(this);
    m_bayerPatternCombo->addItems({"RGGB", "BGGR", "GBRG", "GRBG"});
    m_bayerPatternCombo->setMaximumWidth(80);
    debayerRow->addWidget(m_bayerPatternCombo);

    debayerRow->addWidget(new QLabel(tr("Algorithm:"), this));
    m_debayerAlgoCombo = new QComboBox(this);
    m_debayerAlgoCombo->addItem(tr("Bilinear"));
    m_debayerAlgoCombo->addItem(tr("VNG"));
    m_debayerAlgoCombo->addItem(tr("Super Pixel"));
    m_debayerAlgoCombo->setMaximumWidth(100);
    debayerRow->addWidget(m_debayerAlgoCombo);

    m_cfaEqualizeCheck = new QCheckBox(tr("Equalize CFA"), this);
    m_cfaEqualizeCheck->setToolTip(tr(
        "Equalizes the CFA (Color Filter Array) channel sensitivities "
        "before stacking.\nThis preserves the raw Bayer pattern for "
        "Drizzle integration.\n\n"
        "IMPORTANT: When using Drizzle, do NOT enable Debayer -- instead "
        "enable CFA mode.\nDrizzle works on the raw CFA data; Debayer "
        "should only be applied after stacking."));
    debayerRow->addWidget(m_cfaEqualizeCheck);
    debayerRow->addStretch();
    layout->addLayout(debayerRow);

    // -- Row 5: Cosmetic correction --
    QHBoxLayout* cosmRow = new QHBoxLayout();

    m_cosmeticCheck = new QCheckBox(tr("Cosmetic correction:"), this);
    cosmRow->addWidget(m_cosmeticCheck);

    m_cosmeticModeCombo = new QComboBox(this);
    m_cosmeticModeCombo->addItem(tr("Sigma-clipping"),   0);
    m_cosmeticModeCombo->addItem(tr("From Master Dark"), 1);
    m_cosmeticModeCombo->setMaximumWidth(120);
    m_cosmeticModeCombo->setToolTip(tr(
        "Sigma-clipping: detects hot/cold pixels statistically on each frame.\n"
        "From Master Dark: uses the DARK tab master to map defective pixels "
        "(not Dark for Flat).\n\n"
        "Cosmetic correction always reads the file from the DARK tab, "
        "not the Dark for Flat tab."));
    cosmRow->addWidget(m_cosmeticModeCombo);

    cosmRow->addWidget(new QLabel(tr("Hot sigma:"), this));
    m_hotSigmaSpin = new QDoubleSpinBox(this);
    m_hotSigmaSpin->setRange(1.0, 20.0);
    m_hotSigmaSpin->setValue(3.0);
    m_hotSigmaSpin->setMaximumWidth(70);
    cosmRow->addWidget(m_hotSigmaSpin);

    cosmRow->addWidget(new QLabel(tr("Cold sigma:"), this));
    m_coldSigmaSpin = new QDoubleSpinBox(this);
    m_coldSigmaSpin->setRange(1.0, 20.0);
    m_coldSigmaSpin->setValue(3.0);
    m_coldSigmaSpin->setMaximumWidth(70);
    cosmRow->addWidget(m_coldSigmaSpin);

    cosmRow->addStretch();
    layout->addLayout(cosmRow);

    // -- Wire dependent enable states --
    connect(m_debayerCheck, &QCheckBox::toggled,
            m_bayerPatternCombo, &QWidget::setEnabled);
    connect(m_debayerCheck, &QCheckBox::toggled,
            m_debayerAlgoCombo,  &QWidget::setEnabled);
    connect(m_debayerCheck, &QCheckBox::toggled,
            m_cfaEqualizeCheck,  &QWidget::setEnabled);

    auto updateCosmeticEnabled = [this]() {
        bool en = m_cosmeticCheck->isChecked();
        m_cosmeticModeCombo->setEnabled(en);
        m_hotSigmaSpin->setEnabled(en);
        m_coldSigmaSpin->setEnabled(en);
    };
    connect(m_cosmeticCheck, &QCheckBox::toggled, this, updateCosmeticEnabled);

    // Initialize disabled states
    m_bayerPatternCombo->setEnabled(false);
    m_debayerAlgoCombo->setEnabled(false);
    m_cfaEqualizeCheck->setEnabled(false);
    updateCosmeticEnabled();
}

// ============================================================================
// Light Frames section
// ============================================================================

void PreprocessingDialog::setupLightsGroup()
{
    m_lightsGroup = new QGroupBox(tr("Light Frames to Calibrate"), this);
    QVBoxLayout* layout = new QVBoxLayout(m_lightsGroup);

    // Toolbar
    QHBoxLayout* toolbar = new QHBoxLayout();

    m_addLightsBtn = new QPushButton(tr("Add Files..."), this);
    connect(m_addLightsBtn, &QPushButton::clicked,
            this,           &PreprocessingDialog::onAddLights);
    toolbar->addWidget(m_addLightsBtn);

    m_removeLightsBtn = new QPushButton(tr("Remove"), this);
    connect(m_removeLightsBtn, &QPushButton::clicked,
            this,              &PreprocessingDialog::onRemoveLights);
    toolbar->addWidget(m_removeLightsBtn);

    m_clearLightsBtn = new QPushButton(tr("Clear All"), this);
    connect(m_clearLightsBtn, &QPushButton::clicked,
            this,             &PreprocessingDialog::onClearLights);
    toolbar->addWidget(m_clearLightsBtn);

    toolbar->addStretch();
    layout->addLayout(toolbar);

    // File list
    m_lightsList = new QListWidget(this);
    m_lightsList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_lightsList->setAlternatingRowColors(true);
    layout->addWidget(m_lightsList);

    // Output directory selection
    QHBoxLayout* dirRow = new QHBoxLayout();
    dirRow->addWidget(new QLabel(tr("Output directory:"), this));

    m_outputDir = new QLineEdit(this);
    m_outputDir->setPlaceholderText(tr("(Same as input)"));
    dirRow->addWidget(m_outputDir, 1);

    QPushButton* browseBtn = new QPushButton(tr("..."), this);
    browseBtn->setMaximumWidth(30);
    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        QSettings settings("TStar", "TStar");
        QString initialDir = settings.value(
            "Preprocessing/OutputFolder", QDir::currentPath()).toString();
        QString dir = QFileDialog::getExistingDirectory(
            this, tr("Select Output Directory"), initialDir);
        if (!dir.isEmpty()) {
            m_outputDir->setText(dir);
            settings.setValue("Preprocessing/OutputFolder", dir);
        }
    });
    dirRow->addWidget(browseBtn);
    layout->addLayout(dirRow);
}

// ============================================================================
// Progress section
// ============================================================================

void PreprocessingDialog::setupProgressGroup()
{
    m_progressGroup = new QGroupBox(tr("Progress"), this);
    QVBoxLayout* layout = new QVBoxLayout(m_progressGroup);

    m_progressBar = new QProgressBar(this);
    layout->addWidget(m_progressBar);

    m_logText = new QTextEdit(this);
    m_logText->setReadOnly(true);
    layout->addWidget(m_logText, 1);

    QHBoxLayout* buttonRow = new QHBoxLayout();
    buttonRow->addStretch();

    m_cancelBtn = new QPushButton(tr("Cancel"), this);
    m_cancelBtn->setEnabled(false);
    connect(m_cancelBtn, &QPushButton::clicked,
            this,        &PreprocessingDialog::onCancel);

    m_startBtn = new QPushButton(tr("Start Calibration"), this);
    connect(m_startBtn, &QPushButton::clicked,
            this,       &PreprocessingDialog::onStartCalibration);

    buttonRow->addWidget(m_cancelBtn);
    buttonRow->addWidget(m_startBtn);
    layout->addLayout(buttonRow);
}

// ============================================================================
// File selection handlers
// ============================================================================

/** @brief Helper to retrieve the last-used input folder from settings. */
static QString getInitialInputDir()
{
    QSettings settings("TStar", "TStar");
    return settings.value("Preprocessing/InputFolder",
                          QDir::currentPath()).toString();
}

/** @brief Helper to save the input folder to settings. */
static void saveInputDir(const QString& filePath)
{
    QSettings settings("TStar", "TStar");
    settings.setValue("Preprocessing/InputFolder",
                      QFileInfo(filePath).absolutePath());
}

void PreprocessingDialog::onSelectBias()
{
    QString file = QFileDialog::getOpenFileName(
        this, tr("Select Master Bias"), getInitialInputDir(),
        tr("FITS Files (*.fit *.fits);;All Files (*)"));
    if (!file.isEmpty()) {
        m_biasPath->setText(file);
        saveInputDir(file);
    }
}

void PreprocessingDialog::onSelectDark()
{
    QString file = QFileDialog::getOpenFileName(
        this, tr("Select Master Dark"), getInitialInputDir(),
        tr("FITS Files (*.fit *.fits);;All Files (*)"));
    if (!file.isEmpty()) {
        m_darkPath->setText(file);
        saveInputDir(file);
    }
}

void PreprocessingDialog::onSelectFlat()
{
    QString file = QFileDialog::getOpenFileName(
        this, tr("Select Master Flat"), getInitialInputDir(),
        tr("FITS Files (*.fit *.fits);;All Files (*)"));
    if (!file.isEmpty()) {
        m_flatPath->setText(file);
        saveInputDir(file);
    }
}

void PreprocessingDialog::onSelectDarkFlat()
{
    QString file = QFileDialog::getOpenFileName(
        this, tr("Select Dark for Flat"), getInitialInputDir(),
        tr("FITS Files (*.fit *.fits);;All Files (*)"));
    if (!file.isEmpty()) {
        m_darkFlatPath->setText(file);
        saveInputDir(file);
    }
}

// ============================================================================
// Master frame creation
// ============================================================================

void PreprocessingDialog::onCreateMasterBias()
{
    QString initialDir = getInitialInputDir();
    QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Select Bias Frames"), initialDir,
        tr("FITS Files (*.fit *.fits);;All Files (*)"));
    if (files.isEmpty()) return;

    saveInputDir(files.first());

    QString output = QFileDialog::getSaveFileName(
        this, tr("Save Master Bias"),
        initialDir + "/master_bias.fit",
        tr("FITS Files (*.fit)"));
    if (output.isEmpty()) return;

    saveInputDir(output);

    m_logText->append(tr("Creating master bias from %1 frames...")
                      .arg(files.size()));

    Preprocessing::MasterFrames masters;
    bool success = masters.createMasterBias(
        files, output,
        Stacking::Method::Mean, Stacking::Rejection::Winsorized,
        3.0f, 3.0f,
        [this]([[maybe_unused]] const QString& msg, double pct) {
            m_progressBar->setValue(static_cast<int>(pct * 100));
            QApplication::processEvents();
        });

    if (success) {
        m_biasPath->setText(output);
        m_logText->append(tr("Master bias created: %1").arg(output));
    } else {
        m_logText->append(
            tr("<span style='color:red'>Failed to create master bias</span>"));
    }
    m_progressBar->setValue(0);
}

void PreprocessingDialog::onCreateMasterDark()
{
    QString initialDir = getInitialInputDir();
    QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Select Dark Frames"), initialDir,
        tr("FITS Files (*.fit *.fits);;All Files (*)"));
    if (files.isEmpty()) return;

    saveInputDir(files.first());

    QString output = QFileDialog::getSaveFileName(
        this, tr("Save Master Dark"),
        initialDir + "/master_dark.fit",
        tr("FITS Files (*.fit)"));
    if (output.isEmpty()) return;

    saveInputDir(output);

    m_logText->append(tr("Creating master dark from %1 frames...")
                      .arg(files.size()));

    Preprocessing::MasterFrames masters;
    bool success = masters.createMasterDark(
        files, output, m_biasPath->text(),
        Stacking::Method::Mean, Stacking::Rejection::Winsorized,
        3.0f, 3.0f,
        [this]([[maybe_unused]] const QString& msg, double pct) {
            m_progressBar->setValue(static_cast<int>(pct * 100));
            QApplication::processEvents();
        });

    if (success) {
        m_darkPath->setText(output);
        m_logText->append(tr("Master dark created: %1").arg(output));
    } else {
        m_logText->append(
            tr("<span style='color:red'>Failed to create master dark</span>"));
    }
    m_progressBar->setValue(0);
}

void PreprocessingDialog::onCreateMasterFlat()
{
    QString initialDir = getInitialInputDir();
    QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Select Flat Frames"), initialDir,
        tr("FITS Files (*.fit *.fits);;All Files (*)"));
    if (files.isEmpty()) return;

    saveInputDir(files.first());

    QString output = QFileDialog::getSaveFileName(
        this, tr("Save Master Flat"),
        initialDir + "/master_flat.fit",
        tr("FITS Files (*.fit)"));
    if (output.isEmpty()) return;

    saveInputDir(output);

    m_logText->append(tr("Creating master flat from %1 frames...")
                      .arg(files.size()));

    // Use the Dark-for-Flat if provided, otherwise fall back to the main Dark
    QString darkForFlat = m_darkFlatPath->text().isEmpty()
                              ? m_darkPath->text()
                              : m_darkFlatPath->text();

    Preprocessing::MasterFrames masters;
    bool success = masters.createMasterFlat(
        files, output, m_biasPath->text(), darkForFlat,
        Stacking::Method::Mean, Stacking::Rejection::Winsorized,
        3.0f, 3.0f,
        [this]([[maybe_unused]] const QString& msg, double pct) {
            m_progressBar->setValue(static_cast<int>(pct * 100));
            QApplication::processEvents();
        });

    if (success) {
        m_flatPath->setText(output);
        m_logText->append(tr("Master flat created: %1").arg(output));
    } else {
        m_logText->append(
            tr("<span style='color:red'>Failed to create master flat</span>"));
    }
    m_progressBar->setValue(0);
}

// ============================================================================
// Light frame management
// ============================================================================

void PreprocessingDialog::onAddLights()
{
    QString initialDir = getInitialInputDir();
    QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Select Light Frames"), initialDir,
        tr("FITS Files (*.fit *.fits);;All Files (*)"));

    if (!files.isEmpty())
        saveInputDir(files.first());

    for (const QString& file : files) {
        if (!m_lightFiles.contains(file)) {
            m_lightFiles.append(file);
            m_lightsList->addItem(QFileInfo(file).fileName());
        }
    }
}

void PreprocessingDialog::onRemoveLights()
{
    QList<QListWidgetItem*> selected = m_lightsList->selectedItems();
    for (auto* item : selected) {
        int row = m_lightsList->row(item);
        if (row >= 0 && row < m_lightFiles.size())
            m_lightFiles.removeAt(row);
        delete item;
    }
}

void PreprocessingDialog::onClearLights()
{
    m_lightFiles.clear();
    m_lightsList->clear();
}

// ============================================================================
// Parameter collection
// ============================================================================

Preprocessing::PreprocessParams PreprocessingDialog::gatherParams() const
{
    Preprocessing::PreprocessParams params;

    // Master paths
    params.masterBias     = m_biasPath->text();
    params.masterDark     = m_darkPath->text();
    params.masterFlat     = m_flatPath->text();
    params.masterDarkFlat = m_darkFlatPath->text();

    // Enable flags (only active if path is also non-empty)
    params.useBias = m_useBiasCheck->isChecked() && !params.masterBias.isEmpty();
    params.useDark = m_useDarkCheck->isChecked() && !params.masterDark.isEmpty();
    params.useFlat = m_useFlatCheck->isChecked() && !params.masterFlat.isEmpty();

    params.darkOptim.enabled = m_darkOptimCheck->isChecked();

    // Debayer settings
    params.debayer = m_debayerCheck->isChecked();

    switch (m_bayerPatternCombo->currentIndex()) {
    case 0: params.bayerPattern = Preprocessing::BayerPattern::RGGB; break;
    case 1: params.bayerPattern = Preprocessing::BayerPattern::BGGR; break;
    case 2: params.bayerPattern = Preprocessing::BayerPattern::GBRG; break;
    case 3: params.bayerPattern = Preprocessing::BayerPattern::GRBG; break;
    }

    switch (m_debayerAlgoCombo->currentIndex()) {
    case 0: params.debayerAlgorithm = Preprocessing::DebayerAlgorithm::Bilinear;   break;
    case 1: params.debayerAlgorithm = Preprocessing::DebayerAlgorithm::VNG;        break;
    case 2: params.debayerAlgorithm = Preprocessing::DebayerAlgorithm::SuperPixel; break;
    }

    params.cfaEqualize.enabled = m_cfaEqualizeCheck->isChecked();

    // Cosmetic correction
    if (m_cosmeticCheck->isChecked()) {
        int mode = m_cosmeticModeCombo->currentIndex();
        params.cosmetic.type = (mode == 0)
            ? Preprocessing::CosmeticType::Sigma
            : Preprocessing::CosmeticType::FromMaster;
        params.cosmetic.hotSigma  = static_cast<float>(m_hotSigmaSpin->value());
        params.cosmetic.coldSigma = static_cast<float>(m_coldSigmaSpin->value());
    } else {
        params.cosmetic.type = Preprocessing::CosmeticType::None;
    }

    // Output settings
    params.outputPrefix = m_outputPrefix->text();
    params.outputDir    = m_outputDir->text();

    // Sensor fixes
    params.fixBanding  = m_fixBandingCheck->isChecked();
    params.fixBadLines = m_fixBadLinesCheck->isChecked();
    params.fixXTrans   = m_fixXTransCheck->isChecked();

    // Advanced parameters
    params.biasLevel    = (m_biasLevelSpin->value() > 0)
                              ? m_biasLevelSpin->value() : 1e30;
    params.pedestal     = m_pedestalSpin->value();
    params.equalizeFlat = m_cfaEqualizeCheck->isChecked();

    return params;
}

// ============================================================================
// Calibration execution
// ============================================================================

void PreprocessingDialog::onStartCalibration()
{
    if (m_lightFiles.isEmpty()) {
        QMessageBox::warning(this, tr("No Files"),
            tr("Please add light frames to calibrate."));
        return;
    }

    m_isRunning = true;
    m_startBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);
    m_logText->clear();

    Preprocessing::PreprocessParams params = gatherParams();

    QString outputDir = params.outputDir.isEmpty()
                            ? QFileInfo(m_lightFiles.first()).absolutePath()
                            : params.outputDir;

    m_worker = std::make_unique<Preprocessing::PreprocessingWorker>(
        params, m_lightFiles, outputDir);

    connect(m_worker.get(), &Preprocessing::PreprocessingWorker::progressChanged,
            this,           &PreprocessingDialog::onProgressChanged);
    connect(m_worker.get(), &Preprocessing::PreprocessingWorker::logMessage,
            this,           &PreprocessingDialog::onLogMessage);
    connect(m_worker.get(), &Preprocessing::PreprocessingWorker::imageProcessed,
            this,           &PreprocessingDialog::onImageProcessed);
    connect(m_worker.get(), &Preprocessing::PreprocessingWorker::finished,
            this,           &PreprocessingDialog::onFinished);

    m_logText->append(tr("Starting calibration of %1 images...")
                      .arg(m_lightFiles.size()));
    m_worker->start();
}

void PreprocessingDialog::onCancel()
{
    if (m_worker && m_worker->isRunning())
        m_worker->requestCancel();
}

// ============================================================================
// Progress callbacks
// ============================================================================

void PreprocessingDialog::onProgressChanged(
    [[maybe_unused]] const QString& message, double progress)
{
    if (progress >= 0)
        m_progressBar->setValue(static_cast<int>(progress * 100));
}

void PreprocessingDialog::onLogMessage(
    const QString& message, const QString& color)
{
    QString finalColor = color;
    if (finalColor.toLower() == "neutral")
        finalColor = "";

    if (finalColor.isEmpty())
        m_logText->append(message);
    else
        m_logText->append(
            QString("<span style='color:%1'>%2</span>")
            .arg(finalColor, message));
}

void PreprocessingDialog::onImageProcessed(const QString& path, bool success)
{
    QString filename = QFileInfo(path).fileName();
    if (success)
        m_logText->append(tr("OK: %1").arg(filename));
    else
        m_logText->append(
            tr("<span style='color:red'>FAIL: %1</span>").arg(filename));
}

void PreprocessingDialog::onFinished(bool success)
{
    m_isRunning = false;
    m_startBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);

    if (success)
        m_logText->append(
            tr("<span style='color:green'>Calibration complete!</span>"));
    else
        m_logText->append(
            tr("<span style='color:salmon'>Calibration finished with errors</span>"));

    m_worker.reset();
}