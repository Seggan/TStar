#ifndef HISTOGRAMWIDGET_H
#define HISTOGRAMWIDGET_H

#include <QWidget>
#include <QPixmap>
#include <vector>

// ---------------------------------------------------------------------------
// HistogramWidget
// Renders one or more channel histograms using QPainter with optional:
//   - Ghost histogram overlay (e.g. pre-stretch reference)
//   - Logarithmic Y-axis scaling
//   - Grid lines
//   - Transform curve overlay (stretch/LUT preview)
//
// Rendering is cached to a QPixmap and only recomputed when the data or
// widget size changes, minimising CPU usage during window drag operations.
// ---------------------------------------------------------------------------
class HistogramWidget : public QWidget {
    Q_OBJECT

public:
    explicit HistogramWidget(QWidget* parent = nullptr);

    // -- Data setters (each triggers a cache invalidation and repaint) ----
    void setData(const std::vector<std::vector<int>>& bins, int channels);
    void setGhostData(const std::vector<std::vector<int>>& bins, int channels);
    void setLogScale(bool enabled);
    void setZoom(float h, float v);
    void setShowGrid(bool show);
    void setShowCurve(bool show);
    void setTransformCurve(const std::vector<float>& lut);
    void clear();

    // Shared utility: converts raw histogram bins to display-width float values
    // with optional Gaussian smoothing and log scaling.  Used by GHS, Stretch,
    // and Curves tool widgets as well as this widget.
    static void computeDisplayHistogram(
        const std::vector<std::vector<int>>& srcBins,
        int                                  srcChannels,
        int                                  displayWidth,
        std::vector<std::vector<float>>&     dst,
        double&                              maxVal,
        bool                                 logScale
    );

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    // -- Input data -------------------------------------------------------
    std::vector<std::vector<int>> m_bins;
    std::vector<std::vector<int>> m_ghostBins;

    int   m_channels      = 0;
    int   m_ghostChannels = 0;
    bool  m_logScale      = false;
    float m_zoomH         = 1.0f;
    float m_zoomV         = 1.0f;
    bool  m_showGrid      = true;
    bool  m_showCurve     = true;

    // Transform curve data for the overlay line (values in [0, 1])
    std::vector<float> m_lut;

    // -- Render cache -----------------------------------------------------
    std::vector<std::vector<float>> m_cachedDisplayBins;
    std::vector<std::vector<float>> m_cachedDisplayGhostBins;
    double m_cachedMaxVal      = 0.0;
    double m_cachedGhostMaxVal = 0.0;
    int    m_cachedWidth       = -1;    // Invalidated when the widget is resized
    int    m_cachedHeight      = -1;

    QPixmap m_renderCache;
    bool    m_cacheDirty = true;
};

#endif // HISTOGRAMWIDGET_H