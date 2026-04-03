#ifndef STARHALOREMOVALDIALOG_H
#define STARHALOREMOVALDIALOG_H

/**
 * @file StarHaloRemovalDialog.h
 * @brief Dialog for interactive star halo removal with live preview.
 *
 * Applies a multi-step halo reduction algorithm using unsharp masking
 * and gamma correction, with configurable reduction intensity.
 * Supports live preview, zoom controls, and mask-aware blending.
 */

#include "DialogBase.h"
#include "../ImageBuffer.h"
#include <QImage>

class MainWindowCallbacks;
class QLabel;
class QCheckBox;
class QComboBox;
class QGraphicsView;
class QGraphicsScene;
class QGraphicsPixmapItem;
class QSlider;
class QTimer;

/**
 * @class StarHaloRemovalDialog
 * @brief Interactive dialog for reducing star halos in astronomical images.
 */
class StarHaloRemovalDialog : public DialogBase {
    Q_OBJECT

public:
    explicit StarHaloRemovalDialog(QWidget* parent = nullptr);
    ~StarHaloRemovalDialog();

    /**
     * @brief Set the source image for processing.
     * @param img Source ImageBuffer to process.
     */
    void setSource(const ImageBuffer& img);

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private slots:
    void onApply();
    void onReset();
    void updatePreview();
    void zoomIn();
    void zoomOut();
    void zoomFit();

private:
    void setupUi();

    /**
     * @brief Apply the halo removal algorithm to a source buffer.
     * @param src            Source image buffer.
     * @param reductionLevel Intensity level (0=Extra Low, 3=High).
     * @param isLinear       Whether the input data is in linear space.
     * @param dst            Output buffer receiving the processed result.
     */
    void applyHaloRemoval(const ImageBuffer& src, int reductionLevel,
                          bool isLinear, ImageBuffer& dst) const;

    /**
     * @brief Update the reduction level label text.
     */
    void updateReductionLabel(int level);

    /* Application state */
    MainWindowCallbacks* m_mainWindow = nullptr;
    ImageBuffer m_sourceImage;
    ImageBuffer m_previewImage;

    /* UI controls */
    QSlider*   m_reductionSlider  = nullptr;
    QLabel*    m_reductionLabel   = nullptr;
    QCheckBox* m_linearCheck      = nullptr;
    QComboBox* m_applyTargetCombo = nullptr;
    QCheckBox* m_previewCheck     = nullptr;
    QTimer*    m_previewTimer     = nullptr;

    /* Preview viewport */
    QGraphicsView*       m_view       = nullptr;
    QGraphicsScene*      m_scene      = nullptr;
    QGraphicsPixmapItem* m_pixmapItem = nullptr;

    float m_zoom         = 1.0f;
    bool  m_firstDisplay = true;
};

#endif // STARHALOREMOVALDIALOG_H