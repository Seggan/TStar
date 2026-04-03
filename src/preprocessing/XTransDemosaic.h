/**
 * @file XTransDemosaic.h
 * @brief Demosaicing for Fujifilm X-Trans 6x6 colour filter arrays.
 *
 * Provides two algorithm choices: a Markesteijn-inspired ratio-guided
 * method and a VNG-based directional interpolation adapted for the
 * irregular X-Trans pattern.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#ifndef XTRANSDEMOSAIC_H
#define XTRANSDEMOSAIC_H

#include "../ImageBuffer.h"

#include <vector>

namespace Preprocessing {

/**
 * @brief Static utility class for X-Trans CFA demosaicing.
 */
class XTransDemosaic {
public:
    /**
     * @brief Available X-Trans demosaicing algorithms.
     */
    enum class Algorithm {
        Markesteijn,  ///< Ratio-guided interpolation with chrominance refinement
        VNG           ///< Directional gradient-weighted interpolation
    };

    /**
     * @brief Demosaic a single-channel X-Trans CFA image to RGB.
     * @param input  Single-channel CFA image.
     * @param output Three-channel RGB output image (same dimensions).
     * @param algo   Algorithm to use (default: Markesteijn).
     * @return true on success, false if the input is not single-channel.
     */
    static bool demosaic(const ImageBuffer& input, ImageBuffer& output,
                         Algorithm algo = Algorithm::Markesteijn);

private:
    /**
     * @brief Markesteijn-inspired demosaicing (3-pass: green, R/B ratio, refinement).
     */
    static void interpolateMarkesteijn(const ImageBuffer& input, ImageBuffer& output,
                                       const int pattern[6][6]);

    /**
     * @brief VNG-based demosaicing adapted for the 6x6 X-Trans layout.
     */
    static void interpolateVNG(const ImageBuffer& input, ImageBuffer& output,
                               const int pattern[6][6]);

    /**
     * @brief Return the colour type (0 = G, 1 = R, 2 = B) at position (x, y)
     *        within the repeating 6x6 X-Trans tile.
     */
    static int getPixelType(int x, int y, const int pattern[6][6]);
};

} // namespace Preprocessing

#endif // XTRANSDEMOSAIC_H