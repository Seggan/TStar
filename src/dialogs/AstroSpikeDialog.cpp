// =============================================================================
// AstroSpikeDialog.cpp
//
// Implementation of the AstroSpike diffraction-spike generator, including:
//   - Star detection via adaptive local-maximum peak finding
//   - Interactive canvas with pan/zoom and star editing tools
//   - Full spike rendering pipeline (primary, secondary, halo, rainbow, flare)
//   - Final compositing and document integration
// =============================================================================

#include "AstroSpikeDialog.h"
#include "MainWindowCallbacks.h"
#include "../ImageViewer.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QLabel>
#include <QSlider>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QPainter>
#include <QMouseEvent>
#include <QMenu>
#include <QFileDialog>
#include <QGroupBox>
#include <QtMath>
#include <QDebug>
#include <QTimer>
#include <QSettings>

#include <algorithm>

#ifdef _OPENMP
#include <omp.h>
#endif


// =============================================================================
// StarDetectionThread -- Construction
// =============================================================================

StarDetectionThread::StarDetectionThread(const ImageBuffer& buffer,
                                         QObject* parent)
    : QThread(parent)
{
    // Acquire a thread-safe copy of the display image
    QImage img = buffer.getDisplayImage(ImageBuffer::Display_Linear);
    if (img.isNull())
        return;

    m_width  = img.width();
    m_height = img.height();

    // Allocate 8-bit BGR and grayscale matrices
    m_rgbMat  = cv::Mat(m_height, m_width, CV_8UC3);
    m_grayMat = cv::Mat(m_height, m_width, CV_8UC1);

    const int bytesPerPixel = img.depth() / 8;

    for (int y = 0; y < m_height; ++y) {
        const uint8_t* line    = img.constScanLine(y);
        uint8_t*       bgrRow  = m_rgbMat.ptr<uint8_t>(y);
        uint8_t*       grayRow = m_grayMat.ptr<uint8_t>(y);

        for (int x = 0; x < m_width; ++x) {
            uint8_t r = 0, g = 0, b = 0;

            if (bytesPerPixel == 4) {
                // ARGB32 format: BGRA byte order in memory
                b = line[x * 4];
                g = line[x * 4 + 1];
                r = line[x * 4 + 2];
            } else if (bytesPerPixel == 3) {
                // RGB888 format
                r = line[x * 3];
                g = line[x * 3 + 1];
                b = line[x * 3 + 2];
            }

            bgrRow[x * 3 + 0] = b;
            bgrRow[x * 3 + 1] = g;
            bgrRow[x * 3 + 2] = r;

            // ITU-R BT.709 luminance coefficients
            grayRow[x] = static_cast<uint8_t>(
                0.2126f * r + 0.7152f * g + 0.0722f * b);
        }
    }
}


// =============================================================================
// StarDetectionThread -- Detection Algorithm
// =============================================================================

void StarDetectionThread::run()
{
    if (m_grayMat.empty())
        return;

    const int nx = m_width;
    const int ny = m_height;

    // -------------------------------------------------------------------------
    // Step 1: Pre-processing -- Gaussian blur to suppress noise
    // -------------------------------------------------------------------------
    cv::Mat blurred;
    cv::GaussianBlur(m_grayMat, blurred, cv::Size(0, 0), 1.5);

    // -------------------------------------------------------------------------
    // Step 2: Compute background statistics from the blurred image
    // -------------------------------------------------------------------------
    int hist[256] = {};
    for (int y = 0; y < ny; ++y) {
        const uint8_t* row = blurred.ptr<uint8_t>(y);
        for (int x = 0; x < nx; ++x)
            ++hist[static_cast<int>(row[x])];
    }

    const int totalPix = nx * ny;

    // Determine the median background level
    int cumulative = 0, bgInt = 0;
    for (int v = 0; v < 256; ++v) {
        cumulative += hist[v];
        if (cumulative * 2 >= totalPix) {
            bgInt = v;
            break;
        }
    }
    const double bg = static_cast<double>(bgInt);

    // Estimate background noise from the lower half of the histogram
    double sumN = 0.0, sumSqN = 0.0;
    int    countN = 0;
    const int noiseHalfRange = std::min(bgInt, 30);

    for (int v = bgInt - noiseHalfRange; v <= bgInt; ++v) {
        sumN   += static_cast<double>(v) * hist[v];
        sumSqN += static_cast<double>(v * v) * hist[v];
        countN += hist[v];
    }

    const double meanN   = (countN > 0) ? sumN / countN : bg;
    const double varN    = (countN > 1) ? (sumSqN / countN - meanN * meanN) : 1.0;
    const double bgNoise = std::sqrt(std::max(varN, 1.0));

    // Adaptive threshold: 5-sigma above background, clamped to [bg+3, 250]
    const float threshold = static_cast<float>(
        std::min(std::max(bg + 5.0 * bgNoise, bg + 3.0), 250.0));

    qDebug() << "[AstroSpike] bg=" << bg
             << " noise=" << bgNoise
             << " adaptive threshold=" << threshold;

    // -------------------------------------------------------------------------
    // Step 3: Local-maximum peak scan (DAOFIND-style)
    //
    // For each pixel above threshold, verify it is the strict local maximum
    // in a 7x7 neighbourhood. This approach avoids contour-based detection
    // issues: each star produces exactly one candidate at its true peak,
    // and the measured radius derives from the actual PSF profile (FWHM).
    // -------------------------------------------------------------------------
    const int localMaxRadius = 3;
    QVector<AstroSpike::Star> allDetected;

    for (int y = localMaxRadius; y < ny - localMaxRadius; ++y) {
        const uint8_t* row = blurred.ptr<uint8_t>(y);

        for (int x = localMaxRadius; x < nx - localMaxRadius; ++x) {
            const float pixel = static_cast<float>(row[x]);
            if (pixel <= threshold)
                continue;

            // Verify strict local maximum with tie-breaking by index
            bool isMax = true;
            for (int dy = -localMaxRadius; dy <= localMaxRadius && isMax; ++dy) {
                const uint8_t* nrow = blurred.ptr<uint8_t>(y + dy);
                for (int dx = -localMaxRadius; dx <= localMaxRadius && isMax; ++dx) {
                    if (dx == 0 && dy == 0)
                        continue;

                    const uint8_t neighbor = nrow[x + dx];
                    if (neighbor > pixel) {
                        isMax = false;
                    } else if (neighbor == pixel &&
                               (dx < 0 || (dx == 0 && dy < 0))) {
                        isMax = false;  // Tie: lower-index pixel wins
                    }
                }
            }

            if (!isMax)
                continue;

            // Record peak position and skip ahead to avoid redundant checks
            const int peakX = x;
            const int peakY = y;
            x += localMaxRadius;

            // -----------------------------------------------------------------
            // Step 3a: Saturated plateau correction
            //
            // Large saturated stars have flat plateaus where the local-max
            // tie-break selects the top-left corner. Detect and re-center
            // on the plateau midpoint.
            // -----------------------------------------------------------------
            float minNeighbor8 = 255.f;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    float nv = static_cast<float>(
                        blurred.ptr<uint8_t>(peakY + dy)[peakX + dx]);
                    if (nv < minNeighbor8)
                        minNeighbor8 = nv;
                }
            }

            const bool hasSaturated =
                (pixel >= 240.f) && ((pixel - minNeighbor8) <= 10.f);

            int pCx = peakX, pCy = peakY;
            int plateauExtR = 0, plateauExtD = 0;

            if (hasSaturated) {
                const float satFloor = pixel - 10.f;

                // Scan right to find plateau extent
                while (pCx + plateauExtR + 1 < nx &&
                       static_cast<float>(blurred.ptr<uint8_t>(pCy)[pCx + plateauExtR + 1]) >= satFloor)
                    ++plateauExtR;

                // Scan down to find plateau extent
                while (pCy + plateauExtD + 1 < ny &&
                       static_cast<float>(blurred.ptr<uint8_t>(pCy + plateauExtD + 1)[pCx]) >= satFloor)
                    ++plateauExtD;

                // Re-center on the plateau midpoint
                pCx = std::min(peakX + plateauExtR / 2, nx - 1);
                pCy = std::min(peakY + plateauExtD / 2, ny - 1);

                // Skip past the full plateau width
                if (plateauExtR > localMaxRadius)
                    x += (plateauExtR - localMaxRadius);
            }

            // -----------------------------------------------------------------
            // Step 3b: Sub-pixel centroid refinement (squared-intensity weighted)
            // -----------------------------------------------------------------
            const int refineR = 3;
            const int py0 = std::max(0,      pCy - refineR);
            const int py1 = std::min(ny - 1, pCy + refineR);
            const int px0 = std::max(0,      pCx - refineR);
            const int px1 = std::min(nx - 1, pCx + refineR);

            float refinedCx = 0.f, refinedCy = 0.f, totalW = 0.f;
            float peakVal = 0.f;

            for (int py = py0; py <= py1; ++py) {
                const uint8_t* gRow = m_grayMat.ptr<uint8_t>(py);
                for (int px = px0; px <= px1; ++px) {
                    const float val = static_cast<float>(gRow[px]);
                    const float w   = val * val;  // Squared weight for tight centroid
                    refinedCx += px * w;
                    refinedCy += py * w;
                    totalW    += w;
                    if (val > peakVal)
                        peakVal = val;
                }
            }

            const float cx = (totalW > 0.f) ? refinedCx / totalW : static_cast<float>(pCx);
            const float cy = (totalW > 0.f) ? refinedCy / totalW : static_cast<float>(pCy);

            const int icx = std::max(0, std::min(static_cast<int>(std::round(cx)), nx - 1));
            const int icy = std::max(0, std::min(static_cast<int>(std::round(cy)), ny - 1));

            // -----------------------------------------------------------------
            // Step 3c: Local background estimate via compass probes
            //
            // Sample 8 directions around the star at a distance beyond the
            // plateau (if any), then take the median as the local background.
            // -----------------------------------------------------------------
            const int probeR = std::max(10,
                std::max(plateauExtR, plateauExtD) + 8);

            float probes[8];
            const int pdx[] = { probeR,  probeR, 0, -probeR, -probeR, -probeR,  0,  probeR };
            const int pdy[] = {      0,  probeR, probeR, probeR,  0, -probeR, -probeR, -probeR };

            for (int p = 0; p < 8; ++p) {
                const int spx = std::max(0, std::min(icx + pdx[p], nx - 1));
                const int spy = std::max(0, std::min(icy + pdy[p], ny - 1));
                probes[p] = static_cast<float>(blurred.ptr<uint8_t>(spy)[spx]);
            }

            std::sort(probes, probes + 8);
            const float localBg = (probes[3] + probes[4]) * 0.5f;

            // -----------------------------------------------------------------
            // Step 3d: FWHM-based radius measurement
            //
            // Scan outward along 4 cardinal axes until the profile drops to
            // half of the peak amplitude above local background.
            // -----------------------------------------------------------------
            const float halfMax = localBg + (peakVal - localBg) * 0.5f;
            const int   maxScan = 40;

            auto scanHalfWidth = [&](int dx, int dy) -> float {
                for (int step = 1; step <= maxScan; ++step) {
                    const int spx = icx + dx * step;
                    const int spy = icy + dy * step;
                    if (spx < 0 || spx >= nx || spy < 0 || spy >= ny)
                        return static_cast<float>(step);

                    const float val = static_cast<float>(
                        m_grayMat.ptr<uint8_t>(spy)[spx]);
                    if (val < halfMax) {
                        // Sub-pixel interpolation
                        const float prevVal = static_cast<float>(
                            m_grayMat.ptr<uint8_t>(spy - dy)[spx - dx]);
                        const float denom = prevVal - val;
                        return static_cast<float>(step - 1) +
                               (denom > 0.f ? (prevVal - halfMax) / denom : 0.5f);
                    }
                }
                return static_cast<float>(maxScan);
            };

            const float hwR = scanHalfWidth( 1,  0);
            const float hwL = scanHalfWidth(-1,  0);
            const float hwD = scanHalfWidth( 0,  1);
            const float hwU = scanHalfWidth( 0, -1);

            float fwhmRadius = (hwR + hwL + hwD + hwU) * 0.25f;
            fwhmRadius = std::max(fwhmRadius, 0.5f);

            // -----------------------------------------------------------------
            // Step 3e: Color sampling from the bright core
            // -----------------------------------------------------------------
            const int colorR = std::max(1, static_cast<int>(std::ceil(fwhmRadius)));

            float sumR = 0.f, sumG = 0.f, sumB = 0.f, sumWeight = 0.f;
            const int cy0 = std::max(0,      icy - colorR);
            const int cy1 = std::min(ny - 1, icy + colorR);
            const int cx0 = std::max(0,      icx - colorR);
            const int cx1 = std::min(nx - 1, icx + colorR);

            for (int py = cy0; py <= cy1; ++py) {
                const uint8_t* gRow   = m_grayMat.ptr<uint8_t>(py);
                const uint8_t* bgrRow = m_rgbMat.ptr<uint8_t>(py);
                for (int px = cx0; px <= cx1; ++px) {
                    const float lum = static_cast<float>(gRow[px]);
                    if (lum < peakVal * 0.5f)
                        continue;  // Only sample bright core pixels

                    const float w = lum * lum;
                    sumB      += bgrRow[px * 3 + 0] * w;
                    sumG      += bgrRow[px * 3 + 1] * w;
                    sumR      += bgrRow[px * 3 + 2] * w;
                    sumWeight += w;
                }
            }

            const float avgR = (sumWeight > 0.f) ? sumR / sumWeight : 255.f;
            const float avgG = (sumWeight > 0.f) ? sumG / sumWeight : 255.f;
            const float avgB = (sumWeight > 0.f) ? sumB / sumWeight : 255.f;

            // -----------------------------------------------------------------
            // Step 3f: Brightness normalization relative to local background
            //
            // This ensures stars on bright nebulae are scored by their contrast
            // above their immediate surroundings, not against the global median.
            // -----------------------------------------------------------------
            const float localDynRange = 255.f - localBg;
            const float brightness = (localDynRange > 0.f)
                ? std::max(0.f, std::min(1.f, (peakVal - localBg) / localDynRange))
                : 1.f;

            // Build the star record
            AstroSpike::Star s;
            s.x          = cx;
            s.y          = cy;
            s.brightness = brightness;
            s.radius     = fwhmRadius;
            s.color      = QColor(
                static_cast<int>(std::min(255.f, avgR)),
                static_cast<int>(std::min(255.f, avgG)),
                static_cast<int>(std::min(255.f, avgB)));

            allDetected.append(s);
        }
    }

    qDebug() << "[AstroSpike] Detected" << allDetected.size()
             << "raw star candidates";

    // -------------------------------------------------------------------------
    // Step 4: Sort by brightness (descending) for priority ordering
    // -------------------------------------------------------------------------
    std::sort(allDetected.begin(), allDetected.end(),
        [](const AstroSpike::Star& a, const AstroSpike::Star& b) {
            return a.brightness > b.brightness;
        });

    // -------------------------------------------------------------------------
    // Step 5: Merge close detections (keep the brightest of each pair)
    //
    // The local-max scan prevents duplicates within 'localMaxRadius' pixels,
    // but stars separated by r+1..2r can both pass. This post-merge step
    // collapses genuine same-star detections.
    // -------------------------------------------------------------------------
    QVector<AstroSpike::Star> merged;
    merged.reserve(allDetected.size());

    for (const auto& s : allDetected) {
        bool isDuplicate = false;
        for (const auto& existing : merged) {
            const float dx     = s.x - existing.x;
            const float dy     = s.y - existing.y;
            const float distSq = dx * dx + dy * dy;
            const float mergeR = (existing.radius + s.radius) * 0.9f;

            if (distSq < mergeR * mergeR) {
                isDuplicate = true;
                break;
            }
        }
        if (!isDuplicate)
            merged.append(s);
    }

    qDebug() << "[AstroSpike] After merge:" << merged.size() << "unique stars";

    emit detectionComplete(merged);
}


// =============================================================================
// AstroSpikeCanvas -- Construction and Data Management
// =============================================================================

AstroSpikeCanvas::AstroSpikeCanvas(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    createGlowSprite();
}

void AstroSpikeCanvas::setImage(const QImage& img)
{
    m_image = img;
    updateSpikePreview();
    fitToView();
    update();
}

void AstroSpikeCanvas::setStars(const QVector<AstroSpike::Star>& stars)
{
    m_stars = stars;
    updateSpikePreview();
    update();
}

void AstroSpikeCanvas::setConfig(const AstroSpike::Config& config)
{
    m_config = config;
    updateSpikePreview();
    update();
}

void AstroSpikeCanvas::setToolMode(AstroSpike::ToolMode mode)
{
    m_toolMode = mode;
    setCursor(mode == AstroSpike::ToolMode::None
              ? Qt::OpenHandCursor
              : Qt::CrossCursor);
}


// =============================================================================
// AstroSpikeCanvas -- Cached Spike Preview
// =============================================================================

void AstroSpikeCanvas::updateSpikePreview()
{
    if (m_image.isNull()) {
        m_spikePreview = QImage();
        return;
    }

    // Determine preview dimensions (cap at 2048px for performance)
    QSize targetSize = m_image.size();
    float scale = 1.0f;
    const int MAX_PREVIEW_DIM = 2048;

    if (targetSize.width() > MAX_PREVIEW_DIM ||
        targetSize.height() > MAX_PREVIEW_DIM) {
        scale = static_cast<float>(MAX_PREVIEW_DIM) /
                std::max(targetSize.width(), targetSize.height());
        targetSize = targetSize * scale;
    }

    // Reuse the buffer if dimensions and format match
    if (m_spikePreview.size() != targetSize ||
        m_spikePreview.format() != QImage::Format_ARGB32_Premultiplied) {
        m_spikePreview = QImage(targetSize,
                                QImage::Format_ARGB32_Premultiplied);
    }
    m_spikePreview.fill(Qt::transparent);

    if (!m_stars.isEmpty()) {
        QPainter p(&m_spikePreview);
        if (scale != 1.0f)
            p.scale(scale, scale);
        render(p, 1.0f, QPointF(0, 0));
        p.end();

        // Apply a slight Gaussian blur for anti-aliasing
        float sigma = std::max(0.5f, 1.0f * scale);
        cv::Mat spikeMat(m_spikePreview.height(), m_spikePreview.width(),
                         CV_8UC4, m_spikePreview.bits(),
                         m_spikePreview.bytesPerLine());
        cv::GaussianBlur(spikeMat, spikeMat, cv::Size(0, 0), sigma, sigma);
    }
}


// =============================================================================
// AstroSpikeCanvas -- View Navigation
// =============================================================================

void AstroSpikeCanvas::zoomIn()
{
    zoomToPoint(QPointF(width() / 2.0, height() / 2.0), 1.2f);
}

void AstroSpikeCanvas::zoomOut()
{
    zoomToPoint(QPointF(width() / 2.0, height() / 2.0), 1.0f / 1.2f);
}

void AstroSpikeCanvas::zoomToPoint(QPointF widgetPos, float factor)
{
    // Compute image point under cursor before zoom
    float cx = width()  / 2.0f + m_panOffset.x();
    float cy = height() / 2.0f + m_panOffset.y();
    QPointF imgPt = (widgetPos - QPointF(cx, cy)) / m_zoom;

    m_zoom *= factor;

    // Adjust pan so the same image point remains under cursor
    QPointF newScreenPt = imgPt * m_zoom + QPointF(cx, cy);
    m_panOffset += (widgetPos - newScreenPt);

    update();
}

void AstroSpikeCanvas::fitToView()
{
    if (m_image.isNull())
        return;

    // Defer if widget geometry is not yet valid
    if (width() <= 100 || height() <= 100) {
        QTimer::singleShot(50, this, &AstroSpikeCanvas::fitToView);
        return;
    }

    float wFactor = static_cast<float>(width())  / m_image.width();
    float hFactor = static_cast<float>(height()) / m_image.height();
    m_zoom      = std::min(wFactor, hFactor) * 0.95f;
    m_panOffset = QPointF(0, 0);
    update();
}


// =============================================================================
// AstroSpikeCanvas -- Paint Event
// =============================================================================

void AstroSpikeCanvas::paintEvent([[maybe_unused]] QPaintEvent* event)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::black);

    if (m_image.isNull())
        return;

    // Compute the destination rectangle for the image
    float drawW = m_image.width()  * m_zoom;
    float drawH = m_image.height() * m_zoom;
    float cx    = width()  / 2.0f + m_panOffset.x();
    float cy    = height() / 2.0f + m_panOffset.y();
    QRectF destRect(cx - drawW / 2, cy - drawH / 2, drawW, drawH);

    // Draw the base image
    p.drawImage(destRect, m_image);

    // Overlay the cached spike preview using Screen blending
    if (!m_spikePreview.isNull() && !m_stars.isEmpty()) {
        p.setCompositionMode(QPainter::CompositionMode_Screen);
        p.drawImage(destRect, m_spikePreview);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    }

    // Draw the tool cursor overlay (add/erase brush circle)
    if (m_toolMode != AstroSpike::ToolMode::None &&
        rect().contains(mapFromGlobal(QCursor::pos()))) {
        p.setPen(QPen(Qt::white, 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        QPoint pos = mapFromGlobal(QCursor::pos());

        float r = (m_toolMode == AstroSpike::ToolMode::Add)
                  ? m_brushRadius * m_zoom
                  : m_eraserSize * m_zoom;
        p.drawEllipse(pos, static_cast<int>(r), static_cast<int>(r));
    }
}


// =============================================================================
// AstroSpikeCanvas -- Glow Sprite Generation
// =============================================================================

void AstroSpikeCanvas::createGlowSprite()
{
    const int size = 256;
    m_glowSprite = QImage(size, size, QImage::Format_ARGB32_Premultiplied);
    m_glowSprite.fill(Qt::transparent);

    QPainter p(&m_glowSprite);
    p.setRenderHint(QPainter::Antialiasing);

    QRadialGradient grad(size / 2, size / 2, size / 2);
    grad.setColorAt(0.0, QColor(255, 255, 255, 255));
    grad.setColorAt(0.2, QColor(255, 255, 255, 100));
    grad.setColorAt(0.6, QColor(255, 255, 255, 15));
    grad.setColorAt(1.0, QColor(255, 255, 255, 0));

    p.setBrush(grad);
    p.setPen(Qt::NoPen);
    p.drawRect(0, 0, size, size);
}


// =============================================================================
// AstroSpikeCanvas -- Star Color Computation
//
// Converts the sampled star color into an output color with hue shift,
// saturation boost, and lightness adjustments for rendering.
// =============================================================================

QColor AstroSpikeCanvas::getStarColor(const AstroSpike::Star& star,
                                       float hueShift, float sat,
                                       float alpha)
{
    QColor c = star.color;

    float h = c.hueF();
    float s = c.saturationF();
    float l = c.lightnessF();

    // Handle achromatic colors (hue == -1 in Qt)
    if (h < 0) h = 0;

    // Apply hue rotation
    float newH = h * 360.0f + hueShift;
    newH = std::fmod(newH, 360.0f);
    if (newH < 0) newH += 360.0f;
    newH /= 360.0f;

    // Boost saturation from subtle star colors (16x amplification)
    float boostedS = std::min(1.0f, s * 16.0f);

    float finalS = 0.f;
    float finalL = 0.f;

    if (sat <= 1.0f) {
        // Normal range: linear scale
        finalS = boostedS * sat;
        finalL = std::max(l, 0.65f);
    } else {
        // Hyper-saturation: blend toward full saturation
        float hyper = sat - 1.0f;
        finalS = boostedS + (1.0f - boostedS) * hyper;
        float baseL = std::max(l, 0.65f);
        finalL = baseL + (0.5f - baseL) * hyper;
    }

    finalS = qBound(0.0f, finalS, 1.0f);
    finalL = qBound(0.4f, finalL, 0.95f);

    return QColor::fromHslF(newH, finalS, finalL, alpha);
}


// =============================================================================
// AstroSpikeCanvas -- Spike Rendering
//
// Renders all spike effects (primary, secondary, glow, halo, rainbow) for
// the currently visible stars into the provided QPainter.
// =============================================================================

void AstroSpikeCanvas::render(QPainter& p, float scale, const QPointF& offset)
{
    if (m_stars.isEmpty())
        return;

    // Apply the star amount percentage as a hard cap
    int limit = static_cast<int>(
        m_stars.size() * m_config.starAmount / 100.0f);
    int count = 0;

    p.setRenderHint(QPainter::Antialiasing);
    p.setCompositionMode(QPainter::CompositionMode_Screen);

    const float degToRad    = static_cast<float>(M_PI) / 180.0f;
    const float angleRad    = m_config.angle * degToRad;
    const float secAngleRad = (m_config.angle + m_config.secondaryOffset) * degToRad;

    for (const auto& star : m_stars) {
        if (count++ >= limit)
            break;

        // Apply size filters
        if (star.radius < m_config.minStarSize)
            continue;
        if (star.radius > m_config.maxStarSize)
            continue;

        const float sx = star.x * scale + offset.x();
        const float sy = star.y * scale + offset.y();

        // -----------------------------------------------------------------
        // Artificial star core (radial gradient disk)
        // -----------------------------------------------------------------
        {
            float coreR = star.radius * m_config.globalScale * scale;
            if (coreR >= 1.0f) {
                QColor sc = getStarColor(star, m_config.hueShift,
                                          m_config.colorSaturation,
                                          m_config.intensity);
                QColor edge = sc;
                edge.setAlphaF(0.0);

                QRadialGradient rg(QPointF(sx, sy), coreR);
                rg.setColorAt(0.0, sc);
                rg.setColorAt(1.0, edge);

                p.setOpacity(1.0);
                p.setPen(Qt::NoPen);
                p.setBrush(QBrush(rg));
                p.drawEllipse(QPointF(sx, sy), coreR, coreR);
                p.setBrush(Qt::NoBrush);
            }
        }

        // -----------------------------------------------------------------
        // Soft flare (glow sprite overlay)
        // -----------------------------------------------------------------
        if (m_config.softFlareIntensity > 0) {
            float glowR = (star.radius * m_config.softFlareSize * 0.4f +
                           star.radius * 2.0f) * scale;
            if (glowR > 2) {
                float opacity = m_config.softFlareIntensity * 0.8f * star.brightness;
                p.setOpacity(std::min(1.0f, opacity));
                p.drawImage(QRectF(sx - glowR, sy - glowR,
                                   glowR * 2, glowR * 2), m_glowSprite);
            }
        }
        p.setOpacity(1.0);

        // Compute common spike geometry
        float radiusFactor = std::pow(star.radius, 1.2f);
        float baseLen = radiusFactor * (m_config.length / 40.0f) *
                        m_config.globalScale * scale;
        float thick   = std::max(0.5f,
            star.radius * m_config.spikeWidth * 0.15f *
            m_config.globalScale * scale);

        if (baseLen < 2)
            continue;

        QColor color    = getStarColor(star, m_config.hueShift,
                                        m_config.colorSaturation,
                                        m_config.intensity);
        QColor secColor = getStarColor(star, m_config.hueShift,
                                        m_config.colorSaturation,
                                        m_config.secondaryIntensity);

        // -----------------------------------------------------------------
        // Primary spikes
        //
        // Each spike is a single ray emanating from the center. The quantity
        // defines the number of evenly-spaced rays (not full lines).
        // -----------------------------------------------------------------
        if (m_config.intensity > 0) {
            float rainbowStr = (m_config.enableRainbow && m_config.rainbowSpikes)
                               ? m_config.rainbowIntensity : 0.0f;

            int spikes = std::max(2, static_cast<int>(m_config.quantity));

            for (int i = 0; i < spikes; ++i) {
                float theta = angleRad + (i * M_PI * 2.0f / spikes);

                float dx = std::cos(theta);
                float dy = std::sin(theta);

                // Small offset from center to avoid over-bright buildup
                QPointF rayStart(sx + dx * 1.5f * scale,
                                 sy + dy * 1.5f * scale);
                QPointF rayEnd(sx + dx * baseLen,
                               sy + dy * baseLen);

                // Standard spike with gradient fade-out
                if (rainbowStr > 0) p.setOpacity(0.4);

                QLinearGradient grad(rayStart, rayEnd);
                grad.setColorAt(0, color);
                QColor endC = color;
                endC.setAlpha(0);
                grad.setColorAt(1, endC);

                QPen pen(QBrush(grad), thick, Qt::SolidLine, Qt::FlatCap);
                p.setPen(pen);
                p.drawLine(rayStart, rayEnd);
                p.setOpacity(1.0);

                // Rainbow chromatic overlay
                if (rainbowStr > 0) {
                    QLinearGradient rGrad(rayStart, rayEnd);
                    rGrad.setColorAt(0, color);

                    const int stops = 10;
                    for (int s = 1; s <= stops; ++s) {
                        float pos = static_cast<float>(s) / stops;
                        if (pos > m_config.rainbowLength)
                            break;

                        float hue = std::fmod(
                            pos * 360.0f * m_config.rainbowFrequency, 360.0f);
                        float a = std::min(1.0f,
                            m_config.intensity * rainbowStr * 2.0f) *
                            (1.0f - pos);

                        QColor c = QColor::fromHslF(
                            hue / 360.0f, 0.8f, 0.6f, std::min(1.0f, a));
                        rGrad.setColorAt(pos, c);
                    }
                    rGrad.setColorAt(1, QColor(0, 0, 0, 0));

                    QPen rPen(QBrush(rGrad), thick, Qt::SolidLine, Qt::FlatCap);
                    p.setPen(rPen);
                    p.drawLine(rayStart, rayEnd);
                }
            }
        }

        // -----------------------------------------------------------------
        // Secondary spikes (shorter, offset by secondaryOffset degrees)
        // -----------------------------------------------------------------
        if (m_config.secondaryIntensity > 0) {
            float secLen = baseLen * (m_config.secondaryLength / m_config.length);
            int spikes   = std::max(2, static_cast<int>(m_config.quantity));

            for (int i = 0; i < spikes; ++i) {
                float theta = secAngleRad + (i * M_PI * 2.0f / spikes);

                float dx = std::cos(theta);
                float dy = std::sin(theta);

                QPointF start(sx + dx * 2.0f * scale,
                              sy + dy * 2.0f * scale);
                QPointF end(sx + dx * secLen,
                            sy + dy * secLen);

                QLinearGradient grad(start, end);
                grad.setColorAt(0, secColor);
                grad.setColorAt(1, QColor(0, 0, 0, 0));

                QPen pen(QBrush(grad), thick * 0.6f,
                         Qt::SolidLine, Qt::FlatCap);
                p.setPen(pen);
                p.drawLine(start, end);
            }
        }

        // -----------------------------------------------------------------
        // Halo ring effect
        // -----------------------------------------------------------------
        if (m_config.enableHalo && m_config.haloIntensity > 0) {
            float classScore      = star.radius * star.brightness;
            float intensityWeight = std::pow(
                std::min(1.0f, classScore / 10.0f), 2.0f);

            if (intensityWeight > 0.01f) {
                float finalHaloInt = m_config.haloIntensity * intensityWeight;
                QColor haloColor   = getStarColor(star, m_config.hueShift,
                                                   m_config.haloSaturation,
                                                   finalHaloInt);

                float rHalo = star.radius * m_config.haloScale * scale;
                if (rHalo > 0.5f) {
                    float blurExpand = m_config.haloBlur * 20.0f * scale;
                    float relWidth   = rHalo * (m_config.haloWidth * 0.15f);
                    float innerR     = std::max(0.0f, rHalo - relWidth / 2.0f);
                    float outerR     = rHalo + relWidth / 2.0f;
                    float drawOuter  = outerR + blurExpand;

                    QRadialGradient grad(sx, sy, drawOuter);
                    float stopStart = innerR / drawOuter;
                    float stopEnd   = outerR / drawOuter;

                    grad.setColorAt(0, Qt::transparent);
                    grad.setColorAt(std::max(0.0f, stopStart - 0.05f),
                                    Qt::transparent);
                    grad.setColorAt((stopStart + stopEnd) / 2.0f, haloColor);
                    grad.setColorAt(std::min(1.0f, stopEnd + 0.05f),
                                    Qt::transparent);
                    grad.setColorAt(1, Qt::transparent);

                    p.setBrush(QBrush(grad));
                    p.setPen(Qt::NoPen);
                    p.drawEllipse(QPointF(sx, sy), drawOuter, drawOuter);
                }
            }
        }
    }

    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
}


// =============================================================================
// AstroSpikeCanvas -- Mouse and Input Event Handling
// =============================================================================

void AstroSpikeCanvas::mousePressEvent(QMouseEvent* event)
{
    if (m_toolMode == AstroSpike::ToolMode::None) {
        // Navigation mode: begin panning
        if (event->button() == Qt::MiddleButton ||
            event->button() == Qt::LeftButton) {
            m_dragging = true;
            setCursor(Qt::ClosedHandCursor);
            m_lastMousePos = event->pos();
        }
    } else {
        // Tool mode: apply add/erase at the clicked image position
        if (event->button() == Qt::LeftButton) {
            float drawW   = m_image.width()  * m_zoom;
            float drawH   = m_image.height() * m_zoom;
            float offsetX = width()  / 2.0f + m_panOffset.x() - drawW / 2;
            float offsetY = height() / 2.0f + m_panOffset.y() - drawH / 2;

            QPointF imgPos = (event->position() - QPointF(offsetX, offsetY)) / m_zoom;
            handleTool(imgPos);
        }
    }
}

void AstroSpikeCanvas::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging) {
        QPoint delta = event->pos() - m_lastMousePos;
        m_panOffset += delta;
        m_lastMousePos = event->pos();
        update();
    } else {
        update();  // Redraw cursor circle
    }
}

void AstroSpikeCanvas::mouseReleaseEvent([[maybe_unused]] QMouseEvent* event)
{
    if (m_dragging) {
        m_dragging = false;
        setCursor(Qt::OpenHandCursor);
    }
}

void AstroSpikeCanvas::wheelEvent(QWheelEvent* event)
{
    float factor = event->angleDelta().y() > 0 ? 1.2f : (1.0f / 1.2f);
    zoomToPoint(event->position(), factor);
}

void AstroSpikeCanvas::resizeEvent(QResizeEvent* event)
{
    if (m_firstResize) {
        fitToView();
        m_firstResize = false;
    }
    QWidget::resizeEvent(event);
}


// =============================================================================
// AstroSpikeCanvas -- Interactive Tool Logic
// =============================================================================

void AstroSpikeCanvas::handleTool(const QPointF& imgPos)
{
    if (m_image.isNull())
        return;

    if (m_toolMode == AstroSpike::ToolMode::Add) {
        // Create a new synthetic star at the clicked position
        AstroSpike::Star s;
        s.x          = imgPos.x();
        s.y          = imgPos.y();
        s.radius     = m_brushRadius;
        s.brightness = 1.0f;
        s.color      = Qt::white;

        QVector<AstroSpike::Star> newStars = m_stars;
        newStars.append(s);
        emit starsUpdated(newStars);

    } else if (m_toolMode == AstroSpike::ToolMode::Erase) {
        // Remove all stars within the eraser radius
        QVector<AstroSpike::Star> newStars;
        float rSq    = m_eraserSize * m_eraserSize;
        bool  changed = false;

        for (const auto& s : m_stars) {
            float dx = s.x - imgPos.x();
            float dy = s.y - imgPos.y();
            if (dx * dx + dy * dy > rSq) {
                newStars.append(s);
            } else {
                changed = true;
            }
        }

        if (changed)
            emit starsUpdated(newStars);
    }
}


// =============================================================================
// AstroSpikeDialog -- Construction and Lifecycle
// =============================================================================

AstroSpikeDialog::AstroSpikeDialog(ImageViewer* viewer, QWidget* parent)
    : DialogBase(parent, tr("AstroSpike"), 1300, 700)
    , m_viewer(viewer)
{
    setWindowFlags(windowFlags() | Qt::Dialog |
                   Qt::WindowMaximizeButtonHint |
                   Qt::WindowCloseButtonHint);
    setModal(true);

    setupUI();
    m_canvas->setConfig(m_config);

    // Trigger initial star detection if the viewer has a valid image
    if (m_viewer && m_viewer->getBuffer().isValid()) {
        m_canvas->setImage(
            m_viewer->getBuffer().getDisplayImage(ImageBuffer::Display_Linear));
        m_detectTimer.start(200, this);
    }

    if (parentWidget()) {
        move(parentWidget()->window()->geometry().center() - rect().center());
    }
}

AstroSpikeDialog::~AstroSpikeDialog()
{
    if (m_thread) {
        m_thread->wait();
        m_thread = nullptr;
    }
}

void AstroSpikeDialog::closeEvent(QCloseEvent* event)
{
    if (m_thread && m_thread->isRunning())
        m_thread->wait();
    QDialog::closeEvent(event);
}

void AstroSpikeDialog::timerEvent(QTimerEvent* event)
{
    if (event->timerId() == m_detectTimer.timerId()) {
        m_detectTimer.stop();
        runDetection();
    }
}

bool AstroSpikeDialog::eventFilter(QObject* obj, QEvent* event)
{
    // Block mouse wheel events on sliders to prevent accidental changes
    if (event->type() == QEvent::Wheel) {
        if (qobject_cast<QAbstractSlider*>(obj)) {
            event->ignore();
            return true;
        }
    }
    return QDialog::eventFilter(obj, event);
}


// =============================================================================
// AstroSpikeDialog -- Viewer Management
// =============================================================================

void AstroSpikeDialog::setViewer(ImageViewer* v)
{
    if (m_viewer == v)
        return;

    m_viewer = v;
    if (!m_viewer || !m_viewer->getBuffer().isValid()) {
        m_statusLabel->setText(tr("No valid image."));
        return;
    }

    // Refresh the canvas with the new viewer's image
    m_canvas->setImage(
        m_viewer->getBuffer().getDisplayImage(ImageBuffer::Display_Linear));

    // Reset state and re-run detection
    m_canvas->setStars({});
    m_allStars.clear();
    m_history.clear();
    m_historyIndex = 0;
    updateHistoryButtons();
    runDetection();
}


// =============================================================================
// AstroSpikeDialog -- Star Detection Pipeline
// =============================================================================

void AstroSpikeDialog::runDetection()
{
    if (!m_viewer)
        return;
    if (m_thread && m_thread->isRunning())
        return;

    m_statusLabel->setText(tr("Detecting all stars..."));

    m_thread = new StarDetectionThread(m_viewer->getBuffer(), this);
    connect(m_thread, &StarDetectionThread::detectionComplete,
            this,     &AstroSpikeDialog::onStarsDetected);
    connect(m_thread, &QThread::finished,
            m_thread, &QObject::deleteLater);
    m_thread->start();
}

void AstroSpikeDialog::onStarsDetected(
    const QVector<AstroSpike::Star>& stars)
{
    m_allStars = stars;
    m_thread   = nullptr;  // Will be cleaned up by deleteLater
    filterStarsByThreshold();
}

void AstroSpikeDialog::filterStarsByThreshold()
{
    // Map the slider range [1..100] linearly to a brightness cutoff [0.0..0.92]
    const float cutoff = std::max(0.f,
        (m_config.threshold - 1.0f) / 99.0f * 0.92f);

    QVector<AstroSpike::Star> filtered;
    filtered.reserve(m_allStars.size());

    for (const auto& s : m_allStars) {
        if (s.brightness >= cutoff)
            filtered.append(s);
    }

    m_statusLabel->setText(
        tr("%1 stars (of %2 total)")
            .arg(filtered.size()).arg(m_allStars.size()));
    m_canvas->setStars(filtered);

    // Reset edit history to the newly filtered set
    m_history.clear();
    m_history.append(filtered);
    m_historyIndex = 0;
    updateHistoryButtons();
}


// =============================================================================
// AstroSpikeDialog -- Undo/Redo History
// =============================================================================

void AstroSpikeDialog::onCanvasStarsUpdated(
    const QVector<AstroSpike::Star>& stars)
{
    m_canvas->setStars(stars);
    pushHistory(stars);
}

void AstroSpikeDialog::pushHistory(const QVector<AstroSpike::Star>& stars)
{
    // Truncate any forward history beyond the current index
    if (m_historyIndex < m_history.size() - 1)
        m_history.resize(m_historyIndex + 1);

    m_history.append(stars);
    m_historyIndex++;
    updateHistoryButtons();
}

void AstroSpikeDialog::undo()
{
    if (m_historyIndex > 0) {
        m_historyIndex--;
        m_canvas->setStars(m_history[m_historyIndex]);
        updateHistoryButtons();
        if (auto mw = getCallbacks())
            mw->logMessage(tr("Undo: AstroSpike Star Edit performed."), 1);
    }
}

void AstroSpikeDialog::redo()
{
    if (m_historyIndex < m_history.size() - 1) {
        m_historyIndex++;
        m_canvas->setStars(m_history[m_historyIndex]);
        updateHistoryButtons();
        if (auto mw = getCallbacks())
            mw->logMessage(tr("Redo: AstroSpike Star Edit performed."), 1);
    }
}

void AstroSpikeDialog::updateHistoryButtons()
{
    m_btnUndo->setEnabled(m_historyIndex > 0);
    m_btnRedo->setEnabled(m_historyIndex < m_history.size() - 1);
}


// =============================================================================
// AstroSpikeDialog -- UI Construction
// =============================================================================

void AstroSpikeDialog::setupUI()
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    // ---- Top toolbar ----
    QWidget* topBar = new QWidget(this);
    topBar->setStyleSheet(
        "background: #252526; border-bottom: 1px solid #333;");
    QHBoxLayout* topLayout = new QHBoxLayout(topBar);

    // Apply button
    QPushButton* btnApply = new QPushButton(tr("Apply"), this);
    connect(btnApply, &QPushButton::clicked,
            this, &AstroSpikeDialog::applyToDocument);

    // Undo/Redo buttons
    m_btnUndo = new QPushButton(tr("Undo"), this);
    m_btnUndo->setEnabled(false);
    connect(m_btnUndo, &QPushButton::clicked, this, &AstroSpikeDialog::undo);

    m_btnRedo = new QPushButton(tr("Redo"), this);
    m_btnRedo->setEnabled(false);
    connect(m_btnRedo, &QPushButton::clicked, this, &AstroSpikeDialog::redo);

    // Tool buttons
    QPushButton* btnAdd   = new QPushButton(tr("Add Star"), this);
    QPushButton* btnErase = new QPushButton(tr("Eraser"), this);

    connect(btnAdd,   &QPushButton::clicked, [this]() {
        setToolMode(AstroSpike::ToolMode::Add);
    });
    connect(btnErase, &QPushButton::clicked, [this]() {
        setToolMode(AstroSpike::ToolMode::Erase);
    });

    // Status label
    m_statusLabel = new QLabel(tr("Ready"), this);
    m_statusLabel->setStyleSheet("color: #aaa; margin-left: 20px;");

    topLayout->addWidget(btnApply);
    topLayout->addSpacing(20);
    topLayout->addWidget(m_btnUndo);
    topLayout->addWidget(m_btnRedo);
    topLayout->addSpacing(20);
    topLayout->addWidget(btnAdd);
    topLayout->addWidget(btnErase);

    // ---- Brush/eraser size controls ----
    topLayout->addSpacing(10);

    QLabel* lSize = new QLabel(tr("Size:"), this);
    lSize->setStyleSheet("color:#888");
    topLayout->addWidget(lSize);

    QSlider* sldSize = new QSlider(Qt::Horizontal);
    sldSize->setFixedWidth(80);
    sldSize->setRange(1, 50);
    sldSize->setValue(4);
    sldSize->installEventFilter(this);

    QLabel* lSizeVal = new QLabel("4", this);
    lSizeVal->setFixedWidth(25);

    connect(sldSize, &QSlider::valueChanged, [=](int v) {
        m_canvas->setStarInputRadius(v);
        lSizeVal->setText(QString::number(v));
    });

    topLayout->addWidget(sldSize);
    topLayout->addWidget(lSizeVal);

    QLabel* lErase = new QLabel(tr("Erase:"), this);
    lErase->setStyleSheet("color:#888");
    topLayout->addWidget(lErase);

    QSlider* sldErase = new QSlider(Qt::Horizontal);
    sldErase->setFixedWidth(80);
    sldErase->setRange(5, 200);
    sldErase->setValue(20);
    sldErase->installEventFilter(this);

    QLabel* lEraseVal = new QLabel("20", this);
    lEraseVal->setFixedWidth(25);

    connect(sldErase, &QSlider::valueChanged, [=](int v) {
        m_canvas->setEraserInputSize(v);
        lEraseVal->setText(QString::number(v));
    });

    topLayout->addWidget(sldErase);
    topLayout->addWidget(lEraseVal);
    topLayout->addWidget(m_statusLabel);
    topLayout->addStretch();

    // ---- Zoom controls ----
    QPushButton* btnZoomIn = new QPushButton(tr("+"), this);
    btnZoomIn->setFixedSize(28, 28);
    btnZoomIn->setToolTip(tr("Zoom In"));
    connect(btnZoomIn, &QPushButton::clicked, [this]() {
        m_canvas->zoomIn();
    });
    topLayout->addWidget(btnZoomIn);

    QPushButton* btnZoomOut = new QPushButton(tr("-"), this);
    btnZoomOut->setFixedSize(28, 28);
    btnZoomOut->setToolTip(tr("Zoom Out"));
    connect(btnZoomOut, &QPushButton::clicked, [this]() {
        m_canvas->zoomOut();
    });
    topLayout->addWidget(btnZoomOut);

    QPushButton* btnFit = new QPushButton(tr("Fit"), this);
    btnFit->setFixedSize(40, 28);
    btnFit->setToolTip(tr("Fit to Screen"));
    connect(btnFit, &QPushButton::clicked, [this]() {
        m_canvas->fitToView();
    });
    topLayout->addWidget(btnFit);

    topLayout->addSpacing(10);

    // Reset button
    QPushButton* btnReset = new QPushButton(tr("Reset"), this);
    btnReset->setToolTip(tr("Reset to Defaults"));
    connect(btnReset, &QPushButton::clicked, [this]() {
        resetConfig();
    });
    topLayout->addWidget(btnReset);

    root->addWidget(topBar);

    // ---- Content area (canvas + controls panel) ----
    QWidget* content = new QWidget(this);
    QHBoxLayout* contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);

    // Canvas widget
    m_canvas = new AstroSpikeCanvas(this);
    m_canvas->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_canvas->setMinimumSize(100, 100);
    connect(m_canvas, &AstroSpikeCanvas::starsUpdated,
            this,     &AstroSpikeDialog::onCanvasStarsUpdated);
    contentLayout->addWidget(m_canvas, 1);

    // Scrollable controls panel
    m_controlsScroll = new QScrollArea(this);
    m_controlsScroll->setFixedWidth(300);
    m_controlsScroll->setWidgetResizable(true);

    QWidget* controlsWidget = new QWidget();
    QVBoxLayout* controlsLayout = new QVBoxLayout(controlsWidget);
    setupControls(controlsLayout);
    controlsLayout->addStretch();

    m_controlsScroll->setWidget(controlsWidget);
    contentLayout->addWidget(m_controlsScroll);

    root->addWidget(content);

    // ---- Footer ----
    QLabel* copyright = new QLabel(tr("(C) 2026 Fabio Tempera"), this);
    copyright->setAlignment(Qt::AlignRight);
    copyright->setStyleSheet(
        "color: #888; font-size: 10px; padding: 3px 10px; "
        "background: #1e1e1e; border-top: 1px solid #333;");
    root->addWidget(copyright);

    root->setSizeConstraint(QLayout::SetMinimumSize);
}

void AstroSpikeDialog::setupControls(QVBoxLayout* layout)
{
    // ---- Detection group ----
    QGroupBox* grpDetect  = new QGroupBox(tr("Detection"));
    QVBoxLayout* lDetect  = new QVBoxLayout(grpDetect);

    lDetect->addWidget(createSlider(tr("Threshold"), 1, 100, 1,
        m_config.threshold, &m_config.threshold, ""));
    lDetect->addWidget(createSlider(tr("Amount %"), 0, 100, 1,
        m_config.starAmount, &m_config.starAmount, "%"));
    lDetect->addWidget(createSlider(tr("Min Size"), 0, 100, 1,
        m_config.minStarSize, &m_config.minStarSize, ""));
    lDetect->addWidget(createSlider(tr("Max Size"), 10, 200, 1,
        m_config.maxStarSize, &m_config.maxStarSize, ""));
    layout->addWidget(grpDetect);

    // ---- Spike geometry group ----
    QGroupBox* grpGeo  = new QGroupBox(tr("Spikes"));
    QVBoxLayout* lGeo  = new QVBoxLayout(grpGeo);

    lGeo->addWidget(createSlider(tr("Points"), 2, 8, 1,
        m_config.quantity, &m_config.quantity, ""));
    lGeo->addWidget(createSlider(tr("Length"), 10, 800, 10,
        m_config.length, &m_config.length, ""));
    lGeo->addWidget(createSlider(tr("Angle"), 0, 180, 1,
        m_config.angle, &m_config.angle, "deg"));
    lGeo->addWidget(createSlider(tr("Thickness"), 0.1, 5.0, 0.1,
        m_config.spikeWidth, &m_config.spikeWidth, ""));
    lGeo->addWidget(createSlider(tr("Global Scale"), 0.1, 5.0, 0.1,
        m_config.globalScale, &m_config.globalScale, "x"));
    lGeo->addWidget(createSlider(tr("Intensity"), 0, 1.0, 0.05,
        m_config.intensity, &m_config.intensity, ""));
    layout->addWidget(grpGeo);

    // ---- Appearance group ----
    QGroupBox* grpApp  = new QGroupBox(tr("Appearance"));
    QVBoxLayout* lApp  = new QVBoxLayout(grpApp);

    lApp->addWidget(createSlider(tr("Saturation"), 0.0, 3.0, 0.1,
        m_config.colorSaturation, &m_config.colorSaturation, ""));
    lApp->addWidget(createSlider(tr("Hue Shift"), 0, 360, 5,
        m_config.hueShift, &m_config.hueShift, "deg"));
    layout->addWidget(grpApp);

    // ---- Secondary spikes group ----
    QGroupBox* grpSec  = new QGroupBox(tr("Secondary Spikes"));
    QVBoxLayout* lSec  = new QVBoxLayout(grpSec);

    lSec->addWidget(createSlider(tr("Intensity"), 0, 1.0, 0.05,
        m_config.secondaryIntensity, &m_config.secondaryIntensity, ""));
    lSec->addWidget(createSlider(tr("Length"), 10, 500, 10,
        m_config.secondaryLength, &m_config.secondaryLength, ""));
    lSec->addWidget(createSlider(tr("Offset"), 0, 90, 1,
        m_config.secondaryOffset, &m_config.secondaryOffset, "deg"));
    layout->addWidget(grpSec);

    // ---- Halo group ----
    QGroupBox* grpHalo  = new QGroupBox(tr("Halo"));
    QVBoxLayout* lHalo  = new QVBoxLayout(grpHalo);

    QCheckBox* chkHalo = new QCheckBox(tr("Enable Halo"));
    chkHalo->setChecked(m_config.enableHalo);
    connect(chkHalo, &QCheckBox::toggled, [this](bool c) {
        m_config.enableHalo = c;
        onConfigChanged();
    });
    lHalo->addWidget(chkHalo);

    lHalo->addWidget(createSlider(tr("Intensity"), 0, 2.0, 0.1,
        m_config.haloIntensity, &m_config.haloIntensity, ""));
    lHalo->addWidget(createSlider(tr("Scale"), 1.0, 20.0, 1.0,
        m_config.haloScale, &m_config.haloScale, "x"));
    lHalo->addWidget(createSlider(tr("Width"), 0.1, 5.0, 0.1,
        m_config.haloWidth, &m_config.haloWidth, ""));
    lHalo->addWidget(createSlider(tr("Blur"), 0.0, 2.0, 0.1,
        m_config.haloBlur, &m_config.haloBlur, ""));
    lHalo->addWidget(createSlider(tr("Saturation"), 0.0, 2.0, 0.1,
        m_config.haloSaturation, &m_config.haloSaturation, ""));
    layout->addWidget(grpHalo);

    // ---- Rainbow group ----
    QGroupBox* grpRain  = new QGroupBox(tr("Rainbow"));
    QVBoxLayout* lRain  = new QVBoxLayout(grpRain);

    QCheckBox* chkRain = new QCheckBox(tr("Enable Rainbow"));
    chkRain->setChecked(m_config.enableRainbow);
    connect(chkRain, &QCheckBox::toggled, [this](bool c) {
        m_config.enableRainbow = c;
        onConfigChanged();
    });
    lRain->addWidget(chkRain);

    lRain->addWidget(createSlider(tr("Intensity"), 0, 2.0, 0.1,
        m_config.rainbowIntensity, &m_config.rainbowIntensity, ""));
    lRain->addWidget(createSlider(tr("Frequency"), 0.1, 5.0, 0.1,
        m_config.rainbowFrequency, &m_config.rainbowFrequency, ""));
    lRain->addWidget(createSlider(tr("Length"), 0.1, 1.0, 0.1,
        m_config.rainbowLength,
        reinterpret_cast<float*>(&m_config.rainbowLength), ""));
    layout->addWidget(grpRain);

    // ---- Flare group ----
    QGroupBox* grpFlare  = new QGroupBox(tr("Flare"));
    QVBoxLayout* lFlare  = new QVBoxLayout(grpFlare);

    lFlare->addWidget(createSlider(tr("Intensity"), 0, 1.0, 0.05,
        m_config.softFlareIntensity, &m_config.softFlareIntensity, ""));
    lFlare->addWidget(createSlider(tr("Size"), 1, 50, 1,
        m_config.softFlareSize, &m_config.softFlareSize, ""));
    layout->addWidget(grpFlare);
}

QWidget* AstroSpikeDialog::createSlider(const QString& label,
                                         float min, float max, float step,
                                         float initial, float* target,
                                         const QString& unit)
{
    QWidget* w = new QWidget();
    QVBoxLayout* l = new QVBoxLayout(w);
    l->setContentsMargins(0, 5, 0, 5);
    l->setSpacing(2);

    // Header row: label + current value
    QHBoxLayout* head = new QHBoxLayout();
    QLabel* lblName = new QLabel(label);
    QLabel* lblVal  = new QLabel(QString::number(initial, 'f', 1) + unit);
    head->addWidget(lblName);
    head->addStretch();
    head->addWidget(lblVal);

    // Slider mapped to discrete steps
    QSlider* slider = new QSlider(Qt::Horizontal);
    int steps = static_cast<int>((max - min) / step);
    slider->setRange(0, steps);
    slider->setValue(static_cast<int>((initial - min) / step));
    slider->installEventFilter(this);

    connect(slider, &QSlider::valueChanged, [=](int v) {
        float fVal = min + v * step;
        lblVal->setText(QString::number(fVal, 'f', 1) + unit);
        if (target) {
            *target = fVal;
            onConfigChanged();
        }
    });

    l->addLayout(head);
    l->addWidget(slider);
    return w;
}


// =============================================================================
// AstroSpikeDialog -- Configuration and Tool Management
// =============================================================================

void AstroSpikeDialog::onConfigChanged()
{
    m_canvas->setConfig(m_config);
    filterStarsByThreshold();
}

void AstroSpikeDialog::setToolMode(AstroSpike::ToolMode mode)
{
    m_canvas->setToolMode(mode);
}

void AstroSpikeDialog::resetConfig()
{
    m_config = AstroSpike::Config();

    // Rebuild the controls panel with default values
    if (m_controlsScroll) {
        QWidget* w = new QWidget();
        QVBoxLayout* l = new QVBoxLayout(w);
        setupControls(l);
        l->addStretch();

        QWidget* old = m_controlsScroll->takeWidget();
        if (old)
            old->deleteLater();
        m_controlsScroll->setWidget(w);
    }

    onConfigChanged();
}


// =============================================================================
// AstroSpikeDialog -- Document Compositing
// =============================================================================

void AstroSpikeDialog::applyToDocument()
{
    if (!m_viewer)
        return;

    // Retrieve full-resolution image in a compatible format
    QImage fullImg = m_viewer->getBuffer().getDisplayImage(
        ImageBuffer::Display_Linear);
    if (fullImg.format() != QImage::Format_RGB32 &&
        fullImg.format() != QImage::Format_ARGB32) {
        fullImg = fullImg.convertToFormat(QImage::Format_ARGB32);
    }

    // Render spikes into a transparent offscreen layer
    QImage spikeLayer(fullImg.size(), QImage::Format_ARGB32_Premultiplied);
    spikeLayer.fill(Qt::transparent);
    {
        QPainter sp(&spikeLayer);
        m_canvas->render(sp, 1.0f, QPointF(0, 0));
    }

    // Soften the spike layer with a mild Gaussian blur
    cv::Mat spikeMat(spikeLayer.height(), spikeLayer.width(), CV_8UC4,
                     spikeLayer.bits(), spikeLayer.bytesPerLine());
    cv::GaussianBlur(spikeMat, spikeMat, cv::Size(3, 3), 1.0, 1.0);

    // Composite the spike layer over the original using Screen blending
    QPainter p(&fullImg);
    p.setCompositionMode(QPainter::CompositionMode_Screen);
    p.drawImage(0, 0, spikeLayer);
    p.end();

    // Push undo state and write the composited result back to the buffer
    m_viewer->pushUndo(tr("AstroSpike"));

    ImageBuffer newBuffer = m_viewer->getBuffer();
    ImageBuffer origBuf   = newBuffer;

    if (newBuffer.width()  != fullImg.width() ||
        newBuffer.height() != fullImg.height()) {
        return;
    }

    std::vector<float>& data = newBuffer.data();
    int w = newBuffer.width();
    int c = newBuffer.channels();

    for (int y = 0; y < fullImg.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(
            fullImg.constScanLine(y));
        for (int x = 0; x < fullImg.width(); ++x) {
            float r = qRed(line[x])   / 255.0f;
            float g = qGreen(line[x]) / 255.0f;
            float b = qBlue(line[x])  / 255.0f;

            int idx = (y * w + x) * c;
            if (c >= 3) {
                data[idx + 0] = r;
                data[idx + 1] = g;
                data[idx + 2] = b;
            } else if (c == 1) {
                data[idx] = 0.2126f * r + 0.7152f * g + 0.0722f * b;
            }
        }
    }

    // Respect mask if present: blend processed pixels with original
    if (origBuf.hasMask())
        newBuffer.blendResult(origBuf);

    m_viewer->setBuffer(newBuffer, m_viewer->windowTitle(), true);

    if (auto mw = getCallbacks())
        mw->logMessage(tr("AstroSpike applied."), 1);

    accept();
}

void AstroSpikeDialog::saveImage()
{
    if (!m_viewer)
        return;

    QString filePath = QFileDialog::getSaveFileName(
        this, tr("Save Image"), "astrospike_output.png",
        tr("PNG Images (*.png);;JPEG Images (*.jpg);;TIFF Images (*.tif)"));
    if (filePath.isEmpty())
        return;

    QImage tempImg = m_viewer->getBuffer().getDisplayImage(
        ImageBuffer::Display_Linear);
    if (tempImg.format() != QImage::Format_RGB32 &&
        tempImg.format() != QImage::Format_ARGB32) {
        tempImg = tempImg.convertToFormat(QImage::Format_ARGB32);
    }

    QPainter p(&tempImg);
    m_canvas->render(p, 1.0f, QPointF(0, 0));
    p.end();

    tempImg.save(filePath);
    m_statusLabel->setText(tr("Saved to %1").arg(filePath));
}