#ifndef MORPHOLOGYDIALOG_H
#define MORPHOLOGYDIALOG_H

#include "DialogBase.h"
#include "../ImageBuffer.h"

class QComboBox;
class QSpinBox;
class QSlider;
class QLabel;
class QCheckBox;
class QGraphicsScene;
class QGraphicsView;
class QGraphicsPixmapItem;
class QTimer;

// =============================================================================
// MorphologyDialog
//
// Provides an interactive UI for applying morphological operations (erosion,
// dilation, opening, closing) to the currently active image. Supports
// live preview with debounced updates and optional mask-blended output.
// =============================================================================
class MorphologyDialog : public DialogBase
{
    Q_OBJECT

public:
    explicit MorphologyDialog(QWidget* parent = nullptr);
    ~MorphologyDialog();

    void setSource(const ImageBuffer& img);

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private slots:
    void updatePreview();
    void onApply();
    void onReset();
    void zoomIn();
    void zoomOut();
    void zoomFit();

private:
    void setupUi();

    // Apply the selected morphological operation to src and write the result to dst.
    void applyMorphology(const ImageBuffer& src,
                         int opIndex,
                         int kernelSize,
                         int iterations,
                         ImageBuffer& dst) const;

    class MainWindowCallbacks* m_mainWindow = nullptr;

    ImageBuffer m_sourceImage;
    ImageBuffer m_previewImage;

    // --- UI elements ---
    QComboBox*        m_cbOp           = nullptr;
    QSlider*          m_sliderKernel   = nullptr;
    QSpinBox*         m_spinKernel     = nullptr;
    QSlider*          m_sliderIter     = nullptr;
    QSpinBox*         m_spinIter       = nullptr;
    QCheckBox*        m_previewCheck   = nullptr;
    QComboBox*        m_applyTargetCombo = nullptr;

    QGraphicsScene*      m_scene      = nullptr;
    QGraphicsView*       m_view       = nullptr;
    QGraphicsPixmapItem* m_pixmapItem = nullptr;
    QTimer*              m_previewTimer = nullptr;

    float m_zoom         = 1.0f;
    bool  m_firstDisplay = true;
};

#endif // MORPHOLOGYDIALOG_H