#ifndef CUBICSPLINE_H
#define CUBICSPLINE_H

// ============================================================================
// CubicSpline.h
// Natural cubic spline interpolation for 1D data.
// Provides fitting of a cubic spline through a set of control points
// and evaluation (interpolation) at arbitrary x values.
// Also includes a simple linear interpolation fallback.
// ============================================================================

#include <vector>
#include <cmath>
#include <algorithm>

// ----------------------------------------------------------------------------
// Data structures
// ----------------------------------------------------------------------------

/**
 * @brief A single 2D control point for spline fitting.
 */
struct SplinePoint {
    double x;
    double y;

    bool operator<(const SplinePoint& other) const {
        return x < other.x;
    }
};

/**
 * @brief Precomputed cubic spline coefficients for efficient evaluation.
 *        For each interval [x_i, x_{i+1}], the spline is:
 *          S(x) = y_i + b_i*(x-x_i) + c_i*(x-x_i)^2 + d_i*(x-x_i)^3
 */
struct SplineData {
    std::vector<double> x_values;
    std::vector<double> y_values;
    std::vector<double> b;  // Linear coefficients
    std::vector<double> c;  // Quadratic coefficients
    std::vector<double> d;  // Cubic coefficients
    int n = 0;              // Number of data points
};

// ----------------------------------------------------------------------------
// CubicSpline class
// ----------------------------------------------------------------------------

class CubicSpline {
public:
    /**
     * @brief Fit a natural cubic spline through the given control points.
     *        Points are sorted by x internally. Natural boundary conditions
     *        (S''(x_0) = S''(x_n) = 0) are used.
     * @param points  Vector of (x, y) control points (at least 2 required).
     * @return Precomputed spline data for use with interpolate().
     */
    static SplineData fit(const std::vector<SplinePoint>& points)
    {
        SplineData data;
        if (points.size() < 2) return data;

        data.n = static_cast<int>(points.size());
        int n = data.n;

        // Sort control points by x-coordinate
        auto sortedPoints = points;
        std::sort(sortedPoints.begin(), sortedPoints.end());

        data.x_values.resize(n);
        data.y_values.resize(n);
        for (int i = 0; i < n; ++i) {
            data.x_values[i] = sortedPoints[i].x;
            data.y_values[i] = sortedPoints[i].y;
        }

        // Allocate coefficient arrays
        data.b.resize(n);
        data.c.resize(n);
        data.d.resize(n);

        // Compute interval widths
        std::vector<double> h(n - 1);
        for (int i = 0; i < n - 1; ++i) {
            h[i] = data.x_values[i + 1] - data.x_values[i];
        }

        // Compute second-derivative differences
        std::vector<double> alpha(n - 1);
        for (int i = 1; i < n - 1; ++i) {
            alpha[i] = (3.0 / h[i]) * (data.y_values[i + 1] - data.y_values[i])
                      - (3.0 / h[i - 1]) * (data.y_values[i] - data.y_values[i - 1]);
        }

        // Solve tridiagonal system for c coefficients (natural BC)
        std::vector<double> l(n);
        std::vector<double> mu(n);
        std::vector<double> z(n);

        l[0]  = 1.0;
        mu[0] = 0.0;
        z[0]  = 0.0;

        for (int i = 1; i < n - 1; ++i) {
            l[i]  = 2.0 * (data.x_values[i + 1] - data.x_values[i - 1])
                    - h[i - 1] * mu[i - 1];
            mu[i] = h[i] / l[i];
            z[i]  = (alpha[i] - h[i - 1] * z[i - 1]) / l[i];
        }

        l[n - 1]      = 1.0;
        z[n - 1]      = 0.0;
        data.c[n - 1] = 0.0;

        // Back-substitute to find b, c, d coefficients
        const double oneThird = 1.0 / 3.0;
        for (int j = n - 2; j >= 0; --j) {
            data.c[j] = z[j] - mu[j] * data.c[j + 1];
            data.b[j] = (data.y_values[j + 1] - data.y_values[j]) / h[j]
                        - h[j] * (data.c[j + 1] + 2.0 * data.c[j]) * oneThird;
            data.d[j] = (data.c[j + 1] - data.c[j]) / (3.0 * h[j]);
        }

        return data;
    }

    /**
     * @brief Evaluate the cubic spline at a given x value.
     *        Values outside the data range are clamped to the boundary values.
     * @param x     Evaluation point.
     * @param data  Precomputed spline data from fit().
     * @return Interpolated y value, clamped to [0, 1].
     */
    static double interpolate(double x, const SplineData& data)
    {
        if (data.n < 2) return x;

        // Clamp to data range
        if (x >= data.x_values[data.n - 1]) return data.y_values[data.n - 1];
        if (x <= data.x_values[0])          return data.y_values[0];

        x = std::max(0.0, std::min(1.0, x));

        // Binary search for the containing interval
        auto it = std::upper_bound(data.x_values.begin(),
                                   data.x_values.end(), x);
        int i = static_cast<int>(std::distance(data.x_values.begin(), it)) - 1;
        i = std::max(0, std::min(i, data.n - 2));

        // Evaluate cubic polynomial
        double diff    = x - data.x_values[i];
        double diff_sq = diff * diff;
        double val     = data.y_values[i]
                       + data.b[i] * diff
                       + data.c[i] * diff_sq
                       + data.d[i] * diff * diff_sq;

        return std::max(0.0, std::min(1.0, val));
    }

    /**
     * @brief Simple piecewise linear interpolation through control points.
     *        Provided as a lightweight alternative when cubic smoothness
     *        is not required.
     * @param x       Evaluation point.
     * @param points  Sorted control points.
     * @return Linearly interpolated y value.
     */
    static double interpolateLinear(double x,
                                    const std::vector<SplinePoint>& points)
    {
        if (points.empty())    return x;
        if (points.size() == 1) return points[0].y;

        auto p = points;  // Assume pre-sorted by x

        if (x <= p.front().x) return p.front().y;
        if (x >= p.back().x)  return p.back().y;

        for (size_t i = 0; i < p.size() - 1; ++i) {
            if (x >= p[i].x && x <= p[i + 1].x) {
                double t = (x - p[i].x) / (p[i + 1].x - p[i].x);
                return p[i].y + t * (p[i + 1].y - p[i].y);
            }
        }

        return x;
    }
};

#endif // CUBICSPLINE_H