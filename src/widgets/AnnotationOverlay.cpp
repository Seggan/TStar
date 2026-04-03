#include "AnnotationOverlay.h"
#include "ImageViewer.h"
#include "../astrometry/WCSUtils.h"

#include <QPainter>
#include <QMouseEvent>
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QTextStream>
#include <cmath>

// ---------------------------------------------------------------------------
// formatStarName
// Converts a Bayer-designation prefix (e.g. "Alp", "Bet") found at the
// beginning of a star name into the corresponding lowercase Greek Unicode
// character, producing display-ready labels such as "alpha Cen".
// ---------------------------------------------------------------------------
static QString formatStarName(QString name)
{
    if (name.isEmpty()) return name;

    struct GreekMap { const char* lat; const char* grk; };

    static const GreekMap greekTable[] = {
        {"Alp", "\u03B1"}, {"Bet", "\u03B2"}, {"Gam", "\u03B3"}, {"Del", "\u03B4"},
        {"Eps", "\u03B5"}, {"Zet", "\u03B6"}, {"Eta", "\u03B7"}, {"The", "\u03B8"},
        {"Iot", "\u03B9"}, {"Kap", "\u03BA"}, {"Lam", "\u03BB"}, {"Mu",  "\u03BC"},
        {"Nu",  "\u03BD"}, {"Xi",  "\u03BE"}, {"Omi", "\u03BF"}, {"Pi",  "\u03C0"},
        {"Rho", "\u03C1"}, {"Sig", "\u03C3"}, {"Tau", "\u03C4"}, {"Ups", "\u03C5"},
        {"Phi", "\u03C6"}, {"Chi", "\u03C7"}, {"Psi", "\u03C8"}, {"Ome", "\u03C9"},
        {nullptr, nullptr}
    };

    QString pretty = name;
    for (const auto* p = greekTable; p->lat != nullptr; ++p) {
        if (pretty.startsWith(p->lat)) {
            // Replace only the leading Bayer prefix
            pretty.replace(0, 3, p->grk);
            break;
        }
    }
    return pretty;
}

// ---------------------------------------------------------------------------
// compassAnchorInImage
// Returns the image-space anchor point for the compass rose given the
// selected CompassPosition.  A margin of ~1.5x the arm length is applied so
// the arrowheads always remain fully inside the image bounds.
// ---------------------------------------------------------------------------
static QPointF compassAnchorInImage(AnnotationOverlay::CompassPosition pos, int w, int h)
{
    const double margin = h / 13.0;

    switch (pos) {
        case AnnotationOverlay::CompassPosition::TopLeft:     return QPointF(margin,        margin);
        case AnnotationOverlay::CompassPosition::TopRight:    return QPointF(w - margin,    margin);
        case AnnotationOverlay::CompassPosition::BottomLeft:  return QPointF(margin,        h - margin);
        case AnnotationOverlay::CompassPosition::BottomRight: return QPointF(w - margin,    h - margin);
        default: /* Center */                                 return QPointF(w / 2.0,       h / 2.0);
    }
}

// ===========================================================================
// Construction / Destruction
// ===========================================================================

AnnotationOverlay::AnnotationOverlay(ImageViewer* parent)
    : QWidget(parent)
    , m_viewer(parent)
{
    setAttribute(Qt::WA_TranslucentBackground, true);
    setMouseTracking(true);

    // Initialise to None, which also sets WA_TransparentForMouseEvents
    setDrawMode(DrawMode::None);
}

AnnotationOverlay::~AnnotationOverlay()
{
    // Write a brief diagnostic entry so crash reports can confirm clean teardown
    QFile logFile(QDir::homePath() + "/TStar_annotation_debug.log");
    if (logFile.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&logFile);
        out << QDateTime::currentDateTime().toString("hh:mm:ss.zzz")
            << " | [AnnotationOverlay::~AnnotationOverlay] Destroying overlay with "
            << m_annotations.size() << " annotations\n";
        logFile.close();
    }
}

// ===========================================================================
// Public Interface
// ===========================================================================

void AnnotationOverlay::setDrawMode(DrawMode mode)
{
    m_drawMode = mode;

    // Disable mouse event interception when no tool is active so that
    // pan/zoom interactions reach the underlying ImageViewer unchanged.
    setAttribute(Qt::WA_TransparentForMouseEvents, mode == DrawMode::None);

    update();
}

void AnnotationOverlay::setWCSObjectsVisible(bool visible)
{
    m_wcsObjectsVisible = visible;
    update();
}

void AnnotationOverlay::setWCSObjects(const QVector<CatalogObject>& objects)
{
    m_wcsObjects = objects;
    update();
}

void AnnotationOverlay::clearManualAnnotations()
{
    m_annotations.clear();
    update();
}

void AnnotationOverlay::clearWCSAnnotations()
{
    m_wcsObjects.clear();
    update();
}

void AnnotationOverlay::setAnnotations(const QVector<Annotation>& annotations)
{
    m_annotations = annotations;
    update();
}

void AnnotationOverlay::addAnnotation(const Annotation& ann)
{
    emit aboutToAddAnnotation();    // Notify undo system before mutation
    m_annotations.append(ann);
    emit annotationAdded(ann);
    update();
}

void AnnotationOverlay::placeTextAt(const QPointF& imagePos,
                                    const QString&  text,
                                    const QColor&   color)
{
    // Note: aboutToAddAnnotation is NOT emitted here because the calling
    // dialog has already emitted it before invoking this method.
    Annotation ann;
    ann.type     = AnnotationType::Text;
    ann.start    = imagePos;
    ann.text     = text;
    ann.color    = color;
    ann.penWidth = 2;
    ann.visible  = true;

    m_annotations.append(ann);
    emit annotationAdded(ann);
    update();
}

void AnnotationOverlay::setWCSGridVisible(bool visible)
{
    if (m_wcsGridVisible != visible) {
        m_wcsGridVisible = visible;
        update();
    }
}

void AnnotationOverlay::setCompassVisible(bool visible)
{
    if (m_compassVisible != visible) {
        m_compassVisible = visible;
        update();
    }
}

void AnnotationOverlay::setCompassPosition(CompassPosition pos)
{
    if (m_compassPosition != pos) {
        m_compassPosition = pos;
        update();
    }
}

// ===========================================================================
// WCS Grid Drawing
// ===========================================================================

void AnnotationOverlay::drawWCSGrid(QPainter& painter)
{
    if (!m_viewer) return;

    auto meta = m_viewer->getBuffer().metadata();
    if (!WCSUtils::hasValidWCS(meta)) return;

    painter.save();

    QPen gridPen(QColor(100, 150, 255, 120), 1, Qt::DashLine);
    painter.setPen(gridPen);
    painter.setRenderHint(QPainter::Antialiasing);

    // Determine the field-of-view diagonal to choose an appropriate grid step
    const int w = m_viewer->getBuffer().width();
    const int h = m_viewer->getBuffer().height();

    double fovX, fovY;
    if (!WCSUtils::getFieldOfView(meta, w, h, fovX, fovY)) {
        painter.restore();
        return;
    }

    const double range = std::sqrt(fovX * fovX + fovY * fovY) * 0.5;

    // Select a Dec grid step that keeps the grid density visually comfortable
    double step;
    if      (range > 16.0) step = 8.0;
    else if (range >  8.0) step = 4.0;
    else if (range >  4.0) step = 2.0;
    else if (range >  2.0) step = 1.0;
    else if (range >  1.0) step = 0.5;
    else if (range >  0.5) step = 0.25;
    else if (range >  0.3) step = 1.0 / 6.0;
    else                   step = 1.0 / 12.0;

    // Determine the center RA/Dec to scale the RA step by cos(Dec)
    double centerRa, centerDec;
    WCSUtils::pixelToWorld(meta, w / 2.0, h / 2.0, centerRa, centerDec);

    static const double ra_values[] = {
        45, 30, 15, 10, 7.5, 5, 3.75, 2.5, 1.5, 1.25,
        1, 0.75, 0.5, 0.25, 0.16666, 0.125, 0.08333, 0.0625, 0.04166, 0.025, 0.02083
    };

    const double decRad = centerDec * M_PI / 180.0;
    const double step2  = std::min(45.0, step / (std::cos(decRad) + 1e-6));

    double stepRA = 45.0;
    for (double val : ra_values) {
        if (val < step2) {
            stepRA = val;
            break;
        }
    }

    // Compute RA/Dec bounding box of the image corners
    double cRa[4], cDec[4];
    WCSUtils::pixelToWorld(meta, 0, 0, cRa[0], cDec[0]);
    WCSUtils::pixelToWorld(meta, w, 0, cRa[1], cDec[1]);
    WCSUtils::pixelToWorld(meta, w, h, cRa[2], cDec[2]);
    WCSUtils::pixelToWorld(meta, 0, h, cRa[3], cDec[3]);

    double minRa  = cRa[0],  maxRa  = cRa[0];
    double minDec = cDec[0], maxDec = cDec[0];
    for (int i = 1; i < 4; ++i) {
        if (cRa[i]  < minRa)  minRa  = cRa[i];
        if (cRa[i]  > maxRa)  maxRa  = cRa[i];
        if (cDec[i] < minDec) minDec = cDec[i];
        if (cDec[i] > maxDec) maxDec = cDec[i];
    }

    // Handle images that wrap around RA 0/360
    if (maxRa - minRa > 180.0) { minRa = 0.0; maxRa = 360.0; }
    minRa  -= stepRA; maxRa  += stepRA;
    minDec -= step;   maxDec += step;
    minDec = std::max(-90.0, minDec);
    maxDec = std::min( 90.0, maxDec);

    QFont font = painter.font();
    font.setPointSize(8);
    painter.setFont(font);

    // Lambda: trace one meridian (isRA=true) or parallel (isRA=false) and draw its label
    auto drawPath = [&](bool isRA, double constantVal, double startVal, double endVal)
    {
        QPainterPath path;
        bool    first       = true;
        int     pts         = 50;
        QPointF borderPoint;
        bool    foundBorder = false;

        for (int i = 0; i <= pts; ++i) {
            const double v   = startVal + (endVal - startVal) * (i / (double)pts);
            const double ra  = isRA ? constantVal : v;
            const double dec = isRA ? v : constantVal;

            double px, py;
            if (!WCSUtils::worldToPixel(meta, ra, dec, px, py)) continue;

            QPointF widgetPt = mapFromImage(QPointF(px, py));

            if (first) { path.moveTo(widgetPt); first = false; }
            else         path.lineTo(widgetPt);

            // Record a border-adjacent point for label placement
            if (!foundBorder &&
                widgetPt.x() > 5 && widgetPt.x() < width() - 50 &&
                widgetPt.y() > 5 && widgetPt.y() < height() - 20)
            {
                borderPoint  = widgetPt;
                foundBorder  = true;
            }
        }

        painter.drawPath(path);

        // Draw a coordinate label near the edge if zoom is sufficient
        if (foundBorder && m_viewer->zoomFactor() > 0.05) {
            QString labelText;
            if (isRA) {
                const double hVal    = constantVal / 15.0;
                const int    hours   = (int)hVal;
                const double mRemain = (hVal - hours) * 60.0;
                const int    mins    = (int)mRemain;

                if (stepRA < 0.25) {
                    const int secs = (int)((mRemain - mins) * 60.0 + 0.5);
                    labelText = QString("%1h %2m %3s")
                        .arg(hours)
                        .arg(mins, 2, 10, QChar('0'))
                        .arg(secs, 2, 10, QChar('0'));
                } else {
                    labelText = QString("%1h %2m")
                        .arg(hours)
                        .arg(mins, 2, 10, QChar('0'));
                }
            } else {
                if (step < 0.1) {
                    const double arcmin = std::abs(constantVal - (int)constantVal) * 60.0;
                    labelText = QString("%1 deg %2'")
                        .arg((int)constantVal)
                        .arg((int)(arcmin + 0.5), 2, 10, QChar('0'));
                } else {
                    labelText = QString("%1 deg").arg(constantVal, 0, 'f', 1);
                }
            }

            painter.setPen(QColor(150, 200, 255, 200));
            painter.drawText(
                QRectF(borderPoint.x() + 3, borderPoint.y() + 3, 100, 20),
                Qt::AlignLeft | Qt::AlignTop,
                labelText
            );
            painter.setPen(gridPen);
        }
    };

    // Draw RA meridians
    const double startRa = std::floor(minRa / stepRA) * stepRA;
    for (double ra = startRa; ra <= maxRa; ra += stepRA) {
        double r = ra;
        while (r <    0.0) r += 360.0;
        while (r >= 360.0) r -= 360.0;
        drawPath(true, r, minDec, maxDec);
    }

    // Draw Dec parallels
    const double startDec = std::floor(minDec / step) * step;
    for (double dec = startDec; dec <= maxDec; dec += step) {
        drawPath(false, dec, minRa, maxRa);
    }

    painter.restore();
}

// ===========================================================================
// Coordinate Mapping
// ===========================================================================

QPointF AnnotationOverlay::mapToImage(const QPointF& widgetPos) const
{
    if (!m_viewer) return widgetPos;
    return m_viewer->mapToScene(widgetPos.toPoint());
}

QPointF AnnotationOverlay::mapFromImage(const QPointF& imagePos) const
{
    if (!m_viewer) return imagePos;
    return m_viewer->mapFromScene(imagePos);
}

// ===========================================================================
// Paint Event
// ===========================================================================

void AnnotationOverlay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    if (!m_viewer) return;

    // Skip rendering while the viewer is executing a heavy operation (e.g.
    // stretch or resize) to avoid accessing a partially-updated buffer.
    if (property("isProcessing").toBool()) return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Equatorial coordinate grid
    if (m_wcsGridVisible && WCSUtils::hasValidWCS(m_viewer->getBuffer().metadata()))
        drawWCSGrid(painter);

    // Astronomical compass rose
    if (m_compassVisible && WCSUtils::hasValidWCS(m_viewer->getBuffer().metadata()))
        drawCompass(painter);

    // WCS catalog objects
    if (m_wcsObjectsVisible)
        drawWCSObjects(painter);

    // User-created manual annotations
    for (const auto& ann : m_annotations) {
        if (ann.visible)
            drawAnnotation(painter, ann);
    }

    // Preview of the shape currently being drawn by the user
    if (m_isDrawing && m_drawMode != DrawMode::None && m_drawMode != DrawMode::Text) {
        const QPointF startWidget   = mapFromImage(m_drawStart);
        const QPointF currentWidget = mapFromImage(m_drawCurrent);

        switch (m_drawMode) {
            case DrawMode::Circle: {
                const double radius = std::sqrt(
                    std::pow(currentWidget.x() - startWidget.x(), 2) +
                    std::pow(currentWidget.y() - startWidget.y(), 2)
                );
                painter.setPen(QPen(m_drawColor, 2, Qt::DashLine));
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(startWidget, radius, radius);
                break;
            }
            case DrawMode::Rectangle: {
                painter.setPen(QPen(m_drawColor, 2, Qt::DashLine));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(QRectF(startWidget, currentWidget).normalized());
                break;
            }
            case DrawMode::Arrow: {
                painter.setPen(QPen(m_drawColor, 2, Qt::DashLine));
                painter.drawLine(startWidget, currentWidget);

                const double angle     = std::atan2(
                    currentWidget.y() - startWidget.y(),
                    currentWidget.x() - startWidget.x()
                );
                const double arrowSize = 15;
                const QPointF p1(
                    currentWidget.x() - arrowSize * std::cos(angle - M_PI / 6),
                    currentWidget.y() - arrowSize * std::sin(angle - M_PI / 6)
                );
                const QPointF p2(
                    currentWidget.x() - arrowSize * std::cos(angle + M_PI / 6),
                    currentWidget.y() - arrowSize * std::sin(angle + M_PI / 6)
                );
                painter.drawLine(currentWidget, p1);
                painter.drawLine(currentWidget, p2);
                break;
            }
            default:
                break;
        }
    }
}

// ===========================================================================
// Annotation Rendering
// ===========================================================================

void AnnotationOverlay::drawAnnotation(QPainter& painter, const Annotation& ann)
{
    painter.setPen(QPen(ann.color, ann.penWidth));
    painter.setBrush(Qt::NoBrush);

    const QPointF startWidget = mapFromImage(ann.start);
    const QPointF endWidget   = mapFromImage(ann.end);

    switch (ann.type) {
        case AnnotationType::Circle: {
            const double radiusWidget = ann.radius * m_viewer->zoomFactor();
            painter.drawEllipse(startWidget, radiusWidget, radiusWidget);
            break;
        }
        case AnnotationType::Rectangle: {
            painter.drawRect(QRectF(startWidget, endWidget).normalized());
            break;
        }
        case AnnotationType::Arrow: {
            painter.drawLine(startWidget, endWidget);

            const double angle     = std::atan2(
                endWidget.y() - startWidget.y(),
                endWidget.x() - startWidget.x()
            );
            const double arrowSize = 12;
            const QPointF p1(
                endWidget.x() - arrowSize * std::cos(angle - M_PI / 6),
                endWidget.y() - arrowSize * std::sin(angle - M_PI / 6)
            );
            const QPointF p2(
                endWidget.x() - arrowSize * std::cos(angle + M_PI / 6),
                endWidget.y() - arrowSize * std::sin(angle + M_PI / 6)
            );
            painter.drawLine(endWidget, p1);
            painter.drawLine(endWidget, p2);
            break;
        }
        case AnnotationType::Text: {
            QFont font = painter.font();
            font.setPointSize(12);
            painter.setFont(font);
            painter.drawText(startWidget, ann.text);
            break;
        }
        default:
            break;
    }
}

// ===========================================================================
// WCS Catalog Object Rendering (screen pass)
// ===========================================================================

void AnnotationOverlay::drawWCSObjects(QPainter& painter)
{
    if (!m_viewer) return;

    QFont font = painter.font();
    font.setPointSize(10);
    painter.setFont(font);

    const auto meta  = m_viewer->getBuffer().metadata();
    const int  imgW  = m_viewer->getBuffer().width();
    const int  imgH  = m_viewer->getBuffer().height();

    QList<QRectF> occupiedRects;  // Used for label collision avoidance

    for (const CatalogObject& obj : m_wcsObjects) {
        double px, py;
        if (!WCSUtils::worldToPixel(meta, obj.ra, obj.dec, px, py)) continue;

        // Reject objects whose projected position lies outside the image buffer
        if (!obj.isLine) {
            if (px < 0 || px >= imgW || py < 0 || py >= imgH) continue;
        }

        const QPointF imagePos(px, py);
        const QPointF widgetPos = mapFromImage(imagePos);

        // -- Constellation line segments ---------------------------------
        if (obj.isLine) {
            double pxEnd, pyEnd;
            if (!WCSUtils::worldToPixel(meta, obj.raEnd, obj.decEnd, pxEnd, pyEnd)) continue;

            const QPointF widgetPosEnd = mapFromImage(QPointF(pxEnd, pyEnd));

            painter.save();
            const QPointF p0 = mapFromImage(QPointF(0,    0));
            const QPointF p1 = mapFromImage(QPointF(imgW, imgH));
            painter.setClipRect(QRectF(p0, p1).normalized());
            painter.setPen(QPen(QColor(100, 150, 255, 180), 2));
            painter.drawLine(widgetPos, widgetPosEnd);
            painter.restore();
            continue;
        }

        // -- Catalogue colour by object type -----------------------------
        QColor color = Qt::cyan;
        if      (obj.longType == "Messier")           color = QColor(255, 200,  50);
        else if (obj.longType == "NGC")               color = QColor( 80, 180, 255);
        else if (obj.longType == "IC")                color = QColor(200, 120, 255);
        else if (obj.longType == "Sh2")               color = QColor(255,  80,  80);
        else if (obj.longType == "LdN")               color = QColor(160, 160, 160);
        else if (obj.longType == "Star")              color = QColor(255, 255, 220);
        else if (obj.type     == "Constellation")     color = QColor(120, 160, 255);

        // -- Compute screen-space radius from angular diameter -----------
        double finalRadiusScreen = 3.0;    // Default for point sources

        if (obj.diameter > 0) {
            const double pixScale = m_viewer->pixelScale();
            if (pixScale > 0) {
                const double radiusImagePx   = (obj.diameter * 30.0) / pixScale;
                finalRadiusScreen            = radiusImagePx * m_viewer->zoomFactor();

                // Reduce by 10% for objects that are partially clipped by the
                // image border, matching the export rendering path
                const QRectF markerRect(
                    widgetPos.x() - finalRadiusScreen, widgetPos.y() - finalRadiusScreen,
                    finalRadiusScreen * 2,              finalRadiusScreen * 2
                );
                const QPointF p0v = mapFromImage(QPointF(0,    0));
                const QPointF p1v = mapFromImage(QPointF(imgW, imgH));
                const QRectF  imgRectWidget = QRectF(p0v, p1v).normalized();

                if (imgRectWidget.contains(markerRect.center()) &&
                    !imgRectWidget.contains(markerRect))
                {
                    finalRadiusScreen *= 0.9;
                }
            }
        }

        const bool isConstellationName = (obj.longType == "ConstellationName");

        // Stars always use crosshair markers regardless of computed radius
        if (obj.longType == "Star") finalRadiusScreen = 3.0;

        // -- Label text preparation --------------------------------------
        QString labelText;
        if (obj.longType == "Star") {
            if (!obj.alias.isEmpty()) {
                labelText = obj.alias.split('/').first();
            } else {
                const QString namePart = formatStarName(obj.name);
                const bool isBayer = (namePart.length() > 0 &&
                    (unsigned short)namePart.at(0).unicode() >= 0x0370 &&
                    (unsigned short)namePart.at(0).unicode() <= 0x03FF);
                const bool isFlamsteed = !namePart.isEmpty() &&
                    namePart.at(0).isDigit() && namePart.contains(' ');

                labelText = (isBayer || isFlamsteed) ? namePart : "";
            }
        } else if (isConstellationName) {
            labelText = obj.name;
        } else {
            labelText = obj.name;
        }

        const bool showLabel = !labelText.isEmpty();

        // -- Minor axis radius -------------------------------------------
        double minorRadiusScreen = finalRadiusScreen;
        const bool useShape = (finalRadiusScreen > 1.0 && obj.longType != "Star");

        if (useShape && obj.minorDiameter > 0) {
            const double pixScale = m_viewer->pixelScale();
            if (pixScale > 0) {
                const double minorRadiusImagePx = (obj.minorDiameter * 30.0) / pixScale;
                minorRadiusScreen = minorRadiusImagePx * m_viewer->zoomFactor();

                const QPointF p0v = mapFromImage(QPointF(0,    0));
                const QPointF p1v = mapFromImage(QPointF(imgW, imgH));
                const QRectF  imgRectWidget = QRectF(p0v, p1v).normalized();
                const QRectF  markerRect(
                    widgetPos.x() - finalRadiusScreen, widgetPos.y() - finalRadiusScreen,
                    finalRadiusScreen * 2,              finalRadiusScreen * 2
                );
                if (imgRectWidget.contains(markerRect.center()) &&
                    !imgRectWidget.contains(markerRect))
                {
                    minorRadiusScreen *= 0.9;
                }
            }
        }

        // -- Draw marker -------------------------------------------------
        if (!isConstellationName && useShape) {
            painter.setPen(QPen(color, 1));
            painter.setBrush(Qt::NoBrush);

            if (obj.minorDiameter > 0) {
                // Rotated ellipse: account for image parity when applying the PA
                const double imgPA   = WCSUtils::positionAngle(meta);
                const double totalPA = obj.anglePA - imgPA;

                painter.save();
                painter.translate(widgetPos);
                if (WCSUtils::isParityFlipped(meta))  painter.rotate( totalPA);
                else                                   painter.rotate(-totalPA);
                painter.drawEllipse(QPointF(0, 0), minorRadiusScreen, finalRadiusScreen);
                painter.restore();
            } else {
                painter.drawEllipse(widgetPos, finalRadiusScreen, finalRadiusScreen);
            }
        } else {
            // Crosshair marker for stars and small/point sources
            const double gap = (obj.longType == "Star") ? 3.0 : 5.0;
            const double len = (obj.longType == "Star") ? 7.0 : 10.0;

            painter.setPen(QPen(color, 1));
            painter.drawLine(widgetPos - QPointF(0, gap + len), widgetPos - QPointF(0, gap));
            painter.drawLine(widgetPos + QPointF(0, gap),       widgetPos + QPointF(0, gap + len));
            painter.drawLine(widgetPos - QPointF(gap + len, 0), widgetPos - QPointF(gap, 0));
            painter.drawLine(widgetPos + QPointF(gap, 0),       widgetPos + QPointF(gap + len, 0));
        }

        // -- Draw label --------------------------------------------------
        if (showLabel) {
            const QFontMetricsF fm(painter.font());
            const QRectF textRect = fm.boundingRect(labelText);
            const double labelW   = textRect.width();
            const double labelH   = textRect.height();

            if (useShape) {
                // Place label at the upper-left of the ellipse and draw a
                // leader line from the ellipse edge to the label origin
                double labelAngleRad = -M_PI / 4.0;
                const double imgPA   = WCSUtils::positionAngle(meta);
                const double totalPA = obj.anglePA - imgPA;
                const double rotRad  = WCSUtils::isParityFlipped(meta)
                    ? (totalPA * M_PI / 180.0)
                    : (-totalPA * M_PI / 180.0);

                // Returns a point on the ellipse (or its margin) at a given screen angle
                auto getEllipsePoint = [&](double rx, double ry, double angleRad, double rot) {
                    const double localA = angleRad - rot;
                    const double x      = rx * std::cos(localA);
                    const double y      = ry * std::sin(localA);
                    const double rx_s   = x * std::cos(rot) - y * std::sin(rot);
                    const double ry_s   = x * std::sin(rot) + y * std::cos(rot);
                    return widgetPos + QPointF(rx_s, ry_s);
                };

                const double rx      = (obj.minorDiameter > 0) ? minorRadiusScreen : finalRadiusScreen;
                const double ry      = finalRadiusScreen;
                QPointF circleEdge   = getEllipsePoint(rx,        ry,        labelAngleRad, rotRad);
                QPointF textPos      = getEllipsePoint(rx * 1.15, ry * 1.15, labelAngleRad, rotRad);

                // Flip label to the opposite side if it would be clipped
                if (textPos.x() < 20 || textPos.x() > width() - labelW - 20 ||
                    textPos.y() < 20 || textPos.y() > height() - 20)
                {
                    labelAngleRad += M_PI;
                    circleEdge = getEllipsePoint(rx,        ry,        labelAngleRad, rotRad);
                    textPos    = getEllipsePoint(rx * 1.15, ry * 1.15, labelAngleRad, rotRad);
                }

                painter.setPen(QPen(color, 1));
                painter.drawLine(circleEdge, textPos);

                QPointF labelOrigin = textPos + QPointF(2, -2);

                // Prevent the label from being rendered outside the widget bounds
                if (labelOrigin.x() < 5)                       labelOrigin.setX(5);
                if (labelOrigin.x() > width()  - labelW - 5)   labelOrigin.setX(width()  - labelW - 5);
                if (labelOrigin.y() < 15)                       labelOrigin.setY(15);
                if (labelOrigin.y() > height() - 5)             labelOrigin.setY(height() - 5);

                painter.setPen(color);
                painter.drawText(labelOrigin, labelText);
                occupiedRects.append(QRectF(labelOrigin.x(), labelOrigin.y() - 15, labelW, labelH));

            } else {
                // Four candidate positions around a point-source crosshair;
                // pick the first that does not collide with existing labels
                const double offset = 10.0;

                struct Pos { QPointF p; QRectF r; };

                const QPointF pRight  = widgetPos + QPointF(offset,           labelH / 2 - 2);
                const QPointF pTop    = widgetPos + QPointF(-labelW / 2,      -offset);
                const QPointF pLeft   = widgetPos + QPointF(-offset - labelW,  labelH / 2 - 2);
                const QPointF pBottom = widgetPos + QPointF(-labelW / 2,       offset + labelH);

                std::vector<Pos> candidates = {
                    { pRight,  QRectF(pRight.x(),   pRight.y()  - labelH + 2, labelW, labelH) },
                    { pTop,    QRectF(pTop.x(),     pTop.y()    - labelH + 2, labelW, labelH) },
                    { pLeft,   QRectF(pLeft.x(),    pLeft.y()   - labelH + 2, labelW, labelH) },
                    { pBottom, QRectF(pBottom.x(),  pBottom.y() - labelH + 2, labelW, labelH) }
                };

                Pos  best  = candidates[0];
                bool found = false;

                for (const auto& cand : candidates) {
                    bool collision = (
                        cand.r.left()   <  5              ||
                        cand.r.right()  >  width()  - 5  ||
                        cand.r.top()    <  5              ||
                        cand.r.bottom() >  height() - 5
                    );

                    if (!collision) {
                        for (const auto& occupied : occupiedRects) {
                            if (cand.r.intersects(occupied)) { collision = true; break; }
                        }
                    }

                    if (!collision) { best = cand; found = true; break; }
                }

                // If no collision-free candidate exists, nudge the best one inside bounds
                if (!found) {
                    if (best.r.left()   < 5)              best.p.rx() += (5 - best.r.left());
                    if (best.r.right()  > width() - 5) {
                        best.p.rx() -= (best.r.right() - (width() - 5));
                        best.r.moveRight(width() - 5);
                    }
                    if (best.r.top()    < 5)              best.p.ry() += (5 - best.r.top());
                    if (best.r.bottom() > height() - 5) {
                        best.p.ry() -= (best.r.bottom() - (height() - 5));
                        best.r.moveBottom(height() - 5);
                    }
                }

                painter.setPen(color);
                painter.drawText(best.p, labelText);
                occupiedRects.append(best.r);
            }
        }
    }
}

// ===========================================================================
// Image Export Rendering (burn-in path)
// ===========================================================================

void AnnotationOverlay::renderToPainter(QPainter& painter, const QRectF& imageRect)
{
    if (!m_viewer) return;

    // Compute the zoom factor that corresponds to a "Fit to Window" view so
    // that all size-dependent elements scale consistently in the exported image
    const double vwrW = m_viewer->viewport()->width();
    const double vwrH = m_viewer->viewport()->height();
    const double imgW = m_viewer->getBuffer().width();
    const double imgH = m_viewer->getBuffer().height();

    const double zoomX      = vwrW / imgW;
    const double zoomY      = vwrH / imgH;
    double       fittedZoom = std::min(zoomX, zoomY) * 0.98;
    if (fittedZoom <= 0) fittedZoom = 0.1;

    // scaleM converts screen-space sizes to image-space sizes
    const double scaleM = 1.0 / fittedZoom;

    QFont font = painter.font();
    font.setPointSizeF(10.0 * scaleM);
    painter.setFont(font);

    if (m_wcsGridVisible)
        drawWCSGridToImage(painter, imageRect, scaleM);

    double pixScale = m_viewer->pixelScale();
    if (pixScale <= 0) pixScale = 1.0;

    const auto meta = m_viewer->getBuffer().metadata();

    // -- WCS catalog objects (image-space coordinates) -------------------
    if (m_wcsObjectsVisible) {
        for (const auto& obj : m_wcsObjects) {
            double tx, ty;
            if (!WCSUtils::worldToPixel(meta, obj.ra, obj.dec, tx, ty)) continue;

            const QPointF imagePos(tx, ty);

            // Loose out-of-bounds check; clipping is handled per-case below
            if (tx < -200 || tx > imageRect.width()  + 200 ||
                ty < -200 || ty > imageRect.height() + 200)
            {
                continue;
            }

            if (obj.isLine) {
                double tx2, ty2;
                if (WCSUtils::worldToPixel(meta, obj.raEnd, obj.decEnd, tx2, ty2)) {
                    painter.save();
                    painter.setClipRect(imageRect);
                    painter.setPen(QPen(QColor(100, 150, 255, 180), 2.0 * scaleM));
                    painter.drawLine(imagePos, QPointF(tx2, ty2));
                    painter.restore();
                }
                continue;
            }

            QColor color = Qt::cyan;
            if      (obj.longType == "Messier")       color = QColor(255, 200,  50);
            else if (obj.longType == "NGC")            color = QColor( 80, 180, 255);
            else if (obj.longType == "IC")             color = QColor(200, 120, 255);
            else if (obj.longType == "Sh2")            color = QColor(255,  80,  80);
            else if (obj.longType == "LdN")            color = QColor(160, 160, 160);
            else if (obj.longType == "Star")           color = QColor(255, 255, 220);
            else if (obj.type     == "Constellation")  color = QColor(120, 160, 255);

            double radiusImagePx = 3.0 * scaleM;
            if (obj.diameter > 0 && obj.longType != "Star") {
                radiusImagePx = (obj.diameter * 30.0) / pixScale;

                const QRectF markerRect(
                    imagePos.x() - radiusImagePx, imagePos.y() - radiusImagePx,
                    radiusImagePx * 2,             radiusImagePx * 2
                );
                if (imageRect.contains(markerRect.center()) && !imageRect.contains(markerRect))
                    radiusImagePx *= 0.9;
            }

            double minorRadiusImagePx = radiusImagePx;
            if (obj.minorDiameter > 0 && pixScale > 0) {
                minorRadiusImagePx = (obj.minorDiameter * 30.0) / pixScale;

                const QRectF markerRect(
                    imagePos.x() - minorRadiusImagePx, imagePos.y() - minorRadiusImagePx,
                    minorRadiusImagePx * 2,             minorRadiusImagePx * 2
                );
                if (imageRect.contains(markerRect.center()) && !imageRect.contains(markerRect))
                    minorRadiusImagePx *= 0.9;
            }

            // Marker
            if (obj.longType != "ConstellationName") {
                painter.setPen(QPen(color, 1.0 * scaleM));

                if (radiusImagePx > 5.0 * scaleM) {
                    painter.setBrush(Qt::NoBrush);

                    if (obj.minorDiameter > 0) {
                        const double imgPA   = WCSUtils::positionAngle(meta);
                        const double totalPA = obj.anglePA - imgPA;
                        painter.save();
                        painter.translate(imagePos);
                        if (WCSUtils::isParityFlipped(meta))  painter.rotate( totalPA);
                        else                                   painter.rotate(-totalPA);
                        painter.drawEllipse(QPointF(0, 0), minorRadiusImagePx, radiusImagePx);
                        painter.restore();
                    } else {
                        painter.drawEllipse(imagePos, radiusImagePx, radiusImagePx);
                    }
                } else {
                    const double gap = (obj.longType == "Star") ? 3.0 * scaleM : 5.0 * scaleM;
                    const double len = (obj.longType == "Star") ? 6.0 * scaleM : 8.0 * scaleM;
                    painter.drawLine(imagePos - QPointF(0, gap + len), imagePos - QPointF(0, gap));
                    painter.drawLine(imagePos + QPointF(0, gap),       imagePos + QPointF(0, gap + len));
                    painter.drawLine(imagePos - QPointF(gap + len, 0), imagePos - QPointF(gap, 0));
                    painter.drawLine(imagePos + QPointF(gap, 0),       imagePos + QPointF(gap + len, 0));
                }
            }

            // Label
            QString labelText;
            if (obj.longType == "Star") {
                if (!obj.alias.isEmpty()) {
                    labelText = obj.alias.split('/').first();
                } else {
                    const QString namePart = formatStarName(obj.name);
                    const bool isBayer = (namePart.length() > 0 &&
                        (unsigned short)namePart.at(0).unicode() >= 0x0370 &&
                        (unsigned short)namePart.at(0).unicode() <= 0x03FF);
                    const bool isFlamsteed = !namePart.isEmpty() &&
                        namePart.at(0).isDigit() && namePart.contains(' ');
                    if (isBayer || isFlamsteed) labelText = namePart;
                }
            } else {
                labelText = obj.name;
            }

            if (!labelText.isEmpty()) {
                painter.setPen(QPen(color, 1.0 * scaleM));

                if (radiusImagePx > 5.0 * scaleM) {
                    double labelAngleRad = -M_PI / 4.0;
                    const double imgPA   = WCSUtils::positionAngle(meta);
                    const double totalPA = obj.anglePA - imgPA;
                    const double rotRad  = WCSUtils::isParityFlipped(meta)
                        ? (totalPA * M_PI / 180.0)
                        : (-totalPA * M_PI / 180.0);

                    auto getEllipsePointImage = [&](double rx, double ry,
                                                    double lAngleRad, double rot) {
                        const double localA = lAngleRad - rot;
                        const double x      = rx * std::cos(localA);
                        const double y      = ry * std::sin(localA);
                        const double rx_s   = x * std::cos(rot) - y * std::sin(rot);
                        const double ry_s   = x * std::sin(rot) + y * std::cos(rot);
                        return imagePos + QPointF(rx_s, ry_s);
                    };

                    const double rx   = (obj.minorDiameter > 0) ? minorRadiusImagePx : radiusImagePx;
                    const double ry   = radiusImagePx;
                    QPointF circleEdge = getEllipsePointImage(rx,        ry,        labelAngleRad, rotRad);
                    QPointF tp         = getEllipsePointImage(rx * 1.15, ry * 1.15, labelAngleRad, rotRad);

                    if (tp.x() < 10 || tp.x() > imageRect.width() - 50 ||
                        tp.y() < 10 || tp.y() > imageRect.height() - 10)
                    {
                        labelAngleRad += M_PI;
                        circleEdge = getEllipsePointImage(rx,        ry,        labelAngleRad, rotRad);
                        tp         = getEllipsePointImage(rx * 1.15, ry * 1.15, labelAngleRad, rotRad);
                    }

                    if (obj.longType != "ConstellationName")
                        painter.drawLine(circleEdge, tp);

                    painter.drawText(tp + QPointF(2.0 * scaleM, -2.0 * scaleM), labelText);
                } else {
                    const QFontMetricsF ifm(painter.font());
                    painter.drawText(
                        imagePos + QPointF(10.0 * scaleM, ifm.height() / 2 - 2 * scaleM),
                        labelText
                    );
                }
            }
        }
    }

    // -- Compass rose ----------------------------------------------------
    if (m_compassVisible)
        drawCompassToImage(painter, imageRect, scaleM);

    // -- Manual annotations (already stored in image-pixel coordinates) --
    for (const auto& ann : m_annotations) {
        if (!ann.visible) continue;

        painter.setPen(QPen(ann.color, std::max(1.0, ann.penWidth * scaleM)));
        painter.setBrush(Qt::NoBrush);

        const QPointF start = ann.start;
        const QPointF end   = ann.end;

        switch (ann.type) {
            case AnnotationType::Circle: {
                painter.drawEllipse(start, ann.radius, ann.radius);
                break;
            }
            case AnnotationType::Rectangle: {
                painter.drawRect(QRectF(start, end).normalized());
                break;
            }
            case AnnotationType::Arrow: {
                painter.drawLine(start, end);
                const double angle     = std::atan2(end.y() - start.y(), end.x() - start.x());
                const double arrowSize = 12.0 * scaleM;
                const QPointF p1(
                    end.x() - arrowSize * std::cos(angle - M_PI / 6),
                    end.y() - arrowSize * std::sin(angle - M_PI / 6)
                );
                const QPointF p2(
                    end.x() - arrowSize * std::cos(angle + M_PI / 6),
                    end.y() - arrowSize * std::sin(angle + M_PI / 6)
                );
                painter.drawLine(end, p1);
                painter.drawLine(end, p2);
                break;
            }
            case AnnotationType::Text: {
                QFont f = painter.font();
                f.setPointSizeF(12.0 * scaleM);
                painter.setFont(f);
                painter.drawText(start, ann.text);
                break;
            }
            default:
                break;
        }
    }
}

// ===========================================================================
// WCS Grid Drawing (image export pass)
// ===========================================================================

void AnnotationOverlay::drawWCSGridToImage(QPainter&      painter,
                                           const QRectF&  imageRect,
                                           double         scaleM)
{
    if (!m_viewer) return;

    const auto meta = m_viewer->getBuffer().metadata();
    if (!WCSUtils::hasValidWCS(meta)) return;

    const int w = m_viewer->getBuffer().width();
    const int h = m_viewer->getBuffer().height();

    painter.save();
    painter.setClipRect(imageRect);
    painter.setRenderHint(QPainter::Antialiasing);

    QPen gridPen(QColor(100, 150, 255, 170), std::max(1.0, 1.2 * scaleM), Qt::DashLine);
    painter.setPen(gridPen);

    double fovX, fovY;
    if (!WCSUtils::getFieldOfView(meta, w, h, fovX, fovY)) {
        painter.restore();
        return;
    }

    const double range = std::sqrt(fovX * fovX + fovY * fovY) * 0.5;
    const double step  =
        (range > 16.0) ? 8.0   :
        (range >  8.0) ? 4.0   :
        (range >  4.0) ? 2.0   :
        (range >  2.0) ? 1.0   :
        (range >  1.0) ? 0.5   :
        (range >  0.5) ? 0.25  :
        (range >  0.3) ? 1.0/6.0 : 1.0/12.0;

    double cRa[4], cDec[4];
    WCSUtils::pixelToWorld(meta, 0, 0, cRa[0], cDec[0]);
    WCSUtils::pixelToWorld(meta, w, 0, cRa[1], cDec[1]);
    WCSUtils::pixelToWorld(meta, w, h, cRa[2], cDec[2]);
    WCSUtils::pixelToWorld(meta, 0, h, cRa[3], cDec[3]);

    double minRa  = cRa[0],  maxRa  = cRa[0];
    double minDec = cDec[0], maxDec = cDec[0];
    for (int i = 1; i < 4; ++i) {
        if (cRa[i]  < minRa)  minRa  = cRa[i];
        if (cRa[i]  > maxRa)  maxRa  = cRa[i];
        if (cDec[i] < minDec) minDec = cDec[i];
        if (cDec[i] > maxDec) maxDec = cDec[i];
    }
    if (maxRa - minRa > 180) { minRa = 0; maxRa = 360; }

    const double stepRA = step;  // Simplified: equal RA and Dec step for export grid

    auto drawPath = [&](bool isRA, double constantVal, double startVal, double endVal)
    {
        QPainterPath path;
        bool    first      = true;
        QPointF labelPoint;
        bool    foundLabel = false;

        for (int i = 0; i <= 72; ++i) {
            const double v = startVal + (endVal - startVal) * (i / 72.0);
            double px, py;
            if (!WCSUtils::worldToPixel(meta,
                isRA ? constantVal : v,
                isRA ? v : constantVal,
                px, py))
            {
                continue;
            }
            const QPointF p(px, py);
            if (first) { path.moveTo(p); first = false; }
            else         path.lineTo(p);

            if (!foundLabel && imageRect.adjusted(20, 20, -100, -20).contains(p)) {
                labelPoint = p;
                foundLabel = true;
            }
        }

        if (!first) painter.drawPath(path);

        if (foundLabel) {
            const QString txt = isRA
                ? QString("%1h").arg(constantVal / 15.0, 0, 'f', 1)
                : QString("%1\u00B0").arg(constantVal, 0, 'f', 1);

            painter.setPen(QColor(150, 200, 255, 215));
            painter.drawText(labelPoint + QPointF(5 * scaleM, 5 * scaleM), txt);
            painter.setPen(gridPen);
        }
    };

    QFont gFont = painter.font();
    gFont.setPointSizeF(8.0 * scaleM);
    painter.setFont(gFont);

    for (double ra  = std::floor(minRa  / stepRA) * stepRA; ra  <= maxRa;  ra  += stepRA)
        drawPath(true,  std::fmod(ra + 360.0, 360.0), minDec, maxDec);

    for (double dec = std::floor(minDec / step)   * step;   dec <= maxDec; dec += step)
        drawPath(false, dec, minRa, maxRa);

    painter.restore();
}

// ===========================================================================
// Compass Rose Drawing (screen pass)
// ===========================================================================

void AnnotationOverlay::drawCompass(QPainter& painter)
{
    if (!m_viewer) return;

    const auto meta = m_viewer->getBuffer().metadata();
    if (!WCSUtils::hasValidWCS(meta)) return;

    const int w = m_viewer->getBuffer().width();
    const int h = m_viewer->getBuffer().height();

    const QPointF anchorImg = compassAnchorInImage(m_compassPosition, w, h);

    double anchorRa, anchorDec;
    if (!WCSUtils::pixelToWorld(meta, anchorImg.x(), anchorImg.y(), anchorRa, anchorDec))
        return;

    // Reject anchor positions too close to a celestial pole
    if (90.0 - std::abs(anchorDec) < 2.78e-3) return;

    double northPixX, northPixY, eastPixX, eastPixY;
    if (!WCSUtils::worldToPixel(meta, anchorRa,       anchorDec + 0.1, northPixX, northPixY)) return;
    if (!WCSUtils::worldToPixel(meta, anchorRa + 0.1, anchorDec,       eastPixX,  eastPixY))  return;

    const QPointF centerWidgetPos = mapFromImage(anchorImg);
    const QPointF northWidgetPos  = mapFromImage(QPointF(northPixX, northPixY));
    const QPointF eastWidgetPos   = mapFromImage(QPointF(eastPixX,  eastPixY));

    const double angleN      = std::atan2(northWidgetPos.y() - centerWidgetPos.y(),
                                          northWidgetPos.x() - centerWidgetPos.x());
    const double angleE      = std::atan2(eastWidgetPos.y()  - centerWidgetPos.y(),
                                          eastWidgetPos.x()  - centerWidgetPos.x());
    const double len_scaled  = ((double)h / 20.0) * m_viewer->zoomFactor();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);

    QFont font = painter.font();
    font.setPointSizeF(len_scaled / 3.0);
    painter.setFont(font);

    const QFontMetricsF fm(font);

    // -- North arrow (red) -----------------------------------------------
    painter.save();
    painter.translate(centerWidgetPos);
    painter.rotate(angleN * 180.0 / M_PI);
    painter.setPen(QPen(Qt::red, 3.0));
    painter.setBrush(Qt::red);
    painter.drawLine(QPointF(0, 0), QPointF(len_scaled, 0));

    QPainterPath northArrow;
    northArrow.moveTo(len_scaled, 0);
    northArrow.lineTo(0.75 * len_scaled, -0.15 * len_scaled);
    northArrow.lineTo(0.75 * len_scaled,  0.15 * len_scaled);
    northArrow.closeSubpath();
    painter.drawPath(northArrow);

    painter.translate(len_scaled * 1.3, 0.1 * len_scaled);
    painter.rotate(-angleN * 180.0 / M_PI);

    const QRectF tbN = fm.boundingRect("N");
    painter.drawText(
        QRectF(-tbN.width() / 2, -tbN.height() / 2, tbN.width(), tbN.height()),
        Qt::AlignCenter, "N"
    );
    painter.restore();

    // -- East arrow (white) ----------------------------------------------
    painter.save();
    painter.translate(centerWidgetPos);
    painter.rotate(angleE * 180.0 / M_PI);
    painter.setPen(QPen(Qt::white, 3.0));
    painter.setBrush(Qt::white);
    painter.drawLine(QPointF(0, 0), QPointF(len_scaled / 2.0, 0));

    painter.translate(len_scaled, -0.1 * len_scaled);
    painter.rotate(-angleE * 180.0 / M_PI);

    const QRectF tbE = fm.boundingRect("E");
    painter.drawText(
        QRectF(-tbE.width() / 2, -tbE.height() / 2, tbE.width(), tbE.height()),
        Qt::AlignCenter, "E"
    );
    painter.restore();

    painter.restore();
}

// ===========================================================================
// Compass Rose Drawing (image export pass)
// ===========================================================================

void AnnotationOverlay::drawCompassToImage(QPainter&      painter,
                                           const QRectF&  imageRect,
                                           double         scaleM)
{
    if (!m_viewer) return;

    const auto meta = m_viewer->getBuffer().metadata();
    if (!WCSUtils::hasValidWCS(meta)) return;

    const int w = m_viewer->getBuffer().width();
    const int h = m_viewer->getBuffer().height();

    const QPointF anchorImg = compassAnchorInImage(m_compassPosition, w, h);

    double anchorRa, anchorDec;
    if (!WCSUtils::pixelToWorld(meta, anchorImg.x(), anchorImg.y(), anchorRa, anchorDec))
        return;

    if (90.0 - std::abs(anchorDec) < 2.78e-3) return;

    double northPixX, northPixY, eastPixX, eastPixY;
    if (!WCSUtils::worldToPixel(meta, anchorRa,       anchorDec + 0.1, northPixX, northPixY)) return;
    if (!WCSUtils::worldToPixel(meta, anchorRa + 0.1, anchorDec,       eastPixX,  eastPixY))  return;

    // In image-space the coordinates are already in pixel units
    const QPointF centerImagePos(anchorImg);
    const QPointF northImagePos(northPixX, northPixY);
    const QPointF eastImagePos(eastPixX, eastPixY);

    const double angleN = std::atan2(northImagePos.y() - centerImagePos.y(),
                                     northImagePos.x() - centerImagePos.x());
    const double angleE = std::atan2(eastImagePos.y()  - centerImagePos.y(),
                                     eastImagePos.x()  - centerImagePos.x());
    const double len    = (double)h / 20.0;

    painter.save();
    painter.setClipRect(imageRect);
    painter.setRenderHint(QPainter::Antialiasing);

    QFont font = painter.font();
    font.setPointSizeF(len / 3.0);
    painter.setFont(font);

    const QFontMetricsF fm(font);

    // -- North arrow (red) -----------------------------------------------
    painter.save();
    painter.translate(centerImagePos);
    painter.rotate(angleN * 180.0 / M_PI);
    painter.setPen(QPen(Qt::red, 3.0 * scaleM));
    painter.setBrush(Qt::red);
    painter.drawLine(QPointF(0, 0), QPointF(len, 0));

    QPainterPath northArrow;
    northArrow.moveTo(len, 0);
    northArrow.lineTo(0.75 * len, -0.15 * len);
    northArrow.lineTo(0.75 * len,  0.15 * len);
    northArrow.closeSubpath();
    painter.drawPath(northArrow);

    painter.translate(len * 1.3, 0.1 * len);
    painter.rotate(-angleN * 180.0 / M_PI);

    const QRectF tbN = fm.boundingRect("N");
    painter.drawText(
        QRectF(-tbN.width() / 2, -tbN.height() / 2, tbN.width(), tbN.height()),
        Qt::AlignCenter, "N"
    );
    painter.restore();

    // -- East arrow (white) ----------------------------------------------
    painter.save();
    painter.translate(centerImagePos);
    painter.rotate(angleE * 180.0 / M_PI);
    painter.setPen(QPen(Qt::white, 3.0 * scaleM));
    painter.setBrush(Qt::white);
    painter.drawLine(QPointF(0, 0), QPointF(len / 2.0, 0));

    painter.translate(len, -0.1 * len);
    painter.rotate(-angleE * 180.0 / M_PI);

    const QRectF tbE = fm.boundingRect("E");
    painter.drawText(
        QRectF(-tbE.width() / 2, -tbE.height() / 2, tbE.width(), tbE.height()),
        Qt::AlignCenter, "E"
    );
    painter.restore();

    painter.restore();
}

// ===========================================================================
// Mouse Event Handling
// ===========================================================================

void AnnotationOverlay::mousePressEvent(QMouseEvent* event)
{
    if (!m_viewer) { event->ignore(); return; }

    if (m_drawMode == DrawMode::None) { event->ignore(); return; }

    if (event->button() == Qt::LeftButton) {
        m_drawStart = mapToImage(event->pos());

        if (m_drawMode == DrawMode::Text) {
            // Delegate text input to the owning dialog
            emit textInputRequested(m_drawStart);
            event->accept();
            return;
        }

        m_isDrawing   = true;
        m_drawCurrent = m_drawStart;
        event->accept();
    } else {
        event->ignore();
    }
}

void AnnotationOverlay::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_viewer) { event->ignore(); return; }

    if (m_isDrawing) {
        m_drawCurrent = mapToImage(event->pos());
        update();
        event->accept();
    } else {
        // Passing the event up is critical for ImageViewer pan/zoom to work
        // while the mouse is hovering over the overlay without drawing active
        event->ignore();
    }
}

void AnnotationOverlay::mouseReleaseEvent(QMouseEvent* event)
{
    if (!m_viewer)    { event->ignore(); return; }
    if (!m_isDrawing) { event->ignore(); return; }

    m_isDrawing = false;

    Annotation ann;
    ann.start    = m_drawStart;
    ann.end      = m_drawCurrent;
    ann.color    = m_drawColor;
    ann.penWidth = 2;
    ann.visible  = true;

    switch (m_drawMode) {
        case DrawMode::Circle:
            ann.type   = AnnotationType::Circle;
            ann.radius = std::sqrt(
                std::pow(m_drawCurrent.x() - m_drawStart.x(), 2) +
                std::pow(m_drawCurrent.y() - m_drawStart.y(), 2)
            );
            break;
        case DrawMode::Rectangle:
            ann.type = AnnotationType::Rectangle;
            break;
        case DrawMode::Arrow:
            ann.type = AnnotationType::Arrow;
            break;
        default:
            return;
    }

    // Signal the undo system before committing the annotation
    emit aboutToAddAnnotation();
    m_annotations.append(ann);
    emit annotationAdded(ann);
    update();
    event->accept();
}