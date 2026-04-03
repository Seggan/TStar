#ifndef SIMPLETIFFWRITER_H
#define SIMPLETIFFWRITER_H

#include <QByteArray>
#include <QString>
#include <vector>

/**
 * @brief Minimal, dependency-free TIFF writer.
 *
 * Writes uncompressed baseline TIFF files in chunky (interleaved) sample
 * storage with little-endian byte order. Optionally embeds an ICC color
 * profile using TIFF tag 34675.
 *
 * Supported output sample formats:
 *   - 8-bit unsigned integer  (Format_uint8)
 *   - 16-bit unsigned integer (Format_uint16)
 *   - 32-bit unsigned integer (Format_uint32)
 *   - 32-bit IEEE float       (Format_float32)
 */
class SimpleTiffWriter
{
public:

    enum Format
    {
        Format_uint8,
        Format_uint16,
        Format_uint32,
        Format_float32
    };

    /**
     * @brief Write an image to a TIFF file.
     *
     * @param filename  Output file path.
     * @param width     Image width in pixels.
     * @param height    Image height in pixels.
     * @param channels  Number of samples per pixel (1 = mono, 3 = RGB).
     * @param fmt       Output sample format.
     * @param data      Interleaved pixel data normalized to [0, 1].
     * @param iccData   Optional ICC profile bytes to embed (may be empty).
     * @param errorMsg  Optional: receives a human-readable error description.
     * @return true on success.
     */
    static bool write(const QString&             filename,
                      int                        width,
                      int                        height,
                      int                        channels,
                      Format                     fmt,
                      const std::vector<float>&  data,
                      const QByteArray&          iccData  = QByteArray(),
                      QString*                   errorMsg = nullptr);
};

#endif // SIMPLETIFFWRITER_H