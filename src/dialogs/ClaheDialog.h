#ifndef CLAHEDIALOG_H
#define CLAHEDIALOG_H

// =============================================================================
// ClaheDialog.h
//
// Dialog for CLAHE (Contrast Limited Adaptive Histogram Equalization).
// Provides real-time preview with adjustable clip limit, tile grid size,
// and blending opacity. Supports both color (Lab space) and grayscale images.
// =============================================================================

#include "DialogBase.h"

#include <QSlider>
#include <QCheckBox>
#include <QTimer>
#include <QImage>

#include "../ImageBuffer.h"

class MainWindowCallbacks;
class QLabel;
class QGraphicsView;
class QGraphicsScene;
class QGraphicsPixmapItem;
class QPushButton;

class ClaheDialog : public DialogBase {
    Q_OBJECT

public:
    explicit ClaheDialog(QWidget* parent = nullptr);
    ~ClaheDialog();

    /** Set the source image for processing. */
    void setSource(const ImageBuffer& img);

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private slots:
    void onApply();
    void onReset();
    void updatePreview();

private:
    void setupUi();

    /**
     * Applies CLAHE to the source buffer and stores the result
     * in m_previewImage.
     *
     * @param src           Source image buffer.
     * @param clipLimit     CLAHE clip limit (contrast ceiling).
     * @param tileGridSize  Number of tiles per dimension.
     */
    void createPreview(const ImageBuffer& src, float clipLimit,
                       int tileGridSize);

    // --- Core state ---
    MainWindowCallbacks* m_mainWindow;
    ImageBuffer          m_sourceImage;
    ImageBuffer          m_previewImage;

    // --- Slider controls ---
    QSlider* m_clipSlider;
    QLabel*  m_clipLabel;
    QSlider* m_tileSlider;
    QLabel*  m_tileLabel;
    QSlider* m_opacitySlider  = nullptr;
    QLabel*  m_opacityLabel   = nullptr;

    // --- Preview controls ---
    QCheckBox* m_chkPreview;
    QTimer*    m_previewTimer;

    // --- Graphics view ---
    QGraphicsView*       m_view;
    QGraphicsScene*      m_scene;
    QGraphicsPixmapItem* m_pixmapItem;
    float                m_zoom         = 1.0f;
    bool                 m_firstDisplay = true;
    bool                 m_previewDirty;
};

#endif // CLAHEDIALOG_H