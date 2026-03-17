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

class ColorProfileDialog : public DialogBase {
    Q_OBJECT

public:
    explicit ColorProfileDialog(ImageBuffer* activeBuffer, ImageViewer* viewer = nullptr, QWidget* parent = nullptr);
    void setBuffer(ImageBuffer* buffer, ImageViewer* viewer = nullptr);

public slots:
    void browseCustomProfile();
    void applyChanges();
    void loadCurrentInfo();

private slots:
    void onTargetProfileChanged(int index);

private:
    void setupUI();
    core::ColorProfile getSelectedProfile() const;

    ImageBuffer* m_activeBuffer;
    ImageViewer* m_viewer;

    // UI Elements
    QLabel* m_lblCurrentProfile;
    
    QRadioButton* m_radioAssign;
    QRadioButton* m_radioConvert;
    
    QComboBox* m_targetProfileCombo;
    QLineEdit* m_customProfilePath;
    QPushButton* m_btnBrowseProfile;
    
    QPushButton* m_btnApply;
};

#endif // COLORPROFILEDIALOG_H