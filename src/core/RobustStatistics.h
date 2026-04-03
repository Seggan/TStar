#ifndef ROBUSTSTATISTICS_H
#define ROBUSTSTATISTICS_H

// ============================================================================
// RobustStatistics.h
// Robust statistical estimators for outlier-resistant image analysis.
// Includes histogram-based percentile finder, Qn scale estimator,
// sigma-clipped robust mean, and Siegel's repeated median line fit.
// ============================================================================

#include <vector>
#include <cstddef>
#include <cstdint>

namespace RobustStatistics {

    /**
     * @brief O(N) histogram-based percentile finder supporting full dynamic range.
     *
     * Computes the values at specified lower and upper percentiles using a
     * histogram-based approach with optional OpenMP parallelism.
     *
     * @param data    Input data array.
     * @param size    Number of elements.
     * @param minPrct Lower percentile [0.0, 1.0] (use 0.5 for median).
     * @param minOut  Output: value at the lower percentile (may be nullptr).
     * @param maxPrct Upper percentile [0.0, 1.0].
     * @param maxOut  Output: value at the upper percentile (may be nullptr).
     * @param threads Maximum number of threads for parallel histogram construction.
     */
    void findMinMaxPercentile(const float* data, size_t size,
                              float minPrct, float* minOut,
                              float maxPrct, float* maxOut,
                              int threads);

    /** Compute the median of the data using histogram-based percentile finding. */
    float getMedian(const std::vector<float>& data);

    /** Compute the Median Absolute Deviation (MAD) around a given median. */
    float getMAD(const std::vector<float>& data, float median);

    /** Parameters for Photometric Color Calibration. */
    struct PCCParams {
        float r_factor;
        float g_factor;
        float b_factor;
        float bg_r;
        float bg_g;
        float bg_b;
    };

    /**
     * @brief Robust mean using Qn scale estimator and 3-sigma clipping.
     *
     * Sorts the data, computes the Qn scale, rejects outliers beyond
     * 3 * (2.2219 * Qn), and returns the mean of inliers. Falls back to
     * a 30% trimmed mean if insufficient inliers remain.
     *
     * @param data Input data (will be sorted in-place).
     * @return Robust mean estimate.
     */
    float standardRobustMean(std::vector<float>& data);

    /**
     * @brief Siegel's repeated median linear regression.
     *
     * Fits a line y = slope*x + intercept using the repeated median estimator,
     * which achieves 50% breakdown point (maximally robust).
     *
     * @param x         Independent variable values.
     * @param y         Dependent variable values.
     * @param slope     Output: fitted slope.
     * @param intercept Output: fitted intercept.
     * @param sigma     Output: robust residual standard deviation.
     * @return true if the fit succeeded (requires n >= 2).
     */
    bool repeatedMedianFit(const std::vector<double>& x,
                           const std::vector<double>& y,
                           double& slope,
                           double& intercept,
                           double& sigma);
}

#endif // ROBUSTSTATISTICS_H