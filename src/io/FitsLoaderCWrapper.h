#ifndef FITSLOADERCWRAPPER_H
#define FITSLOADERCWRAPPER_H

/**
 * @file FitsLoaderCWrapper.h
 * @brief C++ wrapper that bridges the C-based FitsLoaderC backend to Qt types.
 *
 * Translates between QStringList / ImageBuffer and the plain-C FitsImage_C
 * structures. Single-file loads use fitsload_single_c() directly; batch loads
 * delegate to fitsload_batch_c() which uses a pthreads pool for parallelism,
 * avoiding Qt-thread overhead for high-volume stacking operations.
 */

#include <QString>
#include <QStringList>
#include <vector>

#include "../ImageBuffer.h"

namespace IO {

class FitsLoaderCWrapper
{
public:

    /**
     * @brief Loads a single FITS file using the optimised C backend.
     *
     * Pixel data is loaded via CFITSIO without Qt overhead. Metadata is
     * subsequently populated using the standard FitsLoader::loadMetadata() path.
     *
     * @param filepath   Absolute path to the FITS file.
     * @param out_buffer Output ImageBuffer; any existing content is replaced.
     * @return true on success.
     */
    static bool loadFitsC(const QString& filepath, ImageBuffer& out_buffer);

    /**
     * @brief Loads multiple FITS files in parallel using a pthreads pool.
     *
     * Significantly faster than sequential Qt-based loading for large batches
     * (e.g. stacking pipelines with hundreds of calibration frames).
     *
     * @param filepaths   List of absolute file paths.
     * @param out_buffers Output vector; populated with one ImageBuffer per
     *                    successfully loaded file, in input order.
     * @param max_threads Maximum number of concurrent worker threads.
     * @return Number of files successfully loaded.
     */
    static int loadFitsBatchC(const QStringList&       filepaths,
                               std::vector<ImageBuffer>& out_buffers,
                               int                      max_threads);

    /**
     * @brief Returns the error message from the most recent C backend operation.
     * @return Human-readable error description, or "No error" when no error occurred.
     */
    static QString getLastError();
};

} // namespace IO

#endif // FITSLOADERCWRAPPER_H