#ifndef SIMDOPS_H
#define SIMDOPS_H

// ============================================================================
// SimdOps.h
// SIMD-accelerated pixel operations with runtime AVX2 dispatch.
// Provides scalar fallback implementations for all platforms.
// ============================================================================

#include <cstddef>
#include <cstdint>

namespace SimdOps {

    /**
     * @brief Parameters for Screen Transfer Function (STF) stretch.
     * All arrays follow strict [R, G, B] channel ordering.
     */
    struct STFParams {
        float shadow[3];     ///< Black point (c0) per channel
        float midtones[3];   ///< Midtone transfer parameter (m) per channel
        float invRange[3];   ///< 1.0 / (highlight - shadow) per channel
    };

    /**
     * @brief Apply per-channel gain to an interleaved RGB float buffer in-place.
     * @param data      Pointer to interleaved float RGB data.
     * @param numPixels Number of pixels (buffer contains numPixels * 3 floats).
     * @param r         Red channel gain multiplier.
     * @param g         Green channel gain multiplier.
     * @param b         Blue channel gain multiplier.
     */
    void applyGainRGB(float* data, size_t numPixels, float r, float g, float b);

    /**
     * @brief Apply STF (midtone transfer function) stretch and convert to 8-bit.
     *
     * Processes a row of interleaved float RGB pixels through the STF curve
     * and writes the result as 8-bit unsigned integers suitable for display.
     *
     * @param src       Source float RGB row.
     * @param dst       Destination 8-bit RGB row (e.g. QImage scanline).
     * @param numPixels Number of pixels in the row.
     * @param params    STF parameters (shadow, midtone, range).
     * @param inverted  If true, invert the output (1.0 - val) before quantization.
     */
    void applySTF_Row(const float* src, uint8_t* dst, size_t numPixels,
                      const STFParams& params, bool inverted);

} // namespace SimdOps

#endif // SIMDOPS_H