/**
 * @file XTransDemosaic.cpp
 * @brief Implementation of X-Trans 6x6 CFA demosaicing algorithms.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#include "XTransDemosaic.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Preprocessing {

// ============================================================================
//  Pattern Helper
// ============================================================================

int XTransDemosaic::getPixelType(int x, int y, const int pattern[6][6])
{
    return pattern[y % 6][x % 6];
}

// ============================================================================
//  Public Entry Point
// ============================================================================

bool XTransDemosaic::demosaic(const ImageBuffer& input, ImageBuffer& output,
                              Algorithm algo)
{
    if (input.channels() != 1) {
        return false;
    }

    // ------------------------------------------------------------------
    // Parse the 6x6 X-Trans pattern from the BAYERPAT header (36 chars).
    // Fall back to the canonical X-Trans layout if the header is absent
    // or too short.
    // ------------------------------------------------------------------
    int pattern[6][6];
    const QString bayerStr = input.getHeaderValue("BAYERPAT");

    if (bayerStr.length() >= 36) {
        for (int i = 0; i < 36; ++i) {
            const char c = bayerStr[i].toLatin1();
            int colour = 0; // default: green
            if (c == 'R') colour = 1;
            else if (c == 'B') colour = 2;
            pattern[i / 6][i % 6] = colour;
        }
    } else {
        // Standard Fujifilm X-Trans II/III pattern.
        static const int stdPattern[6][6] = {
            {0, 1, 0, 0, 2, 0},
            {1, 0, 1, 2, 0, 2},
            {0, 1, 0, 0, 2, 0},
            {0, 2, 0, 0, 1, 0},
            {2, 0, 2, 1, 0, 1},
            {0, 2, 0, 0, 1, 0}
        };
        std::memcpy(pattern, stdPattern, sizeof(pattern));
    }

    // Dispatch to the selected algorithm.
    if (algo == Algorithm::Markesteijn) {
        interpolateMarkesteijn(input, output, pattern);
    } else {
        interpolateVNG(input, output, pattern);
    }

    return true;
}

// ============================================================================
//  Markesteijn-Inspired Demosaicing
// ============================================================================

void XTransDemosaic::interpolateMarkesteijn(const ImageBuffer& input,
                                            ImageBuffer& output,
                                            const int pattern[6][6])
{
    const int w = input.width();
    const int h = input.height();

    output.resize(w, h, 3);

    const float* in  = input.data().data();
    float*       out = output.data().data();

    // ------------------------------------------------------------------
    // Pass 1: Preliminary green channel interpolation.
    //         At green sites the value is copied directly.  At R/B sites
    //         the horizontal or vertical average is chosen based on
    //         which direction has the smaller gradient.
    // ------------------------------------------------------------------
    #pragma omp parallel for
    for (int y = 2; y < h - 2; ++y) {
        for (int x = 2; x < w - 2; ++x) {

            const int idx  = y * w + x;
            const int type = getPixelType(x, y, pattern);

            if (type == 0) {
                // Green pixel: copy directly.
                out[idx * 3 + 1] = in[idx];
            } else {
                // Non-green pixel: directional interpolation.
                const float hGrad = std::abs(in[idx - 1] - in[idx + 1]);
                const float vGrad = std::abs(in[idx - w] - in[idx + w]);

                if (hGrad < vGrad) {
                    out[idx * 3 + 1] = (in[idx - 1] + in[idx + 1]) * 0.5f;
                } else {
                    out[idx * 3 + 1] = (in[idx - w] + in[idx + w]) * 0.5f;
                }
            }
        }
    }

    // ------------------------------------------------------------------
    // Pass 2: Ratio-guided red and blue interpolation.
    //         For each missing R or B value, collect same-colour
    //         neighbours in a 5x5 window, compute their value/green
    //         ratios weighted by inverse squared distance, and
    //         reconstruct the missing channel as green * weighted_ratio.
    // ------------------------------------------------------------------
    #pragma omp parallel for collapse(2)
    for (int y = 3; y < h - 3; ++y) {
        for (int x = 3; x < w - 3; ++x) {

            const int   idx  = y * w + x;
            const int   type = getPixelType(x, y, pattern);
            const float g    = out[idx * 3 + 1];

            // --- Red channel ---
            if (type == 1) {
                out[idx * 3 + 0] = in[idx];
            } else {
                float rSum = 0.0f, rWeight = 0.0f;
                for (int dy = -2; dy <= 2; ++dy) {
                    for (int dx = -2; dx <= 2; ++dx) {
                        if (getPixelType(x + dx, y + dy, pattern) == 1) {
                            const float gn = out[((y + dy) * w + (x + dx)) * 3 + 1];
                            const float wt = 1.0f / (float)(dx * dx + dy * dy + 0.1f);
                            rSum    += (in[(y + dy) * w + (x + dx)] / (gn + 1e-6f)) * wt;
                            rWeight += wt;
                        }
                    }
                }
                out[idx * 3 + 0] = g * ((rWeight > 0.0f) ? rSum / rWeight : 1.0f);
            }

            // --- Blue channel ---
            if (type == 2) {
                out[idx * 3 + 2] = in[idx];
            } else {
                float bSum = 0.0f, bWeight = 0.0f;
                for (int dy = -2; dy <= 2; ++dy) {
                    for (int dx = -2; dx <= 2; ++dx) {
                        if (getPixelType(x + dx, y + dy, pattern) == 2) {
                            const float gn = out[((y + dy) * w + (x + dx)) * 3 + 1];
                            const float wt = 1.0f / (float)(dx * dx + dy * dy + 0.1f);
                            bSum    += (in[(y + dy) * w + (x + dx)] / (gn + 1e-6f)) * wt;
                            bWeight += wt;
                        }
                    }
                }
                out[idx * 3 + 2] = g * ((bWeight > 0.0f) ? bSum / bWeight : 1.0f);
            }
        }
    }

    // ------------------------------------------------------------------
    // Pass 3: Chrominance refinement via median filtering.
    //         Smooth the R-G and B-G colour-difference planes using a
    //         3x3 median filter to suppress colour fringing artefacts.
    // ------------------------------------------------------------------
    #pragma omp parallel for collapse(2)
    for (int y = 2; y < h - 2; ++y) {
        for (int x = 2; x < w - 2; ++x) {

            const int   idx = y * w + x;
            const float g   = out[idx * 3 + 1];

            float rDiffs[9], bDiffs[9];
            int n = 0;

            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nid = (y + dy) * w + (x + dx);
                    const float gn = out[nid * 3 + 1];
                    rDiffs[n] = out[nid * 3 + 0] - gn;
                    bDiffs[n] = out[nid * 3 + 2] - gn;
                    ++n;
                }
            }

            std::sort(rDiffs, rDiffs + 9);
            std::sort(bDiffs, bDiffs + 9);

            out[idx * 3 + 0] = g + rDiffs[4]; // median R-G
            out[idx * 3 + 2] = g + bDiffs[4]; // median B-G
        }
    }
}

// ============================================================================
//  VNG-Based X-Trans Demosaicing
// ============================================================================

void XTransDemosaic::interpolateVNG(const ImageBuffer& input,
                                    ImageBuffer& output,
                                    const int pattern[6][6])
{
    const int w = input.width();
    const int h = input.height();

    output.resize(w, h, 3);

    const float* in  = input.data().data();
    float*       out = output.data().data();

    #pragma omp parallel for
    for (int y = 2; y < h - 2; ++y) {
        for (int x = 2; x < w - 2; ++x) {

            const int currentType = getPixelType(x, y, pattern); // 0=G, 1=R, 2=B
            float* px = &out[(y * w + x) * 3];

            // Write the known channel directly.
            if      (currentType == 0) px[1] = in[y * w + x];
            else if (currentType == 1) px[0] = in[y * w + x];
            else                       px[2] = in[y * w + x];

            // ---- Compute directional gradients (8 compass directions) ----
            auto val  = [&](int dx, int dy) { return in[(y + dy) * w + (x + dx)]; };
            auto type = [&](int dx, int dy) { return getPixelType(x + dx, y + dy, pattern); };

            static const int dirs[8][2] = {
                { 0,-1}, { 0, 1}, {-1, 0}, { 1, 0},
                {-1,-1}, { 1,-1}, {-1, 1}, { 1, 1}
            };

            float gradients[8] = {0.0f};
            for (int d = 0; d < 8; ++d) {
                const int dx = dirs[d][0];
                const int dy = dirs[d][1];
                if (x + 2*dx >= 0 && x + 2*dx < w &&
                    y + 2*dy >= 0 && y + 2*dy < h)
                {
                    gradients[d] = std::abs(val(0, 0) - val(2 * dx, 2 * dy));
                }
            }

            // ---- Identify direction with minimum gradient ----
            int   bestDir = 0;
            float minGrad = gradients[0];
            for (int d = 1; d < 8; ++d) {
                if (gradients[d] < minGrad) {
                    minGrad = gradients[d];
                    bestDir = d;
                }
            }

            // ---- Accumulate colour sums in a 5x5 window ----
            // Neighbour contributions are weighted by inverse distance and
            // boosted when aligned with the minimum-gradient direction.
            float rSum = 0.0f, gSum = 0.0f, bSum = 0.0f;
            float rW   = 0.0f, gW   = 0.0f, bW   = 0.0f;

            for (int dy = -2; dy <= 2; ++dy) {
                for (int dx = -2; dx <= 2; ++dx) {
                    if (dx == 0 && dy == 0) continue;

                    const int   t    = type(dx, dy);
                    const float v    = val(dx, dy);
                    const float dist = std::sqrt(static_cast<float>(dx*dx + dy*dy));
                    float wDist      = 1.0f / (dist + 0.1f);

                    // Directional alignment weighting.
                    const int   bdx = dirs[bestDir][0];
                    const int   bdy = dirs[bestDir][1];
                    const float dot = static_cast<float>(dx*bdx + dy*bdy)
                                    / (dist * std::sqrt(static_cast<float>(bdx*bdx + bdy*bdy)) + 0.01f);

                    if      (std::abs(dot) > 0.8f) wDist *= 2.0f;  // aligned with smooth direction
                    else if (std::abs(dot) < 0.2f) wDist *= 0.2f;  // orthogonal to smooth direction

                    if      (t == 0) { gSum += v * wDist; gW += wDist; }
                    else if (t == 1) { rSum += v * wDist; rW += wDist; }
                    else             { bSum += v * wDist; bW += wDist; }
                }
            }

            // ---- Fill missing channels ----
            if (currentType != 0) px[1] = (gW > 0.0f) ? gSum / gW : px[1];
            if (currentType != 1) px[0] = (rW > 0.0f) ? rSum / rW : 0.0f;
            if (currentType != 2) px[2] = (bW > 0.0f) ? bSum / bW : 0.0f;
        }
    }
}

} // namespace Preprocessing