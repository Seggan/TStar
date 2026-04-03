#ifndef SATURATIONDIALOG_H
#define SATURATIONDIALOG_H

#include "DialogBase.h"
#include "../ImageBuffer.h"
#include "../ImageViewer.h"

#include <QPointer>
#include <QSlider>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>

/**
 * @brief Dialog for adjusting color saturation with hue-selective masking.
 *
 * Supports real-time preview by applying adjustments directly to the live
 * image buffer. The original buffer is preserved internally and restored on
 * cancel. Supports preset hue ranges (Reds, Yellows, Greens, etc.) as well
 * as custom hue center, width, and feathering parameters.
 */
class SaturationDialog : public DialogBase
{
    Q_OBJECT

public:
    /**
     * @brief Serializable state of all slider and combo box values.
     */
    struct State
    {
        int amount;
        int bgFactor;
        int hueCenter;
        int hueWidth;
        int hueSmooth;
        int presetIndex;
    };

    explicit SaturationDialog(QWidget* parent, class ImageViewer* viewer);
    ~SaturationDialog();

    /**
     * @brief Returns the current saturation parameters built from the UI controls.
     */
    ImageBuffer::SaturationParams getParams() const;

    /**
     * @brief Directly assigns an image buffer pointer (without a viewer context).
     * @param buffer Pointer to the live image buffer to operate on.
     */
    void setBuffer(ImageBuffer* buffer);

    /**
     * @brief Sets a new target viewer, restoring any uncommitted preview on the
     *        previous viewer before switching.
     * @param viewer The new target viewer, or nullptr to detach.
     */
    void setViewer(ImageViewer* viewer);

    /**
     * @brief Applies the current parameters as a preview to the live buffer
     *        without committing an undo state.
     */
    void triggerPreview();

    State getState() const;
    void  setState(const State& s);
    void  resetState();

signals:
    /**
     * @brief Emitted when the user confirms the operation (Apply button).
     */
    void applyInternal(const ImageBuffer::SaturationParams& params);

    /**
     * @brief Emitted when preview parameters change (for external listeners).
     */
    void preview(const ImageBuffer::SaturationParams& params);

private slots:
    void onSliderChanged();
    void onPresetChanged(int index);
    void handleApply();

private:
    void setupUI();

    QPointer<ImageViewer> m_viewer;         ///< Tracked viewer (safe against destruction).
    ImageBuffer*          m_buffer;         ///< Pointer to the viewer's live buffer.
    ImageBuffer           m_originalBuffer; ///< Deep-copy backup for preview restoration.
    bool                  m_applied = false;

    // Adjustment sliders
    QSlider* m_sldAmount;
    QSlider* m_sldBgFactor;
    QSlider* m_sldHueCenter;
    QSlider* m_sldHueWidth;
    QSlider* m_sldHueSmooth;

    // Value display labels
    QLabel* m_valAmount;
    QLabel* m_valBgFactor;
    QLabel* m_valHueCenter;
    QLabel* m_valHueWidth;
    QLabel* m_valHueSmooth;

    QComboBox* m_cmbPresets;
    QCheckBox* m_chkPreview = nullptr;
};

#endif // SATURATIONDIALOG_H