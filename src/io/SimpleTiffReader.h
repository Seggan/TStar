#ifndef SIMPLETIFFREADER_H
#define SIMPLETIFFREADER_H

#include <QString>
#include <vector>

/**
 * @brief Minimal, dependency-free TIFF reader.
 *
 * Supports uncompressed baseline TIFF files with the following
 * configurations:
 *   - Chunky (interleaved) and planar sample storage
 *   - Strip-based and tile-based image layout
 *   - 8-bit unsigned, 16-bit unsigned, 32-bit unsigned, and 32-bit float
 *     sample formats
 *
 * Compressed TIFF files (LZW, ZIP, etc.) are not supported.
 */
class SimpleTiffReader
{
public:

    /**
     * @brief Read pixel data from an uncompressed TIFF file as 32-bit floats.
     *
     * Integer sample formats are normalized to [0, 1]. Planar images are
     * converted to interleaved (chunky) layout before returning.
     *
     * @param path         Absolute path to the TIFF file.
     * @param width        Output: image width in pixels.
     * @param height       Output: image height in pixels.
     * @param channels     Output: number of samples per pixel.
     * @param data         Output: interleaved pixel data, normalized to [0, 1].
     * @param errorMsg     Optional: receives a human-readable error description.
     * @param debugInfo    Optional: receives diagnostic statistics (min, max, NaN count).
     * @return true on success.
     */
    static bool readFloat32(const QString&       path,
                            int&                 width,
                            int&                 height,
                            int&                 channels,
                            std::vector<float>&  data,
                            QString*             errorMsg  = nullptr,
                            QString*             debugInfo = nullptr);

    /**
     * @brief Read basic image parameters without decoding pixel data.
     *
     * @param path          Absolute path to the TIFF file.
     * @param width         Output: image width in pixels.
     * @param height        Output: image height in pixels.
     * @param channels      Output: number of samples per pixel.
     * @param bitsPerSample Output: bit depth of each sample.
     * @param errorMsg      Optional: receives a human-readable error description.
     * @return true on success.
     */
    static bool readInfo(const QString& path,
                         int&           width,
                         int&           height,
                         int&           channels,
                         int&           bitsPerSample,
                         QString*       errorMsg = nullptr);
};

#endif // SIMPLETIFFREADER_H