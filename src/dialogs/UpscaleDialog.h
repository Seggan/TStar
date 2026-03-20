#ifndef UPSCALEDIALOG_H
#define UPSCALEDIALOG_H

#include "DialogBase.h"
#include <QPointer>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include "../ImageBuffer.h"

class UpscaleDialog : public DialogBase {
    Q_OBJECT
public:
    explicit UpscaleDialog(QWidget* parent = nullptr);
    ~UpscaleDialog() = default;

    void setViewer(class ImageViewer* v);

private slots:
    void onApply();

private:
    QPointer<class ImageViewer> m_viewer;
    QSpinBox* m_widthSpin;
    QSpinBox* m_heightSpin;
    QComboBox* m_methodCombo;
    float m_aspectRatio = 1.0f;
};

#endif // UPSCALEDIALOG_H
