#include "HistogramWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include <algorithm>

// ===========================================================================
// Construction
// ===========================================================================

HistogramWidget::HistogramWidget(QWidget* parent) : QWidget(parent)
{
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
    setMinimumHeight(150);
}

// ===========================================================================
// Data Setters
// ===========================================================================

void HistogramWidget::setData(const std::vector<std::vector<int>>& bins, int channels)
{
    m_bins     = bins;
    m_channels = channels;
    m_cacheDirty = true;
    update();
}

void HistogramWidget::setGhostData(const std::vector<std::vector<int>>& bins, int channels)
{
    m_ghostBins     = bins;
    m_ghostChannels = channels;
    m_cacheDirty    = true;
    update();
}

void HistogramWidget::setLogScale(bool enabled)
{
    if (m_logScale == enabled) return;
    m_logScale   = enabled;
    m_cacheDirty = true;
    update();
}

void HistogramWidget::setZoom(float h, float v)
{
    m_zoomH      = h;
    m_zoomV      = v;
    m_cacheDirty = true;
    update();
}

void HistogramWidget::setShowGrid(bool show)
{
    m_showGrid   = show;
    m_cacheDirty = true;
    update();
}

void HistogramWidget::setShowCurve(bool show)
{
    m_showCurve  = show;
    m_cacheDirty = true;
    update();
}

void HistogramWidget::setTransformCurve(const std::vector<float>& lut)
{
    m_lut        = lut;
    m_cacheDirty = true;
    update();
}

void HistogramWidget::clear()
{
    m_bins.clear();
    m_ghostBins.clear();
    m_channels      = 0;
    m_ghostChannels = 0;
    m_lut.clear();
    m_cacheDirty    = true;
    update();
}

// ===========================================================================
// Static Utility: computeDisplayHistogram
// Aggregates raw histogram bins into display-width pixels, applies optional
// logarithmic scaling and a fast Gaussian blur (radius 2) for smoothing.
// ===========================================================================

void HistogramWidget::computeDisplayHistogram(
    const std::vector<std::vector<int>>& srcBins,
    int                                  srcChannels,
    int                                  displayWidth,
    std::vector<std::vector<float>>&     dst,
    double&                              maxVal,
    bool                                 logScale)
{
    dst.clear();
    maxVal = 0.0;

    if (srcBins.empty() || srcChannels <= 0 || displayWidth <= 0) return;

    const int numBins = static_cast<int>(srcBins[0].size());
    if (numBins == 0) return;

    dst.assign(srcChannels, std::vector<float>(displayWidth, 0.0f));

    const float binsPerPx = static_cast<float>(numBins) / static_cast<float>(displayWidth);

    for (int c = 0; c < srcChannels && c < static_cast<int>(srcBins.size()); ++c) {
        const int*   srcData = srcBins[c].data();
        float*       dstData = dst[c].data();

        int binIdx = 0;
        for (int px = 0; px < displayWidth; ++px) {
            double       sum           = 0.0;
            const float  maxBinForPx   = (px + 0.5f) * binsPerPx;
            while (binIdx < numBins && static_cast<float>(binIdx) <= maxBinForPx) {
                sum += static_cast<double>(srcData[binIdx]);
                ++binIdx;
            }
            if (logScale && sum > 0.0) sum = std::log(sum);
            dstData[px] = static_cast<float>(sum);
        }

        // Fast fixed-radius Gaussian blur (kernel: 1-4-6-4-1 / 16)
        std::vector<float> finalBlurred(displayWidth);
        float* blurredData = finalBlurred.data();

        for (int px = 0; px < displayWidth; ++px) {
            float sumBlur    = 0.0f;
            float weightSum  = 0.0f;

            for (int d = -2; d <= 2; ++d) {
                const int p = px + d;
                if (p >= 0 && p < displayWidth) {
                    float w;
                    switch (std::abs(d)) {
                        case 0:  w = 6.0f; break;
                        case 1:  w = 4.0f; break;
                        default: w = 1.0f; break;
                    }
                    sumBlur    += dstData[p] * w;
                    weightSum  += w;
                }
            }

            blurredData[px] = sumBlur / weightSum;
            if (blurredData[px] > maxVal) maxVal = static_cast<double>(blurredData[px]);
        }

        dst[c] = std::move(finalBlurred);
    }
}

// ===========================================================================
// Paint Event
// ===========================================================================

void HistogramWidget::paintEvent(QPaintEvent*)
{
    const int w = width();
    const int h = height();
    if (w <= 0 || h <= 0) return;

    // Determine whether the cached pixmap is still valid
    const bool needsRender = m_cacheDirty || w != m_cachedWidth || h != m_cachedHeight;

    if (needsRender) {
        m_cachedWidth  = w;
        m_cachedHeight = h;
        m_cacheDirty   = false;

        if (m_renderCache.size() != size())
            m_renderCache = QPixmap(size());

        QPainter painter(&m_renderCache);
        painter.setRenderHint(QPainter::Antialiasing);

        // -- Background --------------------------------------------------
        painter.fillRect(m_renderCache.rect(), QColor(20, 20, 20));

        // -- Grid lines --------------------------------------------------
        if (m_showGrid) {
            painter.setPen(QPen(QColor(60, 60, 60), 1));
            for (float x = 0.25f; x < 1.0f; x += 0.25f)
                painter.drawLine(static_cast<int>(x * w), 0, static_cast<int>(x * w), h);
            for (float y = 0.25f; y < 1.0f; y += 0.25f)
                painter.drawLine(0, static_cast<int>(y * h), w, static_cast<int>(y * h));

            painter.setPen(QPen(QColor(40, 40, 40), 1, Qt::DotLine));
            for (float x = 0.125f; x < 1.0f; x += 0.125f) {
                if (std::abs(std::fmod(x, 0.25f)) > 0.01f)
                    painter.drawLine(static_cast<int>(x * w), 0, static_cast<int>(x * w), h);
            }
        }

        // -- Recompute display histograms --------------------------------
        computeDisplayHistogram(m_ghostBins, m_ghostChannels, w,
                                m_cachedDisplayGhostBins, m_cachedGhostMaxVal, m_logScale);
        computeDisplayHistogram(m_bins, m_channels, w,
                                m_cachedDisplayBins, m_cachedMaxVal, m_logScale);

        // -- Ghost histogram (dotted, desaturated) -----------------------
        if (!m_cachedDisplayGhostBins.empty() && m_cachedGhostMaxVal > 0.0) {
            painter.setPen(QPen(QColor(100, 100, 100, 120), 1, Qt::DotLine));

            for (int c = 0; c < static_cast<int>(m_cachedDisplayGhostBins.size()); ++c) {
                QPainterPath path;
                bool first = true;
                const float* binData = m_cachedDisplayGhostBins[c].data();

                for (int px = 0; px < w; ++px) {
                    const float py = h - static_cast<float>(binData[px] / m_cachedGhostMaxVal) * h;
                    if (first) { path.moveTo(px, py); first = false; }
                    else         path.lineTo(px, py);
                }
                painter.drawPath(path);
            }
        }

        // -- Main histogram (filled + outlined) --------------------------
        if (!m_cachedDisplayBins.empty() && m_cachedMaxVal > 0.0) {
            QColor colors[3] = {
                QColor(255,  80,  80),
                QColor( 80, 255,  80),
                QColor( 80,  80, 255)
            };
            if (m_channels == 1) colors[0] = Qt::white;

            for (int c = 0; c < static_cast<int>(m_cachedDisplayBins.size()); ++c) {
                const float* binData = m_cachedDisplayBins[c].data();

                // Filled area
                QPainterPath fillPath;
                fillPath.moveTo(0, h);
                for (int px = 0; px < w; ++px) {
                    const float py = h - static_cast<float>(binData[px] / m_cachedMaxVal) * h;
                    fillPath.lineTo(px, py);
                }
                fillPath.lineTo(w, h);
                fillPath.closeSubpath();

                QColor col = colors[c % 3];
                col.setAlpha(60);
                painter.setBrush(col);
                painter.setPen(Qt::NoPen);
                painter.drawPath(fillPath);

                // Outline
                QPainterPath outline;
                bool first = true;
                for (int px = 0; px < w; ++px) {
                    const float py = h - static_cast<float>(binData[px] / m_cachedMaxVal) * h;
                    if (first) { outline.moveTo(px, py); first = false; }
                    else         outline.lineTo(px, py);
                }

                col.setAlpha(200);
                painter.setPen(QPen(col, 1.2));
                painter.setBrush(Qt::NoBrush);
                painter.drawPath(outline);
            }
        }

        // -- Transform curve overlay -------------------------------------
        if (m_showCurve && !m_lut.empty()) {
            // Reference diagonal (linear identity)
            painter.setPen(QPen(QColor(100, 100, 100), 1));
            painter.drawLine(0, h, w, 0);

            // Actual curve
            painter.setPen(QPen(QColor(255, 150, 100), 2));
            QPainterPath path;
            bool first          = true;
            const int   lutSize = static_cast<int>(m_lut.size());
            const float* lutData = m_lut.data();

            for (int i = 0; i < w; ++i) {
                const float t      = static_cast<float>(i) / static_cast<float>(w - 1);
                const int   lutIdx = std::clamp(static_cast<int>(t * (lutSize - 1)), 0, lutSize - 1);
                const float py     = h - std::clamp(lutData[lutIdx], 0.0f, 1.0f) * h;

                if (first) { path.moveTo(i, py); first = false; }
                else         path.lineTo(i, py);
            }
            painter.drawPath(path);
        }
    }

    // Blit the cached pixmap to the widget
    QPainter painter(this);
    painter.drawPixmap(0, 0, m_renderCache);
}