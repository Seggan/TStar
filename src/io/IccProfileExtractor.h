#ifndef ICCPROFILEEXTRACTOR_H
#define ICCPROFILEEXTRACTOR_H

#include <QString>
#include <QByteArray>

/**
 * @brief Utility class for extracting ICC color profiles from image files
 * Supports: TIFF, PNG, JPG, FITS, XISF, and RAW formats
 */
class IccProfileExtractor {
public:
    /**
     * Extract ICC profile data from an image file
     * @param filePath Path to the image file
     * @param iccData Output: the extracted ICC profile data
     * @return true if profile was found and extracted, false otherwise
     */
    static bool extractFromFile(const QString& filePath, QByteArray& iccData);

private:
    // Format-specific extractors
    static bool extractFromTiff(const QString& filePath, QByteArray& iccData);
    static bool extractFromPng(const QString& filePath, QByteArray& iccData);
    static bool extractFromJpeg(const QString& filePath, QByteArray& iccData);
    static bool extractFromFits(const QString& filePath, QByteArray& iccData);
    static bool extractFromXisf(const QString& filePath, QByteArray& iccData);
    static bool extractFromRaw(const QString& filePath, QByteArray& iccData);
};

#endif // ICCPROFILEEXTRACTOR_H
