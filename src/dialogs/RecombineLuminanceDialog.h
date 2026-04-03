#ifndef RECOMBINELUMINANCEDIALOG_H
#define RECOMBINELUMINANCEDIALOG_H

#include "DialogBase.h"
#include "../ImageBuffer.h"

#include <vector>

class MainWindowCallbacks;
class ImageViewer;
class QComboBox;
class QSlider;
class QLabel;

/**
 * @brief Dialog for recombining a luminance channel into an RGB image.
 *
 * Allows the user to select a single-channel (mono) luminance source image,
 * choose the color space for the recombination (HSL, HSV, or CIE L*a*b*),
 * and blend the result with the target image using a configurable blend factor.
 */
class RecombineLuminanceDialog : public DialogBase
{
    Q_OBJECT

public:
    /**
     * @brief Parameters used for luminance recombination.
     */
    struct Params
    {
        int   sourceWindowId;    ///< Index of the selected source window.
        int   colorSpaceIndex;   ///< Color space mode, maps to ChannelOps::ColorSpaceMode.
        float blend;             ///< Blend factor in the range [0.0, 1.0].
    };

    explicit RecombineLuminanceDialog(QWidget* parent = nullptr);

    /**
     * @brief Repopulates the source image combo box with all open viewers
     *        except the current target viewer.
     */
    void refreshSourceList();

private slots:
    /**
     * @brief Applies the luminance recombination to the current target viewer.
     */
    void onApply();

    /**
     * @brief Updates the blend percentage label when the slider value changes.
     * @param val Current slider value (0-100).
     */
    void updateBlendLabel(int val);

private:
    MainWindowCallbacks* m_mainWindow;

    QComboBox* m_sourceCombo;
    QComboBox* m_colorSpaceCombo;
    QSlider*   m_blendSlider;
    QLabel*    m_blendLabel;
};

#endif // RECOMBINELUMINANCEDIALOG_H