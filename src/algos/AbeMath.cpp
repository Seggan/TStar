// ============================================================================
// AbeMath.cpp
// Implementation of mathematical utilities for background extraction:
// linear algebra solver, polynomial surface fitting, RBF interpolation,
// and automatic background sample generation.
// ============================================================================

#include "AbeMath.h"

#include <cmath>
#include <algorithm>
#include <numeric>
#include <random>

namespace AbeMath {

// ============================================================================
// Linear algebra
// ============================================================================

/**
 * @brief Solve a dense NxN linear system Ax = b via Gaussian elimination
 *        with partial pivoting.
 * @param N  System dimension.
 * @param A  Row-major NxN coefficient matrix (may be modified).
 * @param x  Solution vector (output).
 * @param b  Right-hand side vector.
 * @return true on success, false if the matrix is singular.
 */
static bool solveLinear(int N, std::vector<double>& A,
                        std::vector<double>& x,
                        const std::vector<double>& b)
{
    // Build augmented matrix [A | b]
    std::vector<std::vector<double>> M(N, std::vector<double>(N + 1));
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) {
            M[i][j] = A[i * N + j];
        }
        M[i][N] = b[i];
    }

    // Forward elimination with partial pivoting
    for (int i = 0; i < N; ++i) {
        // Select pivot row with maximum absolute value in column i
        int pivot = i;
        for (int j = i + 1; j < N; ++j) {
            if (std::abs(M[j][i]) > std::abs(M[pivot][i])) {
                pivot = j;
            }
        }
        std::swap(M[i], M[pivot]);

        // Singular check
        if (std::abs(M[i][i]) < 1e-9) {
            return false;
        }

        // Eliminate below pivot
        for (int j = i + 1; j < N; ++j) {
            double factor = M[j][i] / M[i][i];
            for (int k = i; k <= N; ++k) {
                M[j][k] -= factor * M[i][k];
            }
        }
    }

    // Back substitution
    x.assign(N, 0.0);
    for (int i = N - 1; i >= 0; --i) {
        double sum = 0.0;
        for (int j = i + 1; j < N; ++j) {
            sum += M[i][j] * x[j];
        }
        x[i] = (M[i][N] - sum) / M[i][i];
    }

    return true;
}

// ============================================================================
// Polynomial surface fitting
// ============================================================================

/**
 * @brief Compute the number of terms in a 2D polynomial of the given degree.
 *        For degree d, this is (d+1)*(d+2)/2.
 */
static int numPolyTerms(int degree)
{
    return (degree + 1) * (degree + 2) / 2;
}

/**
 * @brief Evaluate all monomial basis functions x^i * y^j for i+j <= degree.
 * @return Vector of term values in consistent ordering.
 */
static std::vector<float> getPolyTermValues(float x, float y, int degree)
{
    std::vector<float> terms;
    terms.reserve(numPolyTerms(degree));

    for (int i = 0; i <= degree; ++i) {
        for (int j = 0; j <= degree - i; ++j) {
            terms.push_back(std::pow(x, i) * std::pow(y, j));
        }
    }

    return terms;
}

std::vector<float> fitPolynomial(const std::vector<Sample>& samples, int degree)
{
    int terms = numPolyTerms(degree);
    int N = static_cast<int>(samples.size());

    if (N < terms) {
        return {};
    }

    // ---- Coordinate normalization to [0,1] to prevent overflow ----
    float minX = samples[0].x, maxX = samples[0].x;
    float minY = samples[0].y, maxY = samples[0].y;

    for (const auto& s : samples) {
        if (s.x < minX) minX = s.x;
        if (s.x > maxX) maxX = s.x;
        if (s.y < minY) minY = s.y;
        if (s.y > maxY) maxY = s.y;
    }

    float rangeX = (maxX - minX) > 1e-6f ? (maxX - minX) : 1.0f;
    float rangeY = (maxY - minY) > 1e-6f ? (maxY - minY) : 1.0f;

    // ---- Assemble normal equations: (A^T A) c = A^T b ----
    std::vector<double> ATA(terms * terms, 0.0);
    std::vector<double> ATb(terms, 0.0);

    for (const auto& s : samples) {
        float nx = (s.x - minX) / rangeX;
        float ny = (s.y - minY) / rangeY;
        auto vars = getPolyTermValues(nx, ny, degree);

        for (int i = 0; i < terms; ++i) {
            for (int j = 0; j < terms; ++j) {
                ATA[i * terms + j] += static_cast<double>(vars[i]) * vars[j];
            }
            ATb[i] += static_cast<double>(vars[i]) * s.z;
        }
    }

    // ---- Solve the normal equations ----
    std::vector<double> coeffsD;
    if (!solveLinear(terms, ATA, coeffsD, ATb)) {
        // Fallback: return constant model using average intensity
        float avgZ = 0.0f;
        for (const auto& s : samples) {
            avgZ += s.z;
        }
        avgZ /= N;

        std::vector<float> fallback(terms, 0.0f);
        fallback[0] = avgZ;
        return fallback;
    }

    // Convert to float precision
    std::vector<float> coeffs(coeffsD.begin(), coeffsD.end());
    return coeffs;
}

float evalPolynomial(float x, float y,
                     const std::vector<float>& coeffs, int degree)
{
    if (coeffs.empty()) {
        return 0.0f;
    }

    auto vars = getPolyTermValues(x, y, degree);
    float sum = 0.0f;

    for (size_t i = 0; i < vars.size() && i < coeffs.size(); ++i) {
        sum += vars[i] * coeffs[i];
    }

    return sum;
}

// ============================================================================
// Radial Basis Function (RBF) interpolation
// ============================================================================

/**
 * @brief Compute squared Euclidean distance between two 2D points.
 */
static float distSq(float x1, float y1, float x2, float y2)
{
    float dx = x1 - x2;
    float dy = y1 - y2;
    return dx * dx + dy * dy;
}

/**
 * @brief Multiquadric RBF kernel: sqrt(r^2 + 1).
 *        The smooth parameter is reserved for future use.
 */
static float rbfKernel(float r2, [[maybe_unused]] float smooth)
{
    return std::sqrt(r2 + 1.0f);
}

RbfModel fitRbf(const std::vector<Sample>& samples, float smooth)
{
    int N = static_cast<int>(samples.size());

    std::vector<double> A(N * N);
    std::vector<double> b(N);
    std::vector<double> w;

    // Build the interpolation matrix with Tikhonov regularization
    for (int i = 0; i < N; ++i) {
        b[i] = samples[i].z;
        for (int j = 0; j < N; ++j) {
            float d2 = distSq(samples[i].x, samples[i].y,
                              samples[j].x, samples[j].y);
            double val = rbfKernel(d2, 0.0f);
            if (i == j) {
                val += smooth;  // Diagonal regularization
            }
            A[i * N + j] = val;
        }
    }

    if (!solveLinear(N, A, w, b)) {
        return {};
    }

    RbfModel model;
    model.centers = samples;
    for (double v : w) {
        model.weights.push_back(static_cast<float>(v));
    }
    model.smooth = smooth;

    return model;
}

float evalRbf(float x, float y, const RbfModel& model)
{
    float sum = 0.0f;
    for (size_t i = 0; i < model.centers.size(); ++i) {
        float d2 = distSq(x, y, model.centers[i].x, model.centers[i].y);
        sum += model.weights[i] * rbfKernel(d2, 0.0f);
    }
    return sum;
}

// ============================================================================
// Sampling utilities
// ============================================================================

float getMedianBox(const std::vector<float>& data, int w, int h,
                   int cx, int cy, int size)
{
    int half = size / 2;
    std::vector<float> vals;
    vals.reserve(size * size);

    for (int y = cy - half; y <= cy + half; ++y) {
        for (int x = cx - half; x <= cx + half; ++x) {
            if (x >= 0 && x < w && y >= 0 && y < h) {
                float v = data[y * w + x];
                if (!std::isnan(v)) {
                    vals.push_back(v);
                }
            }
        }
    }

    if (vals.empty()) {
        return 0.0f;
    }

    std::sort(vals.begin(), vals.end());
    return vals[vals.size() / 2];
}

Point findDimmest(const std::vector<float>& data, int w, int h,
                  int cx, int cy, int patchSize)
{
    int curX = cx;
    int curY = cy;
    float curVal = getMedianBox(data, w, h, curX, curY, patchSize);

    // 8-connected neighborhood offsets
    static const int dx[] = { -2,  0,  2, -2, 2, -2, 0, 2 };
    static const int dy[] = { -2, -2, -2,  0, 0,  2, 2, 2 };

    // Iterative gradient descent toward the local minimum median
    for (int iter = 0; iter < 20; ++iter) {
        int bestX = curX;
        int bestY = curY;
        float bestVal = curVal;
        bool found = false;

        for (int i = 0; i < 8; ++i) {
            int nx = curX + dx[i];
            int ny = curY + dy[i];

            if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                float v = getMedianBox(data, w, h, nx, ny, patchSize);
                if (v < bestVal) {
                    bestVal = v;
                    bestX = nx;
                    bestY = ny;
                    found = true;
                }
            }
        }

        if (!found) {
            break;
        }

        curX = bestX;
        curY = bestY;
        curVal = bestVal;
    }

    return { static_cast<float>(curX), static_cast<float>(curY) };
}

std::vector<Point> generateSamples(const std::vector<float>& data, int w, int h,
                                   int numSamples, int patchSize,
                                   const std::vector<bool>& exclusionMask)
{
    std::vector<Point> points;

    // Divide image into a uniform grid, one sample per cell
    int gridM = static_cast<int>(std::sqrt(numSamples));
    float stepX = static_cast<float>(w) / gridM;
    float stepY = static_cast<float>(h) / gridM;

    for (int gy = 0; gy < gridM; ++gy) {
        for (int gx = 0; gx < gridM; ++gx) {
            int cx = static_cast<int>(stepX * (gx + 0.5f));
            int cy = static_cast<int>(stepY * (gy + 0.5f));

            // Walk toward the dimmest patch in this cell
            Point p = findDimmest(data, w, h, cx, cy, patchSize);

            // Skip if the point falls on an excluded region
            if (!exclusionMask.empty()) {
                int px = static_cast<int>(p.x);
                int py = static_cast<int>(p.y);
                if (px >= 0 && px < w && py >= 0 && py < h) {
                    if (!exclusionMask[py * w + px]) {
                        continue;
                    }
                }
            }

            points.push_back(p);
        }
    }

    return points;
}

} // namespace AbeMath