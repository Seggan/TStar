#ifndef SCNRDIALOG_H
#define SCNRDIALOG_H

#include "DialogBase.h"

#include <QPointer>
#include <QComboBox>
#include <QSlider>
#include <QDoubleSpinBox>

class ImageViewer;

/**
 * @brief Dialog for applying Subtractive Chromatic Noise Reduction (SCNR).
 *
 * SCNR removes excess green channel noise from an image using one of three
 * neutral protection methods: Average Neutral, Maximum Neutral, or Minimum Neutral.
 * The operation is triggered by the main window via the apply() signal.
 */
class SCNRDialog : public DialogBase
{
    Q_OBJECT

public:
    /**
     * @brief Defines the algorithm used to protect neutral (gray) tones during reduction.
     */
    enum ProtectionMethod
    {
        AverageNeutral,
        MaximumNeutral,
        MinimumNeutral
    };

    explicit SCNRDialog(QWidget* parent = nullptr);
    ~SCNRDialog();

    /**
     * @brief Returns the current reduction amount in the range [0.0, 1.0].
     */
    float getAmount() const;

    /**
     * @brief Returns the currently selected neutral protection method.
     */
    ProtectionMethod getMethod() const;

    /**
     * @brief Sets the viewer that will receive the SCNR operation.
     * @param v Target image viewer.
     */
    void setViewer(class ImageViewer* v);

signals:
    /**
     * @brief Emitted when the user clicks Apply. The main window connects to this
     *        signal to perform the actual image processing.
     */
    void apply();

private:
    QPointer<class ImageViewer> m_viewer;

    QComboBox*      m_methodCombo;
    QSlider*        m_amountSlider;
    QDoubleSpinBox* m_amountSpin;
};

#endif // SCNRDIALOG_H