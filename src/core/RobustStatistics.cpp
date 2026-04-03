// ============================================================================
// RobustStatistics.cpp
// Robust statistical estimators: histogram-based percentiles, Qn scale,
// sigma-clipped robust mean, and Siegel's repeated median linear fit.
// ============================================================================

#include "RobustStatistics.h"

#include <algorithm>
#include <cmath>
#include <cassert>
#include <iostream>
#include <omp.h>

namespace RobustStatistics {

// ============================================================================
// Internal Helpers
// ============================================================================

/** Clamp a value to [min, max]. */
#define LIM(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))

// ============================================================================
// Qn0 Scale Estimator
// ============================================================================

/**
 * @brief Compute the Qn scale estimator from sorted data.
 *
 * The Qn estimator is based on the k-th order statistic of pairwise
 * absolute differences. It achieves 50% breakdown point like MAD but
 * has higher Gaussian efficiency (~82% vs ~37%).
 *
 * Complexity: O(N^2) for pairwise differences.
 * This is acceptable for the typical stacking context where N represents
 * the number of sub-exposures (usually 20-300 frames).
 *
 * @param sorted_data Pre-sorted input data.
 * @return Qn scale estimate.
 */
static float Qn0(const std::vector<float>& sorted_data)
{
    size_t n = sorted_data.size();
    if (n < 2) return 0.0f;

    // Collect all pairwise absolute differences
    size_t wsize = n * (n - 1) / 2;
    std::vector<float> work;
    work.reserve(wsize);

    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i + 1; j < n; ++j) {
            work.push_back(std::fabs(sorted_data[i] - sorted_data[j]));
        }
    }

    // k-th order statistic index: h = floor(n/2) + 1, k = h*(h-1)/2
    size_t n_2 = n / 2;
    size_t k   = ((n_2 + 1) * n_2) / 2;

    if (work.empty()) return 0.0f;
    if (k >= work.size()) k = work.size() - 1;

    std::nth_element(work.begin(), work.begin() + k, work.end());
    return work[k];
}

// ============================================================================
// Trimmed Mean
// ============================================================================

/**
 * @brief Compute the trimmed mean of pre-sorted data.
 *
 * Discards the lowest and highest (trim * 100)% of observations,
 * then computes the arithmetic mean of the remaining central portion.
 * Falls back to the median if the trim fraction is too large.
 *
 * @param sorted_data Pre-sorted input data.
 * @param trim        Fraction to trim from each tail [0, 0.5).
 * @return Trimmed mean value.
 */
static float standardTrimmedMean(const std::vector<float>& sorted_data,
                                 float trim)
{
    size_t n = sorted_data.size();
    if (n == 0) return 0.0f;

    size_t trimCount = static_cast<size_t>(n * trim);

    // If trim removes all or nearly all data, fall back to median
    if (trimCount * 2 >= n) {
        return sorted_data[n / 2];
    }

    double sum   = 0.0;
    size_t count = 0;
    for (size_t i = trimCount; i < n - trimCount; ++i) {
        sum += sorted_data[i];
        count++;
    }

    return (count > 0) ? static_cast<float>(sum / count) : 0.0f;
}

// ============================================================================
// Robust Mean (Qn + Sigma Clipping)
// ============================================================================

/**
 * @brief Compute a robust mean using Qn scale estimation and 3-sigma clipping.
 *
 * Algorithm:
 *   1. Sort the data
 *   2. Compute median and Qn scale
 *   3. Convert Qn to sigma via consistency factor (2.2219 for normal distribution)
 *   4. Reject observations beyond 3 sigma from the median
 *   5. Return mean of inliers, or 30% trimmed mean if too few inliers remain
 *
 * @param data Input data (sorted in-place).
 * @return Robust mean estimate.
 */
float standardRobustMean(std::vector<float>& data)
{
    if (data.empty()) return 0.0f;

    // Step 1: Sort
    std::sort(data.begin(), data.end());

    // Step 2: Compute initial location and scale
    float med = getMedian(data);
    float qn  = Qn0(data);

    if (qn < 0.0f) return -1.0f;

    // Step 3: Convert Qn to sigma (consistency factor for normal distribution)
    float sx        = 2.2219f * qn;
    float threshold = 3.0f * sx;

    // Step 4: Collect inliers
    std::vector<float> inliers;
    inliers.reserve(data.size());
    double sum = 0.0;

    for (float v : data) {
        if (std::fabs(v - med) <= threshold) {
            inliers.push_back(v);
            sum += v;
        }
    }

    // Step 5: Compute result
    if (inliers.size() < 5) {
        // Insufficient inliers: fall back to 30% trimmed mean
        return standardTrimmedMean(data, 0.3f);
    } else {
        return static_cast<float>(sum / inliers.size());
    }
}

// ============================================================================
// Histogram-Based Percentile Finder
// ============================================================================

/**
 * @brief O(N) histogram-based percentile computation with optional parallelism.
 *
 * Builds a histogram of the input data, then walks through bins to locate
 * the values at the requested lower and upper percentile positions.
 * Interpolates between bin edges for sub-bin accuracy.
 *
 * @param data    Input data array (not modified).
 * @param size    Number of elements.
 * @param minPrct Lower percentile [0, 1].
 * @param minOut  Output for lower percentile (may be nullptr).
 * @param maxPrct Upper percentile [0, 1].
 * @param maxOut  Output for upper percentile (may be nullptr).
 * @param threads Maximum number of threads for parallel histogram construction.
 */
void findMinMaxPercentile(const float* data, size_t size,
                          float minPrct, float* minOut,
                          float maxPrct, float* maxOut,
                          int threads)
{
    assert(minPrct <= maxPrct);

    if (size == 0) {
        if (minOut) *minOut = 0.0f;
        if (maxOut) *maxOut = 0.0f;
        return;
    }

    // -- Determine optimal thread count based on data size --------------------
    size_t numThreads = 1;
#ifdef _OPENMP
    if (threads > 1) {
        const size_t maxThreads = static_cast<size_t>(threads);
        while (size > numThreads * numThreads * 16384
               && numThreads < maxThreads) {
            ++numThreads;
        }
    }
#endif

    // -- Find data range for histogram scaling --------------------------------
    float minVal = data[0];
    float maxVal = data[0];

#ifdef _OPENMP
    #pragma omp parallel for reduction(min:minVal) reduction(max:maxVal) \
        num_threads(numThreads)
#endif
    for (size_t i = 1; i < size; ++i) {
        minVal = std::min(minVal, data[i]);
        maxVal = std::max(maxVal, data[i]);
    }

    if (std::fabs(maxVal - minVal) == 0.0f) {
        if (minOut) *minOut = minVal;
        if (maxOut) *maxOut = minVal;
        return;
    }

    // -- Build histogram ------------------------------------------------------
    const unsigned int histoSize =
        std::min<size_t>(65536, size);
    const float scale = (histoSize - 1) / (maxVal - minVal);

    std::vector<uint32_t> histo(histoSize, 0);

    if (numThreads == 1) {
        for (size_t i = 0; i < size; ++i) {
            histo[static_cast<uint16_t>(scale * (data[i] - minVal))]++;
        }
    } else {
#ifdef _OPENMP
        #pragma omp parallel num_threads(numThreads)
#endif
        {
            // Per-thread local histogram to avoid contention
            std::vector<uint32_t> histothr(histoSize, 0);

#ifdef _OPENMP
            #pragma omp for nowait
#endif
            for (size_t i = 0; i < size; ++i) {
                histothr[static_cast<uint16_t>(
                    scale * (data[i] - minVal))]++;
            }

            // Merge thread-local histograms into the global one
#ifdef _OPENMP
            #pragma omp critical
#endif
            {
                for (size_t i = 0; i < histoSize; ++i) {
                    histo[i] += histothr[i];
                }
            }
        }
    }

    // -- Walk histogram to find percentile values -----------------------------
    size_t k     = 0;
    size_t count = 0;

    // Lower percentile
    if (minOut) {
        const float threshmin = minPrct * size;
        while (count < threshmin && k < histoSize) {
            count += histo[k++];
        }

        if (k > 0) {
            // Interpolate between adjacent bins
            const size_t count_ = count - histo[k - 1];
            const float c0 = count - threshmin;
            const float c1 = threshmin - count_;
            *minOut = (c1 * k + c0 * (k - 1)) / (c0 + c1);
        } else {
            *minOut = static_cast<float>(k);
        }

        *minOut /= scale;
        *minOut += minVal;
        *minOut = LIM(*minOut, minVal, maxVal);
    }

    // Upper percentile (continues accumulation from current state)
    if (maxOut) {
        const float threshmax = maxPrct * size;
        while (count < threshmax && k < histoSize) {
            count += histo[k++];
        }

        if (k > 0) {
            const size_t count_ = count - histo[k - 1];
            const float c0 = count - threshmax;
            const float c1 = threshmax - count_;
            *maxOut = (c1 * k + c0 * (k - 1)) / (c0 + c1);
        } else {
            *maxOut = static_cast<float>(k);
        }

        *maxOut /= scale;
        *maxOut += minVal;
        *maxOut = LIM(*maxOut, minVal, maxVal);
    }
}

// ============================================================================
// Convenience Wrappers
// ============================================================================

float getMedian(const std::vector<float>& data)
{
    if (data.empty()) return 0.0f;

    float med;
    findMinMaxPercentile(data.data(), data.size(),
                         0.5f, &med, 0.5f, nullptr,
                         omp_get_max_threads());
    return med;
}

float getMAD(const std::vector<float>& data, float median)
{
    if (data.empty()) return 0.0f;

    // Compute absolute deviations from the median
    std::vector<float> diffs(data.size());

    #pragma omp parallel for
    for (size_t i = 0; i < data.size(); ++i) {
        diffs[i] = std::fabs(data[i] - median);
    }

    float mad;
    findMinMaxPercentile(diffs.data(), diffs.size(),
                         0.5f, &mad, 0.5f, nullptr,
                         omp_get_max_threads());
    return mad;
}

// ============================================================================
// Siegel's Repeated Median Linear Fit
// ============================================================================

/** Helper: compute the exact median of a double vector (modifies in-place). */
static double getMedianD(std::vector<double>& v)
{
    if (v.empty()) return 0.0;

    size_t n = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + n, v.end());

    if (v.size() % 2 != 0) {
        return v[n];
    } else {
        double val1 = v[n];
        std::nth_element(v.begin(), v.begin() + n - 1, v.end());
        return (val1 + v[n - 1]) * 0.5;
    }
}

/**
 * @brief Siegel's repeated median linear regression.
 *
 * For each point i, compute the median slope with all other points j.
 * The overall slope is the median of these per-point median slopes.
 * The intercept is the median of (y_i - slope * x_i).
 * Sigma is estimated from inlier residuals using 3*MAD*1.4826 rejection.
 *
 * @param x         Independent variable values.
 * @param y         Dependent variable values.
 * @param slope     Output: fitted slope.
 * @param intercept Output: fitted intercept.
 * @param sigma     Output: robust residual standard deviation.
 * @return true if the fit succeeded.
 */
bool repeatedMedianFit(const std::vector<double>& x,
                       const std::vector<double>& y,
                       double& slope, double& intercept, double& sigma)
{
    size_t n = x.size();
    if (n < 2) return false;

    // -- Step 1: Per-point median slopes --------------------------------------
    std::vector<double> pointMedians(n);

    for (size_t i = 0; i < n; ++i) {
        std::vector<double> slopes;
        slopes.reserve(n - 1);

        for (size_t j = 0; j < n; ++j) {
            if (i == j) continue;

            // Guard against coincident x-coordinates
            if (std::abs(x[j] - x[i]) > 1e-9) {
                slopes.push_back((y[j] - y[i]) / (x[j] - x[i]));
            }
        }

        if (!slopes.empty()) {
            pointMedians[i] = getMedianD(slopes);
        } else {
            pointMedians[i] = 0.0;
        }
    }

    // -- Step 2: Overall slope = median of per-point medians ------------------
    std::vector<double> pointMediansCopy = pointMedians;
    slope = getMedianD(pointMediansCopy);

    // -- Step 3: Intercept = median of (y_i - slope * x_i) --------------------
    std::vector<double> intercepts(n);
    for (size_t i = 0; i < n; ++i) {
        intercepts[i] = y[i] - slope * x[i];
    }
    intercept = getMedianD(intercepts);

    // -- Step 4: Residual sigma estimation ------------------------------------
    std::vector<double> residuals(n);
    double sumSq = 0.0;

    for (size_t i = 0; i < n; ++i) {
        double res   = y[i] - (intercept + slope * x[i]);
        residuals[i] = std::abs(res);
        sumSq       += res * res;
    }

    // MAD-based robust sigma with normal consistency factor
    double mad      = getMedianD(residuals);
    double madSigma = mad * 1.4826;

    // Count inliers within 3 * madSigma and compute robust variance
    int    inlierCount = 0;
    double robustSumSq = 0.0;

    for (size_t i = 0; i < n; ++i) {
        double res = y[i] - (intercept + slope * x[i]);
        if (std::abs(res) <= 3.0 * madSigma) {
            inlierCount++;
            robustSumSq += res * res;
        }
    }

    if (inlierCount > 2) {
        sigma = std::sqrt(robustSumSq / inlierCount);
    } else {
        sigma = std::sqrt(sumSq / n);
    }

    return true;
}

} // namespace RobustStatistics