#ifndef ANNOTATION_ENGINE_H
#define ANNOTATION_ENGINE_H

// ============================================================================
// AnnotationEngine
//
// Provides static utilities for loading astronomical catalog objects and
// constellation line segments from CSV files, projecting them onto image
// pixel coordinates via WCS metadata.
// ============================================================================

#include "../widgets/AnnotationOverlay.h"
#include "../ImageBuffer.h"

#include <QVector>
#include <QString>

class AnnotationEngine {
public:
    // ------------------------------------------------------------------------
    // Catalog loading
    // ------------------------------------------------------------------------

    /**
     * Loads point-like catalog objects (stars, DSOs, galaxies) from a CSV file,
     * projects each entry to pixel coordinates using the provided WCS metadata,
     * and returns only those objects that fall within (or near) the image bounds.
     *
     * @param filename     Path to the CSV catalog file.
     * @param meta         WCS metadata used for world-to-pixel projection.
     * @param catalogType  Semantic type label (e.g. "Star", "DSO", "Galaxy").
     * @param width        Image width in pixels.
     * @param height       Image height in pixels.
     * @return             Vector of CatalogObject entries visible in the image.
     */
    static QVector<CatalogObject> loadCatalog(const QString& filename,
                                              const ImageBuffer::Metadata& meta,
                                              const QString& catalogType,
                                              int width, int height);

    /**
     * Loads constellation line segments from a CSV file. Each line is defined
     * by two (RA, Dec) endpoints. Both endpoints are projected to pixel
     * coordinates; a segment is included if at least one endpoint falls
     * within (or near) the image bounds.
     *
     * @param filename  Path to the constellation-lines CSV file.
     * @param meta      WCS metadata used for world-to-pixel projection.
     * @param width     Image width in pixels.
     * @param height    Image height in pixels.
     * @return          Vector of CatalogObject entries representing line segments.
     */
    static QVector<CatalogObject> loadConstellationLines(const QString& filename,
                                                         const ImageBuffer::Metadata& meta,
                                                         int width, int height);

private:
    /**
     * Checks whether a point (px, py) lies inside the image rectangle
     * extended by a configurable margin on each side.
     *
     * @param px      X coordinate to test.
     * @param py      Y coordinate to test.
     * @param width   Image width in pixels.
     * @param height  Image height in pixels.
     * @param margin  Extension beyond the image edges (default 200 px).
     * @return        True if the point is within the extended rectangle.
     */
    static bool isPointInOrNearRect(double px, double py,
                                    int width, int height, int margin = 200);
};

#endif // ANNOTATION_ENGINE_H