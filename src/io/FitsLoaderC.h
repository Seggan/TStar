#ifndef FITSLOADERC_H
#define FITSLOADERC_H

/*
 * FitsLoaderC.h
 *
 * Pure-C interface for high-throughput FITS I/O and Bayer demosaicing.
 *
 * Designed to be called from C++ via FitsLoaderCWrapper. The C implementation
 * avoids Qt overhead and uses pthreads directly for parallel batch loading.
 *
 * All pixel data is stored as planar float32 in row-major order, normalised to
 * the native BITPIX range. Debayering is NOT performed here; that responsibility
 * belongs to the downstream preprocessing pipeline so that calibration frames
 * (bias, dark, flat) can be applied first.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif


/* ============================================================================
 * Data structures
 * ============================================================================ */

/**
 * @brief Holds a single FITS image loaded by the C backend.
 *
 * Memory management:
 *   - When managed == 1, data and header were allocated by this library and
 *     must be released with fitsimg_free_c().
 *   - When managed == 0, the pointers are externally owned.
 */
typedef struct
{
    float*  data;         /**< Interleaved pixel data (row-major, float32). */
    int     width;
    int     height;
    int     channels;     /**< 1 = mono / CFA, 3 = pre-demosaiced RGB.     */
    int     bitpix;       /**< Original FITS BITPIX value.                 */
    char*   header;       /**< Raw FITS header bytes (for preservation).   */
    size_t  header_size;
    int     managed;      /**< 1 = free via fitsimg_free_c(), 0 = external.*/
} FitsImage_C;


/* ============================================================================
 * Single-file loading
 * ============================================================================ */

/**
 * @brief Loads a single FITS file into a FitsImage_C structure.
 *
 * The image data is always stored as raw (un-debayered) float32 values,
 * regardless of whether a Bayer pattern keyword is present.
 *
 * @param filepath Path to the FITS file (UTF-8 encoded).
 * @param out_img  Output structure. Must be zero-initialised by the caller.
 * @return 0 on success, -1 on failure. Use fitserror_c() for details.
 */
int fitsload_single_c(const char* filepath, FitsImage_C* out_img);


/* ============================================================================
 * Parallel batch loading
 * ============================================================================ */

/**
 * @brief Loads multiple FITS files in parallel using a pthreads pool.
 *
 * @param filepaths   Array of file paths, each UTF-8 encoded.
 * @param count       Number of files in the array.
 * @param out_images  Output array of FitsImage_C structures (caller-allocated,
 *                    length must be >= count).
 * @param max_threads Maximum number of concurrent worker threads.
 * @return Number of files successfully loaded.
 */
int fitsload_batch_c(const char**  filepaths,
                     int           count,
                     FitsImage_C*  out_images,
                     int           max_threads);


/* ============================================================================
 * FITS writing
 * ============================================================================ */

/**
 * @brief Writes a FitsImage_C to disk as a float32 FITS file.
 *
 * @param filepath Destination path (UTF-8 encoded). An existing file is overwritten.
 * @param img      Source image data.
 * @return 0 on success, -1 on failure. Use fitserror_c() for details.
 */
int fitssave_c(const char* filepath, const FitsImage_C* img);


/* ============================================================================
 * Bayer demosaicing
 * ============================================================================ */

/**
 * @brief Performs nearest-neighbour Bayer demosaicing on a single-channel CFA image.
 *
 * Nearest-neighbour is chosen for speed; it is adequate for stacking where
 * the per-frame demosaicing quality is less critical than throughput.
 *
 * @param bayer_data CFA input pixel array (float32, row-major).
 * @param width      Image width in pixels.
 * @param height     Image height in pixels.
 * @param pattern    Bayer pattern string: "RGGB", "BGGR", "GRBG", or "GBRG".
 * @return Heap-allocated interleaved RGB float32 array (width * height * 3),
 *         or NULL on failure. The caller must free() the returned pointer.
 */
float* debayer_nn_c(const float* bayer_data,
                    int          width,
                    int          height,
                    const char*  pattern);


/* ============================================================================
 * Memory management and error reporting
 * ============================================================================ */

/**
 * @brief Frees all heap memory owned by a FitsImage_C structure.
 *
 * Only releases data and header when img->managed == 1. Safe to call on a
 * zero-initialised structure.
 *
 * @param img Target structure. The pointer itself is not freed.
 */
void fitsimg_free_c(FitsImage_C* img);

/**
 * @brief Returns a pointer to the last error message produced by this library.
 *
 * The returned string is valid until the next library call on any thread.
 * It is stored in a static buffer and must not be freed by the caller.
 *
 * @return Null-terminated ASCII error string.
 */
const char* fitserror_c(void);


#ifdef __cplusplus
}
#endif

#endif /* FITSLOADERC_H */