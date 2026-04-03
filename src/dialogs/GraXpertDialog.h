#ifndef GRAXPERTDIALOG_H
#define GRAXPERTDIALOG_H

#include "DialogBase.h"
#include "algos/GraXpertRunner.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QRadioButton>

/**
 * @brief Configuration dialog for GraXpert background extraction / denoising.
 *
 * Lets the user choose between background extraction and AI denoising,
 * set smoothing/strength, select an AI model version, and toggle GPU
 * acceleration.  Returns a GraXpertParams struct via getParams().
 */
class GraXpertDialog : public DialogBase {
    Q_OBJECT

public:
    explicit GraXpertDialog(QWidget* parent = nullptr);

    /** @brief Collect the current UI state into a GraXpertParams struct. */
    GraXpertParams getParams() const;

private slots:
    /** @brief Update control visibility based on the selected operation. */
    void updateUI();

private:
    // -- Operation selection --
    QRadioButton*   m_rbBackground;
    QRadioButton*   m_rbDenoise;

    // -- Shared parameter controls --
    QDoubleSpinBox* m_spinStrength;      ///< Reused as smoothing (BG) or strength (denoise).
    QComboBox*      m_aiVersionCombo;
    QCheckBox*      m_gpuCheck;
};

#endif // GRAXPERTDIALOG_H