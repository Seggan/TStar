#ifndef REFERENCEALIGNDIALOG_H
#define REFERENCEALIGNDIALOG_H

#include "DialogBase.h"
#include "../ImageBuffer.h"

#include <QImage>
#include <QPixmap>
#include <QSize>

class QLabel;
class QPushButton;
class QSlider;
class QDoubleSpinBox;
class QCheckBox;

/**
 * @brief Dialog for manually aligning the reference image in CBE tool.
 *
 * Allows the user to flip or rotate a reference image (e.g., a catalog plate)
 * to match the orientation of the target image. Optionally displays a
 * semi-transparent overlay of the target for visual comparison.
 */
class ReferenceAlignDialog : public DialogBase
{
    Q_OBJECT

public:
    /**
     * @brief Constructs the dialog with the given reference and target buffers.
     * @param parent        Parent widget.
     * @param refBuffer     The original reference image buffer to transform.
     * @param targetBuffer  The target image buffer used for overlay comparison.
     * @param paddingFactor Factor by which the reference buffer was padded;
     *                      used to crop back to the correct size after rotation.
     */
    explicit ReferenceAlignDialog(QWidget*           parent,
                                  const ImageBuffer& refBuffer,
                                  const ImageBuffer& targetBuffer,
                                  double             paddingFactor);
    ~ReferenceAlignDialog();

    /**
     * @brief Returns the final transformed image buffer after the user confirms.
     */
    ImageBuffer getAlignedBuffer() const;

private slots:
    void onFlipHorizontal();
    void onFlipVertical();
    void onRotateCW();
    void onRotateCCW();
    void onRotationChanged(double value);
    void onOverlayToggled(bool checked);
    void onOverlayStretchToggled(bool checked);
    void onOpacityChanged(int value);
    void updatePreview();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    /**
     * @brief Applies the current flip and rotation transforms to the original
     *        reference buffer and crops the result to remove padding.
     */
    void   rebuildBuffer();

    /**
     * @brief Converts an ImageBuffer to a QImage scaled to fit the preview label,
     *        optionally applying auto-stretch for better visibility.
     * @param buf         Source image buffer.
     * @param autoStretch Whether to apply automatic stretch during conversion.
     */
    QImage bufferToQImageScaled(const ImageBuffer& buf, bool autoStretch = true);

    // Image data
    ImageBuffer m_originalRefBuffer;  ///< Unmodified original reference buffer.
    ImageBuffer m_targetBuffer;       ///< Target buffer used for overlay display.
    ImageBuffer m_currentBuffer;      ///< Transformed buffer reflecting current settings.
    double      m_paddingFactor;      ///< Padding factor applied to the reference image.

    // Transform state
    bool   m_flipH          = false;
    bool   m_flipV          = false;
    double m_rotationAngle  = 0.0;

    // Overlay state
    bool m_showOverlay        = false;
    bool m_overlayAutoStretch = true;
    int  m_overlayOpacity     = 50;

    // Widgets
    QLabel*         m_previewLabel;
    QSlider*        m_sliderRotation;
    QDoubleSpinBox* m_spinRotation;
    QCheckBox*      m_chkOverlayStretch;
    QSlider*        m_sliderOpacity;

    QSize m_lastPreviewSize;  ///< Tracks previous preview label size to avoid redundant redraws.
};

#endif // REFERENCEALIGNDIALOG_H