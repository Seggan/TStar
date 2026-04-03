#ifndef MAGENTACORRECTIONDIALOG_H
#define MAGENTACORRECTIONDIALOG_H

// =============================================================================
// MagentaCorrectionDialog.h
// Dialog for adjusting magenta color correction parameters (blue channel
// modulation amount, luminance threshold, and optional star-mask restriction).
// =============================================================================

#include "DialogBase.h"

#include <QCheckBox>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QPointer>

class ImageViewer;

class MagentaCorrectionDialog : public DialogBase {
    Q_OBJECT

public:
    explicit MagentaCorrectionDialog(QWidget* parent = nullptr);
    ~MagentaCorrectionDialog();

    // Retrieve the current parameter values.
    float getAmount()       const;
    float getThreshold()    const;
    bool  isWithStarMask()  const;

    // Associate with a viewer for potential preview integration.
    void setViewer(ImageViewer* v);

signals:
    // Emitted when the user clicks Apply.
    void apply();

private:
    QPointer<ImageViewer> m_viewer;

    QSlider*        m_amountSlider  = nullptr;
    QDoubleSpinBox* m_amountSpin    = nullptr;
    QSlider*        m_threshSlider  = nullptr;
    QDoubleSpinBox* m_threshSpin    = nullptr;
    QCheckBox*      m_starMaskCheck = nullptr;
};

#endif // MAGENTACORRECTIONDIALOG_H