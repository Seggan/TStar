#ifndef ARCSINHSTRETCHDIALOG_H
#define ARCSINHSTRETCHDIALOG_H

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QSlider>

#include "DialogBase.h"
#include "ImageBuffer.h"
#include "ImageViewer.h"

/**
 * @brief Dialog for applying an inverse hyperbolic sine (arcsinh) stretch.
 *
 * Provides interactive controls for the stretch factor and black point with
 * real-time preview updating. Clipping statistics (percentage of pixels pushed
 * below zero or above one) are displayed live during slider drag without
 * triggering a full preview render to maintain responsiveness.
 *
 * The original image is restored automatically if the dialog is cancelled
 * or closed without applying.
 */
class ArcsinhStretchDialog : public DialogBase
{
    Q_OBJECT

public:
    explicit ArcsinhStretchDialog(ImageViewer* viewer, QWidget* parent = nullptr);
    ~ArcsinhStretchDialog();

    /**
     * @brief Switches the dialog to operate on @p v.
     *
     * If a preview was active on the previous viewer, the original image is
     * restored before the switch takes place.
     */
    void setViewer(ImageViewer* v);

protected:
    /** Restores the original image when the dialog is cancelled or closed. */
    void reject() override;

private slots:
    void onStretchChanged(int value);
    void onBlackPointChanged(double value);
    void onHumanLuminanceToggled(bool checked);
    void onPreviewToggled(bool checked);
    void onReset();
    void onApply();

private:
    void setupUI();
    void updatePreview();
    void updateClippingStats(const ImageBuffer& buffer);

    /**
     * @brief Computes and displays clipping statistics without updating
     *        the viewer preview. Called during slider drag to avoid lag.
     */
    void updateClippingStatsOnly();

    // --- Viewer and image state ------------------------------------------
    QPointer<ImageViewer> m_viewer;
    ImageBuffer           m_originalBuffer;
    bool                  m_applied = false;

    // --- UI controls -----------------------------------------------------
    QSlider*        m_stretchSlider;
    QDoubleSpinBox* m_stretchSpin;
    QSlider*        m_blackPointSlider;
    QDoubleSpinBox* m_blackPointSpin;
    QCheckBox*      m_humanLuminanceCheck;
    QCheckBox*      m_previewCheck;
    QLabel*         m_lowClipLabel;
    QLabel*         m_highClipLabel;

    // --- Processing parameters -------------------------------------------
    float m_stretch        = 0.0f;
    float m_blackPoint     = 0.0f;
    bool  m_humanLuminance = true;
};

#endif // ARCSINHSTRETCHDIALOG_H