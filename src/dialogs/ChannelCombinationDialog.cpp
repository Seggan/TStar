// =============================================================================
// ChannelCombinationDialog.cpp
//
// Implementation of the channel combination dialog. Allows users to select
// three monochrome source images for R, G, B channels, optionally equalize
// their medians (linear fit), and produce a combined color image.
// =============================================================================

#include "ChannelCombinationDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QCheckBox>
#include <QIcon>

#include "../algos/ChannelOps.h"


// =============================================================================
// Construction
// =============================================================================

ChannelCombinationDialog::ChannelCombinationDialog(
    const std::vector<ChannelSource>& availableSources,
    QWidget* parent)
    : DialogBase(parent, tr("Channel Combination"), 400, 200)
    , m_sources(availableSources)
{
    setWindowIcon(QIcon(":/images/Logo.png"));

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Helper lambda to create a channel assignment row
    auto addChannelRow = [&](const QString& label, QComboBox** combo) {
        QHBoxLayout* row = new QHBoxLayout();
        row->addWidget(new QLabel(label));

        *combo = new QComboBox();
        (*combo)->addItem(tr("None"), -1);

        for (size_t i = 0; i < m_sources.size(); ++i)
            (*combo)->addItem(m_sources[i].name, static_cast<int>(i));

        row->addWidget(*combo);
        mainLayout->addLayout(row);
    };

    addChannelRow(tr("Red:"),   &m_comboR);
    addChannelRow(tr("Green:"), &m_comboG);
    addChannelRow(tr("Blue:"),  &m_comboB);

    // Linear fit checkbox (match medians across channels)
    m_checkLinearFit = new QCheckBox(tr("Linear Fit (Match medians)"));
    m_checkLinearFit->setChecked(false);
    mainLayout->addWidget(m_checkLinearFit);

    // Auto-select channels based on naming conventions (_R, _G, _B suffixes)
    for (size_t i = 0; i < m_sources.size(); ++i) {
        QString name = m_sources[i].name;
        if (name.contains("_R") || name.endsWith(" R"))
            m_comboR->setCurrentIndex(static_cast<int>(i) + 1);
        if (name.contains("_G") || name.endsWith(" G"))
            m_comboG->setCurrentIndex(static_cast<int>(i) + 1);
        if (name.contains("_B") || name.endsWith(" B"))
            m_comboB->setCurrentIndex(static_cast<int>(i) + 1);
    }

    // Action buttons
    QHBoxLayout* btnLayout   = new QHBoxLayout();
    QPushButton* btnOk       = new QPushButton(tr("Apply"));
    QPushButton* btnCancel   = new QPushButton(tr("Cancel"));

    btnLayout->addStretch();
    btnLayout->addWidget(btnCancel);
    btnLayout->addWidget(btnOk);
    mainLayout->addLayout(btnLayout);

    connect(btnOk,     &QPushButton::clicked,
            this, &ChannelCombinationDialog::onApply);
    connect(btnCancel, &QPushButton::clicked,
            this, &ChannelCombinationDialog::onCancel);
}


// =============================================================================
// Apply -- Validate, Optionally Fit, and Combine Channels
// =============================================================================

void ChannelCombinationDialog::onApply()
{
    int idxR = m_comboR->currentData().toInt();
    int idxG = m_comboG->currentData().toInt();
    int idxB = m_comboB->currentData().toInt();

    if (idxR < 0 || idxG < 0 || idxB < 0) {
        QMessageBox::warning(this, tr("Incomplete"),
            tr("Please select a source for all R, G, B channels."));
        return;
    }

    ImageBuffer bufR = m_sources[idxR].buffer;
    ImageBuffer bufG = m_sources[idxG].buffer;
    ImageBuffer bufB = m_sources[idxB].buffer;

    // Optional linear fit: equalize channel medians before combination
    if (m_checkLinearFit && m_checkLinearFit->isChecked()) {
        float medR = bufR.getChannelMedian(0);
        float medG = bufG.getChannelMedian(0);
        float medB = bufB.getChannelMedian(0);

        float targetMed = std::max({medR, medG, medB});
        if (targetMed > 0) {
            if (medR > 1e-7f) bufR.multiply(targetMed / medR);
            if (medG > 1e-7f) bufG.multiply(targetMed / medG);
            if (medB > 1e-7f) bufB.multiply(targetMed / medB);
        }
    }

    // Validate that all sources have matching dimensions
    if (bufR.width()  != bufG.width()  || bufR.height() != bufG.height() ||
        bufR.width()  != bufB.width()  || bufR.height() != bufB.height()) {
        QMessageBox::warning(this, tr("Error"),
            tr("Selected images must have the same dimensions."));
        return;
    }

    // Perform the channel combination
    m_result = ChannelOps::combineChannels(bufR, bufG, bufB);
    if (!m_result.isValid()) {
        QMessageBox::critical(this, tr("Error"),
            tr("Failed to combine channels."));
        return;
    }

    accept();
}

void ChannelCombinationDialog::onCancel()
{
    reject();
}