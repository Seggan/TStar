/**
 * @file OverlapNormalization.cpp
 * @brief Implementation of pairwise overlap normalization.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#include "OverlapNormalization.h"
#include "MathUtils.h"
#include "Statistics.h"
#include <cmath>
#include <algorithm>

namespace Stacking {

// =============================================================================
// Overlap Region Computation
// =============================================================================

size_t OverlapNormalization::computeOverlapRegion(
    const RegistrationData& regI, const RegistrationData& regJ,
    int widthI, int heightI, int widthJ, int heightJ,
    QRect& areaI, QRect& areaJ)
{
    // Extract translation offsets from the homography matrices.
    double dxI = regI.H[0][2],  dyI = regI.H[1][2];
    double dxJ = regJ.H[0][2],  dyJ = regJ.H[1][2];

    // Relative shift of J with respect to I.
    int dx = static_cast<int>(std::round(dxJ - dxI));
    int dy = static_cast<int>(std::round(dyI - dyJ));

    // Overlap in I's coordinate system.
    int x_tli = std::max(0, dx);
    int y_tli = std::max(0, dy);
    int x_bri = std::min(widthI,  dx + widthJ);
    int y_bri = std::min(heightI, dy + heightJ);

    // Overlap in J's coordinate system.
    int x_tlj = std::max(0, -dx);
    int y_tlj = std::max(0, -dy);
    int x_brj = std::min(widthJ,  -dx + widthI);
    int y_brj = std::min(heightJ, -dy + heightI);

    if (x_tli < x_bri && y_tli < y_bri) {
        areaI = QRect(x_tli, y_tli, x_bri - x_tli, y_bri - y_tli);
        areaJ = QRect(x_tlj, y_tlj, x_brj - x_tlj, y_brj - y_tlj);
        return static_cast<size_t>(areaI.width()) * areaI.height();
    }

    return 0;
}

// =============================================================================
// Overlap Statistics
// =============================================================================

bool OverlapNormalization::computeOverlapStats(
    const ImageBuffer& imgI, const ImageBuffer& imgJ,
    const QRect& areaI, const QRect& areaJ,
    int channel, OverlapStats& stats)
{
    if (areaI.width() != areaJ.width() || areaI.height() != areaJ.height()) {
        return false;
    }

    const size_t maxCount = static_cast<size_t>(areaI.width()) * areaI.height();
    if (maxCount < 10) return false;

    std::vector<float> dataI, dataJ;
    dataI.reserve(maxCount);
    dataJ.reserve(maxCount);

    // Collect paired pixel values, skipping zeros (out-of-bounds / masked).
    for (int y = 0; y < areaI.height(); ++y) {
        for (int x = 0; x < areaI.width(); ++x) {
            float vI = imgI.value(areaI.x() + x, areaI.y() + y, channel);
            float vJ = imgJ.value(areaJ.x() + x, areaJ.y() + y, channel);
            if (vI > 0.0f && vJ > 0.0f) {
                dataI.push_back(vI);
                dataJ.push_back(vJ);
            }
        }
    }

    if (dataI.size() < 10) return false;

    stats.pixelCount = dataI.size();
    stats.medianI    = Statistics::quickMedian(dataI);
    stats.medianJ    = Statistics::quickMedian(dataJ);
    stats.madI       = Statistics::mad(dataI, stats.medianI);
    stats.madJ       = Statistics::mad(dataJ, stats.medianJ);

    // Approximate robust location / scale using median and scaled MAD.
    stats.locationI  = stats.medianI;
    stats.locationJ  = stats.medianJ;
    stats.scaleI     = stats.madI * 1.4826;
    stats.scaleJ     = stats.madJ * 1.4826;

    return true;
}

// =============================================================================
// Coefficient Solver
// =============================================================================

bool OverlapNormalization::solveCoefficients(
    const std::vector<OverlapStats>& allStats,
    int numImages, int refIndex, bool additive,
    std::vector<double>& coeffs)
{
    // Initialize: identity coefficients (0 for additive, 1 for multiplicative).
    coeffs.assign(numImages, additive ? 0.0 : 1.0);

    // Direct pairs involving the reference image.
    for (const auto& stat : allStats) {
        if (stat.imgI == refIndex) {
            int j = stat.imgJ;
            coeffs[j] = additive
                      ? (stat.medianI - stat.medianJ)
                      : ((stat.medianJ > 0) ? stat.medianI / stat.medianJ : 1.0);
        } else if (stat.imgJ == refIndex) {
            int i = stat.imgI;
            coeffs[i] = additive
                      ? (stat.medianJ - stat.medianI)
                      : ((stat.medianI > 0) ? stat.medianJ / stat.medianI : 1.0);
        }
    }

    // Note: images not directly overlapping with the reference would need
    // propagation through a chain or a full least-squares global solve.
    // This simplified implementation handles the direct-overlap case only.

    return true;
}

} // namespace Stacking