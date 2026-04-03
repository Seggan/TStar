#ifndef CATALOGGRADIENTEXTRACTOR_H
#define CATALOGGRADIENTEXTRACTOR_H

// ============================================================================
// CatalogGradientExtractor.h
// Reference-based background gradient extraction using HiPS survey imagery.
// Isolates large-scale sky gradients by comparing the target against a
// reference survey image and fitting a polynomial to clean sky regions.
// ============================================================================

#include "../ImageBuffer.h"

#include <functional>
#include <atomic>

namespace Background {

/**
 * @brief Extracts background gradients using a reference survey image.
 *
 * Compares the target image against a spatially aligned reference (e.g. from
 * a HiPS sky survey) to separate genuine sky gradients from nebular/stellar
 * signal. The gradient map can be subtracted from the target to flatten the
 * background while preserving astrophysical detail.
 */
class CatalogGradientExtractor {
public:

    /** Configuration options for gradient extraction. */
    struct Options {
        int  blurScale         = 64;     ///< Gaussian sigma for large-scale extraction (pixels)
        bool protectStars      = true;   ///< Apply morphological filter to suppress stars before blurring
        bool outputGradientMap = false;  ///< If true, output the gradient map instead of the corrected image
    };

    /**
     * @brief Perform catalog-based background gradient extraction.
     *
     * @param target     Image to correct (modified in-place, or replaced with gradient map).
     * @param reference  Aligned reference image from HiPS survey (mono or RGB).
     * @param opts       Extraction configuration.
     * @param progress   Optional progress callback receiving percentage [0..100].
     * @param cancelFlag Optional atomic flag to request cancellation.
     * @return true if extraction completed successfully.
     */
    static bool extract(ImageBuffer& target,
                        const ImageBuffer& reference,
                        const Options& opts,
                        std::function<void(int)> progress = nullptr,
                        std::atomic<bool>* cancelFlag = nullptr);

    /**
     * @brief Compute the gradient map without modifying the target.
     *
     * @param target     Source image to analyze.
     * @param reference  Aligned reference image.
     * @param opts       Extraction configuration.
     * @param cancelFlag Optional atomic flag to request cancellation.
     * @return Gradient map as an ImageBuffer, or invalid buffer on failure.
     */
    static ImageBuffer computeGradientMap(const ImageBuffer& target,
                                          const ImageBuffer& reference,
                                          const Options& opts,
                                          std::atomic<bool>* cancelFlag = nullptr);
};

} // namespace Background

#endif // CATALOGGRADIENTEXTRACTOR_H