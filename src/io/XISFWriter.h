#ifndef XISFWRITER_H
#define XISFWRITER_H

#include <QString>
#include <QDomDocument>
#include <QDomElement>

#include "../ImageBuffer.h"
#include "CompressionUtils.h"

// ============================================================================
// XISFWriter
// ============================================================================

/**
 * @brief Writer for the PixInsight XISF 1.0 file format.
 *
 * Produces spec-compliant monolithic XISF files containing a single image
 * block stored as an attached data block following the XML header.
 *
 * Features:
 *   - Output sample formats: Float32 (default), UInt16, UInt32.
 *   - Planar pixel storage layout (required by PixInsight).
 *   - Little-endian byte order.
 *   - Optional data compression via CompressionUtils (zlib, lz4, lz4hc, zstd).
 *   - Optional byte shuffling prior to compression for improved ratios.
 *   - Block alignment padding between the XML header and the data block.
 *   - Preservation of all FITS keywords from ImageBuffer::Metadata.
 *   - WCS keyword output when valid astrometric data is present.
 *   - Embedded ICC colour profile output (base64 inline inside ICCProfile element).
 *   - Iterative header size stabilisation to resolve the self-referential
 *     attachment position problem.
 */
class XISFWriter
{
public:

    // ------------------------------------------------------------------------
    // Write options
    // ------------------------------------------------------------------------

    /**
     * @brief Configuration options for the write() operation.
     *
     * Default values produce an uncompressed, 4096-byte-aligned Float32 file
     * that preserves all metadata from the source ImageBuffer.
     */
    struct WriteOptions
    {
        CompressionUtils::Codec codec            = CompressionUtils::Codec_None;
        int                     compressionLevel = -1;     ///< -1 uses the codec default.
        bool                    shuffle          = false;  ///< Apply byte shuffle before compression.
        int                     blockAlignment   = 4096;   ///< Data block alignment in bytes.
        bool                    preserveHeaders  = true;   ///< Write all raw FITS header cards.
        QString                 creatorApp       = "TStar"; ///< XISF:CreatorApplication property value.
    };

    // ------------------------------------------------------------------------
    // Public API
    // ------------------------------------------------------------------------

    /**
     * @brief Write @p buffer to an XISF file using default WriteOptions.
     *
     * This convenience overload accepts the bit depth as a plain integer
     * matching the ImageBuffer::BitDepth enum values, for use from contexts
     * that do not import ImageBuffer directly.
     *
     * @param filePath  Absolute destination path for the .xisf file.
     * @param buffer    Source image data and metadata.
     * @param depth     Output bit depth (cast from ImageBuffer::BitDepth).
     * @param errorMsg  Optional pointer that receives a human-readable error
     *                  description on failure.
     * @return true on success, false on failure.
     */
    static bool write(const QString&      filePath,
                      const ImageBuffer&  buffer,
                      int                 depth,
                      QString*            errorMsg = nullptr);

    /**
     * @brief Write @p buffer to an XISF file with full control over options.
     *
     * @param filePath  Absolute destination path for the .xisf file.
     * @param buffer    Source image data and metadata.
     * @param depth     Output sample format.
     * @param options   Compression, alignment, and metadata options.
     * @param errorMsg  Optional error output.
     * @return true on success, false on failure.
     */
    static bool write(const QString&      filePath,
                      const ImageBuffer&  buffer,
                      ImageBuffer::BitDepth depth,
                      const WriteOptions& options,
                      QString*            errorMsg = nullptr);

private:

    // ------------------------------------------------------------------------
    // Header construction
    // ------------------------------------------------------------------------

    /**
     * @brief Build the complete XISF XML header as a UTF-8 byte array.
     *
     * The header contains the root xisf element, a single Image element with
     * geometry and location attributes, all FITS keyword children, an optional
     * ICCProfile element, and a Metadata element with file-level properties.
     *
     * @param buffer              Source image (geometry and metadata).
     * @param depth               Output sample format.
     * @param options             Write options.
     * @param dataSize            Byte size of the (possibly compressed) data block.
     * @param attachmentPosition  Absolute file offset of the data block.
     * @param compressionAttr     Compression attribute string, or empty if none.
     * @return XML bytes ready to write after the 16-byte XISF file header.
     */
    static QByteArray buildHeader(const ImageBuffer&    buffer,
                                  ImageBuffer::BitDepth depth,
                                  const WriteOptions&   options,
                                  quint64               dataSize,
                                  quint64               attachmentPosition,
                                  const QString&        compressionAttr);

    // ------------------------------------------------------------------------
    // Pixel data preparation
    // ------------------------------------------------------------------------

    /**
     * @brief Convert interleaved float pixel data to the target sample format
     *        arranged in planar channel order.
     *
     * XISF stores multi-channel images with all samples for channel 0
     * followed by all samples for channel 1, etc. (planar layout).
     * Integer formats are clamped and scaled from the [0, 1] float range.
     *
     * @param buffer  Source image.
     * @param depth   Target sample format.
     * @return Raw byte array ready for optional compression and writing.
     */
    static QByteArray prepareImageData(const ImageBuffer&    buffer,
                                       ImageBuffer::BitDepth depth);

    // ------------------------------------------------------------------------
    // FITS keyword output
    // ------------------------------------------------------------------------

    /**
     * @brief Append FITSKeyword child elements to the Image DOM element.
     *
     * WCS keywords are written first (when valid astrometric data is present),
     * followed by instrument/observation keywords, and finally all remaining
     * raw header cards that have not already been written.  Structural FITS
     * keywords (SIMPLE, BITPIX, NAXIS, etc.) are excluded.
     *
     * @param doc        DOM document owning all created elements.
     * @param imageElem  The Image element that receives the keyword children.
     * @param meta       Metadata carrying FITS cards, WCS data, and SIP coefficients.
     */
    static void addFITSKeywords(QDomDocument&               doc,
                                QDomElement&                imageElem,
                                const ImageBuffer::Metadata& meta);

    // ------------------------------------------------------------------------
    // Utility helpers
    // ------------------------------------------------------------------------

    /**
     * @brief Round @p pos up to the nearest multiple of @p alignment.
     *
     * Returns @p pos unchanged if @p alignment is zero or negative.
     *
     * @param pos        Byte position to align.
     * @param alignment  Block alignment in bytes (typically 4096).
     * @return Aligned position >= @p pos.
     */
    static quint64 alignPosition(quint64 pos, int alignment);

    /**
     * @brief Return the XISF sampleFormat attribute string for a given depth.
     *
     * @param depth  Target sample format.
     * @return "Float32", "UInt16", or "UInt32".
     */
    static QString sampleFormatString(ImageBuffer::BitDepth depth);

    /**
     * @brief Return the byte size of one sample for a given depth.
     *
     * @param depth  Target sample format.
     * @return 2 for UInt16, 4 for UInt32 and Float32.
     */
    static int bytesPerSample(ImageBuffer::BitDepth depth);
};

#endif // XISFWRITER_H