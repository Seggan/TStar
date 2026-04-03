// =============================================================================
// BinningDialog.cpp
//
// Provides a dialog for selecting and applying pixel binning factors.
// The binning operation averages NxN pixel blocks to reduce resolution.
// =============================================================================

#include "BinningDialog.h"
#include "MainWindowCallbacks.h"
#include "../ImageViewer.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>


// =============================================================================
// Construction
// =============================================================================

BinningDialog::BinningDialog(QWidget* parent)
    : DialogBase(parent, tr("Image Binning"), 200, 80)
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Binning factor selector
    QHBoxLayout* comboLayout = new QHBoxLayout();
    comboLayout->addWidget(new QLabel(tr("Binning Factor:")));

    m_binCombo = new QComboBox(this);
    m_binCombo->addItem(tr("1x1 (None)"), 1);
    m_binCombo->addItem(tr("2x2"),        2);
    m_binCombo->addItem(tr("3x3"),        3);
    m_binCombo->setCurrentIndex(1);  // Default to 2x2

    comboLayout->addWidget(m_binCombo);
    mainLayout->addLayout(comboLayout);
    mainLayout->addStretch();

    // Action buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* closeBtn  = new QPushButton(tr("Close"));
    QPushButton* applyBtn  = new QPushButton(tr("Apply"));

    btnLayout->addWidget(closeBtn);
    btnLayout->addWidget(applyBtn);
    mainLayout->addLayout(btnLayout);

    connect(applyBtn, &QPushButton::clicked, this, &BinningDialog::onApply);
    connect(closeBtn, &QPushButton::clicked, this, &BinningDialog::reject);
}


// =============================================================================
// Viewer Management
// =============================================================================

void BinningDialog::setViewer(ImageViewer* v)
{
    m_viewer = v;
}


// =============================================================================
// Apply Operation
// =============================================================================

void BinningDialog::onApply()
{
    // Resolve the active viewer at the time of application
    MainWindowCallbacks* cb = getCallbacks();
    ImageViewer* v = cb ? cb->getCurrentViewer() : m_viewer.data();

    if (!v) {
        QMessageBox::warning(this, tr("No Image"), tr("Select image."));
        return;
    }

    int factor = m_binCombo->currentData().toInt();
    if (factor <= 1)
        return;

    v->pushUndo(tr("Binning"));
    v->getBuffer().bin(factor);
    v->refreshDisplay(false);
    v->fitToWindow();

    if (cb)
        cb->logMessage(tr("Binning applied."), 1);

    accept();
}