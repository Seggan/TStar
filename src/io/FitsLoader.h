#ifndef FITSLOADER_H
#define FITSLOADER_H

#include <QString>
#include <QMap>
#include <QVariant>

#include "ImageBuffer.h"

/**
 * @brief Describes a single image-type HDU found within a FITS file.
 */
struct FitsExtensionInfo
{
    int     index;    ///< HDU index (0-based, as used by the FitsLoader API).
    QString name;     ///< Extension name from EXTNAME, or the index as a string if unnamed.
    int     width;
    int     height;
    int     channels;
    QString dtype;    ///< Human-readable data-type string (e.g. "float32", "int16").
    int     bitpix;   ///< Raw FITS BITPIX value.
};

/**
 * @brief Loads FITS image files into ImageBuffer objects using CFITSIO.
 *
 * Supports single-extension and multi-extension FITS files, partial (region)
 * reads, metadata-only loads, and SIP distortion coefficient extraction.
 *
 * Pixel data is always returned as normalised float32 in the range [0, 1].
 * Normalisation strategy:
 *   - Integer BITPIX (8/16/32): divided by the type maximum.
 *   - Float BITPIX with values > 1: heuristically scaled by 255 or 65535,
 *     depending on the observed maximum, to handle common ADU-valued FITS files.
 *   - Float BITPIX already in [0, 1]: returned unchanged.
 */
class FitsLoader
{
public:

    /**
     * @brief Loads the primary (first image) HDU of a FITS file.
     * @param filePath Absolute path to the FITS file.
     * @param buffer   Output ImageBuffer.
     * @param errorMsg Optional pointer to receive a human-readable error description.
     * @return true on success.
     */
    static bool load(const QString& filePath,
                     ImageBuffer&   buffer,
                     QString*       errorMsg = nullptr);

    /**
     * @brief Reads only the metadata from the primary HDU without loading pixel data.
     * @param filePath Absolute path to the FITS file.
     * @param buffer   Output ImageBuffer (metadata is populated; pixel data is not allocated).
     * @param errorMsg Optional pointer to receive a human-readable error description.
     * @return true on success.
     */
    static bool loadMetadata(const QString& filePath,
                              ImageBuffer&   buffer,
                              QString*       errorMsg = nullptr);

    /**
     * @brief Lists all image-type HDUs in a FITS file.
     * @param filePath Absolute path to the FITS file.
     * @param errorMsg Optional pointer to receive a human-readable error description.
     * @return Map of upper-case extension name to FitsExtensionInfo.
     */
    static QMap<QString, FitsExtensionInfo> listExtensions(
        const QString& filePath,
        QString*       errorMsg = nullptr);

    /**
     * @brief Loads an HDU identified by its EXTNAME or string-encoded index.
     * @param filePath     Absolute path to the FITS file.
     * @param extensionKey Extension name (case-insensitive) or numeric index string.
     * @param buffer       Output ImageBuffer.
     * @param errorMsg     Optional pointer to receive a human-readable error description.
     * @return true on success.
     */
    static bool loadExtension(const QString& filePath,
                               const QString& extensionKey,
                               ImageBuffer&   buffer,
                               QString*       errorMsg = nullptr);

    /**
     * @brief Loads an HDU identified by its 0-based integer index.
     * @param filePath  Absolute path to the FITS file.
     * @param hduIndex  0-based HDU index.
     * @param buffer    Output ImageBuffer.
     * @param errorMsg  Optional pointer to receive a human-readable error description.
     * @return true on success.
     */
    static bool loadExtension(const QString& filePath,
                               int            hduIndex,
                               ImageBuffer&   buffer,
                               QString*       errorMsg = nullptr);

    /**
     * @brief Loads a rectangular sub-region from the primary HDU.
     * @param filePath Absolute path to the FITS file.
     * @param buffer   Output ImageBuffer.
     * @param x        Left edge of the region (0-based pixel coordinate).
     * @param y        Top edge of the region (0-based pixel coordinate).
     * @param w        Region width in pixels.
     * @param h        Region height in pixels.
     * @param errorMsg Optional pointer to receive a human-readable error description.
     * @return true on success.
     */
    static bool loadRegion(const QString& filePath,
                            ImageBuffer&   buffer,
                            int x, int y, int w, int h,
                            QString*       errorMsg = nullptr);

private:

    /**
     * @brief Parses a right-ascension string (HMS or decimal degrees) to decimal degrees.
     */
    static double parseRAString(const QString& str, bool* ok = nullptr);

    /**
     * @brief Parses a declination string (DMS or decimal degrees) to decimal degrees.
     */
    static double parseDecString(const QString& str, bool* ok = nullptr);

    /**
     * @brief Reads all SIP polynomial coefficients from the current HDU header.
     * @param fptr Opaque pointer to the open fitsfile handle.
     * @param meta Metadata structure to populate.
     */
    static void readSIPCoefficients(void* fptr, ImageBuffer::Metadata& meta);

    /**
     * @brief Reads all common observational and WCS metadata from the current HDU header.
     * @param fptr Opaque pointer to the open fitsfile handle.
     * @param meta Metadata structure to populate.
     */
    static void readCommonMetadata(void* fptr, ImageBuffer::Metadata& meta);

    /**
     * @brief Core HDU loading routine, shared by all public load entry points.
     *
     * @param fptr     Opaque pointer to the open fitsfile handle, positioned at the target HDU.
     * @param hduIndex 0-based HDU index (informational only; used for error messages).
     * @param buffer   Output ImageBuffer.
     * @param errorMsg Optional pointer to receive a human-readable error description.
     * @param filePath Source file path, used for ICC profile extraction and metadata.
     * @param x        Left edge of the read region; 0 for the full image.
     * @param y        Top edge of the read region; 0 for the full image.
     * @param w        Width of the read region; 0 for the full image.
     * @param h        Height of the read region; 0 for the full image.
     * @return true on success.
     */
    static bool loadHDU(void*          fptr,
                         int            hduIndex,
                         ImageBuffer&   buffer,
                         QString*       errorMsg,
                         const QString& filePath = "",
                         int x = 0, int y = 0, int w = 0, int h = 0);
};

#endif // FITSLOADER_H