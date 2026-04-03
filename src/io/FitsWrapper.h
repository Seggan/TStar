#ifndef FITS_IO_H
#define FITS_IO_H

/**
 * @file FitsWrapper.h
 * @brief Unified FITS I/O facade for the stacking module.
 *
 * Provides a thin, interface-compatible wrapper around FitsLoader so that
 * stacking code can call read/write operations without depending directly on
 * FitsLoader internals.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#include "../ImageBuffer.h"
#include "FitsLoader.h"

#include <QString>

namespace Stacking {

/**
 * @brief Stateless FITS I/O facade used by the stacking pipeline.
 *
 * All methods are static and delegate to FitsLoader. The class exists to
 * provide a consistent interface compatible with TiffIO and other I/O adapters
 * in the same namespace.
 */
class FitsIO
{
public:

    /**
     * @brief Loads the primary image HDU of a FITS file into an ImageBuffer.
     * @param path   Absolute path to the FITS file.
     * @param buffer Output ImageBuffer.
     * @return true on success.
     */
    static bool read(const QString& path, ImageBuffer& buffer)
    {
        FitsLoader loader;
        return loader.load(path, buffer);
    }

    /**
     * @brief Reads only the FITS header metadata without loading pixel data.
     * @param path   Absolute path to the FITS file.
     * @param buffer Output ImageBuffer (metadata populated; pixels not allocated).
     * @return true on success.
     */
    static bool readHeader(const QString& path, ImageBuffer& buffer)
    {
        return FitsLoader::loadMetadata(path, buffer);
    }

    /**
     * @brief Loads a rectangular sub-region from the primary FITS HDU.
     * @param path    Absolute path to the FITS file.
     * @param buffer  Output ImageBuffer.
     * @param x       Left edge of the region (0-based).
     * @param y       Top edge of the region (0-based).
     * @param width   Region width in pixels.
     * @param height  Region height in pixels.
     * @param channel Reserved; unused (pass -1 to read all channels).
     * @return true on success.
     */
    static bool readRegion(const QString& path,
                            ImageBuffer&   buffer,
                            int x, int y, int width, int height,
                            int channel = -1)
    {
        Q_UNUSED(channel)
        return FitsLoader::loadRegion(path, buffer, x, y, width, height);
    }

    /**
     * @brief Saves an ImageBuffer to a FITS file.
     * @param path     Destination path. An existing file is overwritten.
     * @param buffer   Source image data.
     * @param bitDepth Output bit depth: 16 for 16-bit integer, 32 for float32.
     * @return true on success.
     */
    static bool write(const QString&      path,
                       const ImageBuffer&  buffer,
                       int                 bitDepth = 32)
    {
        const ImageBuffer::BitDepth depth =
            (bitDepth == 16) ? ImageBuffer::Depth_16Int
                             : ImageBuffer::Depth_32Float;
        return buffer.save(path, "FITS", depth);
    }
};

} // namespace Stacking

#endif // FITS_IO_H