#include "AnnotationEngine.h"
#include "WCSUtils.h"
#include <QFile>
#include <QTextStream>
#include <QStringList>

bool AnnotationEngine::isPointInOrNearRect(double px, double py, int width, int height, int margin) {
    if (px < -margin || px > width + margin) return false;
    if (py < -margin || py > height + margin) return false;
    return true;
}

QVector<CatalogObject> AnnotationEngine::loadCatalog(const QString& filename,
                                                     const ImageBuffer::Metadata& meta,
                                                     const QString& catalogType,
                                                     int width, int height) {
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
        
        if (!headerSkipped) {
            headerSkipped = true;
            continue; // Skip CSV header
        }
        
        QStringList parts = line.split(',');
        if (parts.size() >= 3) {
            CatalogObject obj;
            obj.name = parts[0].trimmed();
            obj.ra = parts[1].toDouble();
            obj.dec = parts[2].toDouble();
            
            if (parts.size() >= 4) obj.diameter = parts[3].toDouble();
            if (parts.size() >= 5) obj.mag = parts[4].toDouble();
            
            // For stars.csv: name,ra,dec,pmra,pmdec,mag,alias (7 columns)
            // For others like messier.csv: name,ra,dec,diameter,mag,alias (6 columns)
            if (catalogType == "Star" && parts.size() >= 7) {
                obj.alias = parts[6].trimmed();
            } else if (parts.size() >= 6) {
                obj.alias = parts[5].trimmed();
            }
            
            obj.type = catalogType;
            obj.longType = catalogType;
            
            // Project to pixels
            double px, py;
            if (WCSUtils::worldToPixel(meta, obj.ra, obj.dec, px, py)) {
                if (isPointInOrNearRect(px, py, width, height)) {
                    obj.pixelX = px;
                    obj.pixelY = py;
                    objects.append(obj);
                }
            }
        }
    }
    
    return objects;
}

QVector<CatalogObject> AnnotationEngine::loadConstellationLines(const QString& filename,
                                                                const ImageBuffer::Metadata& meta,
                                                                int width, int height) {
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
        
        if (!headerSkipped) {
            headerSkipped = true;
            continue; // Skip CSV header
        }
        
        QStringList parts = line.split(',');
        if (parts.size() >= 4) {
            CatalogObject obj;
            obj.isLine = true;
            obj.type = "Constellation";
            obj.longType = "Constellation";
            
            obj.ra = parts[0].toDouble();
            obj.dec = parts[1].toDouble();
            obj.raEnd = parts[2].toDouble();
            obj.decEnd = parts[3].toDouble();
            
            // Project to pixels
            double px1, py1, px2, py2;
            bool ok1 = WCSUtils::worldToPixel(meta, obj.ra, obj.dec, px1, py1);
            bool ok2 = WCSUtils::worldToPixel(meta, obj.raEnd, obj.decEnd, px2, py2);
            
            if (ok1 && ok2) {
                // Keep if either end is inside the rect
                if (isPointInOrNearRect(px1, py1, width, height) || 
                    isPointInOrNearRect(px2, py2, width, height)) {
                    obj.pixelX = px1;
                    obj.pixelY = py1;
                    obj.pixelXEnd = px2;
                    obj.pixelYEnd = py2;
                    lines.append(obj);
                }
            }
        }
    }
    
    return lines;
}
