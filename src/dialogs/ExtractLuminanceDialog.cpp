#include "ExtractLuminanceDialog.h"
#include "DialogBase.h"
#include "../MainWindowCallbacks.h"
#include "../ImageViewer.h"
#include "../ImageBuffer.h"
#include "../algos/ChannelOps.h"
#include "../widgets/CustomMdiSubWindow.h"

#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ExtractLuminanceDialog::ExtractLuminanceDialog(QWidget* parent)
    : DialogBase(parent, tr("Extract Luminance"))
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // -- Method selection row -----------------------------------------------
    QHBoxLayout* methodLayout = new QHBoxLayout();
    methodLayout->addWidget(new QLabel(tr("Method:")));

    m_methodCombo = new QComboBox();
    m_methodCombo->addItem(tr("Rec. 709 (Standard)"), static_cast<int>(ChannelOps::LumaMethod::REC709));
    m_methodCombo->addItem(tr("Rec. 601"),             static_cast<int>(ChannelOps::LumaMethod::REC601));
    m_methodCombo->addItem(tr("Rec. 2020"),            static_cast<int>(ChannelOps::LumaMethod::REC2020));
    m_methodCombo->addItem(tr("Average (Equal)"),      static_cast<int>(ChannelOps::LumaMethod::AVERAGE));
    m_methodCombo->addItem(tr("Max"),                  static_cast<int>(ChannelOps::LumaMethod::MAX));
    m_methodCombo->addItem(tr("Median"),               static_cast<int>(ChannelOps::LumaMethod::MEDIAN));
    m_methodCombo->addItem(tr("SNR (Noise Weighted)"), static_cast<int>(ChannelOps::LumaMethod::SNR));
    m_methodCombo->addItem(tr("Custom / Sensor"),      static_cast<int>(ChannelOps::LumaMethod::CUSTOM));
    methodLayout->addWidget(m_methodCombo);

    mainLayout->addLayout(methodLayout);

    // -- Custom weights group -----------------------------------------------
    m_customGroup = new QGroupBox(tr("Custom RGB Weights"));
    QHBoxLayout* customLayout = new QHBoxLayout(m_customGroup);

    auto addWeight = [&](const QString& label, QDoubleSpinBox*& spin) {
        customLayout->addWidget(new QLabel(label));
        spin = new QDoubleSpinBox();
        spin->setRange(0.0, 10.0);
        spin->setSingleStep(0.01);
        spin->setDecimals(4);
        spin->setValue(0.3333);
        customLayout->addWidget(spin);
    };

    addWeight("R:", m_weightR);
    addWeight("G:", m_weightG);
    addWeight("B:", m_weightB);

    mainLayout->addWidget(m_customGroup);

    // -- SNR settings group -------------------------------------------------
    m_snrGroup = new QGroupBox(tr("SNR Settings"));
    QVBoxLayout* snrLayout = new QVBoxLayout(m_snrGroup);

    m_autoNoiseCheck = new QCheckBox(tr("Auto Estimate Noise"));
    m_autoNoiseCheck->setChecked(true);
    snrLayout->addWidget(m_autoNoiseCheck);

    QHBoxLayout* sigmaLayout = new QHBoxLayout();
    auto addSigma = [&](const QString& label, QDoubleSpinBox*& spin) {
        sigmaLayout->addWidget(new QLabel(label));
        spin = new QDoubleSpinBox();
        spin->setRange(0.0, 1.0);
        spin->setSingleStep(0.0001);
        spin->setDecimals(6);
        spin->setValue(0.01);
        sigmaLayout->addWidget(spin);
    };

    addSigma(QString::fromUtf8("\u03C3R:"), m_sigmaR);
    addSigma(QString::fromUtf8("\u03C3G:"), m_sigmaG);
    addSigma(QString::fromUtf8("\u03C3B:"), m_sigmaB);
    snrLayout->addLayout(sigmaLayout);

    mainLayout->addWidget(m_snrGroup);

    // -- Action buttons -----------------------------------------------------
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* applyBtn = new QPushButton(tr("Extract"));
    QPushButton* closeBtn = new QPushButton(tr("Close"));
    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);
    btnLayout->addWidget(applyBtn);
    mainLayout->addLayout(btnLayout);

    // -- Signal / slot wiring -----------------------------------------------
    connect(m_methodCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExtractLuminanceDialog::onMethodChanged);

    connect(applyBtn,  &QPushButton::clicked, this, &ExtractLuminanceDialog::onApply);
    connect(closeBtn,  &QPushButton::clicked, this, &ExtractLuminanceDialog::reject);

    connect(m_autoNoiseCheck, &QCheckBox::toggled, [this](bool checked) {
        m_sigmaR->setEnabled(!checked);
        m_sigmaG->setEnabled(!checked);
        m_sigmaB->setEnabled(!checked);
    });

    // Trigger initial visibility state.
    onMethodChanged(0);
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void ExtractLuminanceDialog::onMethodChanged(int index)
{
    const int method = m_methodCombo->itemData(index).toInt();
    const bool isCustom = (method == static_cast<int>(ChannelOps::LumaMethod::CUSTOM));
    const bool isSNR    = (method == static_cast<int>(ChannelOps::LumaMethod::SNR));

    m_customGroup->setEnabled(isCustom);
    m_snrGroup->setEnabled(isSNR);
}

ExtractLuminanceDialog::Params ExtractLuminanceDialog::getParams() const
{
    Params p;
    p.methodIndex       = m_methodCombo->currentData().toInt();
    p.customWeights     = { static_cast<float>(m_weightR->value()),
                            static_cast<float>(m_weightG->value()),
                            static_cast<float>(m_weightB->value()) };
    p.autoEstimateNoise = m_autoNoiseCheck->isChecked();
    p.customNoiseSigma  = { static_cast<float>(m_sigmaR->value()),
                            static_cast<float>(m_sigmaG->value()),
                            static_cast<float>(m_sigmaB->value()) };
    return p;
}

void ExtractLuminanceDialog::onApply()
{
    MainWindowCallbacks* mainWindow = getCallbacks();
    if (!mainWindow) return;

    ImageViewer* viewer = mainWindow->getCurrentViewer();
    if (!viewer) return;

    const Params p = getParams();
    const auto method = static_cast<ChannelOps::LumaMethod>(p.methodIndex);

    // Use custom noise sigma only when SNR method is selected and auto-estimate is off.
    std::vector<float> noiseSigma;
    if (method == ChannelOps::LumaMethod::SNR && !p.autoEstimateNoise) {
        noiseSigma = p.customNoiseSigma;
    }

    ImageBuffer result = ChannelOps::computeLuminance(
        viewer->getBuffer(), method, p.customWeights, noiseSigma);

    if (!result.isValid()) {
        QMessageBox::critical(this, tr("Error"), tr("Failed to compute luminance."));
        return;
    }

    const QString title = viewer->getBuffer().name() + "_L";
    mainWindow->createResultWindow(result, title);
    accept();
}