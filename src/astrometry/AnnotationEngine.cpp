#include "AnnotationEngine.h"
#include "WCSUtils.h"

#include <QFile>
#include <QTextStream>
#include <QStringList>

// ============================================================================
// Boundary test
// ============================================================================

bool AnnotationEngine::isPointInOrNearRect(double px, double py,
                                           int width, int height, int margin)
{
    if (px < -margin || px > width  + margin) return false;
    if (py < -margin || py > height + margin) return false;
    return true;
}

// ============================================================================
// loadCatalog
// ============================================================================

QVector<CatalogObject> AnnotationEngine::loadCatalog(const QString& filename,
                                                     const ImageBuffer::Metadata& meta,
                                                     const QString& catalogType,
                                                     int width, int height)
{
    QVector<CatalogObject> objects;

    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return objects;
    }

    QTextStream in(&file);
    bool headerSkipped = false;

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;

        // Skip the first non-empty line (CSV header row)
        if (!headerSkipped) {
            headerSkipped = true;
            continue;
        }

        QStringList parts = line.split(',');
        if (parts.size() < 3) continue;

        // -- Parse common fields ------------------------------------------
        CatalogObject obj;
        QLocale cLocale = QLocale::c();

        obj.name = parts[0].trimmed();
        obj.ra   = cLocale.toDouble(parts[1]);
        obj.dec  = cLocale.toDouble(parts[2]);

        if (parts.size() >= 4) obj.diameter = cLocale.toDouble(parts[3]);
        if (parts.size() >= 5) obj.mag      = cLocale.toDouble(parts[4]);

        // -- Parse alias (column position depends on catalog format) ------
        //
        // CSV column layouts:
        //   stars.csv : name, ra, dec, pmra, pmdec, mag, alias   (7 columns)
        //   messier   : name, ra, dec, diameter, mag, alias       (6 columns)
        //   pgc.csv   : name, ra, dec, diameter, mag, alias,
        //               minorDiameter, anglePA                    (8 columns)
        if (catalogType == "Star" && parts.size() >= 7) {
            obj.alias = parts[6].trimmed();
        } else if (parts.size() >= 6) {
            obj.alias = parts[5].trimmed();
        }

        // -- Parse PGC-specific ellipse columns ---------------------------
        if (parts.size() >= 8) {
            obj.minorDiameter = cLocale.toDouble(parts[6]);
            obj.anglePA       = cLocale.toDouble(parts[7]);
        }

        obj.type     = catalogType;
        obj.longType = catalogType;

        // -- Project (RA, Dec) to pixel coordinates -----------------------
        double px, py;
        if (WCSUtils::worldToPixel(meta, obj.ra, obj.dec, px, py)) {
            if (isPointInOrNearRect(px, py, width, height)) {
                obj.pixelX = px;
                obj.pixelY = py;
                objects.append(obj);
            }
        }
    }

    return objects;
}

// ============================================================================
// loadConstellationLines
// ============================================================================

QVector<CatalogObject> AnnotationEngine::loadConstellationLines(
    const QString& filename,
    const ImageBuffer::Metadata& meta,
    int width, int height)
{
    QVector<CatalogObject> lines;

    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return lines;
    }

    QTextStream in(&file);
    bool headerSkipped = false;

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;

        // Skip the CSV header row
        if (!headerSkipped) {
            headerSkipped = true;
            continue;
        }

        QStringList parts = line.split(',');
        if (parts.size() < 4) continue;

        // -- Build a line-segment catalog object --------------------------
        CatalogObject obj;
        obj.isLine   = true;
        obj.type     = "Constellation";
        obj.longType = "Constellation";

        obj.ra     = parts[0].toDouble();
        obj.dec    = parts[1].toDouble();
        obj.raEnd  = parts[2].toDouble();
        obj.decEnd = parts[3].toDouble();

        // -- Project both endpoints to pixel coordinates ------------------
        double px1, py1, px2, py2;
        bool ok1 = WCSUtils::worldToPixel(meta, obj.ra,    obj.dec,    px1, py1);
        bool ok2 = WCSUtils::worldToPixel(meta, obj.raEnd, obj.decEnd, px2, py2);

        if (ok1 && ok2) {
            // Include the segment if either endpoint is visible
            if (isPointInOrNearRect(px1, py1, width, height) ||
                isPointInOrNearRect(px2, py2, width, height))
            {
                obj.pixelX    = px1;
                obj.pixelY    = py1;
                obj.pixelXEnd = px2;
                obj.pixelYEnd = py2;
                lines.append(obj);
            }
        }
    }

    return lines;
}