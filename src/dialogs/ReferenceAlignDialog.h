#ifndef REFERENCEALIGNDIALOG_H
#define REFERENCEALIGNDIALOG_H

#include "DialogBase.h"
#include "../ImageBuffer.h"
#include <QImage>
#include <QPixmap>

class QLabel;
class QPushButton;
class QSlider;
class QDoubleSpinBox;
class QCheckBox;

class ReferenceAlignDialog : public DialogBase {
    Q_OBJECT
public:
    explicit ReferenceAlignDialog(QWidget* parent, const ImageBuffer& refBuffer, const ImageBuffer& targetBuffer, double paddingFactor);
    ~ReferenceAlignDialog();

    // Retrieves the final transformed buffer
    ImageBuffer getAlignedBuffer() const;

private slots:
    void onFlipHorizontal();
    void onFlipVertical();
    void onRotateCW();
    void onRotateCCW();
    void onRotationChanged(double value);
    void onOverlayToggled(bool checked);
    void onOverlayStretchToggled(bool checked);
    void onOpacityChanged(int value);
    void updatePreview();

private:
    void rebuildBuffer();
    QImage bufferToQImageScaled(const ImageBuffer& buf, bool autoStretch = true);

    ImageBuffer m_originalRefBuffer;
    ImageBuffer m_targetBuffer;
    ImageBuffer m_currentBuffer;
    double m_paddingFactor;

    bool m_flipH = false;
    bool m_flipV = false;
    double m_rotationAngle = 0.0;

    bool m_showOverlay = false;
    bool m_overlayAutoStretch = true;
    int m_overlayOpacity = 50;

    QLabel* m_previewLabel;
    QSlider* m_sliderRotation;
    QDoubleSpinBox* m_spinRotation;
    QCheckBox* m_chkOverlayStretch;
    QSlider* m_sliderOpacity;
};

#endif // REFERENCEALIGNDIALOG_H
