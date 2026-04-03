// ============================================================================
// SimdOps.cpp
// SIMD-accelerated pixel operations with runtime AVX2 dispatch and
// scalar fallback implementations for cross-platform compatibility.
// ============================================================================

#include "SimdOps.h"

#include <algorithm>
#include <cmath>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#endif

// ============================================================================
// Internal Scalar Implementations
// ============================================================================

namespace {

/**
 * @brief NaN-safe clamp to [0, 1].
 * NaN comparisons return false, so !(v >= 0) correctly catches NaN.
 */
static inline float safeClamp01(float v)
{
    if (!(v >= 0.0f)) return 0.0f;
    if (v > 1.0f)     return 1.0f;
    return v;
}

/**
 * @brief Evaluate the Midtone Transfer Function (MTF).
 *
 * MTF(m, x) = (m - 1) * x / ((2m - 1) * x - m)
 *
 * This function maps [0, 1] -> [0, 1] with the midtone parameter m
 * controlling the curvature. m = 0.5 is identity.
 */
static float mtf_scalar(float m, float x)
{
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;

    float numer = (m - 1.0f) * x;
    float denom = (2.0f * m - 1.0f) * x - m;
    return numer / denom;
}

/** Scalar implementation of per-channel RGB gain application. */
static void applyGainRGB_Scalar(float* data, size_t numPixels,
                                float r, float g, float b)
{
    for (size_t i = 0; i < numPixels; ++i) {
        data[i * 3 + 0] *= r;
        data[i * 3 + 1] *= g;
        data[i * 3 + 2] *= b;
    }
}

/**
 * @brief Scalar implementation of STF stretch with 8-bit output.
 *
 * For each pixel:
 *   1. Clamp input to [0, 1]
 *   2. Apply shadow/highlight normalization
 *   3. Apply MTF curve
 *   4. Optionally invert
 *   5. Quantize to 8-bit
 */
static void applySTF_Row_Scalar(const float* src, uint8_t* dst,
                                size_t numPixels,
                                const SimdOps::STFParams& params,
                                bool inverted)
{
    for (size_t i = 0; i < numPixels; ++i) {
        float vals[3];

        for (int c = 0; c < 3; ++c) {
            float v = safeClamp01(src[i * 3 + c]);
            v = (v - params.shadow[c]) * params.invRange[c];
            v = mtf_scalar(params.midtones[c], safeClamp01(v));
            if (inverted) v = 1.0f - v;
            vals[c] = safeClamp01(v);
        }

        dst[i * 3 + 0] = static_cast<uint8_t>(vals[0] * 255.0f + 0.5f);
        dst[i * 3 + 1] = static_cast<uint8_t>(vals[1] * 255.0f + 0.5f);
        dst[i * 3 + 2] = static_cast<uint8_t>(vals[2] * 255.0f + 0.5f);
    }
}

} // anonymous namespace

// ============================================================================
// Runtime CPU Feature Detection
// ============================================================================

#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) \
    && (defined(__GNUC__) || defined(__clang__))
    #define ATTRIBUTE_TARGET_AVX2 __attribute__((target("avx2")))
    static inline bool supports_avx2()
    {
        static bool supported = __builtin_cpu_supports("avx2");
        return supported;
    }
#elif (defined(__x86_64__) || defined(_M_X64)) && defined(_MSC_VER)
    #define ATTRIBUTE_TARGET_AVX2
    static inline bool supports_avx2() { return false; }
#else
    #define ATTRIBUTE_TARGET_AVX2
    [[maybe_unused]] static inline bool supports_avx2() { return false; }
#endif

// ============================================================================
// AVX2 Implementations
// ============================================================================

namespace SimdOps {

#if defined(__x86_64__) || defined(_M_X64)

/**
 * @brief AVX2-accelerated per-channel RGB gain.
 *
 * Processes 8 pixels (24 floats) per iteration using three interleaved
 * gain vectors that cycle through the R, G, B pattern across the 8-wide
 * SIMD lanes.
 */
ATTRIBUTE_TARGET_AVX2
static void applyGainRGB_AVX2(float* data, size_t numPixels,
                              float r, float g, float b)
{
    // Three gain vectors to cover the 24-float (8 pixel) interleaved pattern
    __m256 k1 = _mm256_setr_ps(r, g, b, r, g, b, r, g);
    __m256 k2 = _mm256_setr_ps(b, r, g, b, r, g, b, r);
    __m256 k3 = _mm256_setr_ps(g, b, r, g, b, r, g, b);

    size_t i = 0;
    for (; i + 8 <= numPixels; i += 8) {
        float* ptr = data + i * 3;
        __m256 v1 = _mm256_loadu_ps(ptr);
        __m256 v2 = _mm256_loadu_ps(ptr + 8);
        __m256 v3 = _mm256_loadu_ps(ptr + 16);

        _mm256_storeu_ps(ptr,      _mm256_mul_ps(v1, k1));
        _mm256_storeu_ps(ptr + 8,  _mm256_mul_ps(v2, k2));
        _mm256_storeu_ps(ptr + 16, _mm256_mul_ps(v3, k3));
    }

    // Handle remaining pixels with scalar fallback
    applyGainRGB_Scalar(data + i * 3, numPixels - i, r, g, b);
}

/**
 * @brief AVX2-accelerated STF stretch with 8-bit output.
 *
 * Processes 8 pixels (24 floats) per iteration. Each of the three 8-wide
 * vectors covers a different phase of the R,G,B interleave pattern.
 * Steps:
 *   1. NaN sanitization
 *   2. Input clamping
 *   3. Shadow/highlight normalization
 *   4. MTF rational function evaluation
 *   5. Optional inversion
 *   6. Quantization to uint8
 */
ATTRIBUTE_TARGET_AVX2
static void applySTF_Row_AVX2(const float* src, uint8_t* dst,
                              size_t numPixels,
                              const STFParams& params, bool inverted)
{
    // Build interleaved parameter vectors for the 3 phases of 8-element groups

    // Shadow (c0)
    __m256 c0_1 = _mm256_setr_ps(
        params.shadow[0], params.shadow[1], params.shadow[2],
        params.shadow[0], params.shadow[1], params.shadow[2],
        params.shadow[0], params.shadow[1]);
    __m256 c0_2 = _mm256_setr_ps(
        params.shadow[2], params.shadow[0], params.shadow[1],
        params.shadow[2], params.shadow[0], params.shadow[1],
        params.shadow[2], params.shadow[0]);
    __m256 c0_3 = _mm256_setr_ps(
        params.shadow[1], params.shadow[2], params.shadow[0],
        params.shadow[1], params.shadow[2], params.shadow[0],
        params.shadow[1], params.shadow[2]);

    // Inverse range normalization factor
    __m256 n_1 = _mm256_setr_ps(
        params.invRange[0], params.invRange[1], params.invRange[2],
        params.invRange[0], params.invRange[1], params.invRange[2],
        params.invRange[0], params.invRange[1]);
    __m256 n_2 = _mm256_setr_ps(
        params.invRange[2], params.invRange[0], params.invRange[1],
        params.invRange[2], params.invRange[0], params.invRange[1],
        params.invRange[2], params.invRange[0]);
    __m256 n_3 = _mm256_setr_ps(
        params.invRange[1], params.invRange[2], params.invRange[0],
        params.invRange[1], params.invRange[2], params.invRange[0],
        params.invRange[1], params.invRange[2]);

    // Midtone parameter (m)
    __m256 m_1 = _mm256_setr_ps(
        params.midtones[0], params.midtones[1], params.midtones[2],
        params.midtones[0], params.midtones[1], params.midtones[2],
        params.midtones[0], params.midtones[1]);
    __m256 m_2 = _mm256_setr_ps(
        params.midtones[2], params.midtones[0], params.midtones[1],
        params.midtones[2], params.midtones[0], params.midtones[1],
        params.midtones[2], params.midtones[0]);
    __m256 m_3 = _mm256_setr_ps(
        params.midtones[1], params.midtones[2], params.midtones[0],
        params.midtones[1], params.midtones[2], params.midtones[0],
        params.midtones[1], params.midtones[2]);

    // MTF denominator factor: 2*m - 1
    auto getFactor = [](float m) { return 2.0f * m - 1.0f; };

    __m256 f_1 = _mm256_setr_ps(
        getFactor(params.midtones[0]), getFactor(params.midtones[1]),
        getFactor(params.midtones[2]), getFactor(params.midtones[0]),
        getFactor(params.midtones[1]), getFactor(params.midtones[2]),
        getFactor(params.midtones[0]), getFactor(params.midtones[1]));
    __m256 f_2 = _mm256_setr_ps(
        getFactor(params.midtones[2]), getFactor(params.midtones[0]),
        getFactor(params.midtones[1]), getFactor(params.midtones[2]),
        getFactor(params.midtones[0]), getFactor(params.midtones[1]),
        getFactor(params.midtones[2]), getFactor(params.midtones[0]));
    __m256 f_3 = _mm256_setr_ps(
        getFactor(params.midtones[1]), getFactor(params.midtones[2]),
        getFactor(params.midtones[0]), getFactor(params.midtones[1]),
        getFactor(params.midtones[2]), getFactor(params.midtones[0]),
        getFactor(params.midtones[1]), getFactor(params.midtones[2]));

    // MTF numerator factor: m - 1
    auto getNum = [](float m) { return m - 1.0f; };

    __m256 num_1 = _mm256_setr_ps(
        getNum(params.midtones[0]), getNum(params.midtones[1]),
        getNum(params.midtones[2]), getNum(params.midtones[0]),
        getNum(params.midtones[1]), getNum(params.midtones[2]),
        getNum(params.midtones[0]), getNum(params.midtones[1]));
    __m256 num_2 = _mm256_setr_ps(
        getNum(params.midtones[2]), getNum(params.midtones[0]),
        getNum(params.midtones[1]), getNum(params.midtones[2]),
        getNum(params.midtones[0]), getNum(params.midtones[1]),
        getNum(params.midtones[2]), getNum(params.midtones[0]));
    __m256 num_3 = _mm256_setr_ps(
        getNum(params.midtones[1]), getNum(params.midtones[2]),
        getNum(params.midtones[0]), getNum(params.midtones[1]),
        getNum(params.midtones[2]), getNum(params.midtones[0]),
        getNum(params.midtones[1]), getNum(params.midtones[2]));

    // Constants
    __m256 vZero = _mm256_setzero_ps();
    __m256 vOne  = _mm256_set1_ps(1.0f);
    __m256 v255  = _mm256_set1_ps(255.0f);
    __m256 vHalf = _mm256_set1_ps(0.5f);

    size_t i = 0;
    for (; i + 8 <= numPixels; i += 8) {
        const float* ptr = src + i * 3;
        uint8_t*    dPtr = dst + i * 3;

        // Load 24 floats (8 pixels x 3 channels)
        __m256 v1 = _mm256_loadu_ps(ptr);
        __m256 v2 = _mm256_loadu_ps(ptr + 8);
        __m256 v3 = _mm256_loadu_ps(ptr + 16);

        // Sanitize NaN values (NaN & mask = 0)
        __m256 ord1 = _mm256_cmp_ps(v1, v1, _CMP_ORD_Q);
        __m256 ord2 = _mm256_cmp_ps(v2, v2, _CMP_ORD_Q);
        __m256 ord3 = _mm256_cmp_ps(v3, v3, _CMP_ORD_Q);
        v1 = _mm256_and_ps(v1, ord1);
        v2 = _mm256_and_ps(v2, ord2);
        v3 = _mm256_and_ps(v3, ord3);

        // Clamp input to [0, 1]
        v1 = _mm256_max_ps(vZero, _mm256_min_ps(vOne, v1));
        v2 = _mm256_max_ps(vZero, _mm256_min_ps(vOne, v2));
        v3 = _mm256_max_ps(vZero, _mm256_min_ps(vOne, v3));

        // Shadow/highlight normalization: (v - shadow) * invRange, clamped
        v1 = _mm256_max_ps(vZero, _mm256_min_ps(vOne,
            _mm256_mul_ps(_mm256_sub_ps(v1, c0_1), n_1)));
        v2 = _mm256_max_ps(vZero, _mm256_min_ps(vOne,
            _mm256_mul_ps(_mm256_sub_ps(v2, c0_2), n_2)));
        v3 = _mm256_max_ps(vZero, _mm256_min_ps(vOne,
            _mm256_mul_ps(_mm256_sub_ps(v3, c0_3), n_3)));

        // MTF: (m-1)*x / ((2m-1)*x - m)
        v1 = _mm256_div_ps(_mm256_mul_ps(v1, num_1),
            _mm256_sub_ps(_mm256_mul_ps(f_1, v1), m_1));
        v2 = _mm256_div_ps(_mm256_mul_ps(v2, num_2),
            _mm256_sub_ps(_mm256_mul_ps(f_2, v2), m_2));
        v3 = _mm256_div_ps(_mm256_mul_ps(v3, num_3),
            _mm256_sub_ps(_mm256_mul_ps(f_3, v3), m_3));

        // Optional inversion
        if (inverted) {
            v1 = _mm256_sub_ps(vOne, v1);
            v2 = _mm256_sub_ps(vOne, v2);
            v3 = _mm256_sub_ps(vOne, v3);
        }

        // Final clamp
        v1 = _mm256_max_ps(vZero, _mm256_min_ps(vOne, v1));
        v2 = _mm256_max_ps(vZero, _mm256_min_ps(vOne, v2));
        v3 = _mm256_max_ps(vZero, _mm256_min_ps(vOne, v3));

        // Quantize to 8-bit: round(v * 255)
        __m256i i1 = _mm256_cvtps_epi32(
            _mm256_add_ps(_mm256_mul_ps(v1, v255), vHalf));
        __m256i i2 = _mm256_cvtps_epi32(
            _mm256_add_ps(_mm256_mul_ps(v2, v255), vHalf));
        __m256i i3 = _mm256_cvtps_epi32(
            _mm256_add_ps(_mm256_mul_ps(v3, v255), vHalf));

        // Store to aligned temporary and copy with clamping
        alignas(32) int32_t tempI[24];
        _mm256_store_si256((__m256i*)tempI,        i1);
        _mm256_store_si256((__m256i*)(tempI + 8),  i2);
        _mm256_store_si256((__m256i*)(tempI + 16), i3);

        for (int k = 0; k < 24; ++k) {
            dPtr[k] = static_cast<uint8_t>(
                std::min(std::max(tempI[k], 0), 255));
        }
    }

    // Handle remaining pixels with scalar fallback
    applySTF_Row_Scalar(src + i * 3, dst + i * 3,
                        numPixels - i, params, inverted);
}

#endif // x86_64

// ============================================================================
// Public Dispatchers
// ============================================================================

void applyGainRGB(float* data, size_t numPixels, float r, float g, float b)
{
#if defined(__x86_64__) || defined(_M_X64)
    if (supports_avx2()) {
        applyGainRGB_AVX2(data, numPixels, r, g, b);
        return;
    }
#endif
    applyGainRGB_Scalar(data, numPixels, r, g, b);
}

void applySTF_Row(const float* src, uint8_t* dst, size_t numPixels,
                  const STFParams& params, bool inverted)
{
#if defined(__x86_64__) || defined(_M_X64)
    if (supports_avx2()) {
        applySTF_Row_AVX2(src, dst, numPixels, params, inverted);
        return;
    }
#endif
    applySTF_Row_Scalar(src, dst, numPixels, params, inverted);
}

} // namespace SimdOps