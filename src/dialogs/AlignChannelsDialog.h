#ifndef ALIGN_CHANNELS_DIALOG_H
#define ALIGN_CHANNELS_DIALOG_H

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>

#include "DialogBase.h"

class ImageViewer;
class MainWindowCallbacks;

/**
 * @brief Dialog for aligning multiple open images to a common reference.
 *
 * Uses the star-based registration engine to compute a geometric transform
 * (translation, optionally rotation and scale) between each selected target
 * image and the designated reference. The resulting warp is applied in-place
 * to each target viewer with full undo support. No photometric changes are
 * made to the image data.
 */
class AlignChannelsDialog : public DialogBase
{
    Q_OBJECT

public:
    explicit AlignChannelsDialog(QWidget* parent = nullptr);

    /** Re-populates all combo boxes from the currently open MDI sub-windows. */
    void refreshImageList();

private slots:
    void onApply();

private:
    /** Populates @p combo with all open viewers that contain a valid buffer. */
    void populateCombo(QComboBox* combo);

    /**
     * @brief Extracts the ImageViewer pointer stored as user data in @p combo.
     * @return The viewer pointer, or nullptr if the combo has no valid selection.
     */
    ImageViewer* viewerFromCombo(QComboBox* combo) const;

    // --- Application state -----------------------------------------------
    MainWindowCallbacks* m_mainWindow = nullptr;

    // --- Reference image control -----------------------------------------
    QComboBox* m_refCombo = nullptr;

    // --- Target image rows (up to kMaxTargets) ---------------------------
    struct TargetRow
    {
        QCheckBox* check = nullptr;
        QComboBox* combo = nullptr;
    };

    static constexpr int kMaxTargets = 3;
    TargetRow            m_targets[kMaxTargets];

    // --- Registration parameters -----------------------------------------
    QCheckBox*      m_allowRotationCheck = nullptr;
    QCheckBox*      m_allowScaleCheck    = nullptr;
    QDoubleSpinBox* m_thresholdSpin      = nullptr;

    // --- Status and actions ----------------------------------------------
    QLabel*       m_statusLabel = nullptr;
    QPushButton*  m_applyBtn    = nullptr;
};

#endif // ALIGN_CHANNELS_DIALOG_H