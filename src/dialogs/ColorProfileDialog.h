#ifndef COLORPROFILEDIALOG_H
#define COLORPROFILEDIALOG_H

#include "DialogBase.h"

#include <QLabel>
#include <QComboBox>
#include <QRadioButton>
#include <QLineEdit>
#include <QPushButton>

// Forward declarations
class ImageBuffer;
class ImageViewer;

namespace core { class ColorProfile; }

/**
 * @brief Dialog for managing ICC color profiles on the active image.
 *
 * Supports two operations:
 *  - Assign: reinterprets the image under a different profile without altering pixel data.
 *  - Convert: transforms pixel data to match the visual intent of the target profile.
 */
class ColorProfileDialog : public DialogBase
{
    Q_OBJECT

public:
    explicit ColorProfileDialog(ImageBuffer* activeBuffer,
                                ImageViewer*  viewer = nullptr,
                                QWidget*      parent = nullptr);

    /**
     * @brief Replaces the active buffer and viewer, then refreshes the displayed info.
     */
    void setBuffer(ImageBuffer* buffer, ImageViewer* viewer = nullptr);

public slots:
    void browseCustomProfile();
    void applyChanges();
    void loadCurrentInfo();

private slots:
    void onTargetProfileChanged(int index);

private:
    void setupUI();

    /**
     * @brief Constructs a ColorProfile from the current UI selection.
     */
    core::ColorProfile getSelectedProfile() const;

    // Data
    ImageBuffer* m_activeBuffer;
    ImageViewer* m_viewer;

    // UI elements - current image info
    QLabel* m_lblCurrentProfile;

    // UI elements - operation selection
    QRadioButton* m_radioAssign;
    QRadioButton* m_radioConvert;

    // UI elements - target profile selection
    QComboBox*   m_targetProfileCombo;
    QLineEdit*   m_customProfilePath;
    QPushButton* m_btnBrowseProfile;

    // UI elements - actions
    QPushButton* m_btnApply;
};

#endif // COLORPROFILEDIALOG_H