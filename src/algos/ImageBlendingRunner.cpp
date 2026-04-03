#include "ImageBlendingRunner.h"

#include <algorithm>
#include <cmath>

// =============================================================================
// ImageBlendingRunner
// =============================================================================

ImageBlendingRunner::ImageBlendingRunner(QObject* parent)
    : QObject(parent)
{}

// -----------------------------------------------------------------------------
// Blend the top image into the base image according to the given parameters.
//
// Supports:
//   - Multiple blend modes (Normal, Multiply, Screen, Overlay, etc.)
//   - Per-pixel intensity masking with configurable range and feathering
//   - Optional channel targeting when blending mono into color
//   - Mixed mono/color inputs (result inherits the higher channel count)
//
// Returns true on success; on failure, fills *errorMsg if non-null.
// -----------------------------------------------------------------------------
bool ImageBlendingRunner::run(const ImageBuffer&         base,
                              const ImageBuffer&         top,
                              ImageBuffer&               result,
                              const ImageBlendingParams& params,
                              QString*                   errorMsg)
{
    // -- Validate dimensions --------------------------------------------------
    if (base.width()  != top.width() ||
        base.height() != top.height()) {
        if (errorMsg)
            *errorMsg = "Dimensions mismatch between base and top images.";
        return false;
    }

    const int w     = base.width();
    const int h     = base.height();
    const int baseC = base.channels();
    const int topC  = top.channels();

    // Output inherits the higher channel count (color wins over mono).
    const int outC = std::max(baseC, topC);
    result.resize(w, h, outC);

    const float* bData   = base.data().data();
    const float* tData   = top.data().data();
    float*       outData = result.data().data();

    const float low     = params.lowRange;
    const float high    = params.highRange;
    const float feather = params.feather;
    const float opacity = params.opacity;

    const size_t numPixels = static_cast<size_t>(w) * h;

    // -- Per-pixel blending ---------------------------------------------------
    #pragma omp parallel for
    for (int i = 0; i < static_cast<int>(numPixels); ++i) {

        // Compute the top-layer pixel intensity (used for range masking).
        float topIntensity = 0.0f;
        if (topC == 3) {
            topIntensity = (tData[i * 3]
                          + tData[i * 3 + 1]
                          + tData[i * 3 + 2]) / 3.0f;
        } else {
            topIntensity = tData[i];
        }

        // Compute the range/feather mask [0..1].
        float mask = 1.0f;
        if (feather > 1e-6f) {
            const float lowM  = std::clamp(
                (topIntensity - (low  - feather)) / feather, 0.0f, 1.0f);
            const float highM = std::clamp(
                ((high + feather) - topIntensity) / feather, 0.0f, 1.0f);
            mask = lowM * highM;
        } else {
            if (topIntensity < low || topIntensity > high)
                mask = 0.0f;
        }

        const float totalAlpha = mask * opacity;

        // Blend each output channel independently.
        for (int ch = 0; ch < outC; ++ch) {
            const float bVal = (baseC == 3) ? bData[i * 3 + ch] : bData[i];
            const float tVal = (topC  == 3) ? tData[i * 3 + ch] : tData[i];

            // When blending a mono top into a color base, respect the
            // target-channel setting (0=R, 1=G, 2=B, 3=All).
            bool applyInChannel = true;
            if (baseC == 3 && topC == 1) {
                if (params.targetChannel != 3
                    && params.targetChannel != ch) {
                    applyInChannel = false;
                }
            }

            if (applyInChannel) {
                const float blended  = blendPixel(bVal, tVal, params.mode);
                const float finalVal = bVal * (1.0f - totalAlpha)
                                     + blended * totalAlpha;
                outData[i * outC + ch] = std::clamp(finalVal, 0.0f, 1.0f);
            } else {
                outData[i * outC + ch] = bVal;
            }
        }
    }

    result.setMetadata(base.metadata());
    return true;
}

// -----------------------------------------------------------------------------
// Apply a single blend-mode operation to one pair of pixel values.
// All inputs and outputs are in [0, 1].
// -----------------------------------------------------------------------------
float ImageBlendingRunner::blendPixel(float b, float t,
                                      ImageBlendingParams::BlendMode mode)
{
    switch (mode) {
    case ImageBlendingParams::Normal:
        return t;
    case ImageBlendingParams::Multiply:
        return b * t;
    case ImageBlendingParams::Screen:
        return 1.0f - (1.0f - b) * (1.0f - t);
    case ImageBlendingParams::Overlay:
        return (b < 0.5f)
            ? (2.0f * b * t)
            : (1.0f - 2.0f * (1.0f - b) * (1.0f - t));
    case ImageBlendingParams::Add:
        return std::min(1.0f, b + t);
    case ImageBlendingParams::Subtract:
        return std::max(0.0f, b - t);
    case ImageBlendingParams::Difference:
        return std::abs(b - t);
    case ImageBlendingParams::SoftLight:
        return (1.0f - 2.0f * t) * b * b + 2.0f * t * b;
    case ImageBlendingParams::HardLight:
        return (t < 0.5f)
            ? (2.0f * b * t)
            : (1.0f - 2.0f * (1.0f - b) * (1.0f - t));
    default:
        return t;
    }
}