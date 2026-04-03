#ifndef CROPROTATEDIALOG_H
#define CROPROTATEDIALOG_H

#include "DialogBase.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPointer>
#include <QPushButton>
#include <QSlider>

/**
 * @brief Tool dialog for interactive rotation and cropping of the active image.
 *
 * Activates crop-mode on the target ImageViewer, keeps the angle and aspect
 * ratio synchronized between the spin box and slider, and supports applying
 * the same crop region to all open images via the batch action.
 */
class CropRotateDialog : public DialogBase
{
    Q_OBJECT

public:
    explicit CropRotateDialog(QWidget* parent = nullptr);

    /**
     * @brief Destructor - disables crop mode on the associated viewer if still set.
     */
    ~CropRotateDialog();

    /**
     * @brief Associates the dialog with an ImageViewer.
     *        Enables crop mode on the new viewer and disables it on the previous one.
     */
    void setViewer(class ImageViewer* v);

private slots:
    void onApply();
    void onBatchApply();
    void onRotationChanged();
    void onRatioChanged();

private:
    QDoubleSpinBox* m_angleSpin;
    QSlider*        m_angleSlider;
    QComboBox*      m_aspectCombo;

    QPointer<class ImageViewer> m_viewer;
};

#endif // CROPROTATEDIALOG_H