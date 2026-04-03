#ifndef ICCPROFILEEXTRACTOR_H
#define ICCPROFILEEXTRACTOR_H

#include <QString>
#include <QByteArray>

/**
 * @brief Extracts embedded ICC colour profiles from a variety of image file formats.
 *
 * Supported formats:
 *   - TIFF  : IFD tag 34675 (ICC_PROFILE).
 *   - PNG   : iCCP chunk (zlib-compressed).
 *   - JPEG  : APP2 markers with the ICC_PROFILE identifier.
 *   - FITS  : IMAGE HDU with EXTNAME "ICC_PROFILE" or "ICCPROFILE".
 *   - XISF  : Inline base64-encoded ICCProfile XML element.
 *   - RAW   : Camera RAW formats via LibRaw (requires HAVE_LIBRAW).
 *
 * All methods are stateless and thread-safe.
 */
class IccProfileExtractor
{
public:

    /**
     * @brief Extracts an ICC profile from an image file.
     *
     * The appropriate format-specific extractor is selected automatically based
     * on the file extension (case-insensitive).
     *
     * @param filePath Absolute path to the image file.
     * @param iccData  Output: raw ICC profile bytes. Unchanged on failure.
     * @return true if a profile was found and successfully extracted.
     */
    static bool extractFromFile(const QString& filePath, QByteArray& iccData);

private:

    /**
     * @brief Extracts an ICC profile from a TIFF file via IFD tag 34675.
     */
    static bool extractFromTiff(const QString& filePath, QByteArray& iccData);

    /**
     * @brief Extracts an ICC profile from a PNG file via the iCCP chunk.
     *
     * The embedded profile data is zlib-compressed and is decompressed before
     * being returned.
     */
    static bool extractFromPng(const QString& filePath, QByteArray& iccData);

    /**
     * @brief Extracts an ICC profile from a JPEG file via APP2 markers.
     *
     * Handles multi-segment ICC profiles (reassembled in index order).
     */
    static bool extractFromJpeg(const QString& filePath, QByteArray& iccData);

    /**
     * @brief Extracts an ICC profile from a FITS file via a dedicated IMAGE HDU.
     *
     * Searches all HDUs for one with EXTNAME == "ICC_PROFILE" or "ICCPROFILE"
     * and reads its pixel data as raw bytes.
     */
    static bool extractFromFits(const QString& filePath, QByteArray& iccData);

    /**
     * @brief Extracts an ICC profile from an XISF file via the ICCProfile element.
     *
     * Currently supports inline base64-encoded profiles only.
     */
    static bool extractFromXisf(const QString& filePath, QByteArray& iccData);

    /**
     * @brief Extracts an ICC profile from a camera RAW file via LibRaw.
     *
     * Only available when HAVE_LIBRAW is defined at build time.
     */
    static bool extractFromRaw(const QString& filePath, QByteArray& iccData);
};

#endif // ICCPROFILEEXTRACTOR_H