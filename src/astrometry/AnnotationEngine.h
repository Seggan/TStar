#ifndef ANNOTATION_ENGINE_H
#define ANNOTATION_ENGINE_H

#include "../widgets/AnnotationOverlay.h"
#include "../ImageBuffer.h"
#include <QVector>
#include <QString>

class AnnotationEngine {
public:
    static QVector<CatalogObject> loadCatalog(const QString& filename,
                                              const ImageBuffer::Metadata& meta,
                                              const QString& catalogType,
                                              int width, int height);

    static QVector<CatalogObject> loadConstellationLines(const QString& filename,
                                                         const ImageBuffer::Metadata& meta,
                                                         int width, int height);

private:
    static bool isPointInOrNearRect(double px, double py, int width, int height, int margin = 200);
};

#endif // ANNOTATION_ENGINE_H
