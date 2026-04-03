#ifndef UPSCALEDIALOG_H
#define UPSCALEDIALOG_H

// =============================================================================
// UpscaleDialog.h
// Dialog for resampling (upscaling/downscaling) the active image with a choice
// of interpolation methods. Width and height are linked by the original aspect
// ratio.
// =============================================================================

#include "DialogBase.h"
#include "../ImageBuffer.h"

#include <QPointer>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>

class ImageViewer;

class UpscaleDialog : public DialogBase {
    Q_OBJECT

public:
    explicit UpscaleDialog(QWidget* parent = nullptr);
    ~UpscaleDialog() = default;

    /// Sets the target viewer and initialises dimension spin boxes from its buffer.
    void setViewer(ImageViewer* v);

private slots:
    void onApply();

private:
    QPointer<ImageViewer> m_viewer;

    QSpinBox*  m_widthSpin;
    QSpinBox*  m_heightSpin;
    QComboBox* m_methodCombo;

    float m_aspectRatio = 1.0f;
};

#endif // UPSCALEDIALOG_H