#ifndef ANNOTATION_OVERLAY_H
#define ANNOTATION_OVERLAY_H

#include <QWidget>
#include <QPainter>
#include <QMouseEvent>
#include <QVector>
#include <QString>
#include <QPointer>

class ImageViewer;

// ---------------------------------------------------------------------------
// AnnotationType
// Enumerates the kinds of shapes or labels that can be drawn as annotations.
// ---------------------------------------------------------------------------
enum class AnnotationType {
    Circle,
    Rectangle,
    Arrow,
    Text,
    WCSObject   // Automatically generated from astrometric catalog data
};

// ---------------------------------------------------------------------------
// Annotation
// Represents a single user-drawn or programmatically placed annotation.
// Coordinates are stored in image-pixel space.
// ---------------------------------------------------------------------------
struct Annotation {
    AnnotationType type;
    QPointF        start;           // Origin point in image coordinates
    QPointF        end;             // Terminal point (arrows, rectangles)
    double         radius  = 0;     // Circle radius in image pixels
    QString        text;            // Label string (text annotations, WCS labels)
    QColor         color   = Qt::yellow;
    int            penWidth = 2;
    bool           visible  = true;
};

// ---------------------------------------------------------------------------
// CatalogObject
// Describes a single entry loaded from an astronomical catalog (Messier, NGC,
// IC, star catalogs, constellation lines, etc.).
// ---------------------------------------------------------------------------
struct CatalogObject {
    double  ra              = 0;    // Right ascension (degrees)
    double  dec             = 0;    // Declination (degrees)
    QString name;
    QString alias;
    QString type;
    QString longType;
    double  diameter        = 0;    // Major axis diameter (arcmin)
    double  minorDiameter   = 0;    // Minor axis diameter (arcmin)
    double  anglePA         = 0;    // Position angle, measured East of North (degrees)
    double  mag             = 99;
    double  redshift        = 0;
    double  pmra            = 0;    // Proper motion in RA (mas/yr)
    double  pmdec           = 0;    // Proper motion in Dec (mas/yr)
    double  pixelX          = 0;
    double  pixelY          = 0;

    // Fields used for constellation line segments
    double  raEnd           = 0;
    double  decEnd          = 0;
    double  pixelXEnd       = 0;
    double  pixelYEnd       = 0;
    bool    isLine          = false;
};

// ---------------------------------------------------------------------------
// AnnotationOverlay
// A transparent QWidget rendered on top of an ImageViewer that draws:
//   - User-created annotations (circles, rectangles, arrows, text)
//   - WCS catalog objects projected onto the image
//   - An equatorial coordinate grid (RA/Dec lines)
//   - An astronomical compass (North/East arrows)
//
// Mouse events are forwarded to the underlying ImageViewer when the overlay
// is in DrawMode::None via Qt::WA_TransparentForMouseEvents.
// ---------------------------------------------------------------------------
class AnnotationOverlay : public QWidget {
    Q_OBJECT

public:
    // Drawing modes available to the user
    enum class DrawMode {
        None,
        Circle,
        Rectangle,
        Arrow,
        Text
    };

    // Compass anchor position relative to the image
    enum class CompassPosition {
        Center      = 0,
        TopLeft     = 1,
        TopRight    = 2,
        BottomLeft  = 3,
        BottomRight = 4
    };

    explicit AnnotationOverlay(ImageViewer* parent);
    ~AnnotationOverlay();

    // -- Draw mode control ------------------------------------------------
    void      setDrawMode(DrawMode mode);
    DrawMode  drawMode()  const { return m_drawMode; }

    void      setDrawColor(const QColor& color) { m_drawColor = color; }
    QColor    drawColor()  const { return m_drawColor; }

    // Text to place when the user clicks in Text mode
    void      setPendingText(const QString& text) { m_pendingText = text; }
    QString   pendingText() const { return m_pendingText; }

    // -- WCS catalog object overlay ---------------------------------------
    void setWCSObjectsVisible(bool visible);
    bool wcsObjectsVisible() const { return m_wcsObjectsVisible; }

    void setWCSObjects(const QVector<CatalogObject>& objects);
    const QVector<CatalogObject>& wcsObjects() const { return m_wcsObjects; }

    // -- Equatorial grid overlay ------------------------------------------
    void setWCSGridVisible(bool visible);
    bool wcsGridVisible() const { return m_wcsGridVisible; }

    // -- Astronomical compass overlay -------------------------------------
    void           setCompassVisible(bool visible);
    bool           compassVisible()  const { return m_compassVisible; }
    void           setCompassPosition(CompassPosition pos);
    CompassPosition compassPosition() const { return m_compassPosition; }

    // -- Manual annotation management ------------------------------------
    void clearManualAnnotations();
    void clearWCSAnnotations();

    const QVector<Annotation>& annotations() const { return m_annotations; }
    void setAnnotations(const QVector<Annotation>& annotations);
    void addAnnotation(const Annotation& ann);
    void placeTextAt(const QPointF& imagePos, const QString& text, const QColor& color);

    // Render all overlays directly into a painter for image export (burn-in)
    void renderToPainter(QPainter& painter, const QRectF& imageRect);

signals:
    // Emitted before an annotation is appended (used by undo system)
    void aboutToAddAnnotation();
    // Emitted after an annotation has been appended
    void annotationAdded(const Annotation& ann);
    // Emitted when the user clicks in Text mode to request a label dialog
    void textInputRequested(const QPointF& imagePos);
    void requestCatalogQuery();

protected:
    void paintEvent(QPaintEvent* event)         override;
    void mousePressEvent(QMouseEvent* event)    override;
    void mouseMoveEvent(QMouseEvent* event)     override;
    void mouseReleaseEvent(QMouseEvent* event)  override;

private:
    // -- Rendering helpers ------------------------------------------------
    void drawAnnotation(QPainter& painter, const Annotation& ann);
    void drawArrow(QPainter& painter, const Annotation& ann, const QPen& pen);
    void drawWCSObjects(QPainter& painter);
    void drawWCSGrid(QPainter& painter);
    void drawWCSGridToImage(QPainter& painter, const QRectF& imageRect, double scaleM);
    void drawCompass(QPainter& painter);
    void drawCompassToImage(QPainter& painter, const QRectF& imageRect, double scaleM);

    // -- Coordinate mapping -----------------------------------------------
    QPointF mapToImage(const QPointF& widgetPos) const;
    QPointF mapFromImage(const QPointF& imagePos) const;

    // -- Data members -----------------------------------------------------
    QPointer<ImageViewer> m_viewer;

    DrawMode  m_drawMode   = DrawMode::None;
    QColor    m_drawColor  = Qt::yellow;
    QString   m_pendingText = "Label";

    // Interactive drawing state
    bool    m_isDrawing   = false;
    QPointF m_drawStart;
    QPointF m_drawCurrent;

    // Manual annotations
    QVector<Annotation> m_annotations;

    // WCS catalog data and visibility flags
    QVector<CatalogObject> m_wcsObjects;
    bool           m_wcsObjectsVisible = false;
    bool           m_wcsGridVisible    = false;
    bool           m_compassVisible    = false;
    CompassPosition m_compassPosition  = CompassPosition::Center;
};

#endif // ANNOTATION_OVERLAY_H