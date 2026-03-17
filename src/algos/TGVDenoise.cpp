/*
 * TGVDenoise.cpp  —  Total Generalised Variation denoising (TGV²-L2)
 *
 * Chambolle–Pock primal-dual splitting.  All dual variables live in
 * double precision for numerical stability; the input/output data
 * buffer remains float.
 *
 * Layout convention: pixel (x, y) → index  y*w + x  (row-major).
 *
 * Boundary conditions: Neumann (zero-flux) by clamping index reads.
 */

#include "TGVDenoise.h"
#include "core/Logger.h"

#include <cmath>
#include <algorithm>
#include <numeric>
#include <vector>
#include <cassert>
#include <omp.h>

// ─── Helpers ──────────────────────────────────────────────────────────────────

static inline int clamp(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Forward difference (Neumann BC)
static inline double dx_fwd(const std::vector<double>& u, int x, int y, int w, int /*h*/) {
    return u[y*w + clamp(x+1, 0, w-1)] - u[y*w + x];
}
static inline double dy_fwd(const std::vector<double>& u, int x, int y, int w, int h) {
    return u[clamp(y+1, 0, h-1)*w + x] - u[y*w + x];
}
// Backward divergence (adjoint of forward difference)
static inline double dx_bwd(const std::vector<double>& p, int x, int y, int w, int /*h*/) {
    double a = p[y*w + x];
    double b = (x > 0) ? p[y*w + (x-1)] : 0.0;
    return a - b;
}
static inline double dy_bwd(const std::vector<double>& p, int x, int y, int w, int /*h*/) {
    double a = p[y*w + x];
    double b = (y > 0) ? p[(y-1)*w + x] : 0.0;
    return a - b;
}

// ─── Dual projection helpers ──────────────────────────────────────────────────

void TGVDenoise::projectP1(double& px, double& py, double alpha1) {
    double norm = std::sqrt(px*px + py*py);
    if (norm > alpha1) { double s = alpha1 / norm; px *= s; py *= s; }
}

void TGVDenoise::projectP0(double& qxx, double& qxy, double& qyx, double& qyy,
                            double alpha0)
{
    // Frobenius norm projection onto ball of radius alpha0
    double norm = std::sqrt(qxx*qxx + qxy*qxy + qyx*qyx + qyy*qyy);
    if (norm > alpha0) { double s = alpha0 / norm; qxx*=s; qxy*=s; qyx*=s; qyy*=s; }
}

// ─── Primal energy (for convergence diagnostics) ──────────────────────────────

double TGVDenoise::primalEnergy(const std::vector<float>& u,
                                const std::vector<float>& f,
                                const std::vector<float>& vx,
                                const std::vector<float>& vy,
                                int w, int h,
                                double lambda, double alpha0, double alpha1)
{
    double E = 0.0;
    #pragma omp parallel for reduction(+:E) schedule(static)
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int idx = y*w + x;
            double diff = u[idx] - f[idx];
            E += 0.5 * lambda * diff * diff;

            // |∇u − v|
            double gux = (x < w-1) ? (u[y*w+x+1] - u[idx]) : 0.0;
            double guy = (y < h-1) ? (u[(y+1)*w+x] - u[idx]) : 0.0;
            double ex  = gux - vx[idx];
            double ey  = guy - vy[idx];
            E += alpha1 * std::sqrt(ex*ex + ey*ey);

            // |εv| — symmetrised gradient of v
            double dvxx = (x < w-1) ? (vx[y*w+x+1] - vx[idx]) : 0.0;
            double dvyy = (y < h-1) ? (vy[(y+1)*w+x] - vy[idx]) : 0.0;
            double dvxy = 0.5*((x < w-1 ? vy[y*w+x+1] - vy[idx] : 0.0) +
                               (y < h-1 ? vx[(y+1)*w+x] - vx[idx] : 0.0));
            E += alpha0 * std::sqrt(dvxx*dvxx + 2.0*dvxy*dvxy + dvyy*dvyy);
        }
    }
    return E;
}

// ─── Core per-plane denoising ─────────────────────────────────────────────────

void TGVDenoise::denoisePlane(float* data, int w, int h,
                               const TGVParams& p, TGVResult* result)
{
    const int N = w * h;

    // Step sizes (auto-selection based on Chambolle–Pock theory: τσL²≤1, L=√12)
    const double L  = std::sqrt(12.0);
    double tau   = (p.tau  > 0.0) ? p.tau  : 1.0 / (L * std::sqrt(2.0));
    double sigma = (p.sigma > 0.0) ? p.sigma : 1.0 / (L * std::sqrt(2.0));
    const double lam = p.lambda;
    const double a0  = p.alpha0;
    const double a1  = p.alpha1;

    // ── Allocate primal and dual variables ─────────────────────────────────
    std::vector<double> u(N), u_bar(N);          // primal image
    std::vector<double> vx(N, 0.0), vy(N, 0.0); // primal vector field
    std::vector<double> vx_bar(N, 0.0), vy_bar(N, 0.0);

    // Dual variables
    std::vector<double> p1x(N, 0.0), p1y(N, 0.0);   // for α₁‖∇u−v‖
    std::vector<double> q0xx(N,0.0), q0xy(N,0.0),   // for α₀‖εv‖
                        q0yx(N,0.0), q0yy(N,0.0);

    // Initialise primal with input
    for (int i = 0; i < N; i++) { u[i] = data[i]; u_bar[i] = data[i]; }

    const double lam_inv = 1.0 / (1.0 + tau * lam);

    int iter = 0;
    double gap = 1e10;
    std::vector<double> u_prev(N);

    for (iter = 0; iter < p.maxIter && gap > p.tol; iter++) {

        // ── Dual update: p1 (gradient residual) ───────────────────────────
        #pragma omp parallel for schedule(static)
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int idx = y*w + x;
                double gx = dx_fwd(u_bar, x, y, w, h);
                double gy = dy_fwd(u_bar, x, y, w, h);
                double nx = p1x[idx] + sigma * (gx - vx_bar[idx]);
                double ny = p1y[idx] + sigma * (gy - vy_bar[idx]);
                projectP1(nx, ny, a1);
                p1x[idx] = nx;
                p1y[idx] = ny;
            }
        }

        // ── Dual update: q0 (symmetrised gradient of v) ───────────────────
        #pragma omp parallel for schedule(static)
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int idx = y*w + x;
                // εv:  ½(∇v + ∇vᵀ)
                double dvxx = dx_fwd(vx_bar, x, y, w, h);
                double dvyy = dy_fwd(vy_bar, x, y, w, h);
                double dvxy = 0.5 * (dy_fwd(vx_bar, x, y, w, h) + dx_fwd(vy_bar, x, y, w, h));
                double dvyx = dvxy;

                double nxx = q0xx[idx] + sigma * dvxx;
                double nxy = q0xy[idx] + sigma * dvxy;
                double nyx = q0yx[idx] + sigma * dvyx;
                double nyy = q0yy[idx] + sigma * dvyy;
                projectP0(nxx, nxy, nyx, nyy, a0);
                q0xx[idx] = nxx; q0xy[idx] = nxy;
                q0yx[idx] = nyx; q0yy[idx] = nyy;
            }
        }

        // ── Primal update: u  ─────────────────────────────────────────────
        // u_new = (u - τ·div(p1) + τ·λ·f) / (1 + τ·λ)
        #pragma omp parallel for schedule(static)
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int idx = y*w + x;
                double div_p1 = dx_bwd(p1x, x, y, w, h) + dy_bwd(p1y, x, y, w, h);
                double u_new  = (u[idx] - tau * div_p1 + tau * lam * data[idx]) * lam_inv;
                u_bar[idx] = 2.0 * u_new - u[idx];
                u[idx]     = u_new;
            }
        }

        // ── Primal update: v  ─────────────────────────────────────────────
        // v_new = v + τ·(p1 − div(q0))
        #pragma omp parallel for schedule(static)
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int idx = y*w + x;
                // div of symmetric tensor q0: row-wise divergence
                double div_qx = dx_bwd(q0xx, x, y, w, h) + dy_bwd(q0yx, x, y, w, h);
                double div_qy = dx_bwd(q0xy, x, y, w, h) + dy_bwd(q0yy, x, y, w, h);
                double vx_new = vx[idx] + tau * (p1x[idx] - div_qx);
                double vy_new = vy[idx] + tau * (p1y[idx] - div_qy);
                vx_bar[idx] = 2.0*vx_new - vx[idx];
                vy_bar[idx] = 2.0*vy_new - vy[idx];
                vx[idx] = vx_new;
                vy[idx] = vy_new;
            }
        }

        // ── Convergence check every 50 iterations ─────────────────────────
        if ((iter % 50) == 49) {
            // Relative change between iterates
            double num = 0.0, den = 0.0;
            for (int i = 0; i < N; i++) {
                double d = u[i] - u_prev[i];
                num += d*d;
                den += u[i]*u[i];
            }
            gap = (den > 1e-12) ? std::sqrt(num / den) : 0.0;
        }

        u_prev = u;
    }

    // Write result back to float buffer, clamped to [0,1]
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < N; i++) {
        data[i] = static_cast<float>(std::max(0.0, std::min(1.0, u[i])));
    }

    if (result) {
        result->success    = true;
        result->iterations = iter;
        result->finalGap   = gap;
    }
}

// ─── ImageBuffer wrapper ──────────────────────────────────────────────────────

TGVResult TGVDenoise::denoiseBuffer(ImageBuffer& buf, const TGVParams& p)
{
    TGVResult res;
    if (!buf.isValid()) {
        res.errorMsg = "Invalid image buffer.";
        return res;
    }

    const int w  = buf.width();
    const int h  = buf.height();
    const int ch = buf.channels();
    std::vector<float>& data = buf.data();

    TGVResult chRes;
    for (int c = 0; c < ch; c++) {
        std::vector<float> plane(static_cast<size_t>(w) * h);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const size_t idx = (static_cast<size_t>(y) * w + x) * ch + c;
                plane[static_cast<size_t>(y) * w + x] = data[idx];
            }
        }

        TGVResult cr;
        denoisePlane(plane.data(), w, h, p, &cr);
        if (!cr.success) { res.errorMsg = cr.errorMsg; return res; }

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const size_t idx = (static_cast<size_t>(y) * w + x) * ch + c;
                data[idx] = plane[static_cast<size_t>(y) * w + x];
            }
        }

        // Accumulate diagnostics
        res.iterations  = std::max(res.iterations, cr.iterations);
        res.finalGap   += cr.finalGap;
    }
    res.finalGap /= std::max(ch, 1);
    res.success   = true;
    buf.setModified(true);
    return res;
}
