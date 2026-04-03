// =============================================================================
// MagentaCorrectionDialog.cpp
// Implementation of the magenta correction parameter dialog. Provides
// synchronized slider/spinbox controls for blue-channel modulation amount
// and luminance threshold, plus an optional star-mask toggle.
// =============================================================================

#include "MagentaCorrectionDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QIcon>
#include <QCheckBox>

#include "../ImageViewer.h"

// =============================================================================
// Construction / Destruction
// =============================================================================

MagentaCorrectionDialog::MagentaCorrectionDialog(QWidget* parent)
    : DialogBase(parent, tr("Magenta Correction"), 380, 180)
{
    setModal(false);
    setWindowModality(Qt::NonModal);
    setWindowIcon(QIcon(":/images/Logo.png"));

    QVBoxLayout* layout = new QVBoxLayout(this);

    // -- Amount slider (blue-channel modulation) ------------------------------
    // 1.0 = no change, 0.0 = full modulation.
    QHBoxLayout* amountLayout = new QHBoxLayout();
    amountLayout->addWidget(new QLabel(tr("Amount (Mod B):")));

    m_amountSlider = new QSlider(Qt::Horizontal);
    m_amountSlider->setRange(0, 100);
    m_amountSlider->setValue(50);

    m_amountSpin = new QDoubleSpinBox();
    m_amountSpin->setRange(0.0, 1.0);
    m_amountSpin->setSingleStep(0.05);
    m_amountSpin->setValue(0.5);

    amountLayout->addWidget(m_amountSlider);
    amountLayout->addWidget(m_amountSpin);
    layout->addLayout(amountLayout);

    // -- Luminance threshold slider -------------------------------------------
    QHBoxLayout* threshLayout = new QHBoxLayout();
    threshLayout->addWidget(new QLabel(tr("Luma Threshold:")));

    m_threshSlider = new QSlider(Qt::Horizontal);
    m_threshSlider->setRange(0, 100);
    m_threshSlider->setValue(10);

    m_threshSpin = new QDoubleSpinBox();
    m_threshSpin->setRange(0.0, 1.0);
    m_threshSpin->setSingleStep(0.01);
    m_threshSpin->setValue(0.1);

    threshLayout->addWidget(m_threshSlider);
    threshLayout->addWidget(m_threshSpin);
    layout->addLayout(threshLayout);

    // -- Star mask checkbox ---------------------------------------------------
    m_starMaskCheck = new QCheckBox(tr("Restrict to Stars (Star Mask)"));
    m_starMaskCheck->setStyleSheet("color: white;");
    layout->addWidget(m_starMaskCheck);

    // -- Synchronize slider <-> spinbox for Amount ----------------------------
    connect(m_amountSlider, &QSlider::valueChanged, [this](int val) {
        m_amountSpin->setValue(val / 100.0);
    });
    connect(m_amountSpin,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) {
        m_amountSlider->setValue(static_cast<int>(val * 100));
    });

    // -- Synchronize slider <-> spinbox for Threshold -------------------------
    connect(m_threshSlider, &QSlider::valueChanged, [this](int val) {
        m_threshSpin->setValue(val / 100.0);
    });
    connect(m_threshSpin,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) {
        m_threshSlider->setValue(static_cast<int>(val * 100));
    });

    // -- Action buttons -------------------------------------------------------
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* applyBtn  = new QPushButton(tr("Apply"));
    QPushButton* closeBtn  = new QPushButton(tr("Close"));
    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);
    btnLayout->addWidget(applyBtn);
    layout->addLayout(btnLayout);

    connect(applyBtn, &QPushButton::clicked,
            this, &MagentaCorrectionDialog::apply);
    connect(closeBtn, &QPushButton::clicked,
            this, &QDialog::close);
}

MagentaCorrectionDialog::~MagentaCorrectionDialog() {}

// =============================================================================
// Viewer binding
// =============================================================================

void MagentaCorrectionDialog::setViewer(ImageViewer* v)
{
    m_viewer = v;
}

// =============================================================================
// Parameter accessors
// =============================================================================

float MagentaCorrectionDialog::getAmount() const
{
    return static_cast<float>(m_amountSpin->value());
}

float MagentaCorrectionDialog::getThreshold() const
{
    return static_cast<float>(m_threshSpin->value());
}

bool MagentaCorrectionDialog::isWithStarMask() const
{
    return m_starMaskCheck->isChecked();
}