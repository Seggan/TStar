#ifndef ABEMATH_H
#define ABEMATH_H

// ============================================================================
// AbeMath.h
// Mathematical utilities for background extraction and surface fitting.
// Provides polynomial fitting/evaluation, radial basis function (RBF)
// interpolation, and automatic background sample generation.
// ============================================================================

#include <vector>
#include <functional>

namespace AbeMath {

// ----------------------------------------------------------------------------
// Data structures
// ----------------------------------------------------------------------------

/**
 * @brief 2D point in image coordinates.
 */
struct Point {
    float x, y;
};

/**
 * @brief 2D sample with an associated intensity value.
 */
struct Sample {
    float x, y;
    float z;  // Intensity value at (x, y)
};

// ----------------------------------------------------------------------------
// Polynomial surface fitting
// ----------------------------------------------------------------------------

/**
 * @brief Fit a 2D polynomial surface to the given samples.
 * @param samples  Collection of (x, y, z) data points.
 * @param degree   Polynomial degree (e.g. 2 for quadratic).
 * @return Coefficient vector, or empty on failure.
 */
std::vector<float> fitPolynomial(const std::vector<Sample>& samples, int degree);

/**
 * @brief Evaluate a fitted polynomial surface at a given point.
 * @param x, y    Coordinates (should be in same normalization as fitting).
 * @param coeffs  Coefficient vector from fitPolynomial().
 * @param degree  Polynomial degree used during fitting.
 * @return Interpolated intensity value.
 */
float evalPolynomial(float x, float y, const std::vector<float>& coeffs, int degree);

// ----------------------------------------------------------------------------
// Radial Basis Function (RBF) interpolation
// ----------------------------------------------------------------------------

/**
 * @brief Fitted RBF model containing centers, weights, and regularization.
 */
struct RbfModel {
    std::vector<Sample> centers;
    std::vector<float>  weights;
    float               smooth;
};

/**
 * @brief Fit an RBF interpolation model to the given samples.
 * @param samples  Collection of (x, y, z) data points.
 * @param smooth   Regularization parameter (Tikhonov smoothing).
 * @return Fitted RBF model, or empty model on failure.
 */
RbfModel fitRbf(const std::vector<Sample>& samples, float smooth);

/**
 * @brief Evaluate a fitted RBF model at a given point.
 * @param x, y   Coordinates.
 * @param model  Fitted RBF model from fitRbf().
 * @return Interpolated intensity value.
 */
float evalRbf(float x, float y, const RbfModel& model);

// ----------------------------------------------------------------------------
// Sampling utilities
// ----------------------------------------------------------------------------

/**
 * @brief Compute the median pixel value within a square box.
 * @param data  Flat image data (single channel).
 * @param w, h  Image dimensions.
 * @param cx, cy  Center of the box.
 * @param size  Side length of the box.
 * @return Median value of valid pixels in the box.
 */
float getMedianBox(const std::vector<float>& data, int w, int h,
                   int cx, int cy, int size);

/**
 * @brief Find the dimmest local region near (cx, cy) via gradient descent.
 * @param data      Flat image data (single channel).
 * @param w, h      Image dimensions.
 * @param cx, cy    Starting search position.
 * @param patchSize Size of the evaluation patch.
 * @return Coordinates of the dimmest region found.
 */
Point findDimmest(const std::vector<float>& data, int w, int h,
                  int cx, int cy, int patchSize);

/**
 * @brief Generate background sample positions by grid subdivision,
 *        walking each cell toward its dimmest patch.
 * @param data           Flat image data (single channel).
 * @param w, h           Image dimensions.
 * @param numSamples     Approximate number of grid cells (sqrt determines grid).
 * @param patchSize      Patch size for dimmest-region search.
 * @param exclusionMask  Optional mask; false = excluded pixel.
 * @return Vector of sample positions.
 */
std::vector<Point> generateSamples(const std::vector<float>& data, int w, int h,
                                   int numSamples, int patchSize,
                                   const std::vector<bool>& exclusionMask);

} // namespace AbeMath

#endif // ABEMATH_H