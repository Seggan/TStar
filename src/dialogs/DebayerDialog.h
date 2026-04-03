#ifndef DEBAYER_DIALOG_H
#define DEBAYER_DIALOG_H

#include "DialogBase.h"

#include <QComboBox>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>

class ImageViewer;
class MainWindowCallbacks;

/**
 * @brief Dialog for demosaicing a single-channel Bayer mosaic image.
 *
 * The Bayer pattern can be set explicitly or resolved automatically by:
 *   1. Parsing known FITS/XISF header keywords (BAYERPAT, CFAPATTERN, etc.)
 *   2. Scoring all four candidate patterns by the colour neutrality of the
 *      bilinear-debayered result and selecting the best.
 *
 * Supported interpolation methods: edge-aware, bilinear.
 */
class DebayerDialog : public DialogBase
{
    Q_OBJECT

public:
    explicit DebayerDialog(QWidget* parent = nullptr);

    /**
     * @brief Associates the dialog with an ImageViewer and refreshes the
     *        detected pattern label from the buffer metadata.
     */
    void setViewer(ImageViewer* v);

private slots:
    void onApply();
    void onPatternChanged(int index);

private:
    /**
     * @brief Updates the detected pattern label from buffer metadata.
     */
    void updatePatternLabel();

    /**
     * @brief Scans known FITS/XISF header keywords for a Bayer pattern string.
     * @return The detected pattern (e.g. "RGGB") or an empty string if not found.
     */
    QString detectPatternFromHeader();

    /**
     * @brief Tries all four Bayer patterns and returns the one that produces
     *        the most spectrally neutral result under bilinear interpolation.
     */
    QString autoDetectByScoring();

    ImageViewer* m_viewer = nullptr;

    QComboBox*    m_patternCombo;
    QComboBox*    m_methodCombo;
    QLabel*       m_detectedLabel;
    QLabel*       m_statusLabel;
    QProgressBar* m_progress;
    QPushButton*  m_applyBtn;
};

#endif // DEBAYER_DIALOG_H