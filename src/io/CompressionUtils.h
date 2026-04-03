#ifndef COMPRESSIONUTILS_H
#define COMPRESSIONUTILS_H

#include <QByteArray>
#include <QString>
#include <cstdint>

/**
 * @brief Compression and decompression utilities for the XISF file format.
 *
 * Implements the codec set defined by the XISF 1.0 specification:
 *   - zlib   : General-purpose deflate compression (levels 1-9, default 6).
 *   - lz4    : Fast compression with no configurable level.
 *   - lz4hc  : High-compression LZ4 variant (levels 1-12, default 9).
 *   - zstd   : Zstandard compression (levels 1-22, default 3).
 *
 * Byte shuffling (byte-transposition) is also supported to improve compression
 * ratios for typed numerical data by grouping bytes of equal significance.
 */
class CompressionUtils
{
public:

    // -------------------------------------------------------------------------
    // Codec enumeration
    // -------------------------------------------------------------------------

    /**
     * @brief Identifies the compression codec to use for a data block.
     */
    enum Codec
    {
        Codec_None  = 0,
        Codec_Zlib  = 1,
        Codec_LZ4   = 2,
        Codec_LZ4HC = 3,
        Codec_Zstd  = 4
    };

    // -------------------------------------------------------------------------
    // Codec metadata
    // -------------------------------------------------------------------------

    /**
     * @brief Returns the recommended default compression level for the given codec.
     * @param codec Target codec.
     * @return Default level integer, or 0 when the codec has no level concept.
     */
    static int defaultLevel(Codec codec);

    /**
     * @brief Returns the canonical lower-case name string for the given codec.
     * @param codec Target codec.
     * @return Name string (e.g. "zlib", "lz4hc"), or an empty string for Codec_None.
     */
    static QString codecName(Codec codec);

    /**
     * @brief Parses a codec name string into the corresponding Codec enumerator.
     *
     * The "+sh" shuffle suffix, if present, is stripped before matching.
     *
     * @param name Codec name string (case-insensitive).
     * @return Matched Codec, or Codec_None when the name is unrecognised.
     */
    static Codec parseCodecName(const QString& name);

    // -------------------------------------------------------------------------
    // XISF compression attribute helpers
    // -------------------------------------------------------------------------

    /**
     * @brief Parses an XISF compression attribute string into its components.
     *
     * Accepted formats:
     *   "codec:uncompressed_size"
     *   "codec+sh:uncompressed_size:item_size"
     *
     * Examples: "zlib:123456", "lz4hc+sh:123456:4"
     *
     * @param compressionStr  Source attribute string.
     * @param codec           Output: identified codec.
     * @param uncompressedSize Output: original data size in bytes.
     * @param shuffleItemSize  Output: shuffle item size in bytes, or 0 if not shuffled.
     * @return true on success; false if the string is malformed.
     */
    static bool parseCompressionAttr(const QString& compressionStr,
                                     Codec&         codec,
                                     qint64&        uncompressedSize,
                                     int&           shuffleItemSize);

    /**
     * @brief Builds an XISF compression attribute string from its components.
     *
     * @param codec            Codec to encode.
     * @param uncompressedSize Original (pre-compression) data size in bytes.
     * @param shuffleItemSize  Item size used for byte shuffling, or 0 to omit.
     * @return Formatted attribute string, or an empty string for Codec_None.
     */
    static QString buildCompressionAttr(Codec  codec,
                                        qint64 uncompressedSize,
                                        int    shuffleItemSize = 0);

    // -------------------------------------------------------------------------
    // Compression / decompression
    // -------------------------------------------------------------------------

    /**
     * @brief Decompresses a data block using the specified codec.
     *
     * @param data             Compressed input data.
     * @param codec            Codec that was used to compress the data.
     * @param uncompressedSize Expected size of the decompressed output in bytes.
     * @param errorMsg         Optional pointer to receive a human-readable error description.
     * @return Decompressed data, or an empty QByteArray on failure.
     */
    static QByteArray decompress(const QByteArray& data,
                                 Codec             codec,
                                 qint64            uncompressedSize,
                                 QString*          errorMsg = nullptr);

    /**
     * @brief Compresses a data block using the specified codec.
     *
     * @param data     Raw input data.
     * @param codec    Codec to apply.
     * @param level    Compression level, or -1 to use the codec default.
     * @param errorMsg Optional pointer to receive a human-readable error description.
     * @return Compressed data, or an empty QByteArray on failure.
     */
    static QByteArray compress(const QByteArray& data,
                               Codec             codec,
                               int               level    = -1,
                               QString*          errorMsg = nullptr);

    // -------------------------------------------------------------------------
    // Byte shuffling
    // -------------------------------------------------------------------------

    /**
     * @brief Applies byte-level shuffling to improve compression of typed data.
     *
     * Reorders the input so that all bytes at the same offset within each
     * multi-byte element are grouped together. For example, with itemSize == 4:
     *
     *   Input:    [a0 a1 a2 a3][b0 b1 b2 b3][c0 c1 c2 c3] ...
     *   Shuffled: [a0 b0 c0 ...][a1 b1 c1 ...][a2 b2 c2 ...][a3 b3 c3 ...]
     *
     * This significantly reduces entropy for homogeneous typed arrays (e.g.
     * float32 pixel planes), resulting in better downstream compression ratios.
     *
     * @param data     Input byte array.
     * @param itemSize Size of each data element in bytes (e.g. 4 for float32).
     * @return Shuffled byte array. Returns the input unchanged if itemSize <= 1.
     */
    static QByteArray shuffle(const QByteArray& data, int itemSize);

    /**
     * @brief Reverses byte-level shuffling applied by shuffle().
     *
     * @param data     Shuffled input byte array.
     * @param itemSize Size of each original data element in bytes.
     * @return Unshuffled byte array. Returns the input unchanged if itemSize <= 1.
     */
    static QByteArray unshuffle(const QByteArray& data, int itemSize);

private:

    // -------------------------------------------------------------------------
    // Per-codec implementation helpers
    // -------------------------------------------------------------------------

    static QByteArray decompressZlib(const QByteArray& data,
                                     qint64            uncompressedSize,
                                     QString*          errorMsg);

    static QByteArray decompressLZ4(const QByteArray& data,
                                    qint64            uncompressedSize,
                                    QString*          errorMsg);

    static QByteArray decompressZstd(const QByteArray& data,
                                     qint64            uncompressedSize,
                                     QString*          errorMsg);

    static QByteArray compressZlib(const QByteArray& data,
                                   int               level,
                                   QString*          errorMsg);

    static QByteArray compressLZ4(const QByteArray& data,
                                  QString*          errorMsg);

    static QByteArray compressLZ4HC(const QByteArray& data,
                                    int               level,
                                    QString*          errorMsg);

    static QByteArray compressZstd(const QByteArray& data,
                                   int               level,
                                   QString*          errorMsg);
};

#endif // COMPRESSIONUTILS_H