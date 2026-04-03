#ifndef ASTROSPIKEDIALOG_H
#define ASTROSPIKEDIALOG_H

// =============================================================================
// AstroSpikeDialog.h
//
// Declares the AstroSpike diffraction-spike generator dialog, including:
//   - AstroSpike namespace (data structures and configuration)
//   - AstroSpikeCanvas (interactive preview widget)
//   - StarDetectionThread (background star detection via OpenCV)
//   - AstroSpikeDialog (main dialog controller)
// =============================================================================

#include <QDialog>
#include <QWidget>
#include <QImage>
#include <QThread>
#include <QBasicTimer>
#include <QVector>
#include <QColor>
#include <QPointF>
#include <QMutex>
#include <QPointer>

#include "../ImageBuffer.h"
#include "DialogBase.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

// Forward declarations
class QLabel;
class QSlider;
class QCheckBox;
class QDoubleSpinBox;
class QScrollArea;
class QVBoxLayout;
class QPushButton;
class ImageViewer;

// =============================================================================
// Namespace: AstroSpike
// Contains all shared data structures for the spike generator system.
// =============================================================================
namespace AstroSpike {

    /**
     * Represents a single detected star with its spatial, photometric,
     * and colorimetric properties.
     */
    struct Star {
        float  x          = 0;      // Centroid X position (pixels)
        float  y          = 0;      // Centroid Y position (pixels)
        float  brightness = 0;      // Normalized brightness [0..1]
        float  radius     = 0;      // FWHM-based radius (pixels)
        QColor color;               // Sampled RGB color of the star core
    };

    /**
     * Enumerates the interactive tool modes available on the canvas.
     */
    enum class ToolMode {
        None,   // Pan/zoom navigation mode
        Add,    // Add a synthetic star at click position
        Erase   // Remove stars within the eraser radius
    };

    /**
     * Complete configuration for the spike rendering pipeline.
     * Default values produce a standard 4-point diffraction pattern.
     */
    struct Config {
        // -- Detection parameters --
        float threshold    = 80.0f;     // Brightness threshold slider [1..100]
        float starAmount   = 100.0f;    // Percentage of detected stars to render
        float minStarSize  = 0.0f;      // Minimum star radius filter (pixels)
        float maxStarSize  = 100.0f;    // Maximum star radius filter (pixels)

        // -- Primary spike geometry --
        float quantity     = 4.0f;      // Number of spike rays (points)
        float length       = 300.0f;    // Spike length scaling factor
        float globalScale  = 1.0f;      // Global size multiplier
        float angle        = 45.0f;     // Base rotation angle (degrees)
        float intensity    = 1.0f;      // Primary spike brightness [0..1]
        float spikeWidth   = 1.0f;      // Spike thickness multiplier

        // -- Color appearance --
        float colorSaturation = 1.0f;   // Color saturation boost factor
        float hueShift        = 0.0f;   // Hue rotation offset (degrees)

        // -- Secondary spikes --
        float secondaryIntensity = 0.3f;  // Secondary spike brightness
        float secondaryLength    = 120.0f;// Secondary spike length
        float secondaryOffset    = 45.0f; // Angular offset from primary (degrees)

        // -- Soft flare (glow) --
        float softFlareIntensity = 0.2f;  // Glow layer opacity
        float softFlareSize      = 15.0f; // Glow radius multiplier

        // -- Halo ring --
        bool  enableHalo      = false;
        float haloIntensity   = 0.5f;
        float haloScale       = 5.0f;
        float haloWidth       = 1.0f;
        float haloBlur        = 0.5f;
        float haloSaturation  = 1.0f;

        // -- Rainbow chromatic effect --
        bool   enableRainbow     = false;
        bool   rainbowSpikes     = true;
        float  rainbowIntensity  = 0.8f;
        float  rainbowFrequency  = 1.0f;
        double rainbowLength     = 0.8f;
    };

} // namespace AstroSpike

// =============================================================================
// Class: AstroSpikeCanvas
// Interactive widget for previewing and editing spike overlays on the image.
// Supports pan, zoom, star addition/erasure, and cached spike rendering.
// =============================================================================
class AstroSpikeCanvas : public QWidget {
    Q_OBJECT

public:
    explicit AstroSpikeCanvas(QWidget* parent = nullptr);

    // --- Data setters ---
    void setImage(const QImage& img);
    void setStars(const QVector<AstroSpike::Star>& stars);
    void setConfig(const AstroSpike::Config& config);
    void setToolMode(AstroSpike::ToolMode mode);

    // --- Brush/eraser size controls ---
    void setStarInputRadius(float r) { m_brushRadius = r; update(); }
    void setEraserInputSize(float s) { m_eraserSize = s; update(); }

    // --- Accessors ---
    const QVector<AstroSpike::Star>& getStars() const { return m_stars; }

    // --- View navigation ---
    void zoomIn();
    void zoomOut();
    void zoomToPoint(QPointF widgetPos, float factor);
    void fitToView();

    /**
     * Renders all spike effects into the given painter.
     * Used both for on-screen preview and final full-resolution compositing.
     *
     * @param p      Target QPainter (must be active).
     * @param scale  Coordinate scale factor (1.0 for full resolution).
     * @param offset Translation offset applied to star positions.
     */
    void render(QPainter& p, float scale, const QPointF& offset);

signals:
    /** Emitted when the star list is modified by an interactive tool. */
    void starsUpdated(const QVector<AstroSpike::Star>& stars);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    // --- Rendering helpers ---
    void   drawPreview(QPainter& p, float scale);
    QColor getStarColor(const AstroSpike::Star& star, float hueShift,
                        float sat, float alpha);
    void   createGlowSprite();
    void   handleTool(const QPointF& imgPos);
    void   updateSpikePreview();

    // --- State ---
    QImage                       m_image;            // Source display image (8-bit)
    QVector<AstroSpike::Star>    m_stars;            // Current star list
    AstroSpike::Config           m_config;           // Active rendering configuration
    AstroSpike::ToolMode         m_toolMode = AstroSpike::ToolMode::None;

    // --- View transform ---
    float   m_zoom        = 1.0f;
    QPointF m_panOffset;
    QPoint  m_lastMousePos;
    bool    m_dragging    = false;
    bool    m_firstResize = true;

    // --- Tool parameters ---
    float m_brushRadius = 4.0f;
    float m_eraserSize  = 20.0f;

    // --- Cached rendering assets ---
    QImage m_glowSprite;        // Pre-computed radial glow texture
    QImage m_spikePreview;      // Cached spike overlay at preview resolution
};

// =============================================================================
// Class: StarDetectionThread
// Background thread that detects all stars in an image using adaptive
// thresholding and local-maximum peak finding (DAOFIND-style algorithm).
// Emits the complete star list sorted by brightness (descending).
// =============================================================================
class StarDetectionThread : public QThread {
    Q_OBJECT

public:
    explicit StarDetectionThread(const ImageBuffer& buffer,
                                 QObject* parent = nullptr);
    void run() override;

signals:
    /** Emitted when detection completes with the full star catalog. */
    void detectionComplete(const QVector<AstroSpike::Star>& stars);

private:
    int     m_width  = 0;
    int     m_height = 0;
    cv::Mat m_grayMat;  // 8-bit grayscale for peak detection
    cv::Mat m_rgbMat;   // 8-bit BGR for color sampling
};

// =============================================================================
// Class: AstroSpikeDialog
// Main dialog window for the AstroSpike diffraction-spike generator.
// Manages the detection pipeline, interactive canvas, parameter controls,
// undo/redo history, and final image compositing.
// =============================================================================
class AstroSpikeDialog : public DialogBase {
    Q_OBJECT

public:
    explicit AstroSpikeDialog(ImageViewer* viewer, QWidget* parent = nullptr);
    ~AstroSpikeDialog();

    /** Switch the active viewer (triggers re-detection). */
    void setViewer(ImageViewer* v);

protected:
    void closeEvent(QCloseEvent* event) override;
    void timerEvent(QTimerEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void runDetection();
    void onStarsDetected(const QVector<AstroSpike::Star>& stars);
    void onCanvasStarsUpdated(const QVector<AstroSpike::Star>& stars);
    void filterStarsByThreshold();

    // Tool management
    void setToolMode(AstroSpike::ToolMode mode);
    void onConfigChanged();
    void resetConfig();

    // Document actions
    void applyToDocument();
    void saveImage();

private:
    // --- UI construction ---
    void     setupUI();
    void     setupControls(QVBoxLayout* layout);
    QWidget* createSlider(const QString& label, float min, float max,
                          float step, float initial, float* target,
                          const QString& unit = "");

    // --- Core references ---
    ImageViewer*        m_viewer;
    AstroSpike::Config  m_config;

    // --- Child widgets ---
    AstroSpikeCanvas*    m_canvas;
    StarDetectionThread* m_thread = nullptr;
    QBasicTimer          m_detectTimer;

    // --- Star data ---
    QVector<AstroSpike::Star> m_allStars;  // Full detection result (pre-filter)

    // --- UI controls ---
    QScrollArea* m_controlsScroll;
    QLabel*      m_statusLabel;

    // --- Undo/redo history ---
    QVector<QVector<AstroSpike::Star>> m_history;
    int          m_historyIndex = -1;
    QPushButton* m_btnUndo;
    QPushButton* m_btnRedo;

    void pushHistory(const QVector<AstroSpike::Star>& stars);
    void undo();
    void redo();
    void updateHistoryButtons();
};

#endif // ASTROSPIKEDIALOG_H