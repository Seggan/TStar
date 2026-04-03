#ifndef TIFF_IO_H
#define TIFF_IO_H

#include <QString>

#include "../ImageBuffer.h"
#include "SimpleTiffReader.h"
#include "SimpleTiffWriter.h"

namespace Stacking {

/**
 * @brief Unified TIFF I/O interface for the stacking module.
 *
 * Provides a consistent read / write API over SimpleTiffReader and
 * SimpleTiffWriter, isolating the rest of the stacking pipeline from
 * the lower-level format details.
 */
class TiffIO
{
public:

    /**
     * @brief Read a TIFF file into an ImageBuffer.
     *
     * @param path   Absolute path to the TIFF file.
     * @param buffer Output ImageBuffer (will be overwritten).
     * @return true on success.
     */
    static bool read(const QString& path, ImageBuffer& buffer)
    {
        int                w, h, c;
        std::vector<float> data;

        if (!SimpleTiffReader::readFloat32(path, w, h, c, data))
            return false;

        buffer.setData(w, h, c, data);
        return true;
    }

    /**
     * @brief Read basic image parameters without decoding pixel data.
     *
     * @param path     Absolute path to the TIFF file.
     * @param width    Output: image width in pixels.
     * @param height   Output: image height in pixels.
     * @param channels Output: number of channels.
     * @param bits     Output: bits per channel.
     * @return true on success.
     */
    static bool readInfo(const QString& path,
                         int&           width,
                         int&           height,
                         int&           channels,
                         int&           bits)
    {
        return SimpleTiffReader::readInfo(path, width, height, channels, bits);
    }

    /**
     * @brief Write an ImageBuffer to a TIFF file.
     *
     * @param path     Output file path.
     * @param buffer   Image to write.
     * @param bitDepth Output bit depth: 8, 16, or 32 (float).
     * @return true on success.
     */
    static bool write(const QString&      path,
                      const ImageBuffer&  buffer,
                      int                 bitDepth = 16)
    {
        SimpleTiffWriter::Format fmt = SimpleTiffWriter::Format_uint16;

        if (bitDepth == 8)
            fmt = SimpleTiffWriter::Format_uint8;
        else if (bitDepth == 32)
            fmt = SimpleTiffWriter::Format_float32;

        return SimpleTiffWriter::write(path,
                                       buffer.width(),
                                       buffer.height(),
                                       buffer.channels(),
                                       fmt,
                                       buffer.data());
    }
};

} // namespace Stacking

#endif // TIFF_IO_H