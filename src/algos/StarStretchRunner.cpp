#include "StarStretchRunner.h"

#include <cmath>
#include <vector>
#include <algorithm>
#include <iostream>

// ============================================================================
// Internal Processing Functions
// ============================================================================

// ----------------------------------------------------------------------------
// applyPixelMath
//
// Applies the star stretch rational function:
//   output = (factor * p) / (factor - 1) * p + 1)
//   where factor = 3^amount
//
// This function is equivalent to the PixInsight PixelMath star stretch formula
// and compresses the background while preserving star peak values.
// ----------------------------------------------------------------------------
static void applyPixelMath(std::vector<float>& data, float amount) {
    if (std::abs(amount) < 1e-5f) return;

    const double factor       = std::pow(3.0, amount);
    const double denom_factor = factor - 1.0;

    for (float& p : data) {
        p = std::clamp(p, 0.0f, 1.0f);
        double val = (factor * static_cast<double>(p))
                   / (denom_factor * static_cast<double>(p) + 1.0);
        p = static_cast<float>(std::max(0.0, std::min(1.0, val)));
    }
}

// ----------------------------------------------------------------------------
// applySaturation
//
// Scales per-pixel colour saturation using a simple mean-based formula:
//   C' = mean + (C - mean) * amount
//
// Using the arithmetic mean (rather than luminance) matches the reference
// implementation used in PixInsight's ColorSaturation process.
// ----------------------------------------------------------------------------
static void applySaturation(std::vector<float>& data,
                             int w, int h, int c, float amount)
{
    if (c < 3) return;
    if (std::abs(amount - 1.0f) < 1e-4f) return;

    const size_t numPixels = static_cast<size_t>(w) * h;

    for (size_t i = 0; i < numPixels; ++i) {
        float r = data[i * c + 0];
        float g = data[i * c + 1];
        float b = data[i * c + 2];

        float mean = (r + g + b) / 3.0f;

        data[i * c + 0] = std::clamp(mean + (r - mean) * amount, 0.0f, 1.0f);
        data[i * c + 1] = std::clamp(mean + (g - mean) * amount, 0.0f, 1.0f);
        data[i * c + 2] = std::clamp(mean + (b - mean) * amount, 0.0f, 1.0f);
    }
}

// ----------------------------------------------------------------------------
// applySCNR
//
// Subtractive Chromatic Noise Reduction (green channel).
// Clamps the green channel to the average of red and blue:
//   G' = min(G, 0.5 * (R + B))
//
// This suppresses the magenta-green channel imbalance common in narrowband
// and uncalibrated broadband data.
// ----------------------------------------------------------------------------
static void applySCNR(std::vector<float>& data, int w, int h, int c) {
    if (c < 3) return;

    const size_t numPixels = static_cast<size_t>(w) * h;

    for (size_t i = 0; i < numPixels; ++i) {
        float r     = data[i * c + 0];
        float g     = data[i * c + 1];
        float b     = data[i * c + 2];
        float limit = 0.5f * (r + b);

        if (g > limit) data[i * c + 1] = limit;
    }
}

// ============================================================================
// StarStretchRunner Implementation
// ============================================================================

StarStretchRunner::StarStretchRunner(QObject* parent)
    : QObject(parent)
{}

// ----------------------------------------------------------------------------
// run
//
// Pipeline:
//   1. Copy input to output.
//   2. Pixel Math stretch (if stretchAmount > 0).
//   3. Saturation boost (RGB only, if colorBoost != 1.0).
//   4. SCNR green removal (RGB only, if scnr is true).
//   5. Blend with mask if one is attached.
// ----------------------------------------------------------------------------
bool StarStretchRunner::run(const ImageBuffer& input,
                             ImageBuffer&       output,
                             const StarStretchParams& params,
                             QString* /*errorMsg*/)
{
    output = input;

    const int w = output.width();
    const int h = output.height();
    const int c = output.channels();
    auto&     data = output.data();

    // Step 1: Pixel Math stretch.
    if (params.stretchAmount > 0.0f) {
        emit processOutput("Applying star stretch (Pixel Math)...");
        applyPixelMath(data, params.stretchAmount);
    }

    // Step 2: Saturation boost (RGB only).
    if (c == 3 && std::abs(params.colorBoost - 1.0f) > 1e-4f) {
        emit processOutput("Applying colour boost...");
        applySaturation(data, w, h, c, params.colorBoost);
    }

    // Step 3: SCNR green removal (RGB only).
    if (c == 3 && params.scnr) {
        emit processOutput("Applying SCNR (green channel removal)...");
        applySCNR(data, w, h, c);
    }

    // Step 4: Blend with mask if one is attached to the buffer.
    if (output.hasMask()) {
        output.blendResult(input);
    }

    return true;
}