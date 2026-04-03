#ifndef XISFREADER_H
#define XISFREADER_H

#include <QString>
#include <QVariant>
#include <QMap>
#include <QFile>
#include <QList>
#include <QXmlStreamReader>
#include <vector>

#include "../ImageBuffer.h"
#include "CompressionUtils.h"

// ============================================================================
// Public data structures
// ============================================================================

/**
 * @brief Describes a single image block found inside an XISF file.
 *
 * Used by XISFReader::listImages() to report what images are available
 * before any pixel data is loaded.
 */
struct XISFImageInfo
{
    int     index;        ///< Zero-based position of this image in the file.
    QString name;         ///< Image id attribute, or a generated "Image_N" fallback.
    int     width;
    int     height;
    int     channels;
    QString sampleFormat; ///< e.g. "Float32", "UInt16", "UInt8".
    QString colorSpace;   ///< "Gray" or "RGB".
};

// ============================================================================
// XISFReader
// ============================================================================

/**
 * @brief Reader for the PixInsight XISF 1.0 file format.
 *
 * Supports:
 *   - Attached, inline (base64/hex), and embedded data blocks.
 *   - Sample formats: Float32, Float64, UInt8, UInt16, UInt32.
 *   - Planar and interleaved (normal) pixel storage.
 *   - Compression codecs via CompressionUtils (zlib, lz4, lz4hc, zstd).
 *   - Byte-shuffle pre-processing for improved compression ratios.
 *   - ICC profile extraction from attached, inline, or embedded blocks.
 *   - FITS keyword parsing and WCS metadata reconstruction.
 *   - Multi-image XISF files through listImages() / readImage().
 *
 * Only little-endian byte order is supported; big-endian files are rejected
 * with an appropriate error message.
 */
class XISFReader
{
public:

    // ------------------------------------------------------------------------
    // Public API
    // ------------------------------------------------------------------------

    /**
     * @brief Load the first image from an XISF file into @p buffer.
     *
     * This is the primary entry point for single-image files.  Pixel data,
     * metadata, FITS keywords, WCS information, and any embedded ICC profile
     * are all transferred to @p buffer on success.
     *
     * @param filePath  Absolute path to the .xisf file.
     * @param buffer    Output ImageBuffer that receives the image data.
     * @param errorMsg  Optional pointer that receives a human-readable error
     *                  description on failure.
     * @return true on success, false on failure.
     */
    static bool read(const QString& filePath,
                     ImageBuffer&   buffer,
                     QString*       errorMsg = nullptr);

    /**
     * @brief Enumerate all image blocks contained in an XISF file.
     *
     * No pixel data is read; only the XML header is parsed.
     *
     * @param filePath  Absolute path to the .xisf file.
     * @param errorMsg  Optional error output.
     * @return A list of XISFImageInfo descriptors, one per valid image block.
     */
    static QList<XISFImageInfo> listImages(const QString& filePath,
                                           QString*       errorMsg = nullptr);

    /**
     * @brief Load a specific image block from a multi-image XISF file.
     *
     * @param filePath    Absolute path to the .xisf file.
     * @param imageIndex  Zero-based index into the image list returned by
     *                    listImages().
     * @param buffer      Output ImageBuffer.
     * @param errorMsg    Optional error output.
     * @return true on success, false on failure.
     */
    static bool readImage(const QString& filePath,
                          int            imageIndex,
                          ImageBuffer&   buffer,
                          QString*       errorMsg = nullptr);

    // ------------------------------------------------------------------------
    // Public nested types
    // ------------------------------------------------------------------------

    /**
     * @brief Represents a parsed XISF Property element.
     *
     * XISF properties carry typed metadata (scalars, vectors, matrices,
     * strings, timestamps) identified by a dot-separated id string such as
     * "PCL:AstrometricSolution:ReferenceImageCoordinates".
     */
    struct XISFProperty
    {
        QString  id;
        QString  type;
        QVariant value;
        QString  format;
        QString  comment;
    };

private:

    // ========================================================================
    // Private internal header descriptor
    // ========================================================================

    /**
     * @brief All information extracted from a single XISF Image element.
     *
     * Populated by parseHeader() / parseAllImages() and consumed by
     * loadImage() to actually read and decode pixel data.
     */
    struct XISFHeaderInfo
    {
        // Image geometry
        int  width    = 0;
        int  height   = 0;
        int  channels = 1;

        // Data block location (attachment)
        long long dataLocation = 0;
        long long dataSize     = 0;

        // Sample encoding
        QString sampleFormat; ///< "Float32", "Float64", "UInt16", "UInt32", "UInt8"
        QString colorSpace;   ///< "Gray", "RGB"
        QString pixelStorage; ///< "planar" (default) or "normal" (interleaved)
        QString byteOrder;    ///< "little" (default) or "big"
        QString imageId;      ///< Image id attribute used for naming

        // Compression
        CompressionUtils::Codec compressionCodec  = CompressionUtils::Codec_None;
        long long               uncompressedSize  = 0;
        int                     shuffleItemSize   = 0;

        // Data location type
        enum LocationType { Attachment, Inline, Embedded };
        LocationType locationType  = Attachment;
        QString      inlineEncoding;  ///< "base64" or "hex" for Inline blocks
        QString      embeddedData;    ///< Raw text content for Embedded/Inline

        // Parsed image metadata (FITS keywords, WCS, etc.)
        ImageBuffer::Metadata meta;

        // XISF Property elements parsed from the Image block
        QMap<QString, XISFProperty> properties;

        // Resolution (default 72 dpi)
        double  resolutionH    = 72.0;
        double  resolutionV    = 72.0;
        QString resolutionUnit = "inch";

        // Embedded ICC profile
        bool      hasICCProfile       = false;
        long long iccLocation         = 0;
        long long iccSize             = 0;
        CompressionUtils::Codec iccCompression       = CompressionUtils::Codec_None;
        long long               iccUncompressedSize  = 0;
        int                     iccShuffleItemSize   = 0;
        QString                 iccInlineEncoding;
        QString                 iccEmbeddedData;
        LocationType            iccLocationType = Attachment;

        // Thumbnail presence flag (not loaded, only noted)
        bool hasThumbnail = false;
    };

    // ========================================================================
    // Header parsing
    // ========================================================================

    /**
     * @brief Parse the XML header of an XISF file and populate @p info with
     *        the attributes of the first Image element found.
     *
     * @param headerXml  Raw XML bytes read from the file.
     * @param info       Output descriptor to fill.
     * @param errorMsg   Optional error output.
     * @return true if at least one valid Image element was found.
     */
    static bool parseHeader(const QByteArray& headerXml,
                            XISFHeaderInfo&   info,
                            QString*          errorMsg);

    /**
     * @brief Parse all Image elements from the XML header into a list.
     *
     * Used by listImages() and readImage() to support multi-image files.
     *
     * @param headerXml  Raw XML bytes.
     * @param images     Output list, one entry per valid Image element.
     * @param errorMsg   Optional error output.
     * @return true if parsing succeeded without XML errors.
     */
    static bool parseAllImages(const QByteArray&      headerXml,
                               QList<XISFHeaderInfo>& images,
                               QString*               errorMsg);

    // ========================================================================
    // Data block I/O
    // ========================================================================

    /**
     * @brief Dispatch to the correct data-reading strategy based on
     *        @p info.locationType.
     */
    static QByteArray readDataBlock(QFile&                file,
                                    const XISFHeaderInfo& info,
                                    QString*              errorMsg);

    /**
     * @brief Read a raw data block from a file at a given byte offset.
     *
     * @param file      Open readable QFile.
     * @param position  Absolute byte offset of the data block.
     * @param size      Number of bytes to read.
     * @param errorMsg  Optional error output.
     * @return The raw bytes on success, or an empty QByteArray on failure.
     */
    static QByteArray readAttachedDataBlock(QFile&    file,
                                            long long position,
                                            long long size,
                                            QString*  errorMsg);

    /**
     * @brief Decode a base64- or hex-encoded inline data string.
     *
     * @param data      Encoded text content from the XML element.
     * @param encoding  "base64" or "hex".
     * @param errorMsg  Optional error output.
     * @return Decoded binary data, or empty on unknown encoding.
     */
    static QByteArray decodeInlineData(const QString& data,
                                       const QString& encoding,
                                       QString*       errorMsg);

    /**
     * @brief Parse and decode a Data child element from an embedded XML
     *        fragment (used for Embedded location type).
     *
     * @param embeddedXml  XML text containing a Data element.
     * @param errorMsg     Optional error output.
     * @return Decoded binary data, or empty on failure.
     */
    static QByteArray decodeEmbeddedData(const QString& embeddedXml,
                                         QString*       errorMsg);

    // ========================================================================
    // Pixel data conversion
    // ========================================================================

    /**
     * @brief Convert raw binary sample data to a float32 vector.
     *
     * Handles all supported sample formats.  Integer formats are normalised
     * to the [0, 1] range.
     *
     * @param rawData   Uncompressed, unshuffled byte array of pixel samples.
     * @param info      Header descriptor carrying geometry and format info.
     * @param outData   Output float vector (size = width * height * channels).
     * @param errorMsg  Optional error output.
     * @return true on success.
     */
    static bool convertToFloat(const QByteArray&     rawData,
                               const XISFHeaderInfo& info,
                               std::vector<float>&   outData,
                               QString*              errorMsg);

    /**
     * @brief Reorder pixel samples from planar layout to interleaved layout.
     *
     * XISF stores multi-channel images as [C0_plane][C1_plane]..., while
     * ImageBuffer expects interleaved [R G B R G B ...] ordering.
     *
     * @param planar       Input planar data (channel planes concatenated).
     * @param interleaved  Output interleaved data (pre-allocated).
     * @param width        Image width in pixels.
     * @param height       Image height in pixels.
     * @param channels     Number of colour channels.
     */
    static void planarToInterleaved(const std::vector<float>& planar,
                                    std::vector<float>&       interleaved,
                                    int width, int height, int channels);

    // ========================================================================
    // Property element parsing
    // ========================================================================

    /**
     * @brief Parse a single Property XML element into an XISFProperty.
     *
     * Advances the @p xml reader past the closing tag.
     *
     * @param xml  Stream reader positioned at the Property start element.
     * @return Populated XISFProperty; id will be empty on failure.
     */
    static XISFProperty parseProperty(QXmlStreamReader& xml);

    /**
     * @brief Convert a typed XISF property value to a QVariant.
     *
     * Scalar types map to the corresponding Qt integer/float/bool types.
     * Vector and Matrix types produce a QVariantList of numeric values.
     *
     * @param type         XISF type string (e.g. "Float64", "Vector<Float64>").
     * @param valueStr     Value attribute text.
     * @param textContent  Element text content (used for vectors/matrices).
     * @param length       Element length attribute (informational).
     * @param rows         Matrix row count attribute (informational).
     * @param columns      Matrix column count attribute (informational).
     * @return A QVariant holding the parsed value.
     */
    static QVariant parsePropertyValue(const QString& type,
                                       const QString& valueStr,
                                       const QString& textContent,
                                       int            length  = 0,
                                       int            rows    = 0,
                                       int            columns = 0);

    // ========================================================================
    // Utility helpers
    // ========================================================================

    /**
     * @brief Return the byte size of a single sample for the given format string.
     *
     * Returns 4 (Float32) for unrecognised format strings.
     *
     * @param format  XISF sample format string (e.g. "UInt16", "Float64").
     * @return Bytes per sample (1, 2, 4, or 8).
     */
    static int getSampleSize(const QString& format);

    /**
     * @brief Load pixel data and metadata from a file for a pre-parsed image
     *        descriptor, then store the result in @p buffer.
     *
     * This is the shared implementation used by both read() and readImage().
     *
     * @param file     Open readable QFile (seekable).
     * @param info     Fully populated header descriptor for the target image.
     * @param buffer   Output ImageBuffer.
     * @param errorMsg Optional error output.
     * @return true on success.
     */
    static bool loadImage(QFile&          file,
                          XISFHeaderInfo& info,
                          ImageBuffer&    buffer,
                          QString*        errorMsg);
};

#endif // XISFREADER_H