#ifndef COSMICCLARITYDIALOG_H
#define COSMICCLARITYDIALOG_H

#include "DialogBase.h"
#include "algos/CosmicClarityRunner.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QSlider>

/**
 * @brief Dialog for configuring and launching a Cosmic Clarity processing pass.
 *
 * Supports four operation modes:
 *   Sharpen      - stellar and/or non-stellar sharpening
 *   Denoise      - luminance and/or colour denoising
 *   Both         - combined sharpen + denoise in a single pass
 *   Super Resolution - upscaling by 2x, 3x, or 4x
 *
 * GPU availability is detected at construction time; the combo box default
 * is set accordingly.
 */
class CosmicClarityDialog : public DialogBase
{
    Q_OBJECT

public:
    explicit CosmicClarityDialog(QWidget* parent = nullptr);

    /**
     * @brief Returns the processing parameters as configured by the user.
     */
    CosmicClarityParams getParams() const;

private slots:
    /**
     * @brief Shows or hides parameter groups based on the selected mode.
     */
    void updateUI();

private:
    // --- Mode and GPU ---
    QComboBox* m_cmbMode;
    QComboBox* m_cmbGpu;

    // --- Sharpen parameters ---
    QLabel*    m_lblShMode;  QComboBox* m_cmbShMode;
    QCheckBox* m_chkShSep;
    QCheckBox* m_chkAutoPsf;
    QLabel*    m_lblPsf;     QSlider*   m_sldPsf;
    QLabel*    m_lblStAmt;   QSlider*   m_sldStAmt;
    QLabel*    m_lblNstAmt;  QSlider*   m_sldNstAmt;

    // --- Denoise parameters ---
    QLabel*    m_lblDnLum;   QSlider*   m_sldDnLum;
    QLabel*    m_lblDnCol;   QSlider*   m_sldDnCol;
    QLabel*    m_lblDnMode;  QComboBox* m_cmbDnMode;
    QCheckBox* m_chkDnSep;

    // --- Super Resolution parameters ---
    QLabel*    m_lblScale;   QComboBox* m_cmbScale;
};

#endif // COSMICCLARITYDIALOG_H