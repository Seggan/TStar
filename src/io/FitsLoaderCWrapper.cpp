/**
 * @file FitsLoaderCWrapper.cpp
 * @brief C++ wrapper implementation: bridges the C FITS backend to ImageBuffer.
 */

#include "FitsLoaderCWrapper.h"
#include "FitsLoaderC.h"
#include "FitsLoader.h"

#include <QDebug>
#include <cstring>

namespace IO {

// =============================================================================
// Single-file load
// =============================================================================

bool FitsLoaderCWrapper::loadFitsC(const QString& filepath, ImageBuffer& out_buffer)
{
    // Convert the Qt string to a null-terminated UTF-8 byte array that remains
    // valid for the duration of the C function call.
    const QByteArray pathBytes = filepath.toUtf8();

    FitsImage_C c_img;
    memset(&c_img, 0, sizeof(c_img));

    // Delegate to the optimised C loader (direct CFITSIO, no Qt overhead).
    if (fitsload_single_c(pathBytes.constData(), &c_img) != 0)
    {
        qWarning() << "FitsLoaderC error:" << QString::fromUtf8(fitserror_c());
        return false;
    }

    // Copy the pixel data into a std::vector owned by the ImageBuffer.
    std::vector<float> bufferData;
    if (c_img.data)
    {
        const size_t totalSamples =
            static_cast<size_t>(c_img.width) *
            static_cast<size_t>(c_img.height) *
            static_cast<size_t>(c_img.channels);

        bufferData.assign(c_img.data, c_img.data + totalSamples);
    }

    out_buffer.setData(c_img.width, c_img.height, c_img.channels, bufferData);

    // Populate metadata via the standard C++ path (header parsing, WCS, etc.).
    FitsLoader::loadMetadata(filepath, out_buffer);

    fitsimg_free_c(&c_img);
    return true;
}


// =============================================================================
// Parallel batch load
// =============================================================================

int FitsLoaderCWrapper::loadFitsBatchC(const QStringList&       filepaths,
                                        std::vector<ImageBuffer>& out_buffers,
                                        int                      max_threads)
{
    const int count = filepaths.size();
    if (count <= 0)
        return 0;

    // Build a stable array of UTF-8 byte arrays and a corresponding pointer
    // array for the C API. Both must outlive the fitsload_batch_c() call.
    std::vector<QByteArray>    pathBytes;
    std::vector<const char*>   cFilepaths;

    pathBytes.reserve(count);
    cFilepaths.reserve(count);

    for (const QString& path : filepaths)
        pathBytes.push_back(path.toUtf8());

    for (const QByteArray& bytes : pathBytes)
        cFilepaths.push_back(bytes.constData());

    // Allocate the C image array (zero-initialised via calloc).
    FitsImage_C* c_images =
        static_cast<FitsImage_C*>(calloc(static_cast<size_t>(count),
                                          sizeof(FitsImage_C)));
    if (!c_images)
        return 0;

    // Load all files in parallel using the pthreads pool.
    const int loaded = fitsload_batch_c(
        cFilepaths.data(), count, c_images, max_threads);

    // Convert successfully loaded C images to ImageBuffers.
    out_buffers.clear();
    out_buffers.reserve(count);

    for (int i = 0; i < count; i++)
    {
        if (c_images[i].data == nullptr)
            continue;

        const size_t totalSamples =
            static_cast<size_t>(c_images[i].width)    *
            static_cast<size_t>(c_images[i].height)   *
            static_cast<size_t>(c_images[i].channels);

        std::vector<float> bufferData(
            c_images[i].data, c_images[i].data + totalSamples);

        ImageBuffer img_buf;
        img_buf.setData(c_images[i].width,
                         c_images[i].height,
                         c_images[i].channels,
                         bufferData);

        // Populate metadata for this file via the standard C++ path.
        FitsLoader::loadMetadata(filepaths[i], img_buf);

        out_buffers.push_back(std::move(img_buf));
    }

    // Release all C-side allocations.
    for (int i = 0; i < count; i++)
        fitsimg_free_c(&c_images[i]);

    free(c_images);
    return loaded;
}


// =============================================================================
// Error reporting
// =============================================================================

QString FitsLoaderCWrapper::getLastError()
{
    return QString::fromUtf8(fitserror_c());
}

} // namespace IO