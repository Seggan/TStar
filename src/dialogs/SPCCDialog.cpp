/*
 * SPCCDialog.cpp  —  Spectrophotometric Color Calibration dialog (Qt6)
 */

#include "SPCCDialog.h"
#include "ImageViewer.h"
#include "MainWindow.h"
#include "core/Logger.h"
#include "SPCC.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QHeaderView>
#include <QPushButton>
#include <QProgressBar>
#include <QMessageBox>
#include <QCloseEvent>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QFrame>
#include <QPainter>
#include <QtConcurrent/QtConcurrentRun>
#include <cmath>

// ─── Constructor ──────────────────────────────────────────────────────────────

SPCCDialog::SPCCDialog(ImageViewer* viewer, MainWindow* mw, QWidget* parent)
    : QDialog(parent), m_viewer(viewer), m_mainWindow(mw)
{
    setWindowTitle(tr("Spectrophotometric Color Calibration (SPCC)"));
    setWindowFlags(windowFlags() | Qt::Tool);
    setMinimumWidth(480);

    m_dataPath = QCoreApplication::applicationDirPath() + "/data/spcc";

    if (m_viewer && m_viewer->getBuffer().isValid())
        m_originalBuffer = m_viewer->getBuffer();

    m_watcher = new QFutureWatcher<SPCCResult>(this);

    buildUI();
    connectSignals();
    populateProfiles();
}

SPCCDialog::~SPCCDialog() {
    if (m_watcher->isRunning()) m_watcher->cancel();
}

void SPCCDialog::setViewer(ImageViewer* viewer)
{
    m_viewer = viewer;
    if (m_viewer && m_viewer->getBuffer().isValid()) {
        m_originalBuffer = m_viewer->getBuffer();
    }
}

// ─── Build UI ────────────────────────────────────────────────────────────────

void SPCCDialog::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setSpacing(10);
    root->setContentsMargins(12, 12, 12, 12);

    // ── Sensor / Filter ───────────────────────────────────────────────────
    auto* grpCam = new QGroupBox(tr("Sensor & Filter Profile"), this);
    auto* frmCam = new QFormLayout(grpCam);

    m_cameraCombo = new QComboBox(this);
    m_cameraCombo->setToolTip(tr("Select the spectral response profile of your camera sensor.\n"
                                 "These profiles describe how each channel responds to different wavelengths."));
    frmCam->addRow(tr("Camera / Sensor:"), m_cameraCombo);

    m_filterCombo = new QComboBox(this);
    m_filterCombo->setToolTip(tr("Optional optical filter in the optical path (L, UV/IR cut, etc.)"));
    frmCam->addRow(tr("Filter:"), m_filterCombo);

    root->addWidget(grpCam);

    // ── Star detection ────────────────────────────────────────────────────
    auto* grpDet = new QGroupBox(tr("Star Detection & Cross-Match"), this);
    auto* frmDet = new QFormLayout(grpDet);

    m_minSNRSpin = new QDoubleSpinBox(this);
    m_minSNRSpin->setRange(5.0, 200.0); m_minSNRSpin->setSingleStep(5.0);
    m_minSNRSpin->setValue(20.0);
    m_minSNRSpin->setToolTip(tr("Minimum signal-to-noise ratio for a star to be used."));
    frmDet->addRow(tr("Minimum SNR:"), m_minSNRSpin);

    m_maxStarsSpin = new QSpinBox(this);
    m_maxStarsSpin->setRange(10, 500); m_maxStarsSpin->setSingleStep(10);
    m_maxStarsSpin->setValue(200);
    frmDet->addRow(tr("Max stars:"), m_maxStarsSpin);

    m_apertureSpin = new QDoubleSpinBox(this);
    m_apertureSpin->setRange(1.0, 20.0); m_apertureSpin->setSingleStep(0.5);
    m_apertureSpin->setValue(4.0); m_apertureSpin->setSuffix(" px");
    frmDet->addRow(tr("Aperture radius:"), m_apertureSpin);

    auto* hMag = new QHBoxLayout;
    m_limitMagCheck = new QCheckBox(tr("Limit to mag <"), this);
    m_limitMagCheck->setChecked(true);
    m_magLimitSpin = new QDoubleSpinBox(this);
    m_magLimitSpin->setRange(8.0, 20.0); m_magLimitSpin->setSingleStep(0.5);
    m_magLimitSpin->setValue(13.5);
    hMag->addWidget(m_limitMagCheck);
    hMag->addWidget(m_magLimitSpin);
    hMag->addStretch();
    frmDet->addRow(tr("Magnitude limit:"), hMag);

    root->addWidget(grpDet);

    // ── Options ───────────────────────────────────────────────────────────
    auto* grpOpts = new QGroupBox(tr("Calibration Options"), this);
    auto* vOpts   = new QVBoxLayout(grpOpts);
    m_fullMatrixCheck = new QCheckBox(tr("Use full 3×3 colour matrix (vs. diagonal)"), this);
    m_fullMatrixCheck->setChecked(false);
    m_fullMatrixCheck->setToolTip(tr("Full 3×3 corrects cross-channel colour mixing; "
                                     "diagonal only rescales each channel independently.\n"
                                     "Use full matrix only if your sensor has significant channel crosstalk."));
    m_solarRefCheck = new QCheckBox(tr("Normalise to solar (G2V) white point"), this);
    m_solarRefCheck->setChecked(true);
    m_neutralBgCheck = new QCheckBox(tr("Subtract background before calibration"), this);
    m_neutralBgCheck->setChecked(true);
    vOpts->addWidget(m_fullMatrixCheck);
    vOpts->addWidget(m_solarRefCheck);
    vOpts->addWidget(m_neutralBgCheck);
    root->addWidget(grpOpts);

    // ── Results ───────────────────────────────────────────────────────────
    auto* grpRes = new QGroupBox(tr("Calibration Results"), this);
    auto* vRes   = new QVBoxLayout(grpRes);

    auto* hInfo = new QHBoxLayout;
    m_starsLabel    = new QLabel(tr("Stars used: —"), this);
    m_residualLabel = new QLabel(tr("Residual: —"),    this);
    m_scalesLabel   = new QLabel(tr("R/G/B scales: —"), this);
    hInfo->addWidget(m_starsLabel);
    hInfo->addWidget(m_residualLabel);
    vRes->addLayout(hInfo);
    vRes->addWidget(m_scalesLabel);

    // Colour swatch (shows resulting white balance)
    auto* hSwatch = new QHBoxLayout;
    hSwatch->addWidget(new QLabel(tr("White balance:"), this));
    m_colourSwatch = new QLabel(this);
    m_colourSwatch->setFixedSize(120, 22);
    m_colourSwatch->setFrameShape(QFrame::StyledPanel);
    hSwatch->addWidget(m_colourSwatch);
    hSwatch->addStretch();
    vRes->addLayout(hSwatch);

    // 3×3 matrix display
    m_matrixTable = new QTableWidget(3, 3, this);
    m_matrixTable->setHorizontalHeaderLabels({tr("R_in"), tr("G_in"), tr("B_in")});
    m_matrixTable->setVerticalHeaderLabels({tr("R_out"), tr("G_out"), tr("B_out")});
    m_matrixTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_matrixTable->setFixedHeight(100);
    m_matrixTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            auto* it = new QTableWidgetItem(i==j ? "1.0000" : "0.0000");
            it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_matrixTable->setItem(i, j, it);
        }
    vRes->addWidget(m_matrixTable);
    root->addWidget(grpRes);

    // ── Progress ──────────────────────────────────────────────────────────
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 0);
    m_progressBar->setVisible(false);
    m_statusLabel = new QLabel(this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    root->addWidget(m_progressBar);
    root->addWidget(m_statusLabel);

    // ── Buttons ───────────────────────────────────────────────────────────
    auto* hBtns = new QHBoxLayout;
    m_resetBtn = new QPushButton(tr("Reset"), this);
    m_runBtn   = new QPushButton(tr("Run SPCC"), this);
    m_closeBtn = new QPushButton(tr("Close"), this);
    m_runBtn->setDefault(true);
    hBtns->addWidget(m_resetBtn);
    hBtns->addStretch();
    hBtns->addWidget(m_runBtn);
    hBtns->addWidget(m_closeBtn);
    root->addLayout(hBtns);
}

// ─── Populate camera/filter combos ───────────────────────────────────────────

void SPCCDialog::populateProfiles()
{
    m_cameraCombo->clear();
    m_filterCombo->clear();

    QStringList cameras = SPCC::availableCameraProfiles(m_dataPath);
    if (cameras.isEmpty()) {
        cameras << "Generic DSLR" << "Generic Mono" << "Canon EOS Ra"
                << "Nikon Z6 II" << "Sony A7S III" << "ZWO ASI2600MC"
                << "ZWO ASI6200MM";
    }
    m_cameraCombo->addItems(cameras);

    QStringList filters = SPCC::availableFilterProfiles(m_dataPath);
    if (!filters.contains("Luminance")) filters.prepend("Luminance");
    if (!filters.contains("No Filter")) filters.append("No Filter");
    if (filters.size() <= 2) {
        filters << "UV/IR Cut" << "Baader L-Booster" << "Optolong L-Pro" << "Astronomik L-2";
    }
    m_filterCombo->addItems(filters);

    const int noFilterIdx = m_filterCombo->findText("No Filter");
    if (noFilterIdx >= 0) {
        m_filterCombo->setCurrentIndex(noFilterIdx);
    }
}

// ─── Connect signals ──────────────────────────────────────────────────────────

void SPCCDialog::connectSignals()
{
    connect(m_runBtn,   &QPushButton::clicked, this, &SPCCDialog::onRun);
    connect(m_resetBtn, &QPushButton::clicked, this, &SPCCDialog::onReset);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::close);
    connect(m_watcher,  &QFutureWatcher<SPCCResult>::finished, this, &SPCCDialog::onFinished);
    connect(m_cameraCombo, &QComboBox::currentTextChanged, this, &SPCCDialog::onCameraChanged);
}

// ─── Collect params ───────────────────────────────────────────────────────────

SPCCParams SPCCDialog::collectParams() const
{
    SPCCParams p;
    p.cameraProfile     = m_cameraCombo->currentText();
    p.filterProfile     = m_filterCombo->currentText();
    p.useFullMatrix     = m_fullMatrixCheck->isChecked();
    p.solarReference    = m_solarRefCheck->isChecked();
    p.neutralBackground = m_neutralBgCheck->isChecked();
    p.minSNR            = m_minSNRSpin->value();
    p.maxStars          = m_maxStarsSpin->value();
    p.apertureR         = m_apertureSpin->value();
    p.limitMagnitude    = m_limitMagCheck->isChecked();
    p.magLimit          = m_magLimitSpin->value();
    p.dataPath          = m_dataPath;
    return p;
}

// ─── Run slot ─────────────────────────────────────────────────────────────────

void SPCCDialog::onRun()
{
    if (!m_viewer || !m_viewer->getBuffer().isValid()) return;
    if (m_watcher->isRunning()) return;

    setControlsEnabled(false);
    m_progressBar->setVisible(true);
    m_statusLabel->setText(tr("Running SPCC…"));

    auto buf    = std::make_shared<ImageBuffer>(m_viewer->getBuffer());
    auto params = collectParams();

    auto* raw = new std::shared_ptr<ImageBuffer>(buf);
    m_watcher->setProperty("bufPtr", QVariant::fromValue(static_cast<void*>(raw)));

    QFuture<SPCCResult> fut = QtConcurrent::run([buf, params]() {
        return SPCC::calibrate(*buf, params);
    });
    m_watcher->setFuture(fut);
}

void SPCCDialog::onFinished()
{
    m_progressBar->setVisible(false);
    setControlsEnabled(true);

    auto* raw = static_cast<std::shared_ptr<ImageBuffer>*>(
        m_watcher->property("bufPtr").value<void*>());
    if (!raw) return;

    SPCCResult res = m_watcher->result();
    if (res.success && m_viewer) {
        m_viewer->pushUndo();
        m_viewer->setBuffer(**raw, m_viewer->windowTitle(), true);
        m_viewer->refreshDisplay(true);
        showResults(res);
        if (m_mainWindow) Logger::info(res.logMsg);
        m_statusLabel->setText(tr("Calibration complete."));
    } else {
        m_statusLabel->setText(tr("Failed: ") + res.errorMsg);
        QMessageBox::warning(this, tr("SPCC"), tr("Calibration failed:\n") + res.errorMsg);
    }
    delete raw;
}

void SPCCDialog::onReset()
{
    if (!m_viewer || !m_viewer->getBuffer().isValid()) return;
    m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
    m_viewer->refreshDisplay(true);
    m_statusLabel->setText(tr("Reset to original."));
}

void SPCCDialog::onCameraChanged(const QString& /*name*/) {
    // Could reload filter options per camera here
}

// ─── Show results ─────────────────────────────────────────────────────────────

void SPCCDialog::showResults(const SPCCResult& res)
{
    m_starsLabel->setText(tr("Stars used: %1 / %2")
        .arg(res.starsUsed).arg(res.starsFound));
    m_residualLabel->setText(tr("RMS residual: %1")
        .arg(res.residual, 0, 'f', 5));
    m_scalesLabel->setText(tr("Scales  R=×%1   G=×%2   B=×%3")
        .arg(res.scaleR, 0, 'f', 4)
        .arg(res.scaleG, 0, 'f', 4)
        .arg(res.scaleB, 0, 'f', 4));

    // Matrix display
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            m_matrixTable->item(i, j)->setText(
                QString::number(res.corrMatrix[i][j], 'f', 4));

    // Colour swatch: render a gradient representing the white-balance shift
    updateColourPreview(res.scaleR, res.scaleG, res.scaleB);
}

void SPCCDialog::updateColourPreview(double r, double g, double b)
{
    QPixmap pm(m_colourSwatch->size());
    pm.fill(Qt::transparent);
    QPainter p(&pm);

    double mx = std::max({r, g, b});
    if (mx < 1e-6) mx = 1.0;
    int ri = std::min(255, static_cast<int>(255.0 * r / mx));
    int gi = std::min(255, static_cast<int>(255.0 * g / mx));
    int bi = std::min(255, static_cast<int>(255.0 * b / mx));

    // Left half: theoretical white (neutral)
    p.fillRect(0, 0, pm.width()/2, pm.height(), QColor(200, 200, 200));
    // Right half: calibrated colour
    p.fillRect(pm.width()/2, 0, pm.width()/2, pm.height(), QColor(ri, gi, bi));
    p.end();

    m_colourSwatch->setPixmap(pm);
    m_colourSwatch->setToolTip(
        tr("Left: neutral reference   Right: calibrated white point\n"
           "R=%1  G=%2  B=%3").arg(ri).arg(gi).arg(bi));
}

void SPCCDialog::setControlsEnabled(bool en)
{
    m_cameraCombo->setEnabled(en);
    m_filterCombo->setEnabled(en);
    m_minSNRSpin->setEnabled(en);
    m_maxStarsSpin->setEnabled(en);
    m_apertureSpin->setEnabled(en);
    m_magLimitSpin->setEnabled(en);
    m_fullMatrixCheck->setEnabled(en);
    m_solarRefCheck->setEnabled(en);
    m_neutralBgCheck->setEnabled(en);
    m_runBtn->setEnabled(en);
    m_resetBtn->setEnabled(en);
}

void SPCCDialog::closeEvent(QCloseEvent* event)
{
    if (m_watcher->isRunning()) { m_watcher->cancel(); m_watcher->waitForFinished(); }
    QDialog::closeEvent(event);
}
