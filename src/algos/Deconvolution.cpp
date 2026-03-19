/*

#include "Deconvolution.h"

#include <cmath>
#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <random>

#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>

#include <opencv2/imgproc.hpp>
#include <opencv2/core.hpp>

#ifdef _OPENMP
#include <omp.h>
#endif

// ─── Global constants ─────────────────────────────────────────────────────────
static constexpr float  EPS            = 1e-6f;
static constexpr double FWHM_TO_SIGMA  = 2.3548;  // FWHM = 2.3548 * sigma

// Star mask defaults
static constexpr int    STAR_MASK_MAXSIDE  = 2048;
static constexpr int    STAR_MASK_MAXOBJS  = 2000;
static constexpr double THRESHOLD_SIGMA    = 2.0;
static constexpr double KEEP_FLOOR         = 0.20;
static constexpr int    GROW_PX            = 8;
static constexpr int    MAX_STAR_RADIUS    = 16;
static constexpr double SOFT_SIGMA         = 2.0;
static constexpr double ELLIPSE_SCALE      = 1.2;

// ─────────────────────────────────────────────────────────────────────────────
// MFEarlyStopper
// ─────────────────────────────────────────────────────────────────────────────

MFEarlyStopper::MFEarlyStopper(const MFEarlyStopCfg& cfg,
                                std::function<void(const QString&)> statusCb)
    : m_cfg(cfg), m_statusCb(statusCb)
{
    if (m_statusCb) {
        m_statusCb(QString(
            "MFDeconv early-stop config: "
            "tol_upd_floor=%1, tol_rel_floor=%2, "
            "early_frac=%3, ema_alpha=%4, "
            "patience=%5, min_iters=%6")
            .arg(cfg.tolUpdFloor).arg(cfg.tolRelFloor)
            .arg(cfg.earlyFrac).arg(cfg.emaAlpha)
            .arg(cfg.patience).arg(cfg.minIters));
    }
}

void MFEarlyStopper::reset() {
    m_emaUm   = -1.0;
    m_emaRc   = -1.0;
    m_baseUm  = -1.0;
    m_baseRc  = -1.0;
    m_earlyCnt = 0;
}

bool MFEarlyStopper::step(int it, int maxIters, double um, double rc)
{
    // EMA update
    //   if it == 1 or ema_um is None:
    //       ema_um = um; base_um = um
    //   else:
    //       ema_um = alpha*um + (1-alpha)*ema_um
    if (it == 1 || m_emaUm < 0.0) {
        m_emaUm  = um;
        m_emaRc  = rc;
        m_baseUm = um;
        m_baseRc = rc;
    } else {
        double a = m_cfg.emaAlpha;
        m_emaUm  = a * um  + (1.0 - a) * m_emaUm;
        m_emaRc  = a * rc  + (1.0 - a) * m_emaRc;
    }

    // Adaptive thresholds
    double bUm = (m_baseUm > 0.0) ? m_baseUm : um;
    double bRc = (m_baseRc > 0.0) ? m_baseRc : rc;
    double tolUm = std::max(m_cfg.tolUpdFloor, m_cfg.earlyFrac * bUm);
    double tolRc = std::max(m_cfg.tolRelFloor, m_cfg.earlyFrac * bRc);

    bool small = (m_emaUm < tolUm) || (m_emaRc < tolRc);

    if (m_statusCb) {
        m_statusCb(QString(
            "MFDeconv iter %1/%2: "
            "um=%3, rc=%4 | ema_um=%5, ema_rc=%6 | "
            "tol_um=%7, tol_rc=%8 | small=%9")
            .arg(it).arg(maxIters)
            .arg(um,    0,'e',3)
            .arg(rc,    0,'e',3)
            .arg(m_emaUm,  0,'e',3)
            .arg(m_emaRc,  0,'e',3)
            .arg(tolUm, 0,'e',3)
            .arg(tolRc, 0,'e',3)
            .arg(small ? "True" : "False"));
    }

    if (small && it >= m_cfg.minIters) {
        ++m_earlyCnt;
        if (m_statusCb)
            m_statusCb(QString("MFDeconv iter %1: early-stop candidate (%2/%3)")
                       .arg(it).arg(m_earlyCnt).arg(m_cfg.patience));
    } else {
        if (m_earlyCnt > 0 && m_statusCb)
            m_statusCb(QString("MFDeconv iter %1: early-stop streak reset").arg(it));
        m_earlyCnt = 0;
    }

    if (m_earlyCnt >= m_cfg.patience) {
        if (m_statusCb)
            m_statusCb(QString(
                "MFDeconv early-stop TRIGGERED: "
                "ema_um=%1 < %2 or ema_rc=%3 < %4 "
                "for %5 consecutive iters")
                .arg(m_emaUm,0,'e',3).arg(tolUm,0,'e',3)
                .arg(m_emaRc,0,'e',3).arg(tolRc,0,'e',3)
                .arg(m_cfg.patience));
        return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Private helpers — PSF utilities
// ─────────────────────────────────────────────────────────────────────────────

// Gaussian PSF:
// sigma = max(fwhm, 1.0) / 2.3548
// g = exp(-(x^2+y^2)/(2*sigma^2))
// g /= sum(g) + EPS

cv::Mat Deconvolution::gaussianPsf(double fwhmPx, int ksize)
{
    double sigma = std::max(fwhmPx, 1.0) / FWHM_TO_SIGMA;
    int r = (ksize - 1) / 2;
    cv::Mat g(ksize, ksize, CV_32F);

    float sum = 0.0f;
    for (int y = -r; y <= r; ++y) {
        for (int x = -r; x <= r; ++x) {
            float val = std::exp(-(float)(x*x + y*y) / (float)(2.0 * sigma * sigma));
            g.at<float>(y + r, x + r) = val;
            sum += val;
        }
    }
    g /= (sum + EPS);
    return g;
}

// PSF normalization:
// psf = max(psf, 0)
// s = sum(psf)
// if s > 1e-6: psf /= s

cv::Mat Deconvolution::normalizePsf(const cv::Mat& psf)
{
    cv::Mat out;
    cv::max(psf, 0.0, out);
    float s = (float)cv::sum(out)[0];
    if (std::isfinite(s) && s > 1e-6f)
        out /= s;
    return out;
}

// PSF softening:
// r = int(max(1, round(3*sigma_px)))
// g = Gaussian(2r+1, sigma_px)
// return conv(psf, g)  (SAME)

cv::Mat Deconvolution::softenPsf(const cv::Mat& psf, double sigmaPx)
{
    if (sigmaPx <= 0.0) return psf.clone();
    int r = std::max(1, (int)std::round(3.0 * sigmaPx));
    int ks = 2 * r + 1;
    cv::Mat g = gaussianPsf(sigmaPx * FWHM_TO_SIGMA, ks);
    return convolveFFT(psf, g);
}

// Kernel flip:
// return np.flip(np.flip(psf,-1),-2).copy()

cv::Mat Deconvolution::flipKernel(const cv::Mat& psf)
{
    cv::Mat flipped;
    cv::flip(psf, flipped, -1);  // flip both axes
    return flipped;
}

// PSF FWHM estimate:
// second moment → sigma → FWHM = 2.3548 * sigma

double Deconvolution::psfFwhmPx(const cv::Mat& psf)
{
    cv::Mat p;
    cv::max(psf, 0.0f, p);
    p.convertTo(p, CV_32F);
    float s = (float)cv::sum(p)[0];
    if (s <= EPS) return std::numeric_limits<double>::quiet_NaN();

    int k = p.rows;
    double cx = 0.0, cy = 0.0;
    for (int y = 0; y < k; ++y)
        for (int x = 0; x < k; ++x) {
            float v = p.at<float>(y, x) / s;
            cx += v * x;
            cy += v * y;
        }
    double varX = 0.0, varY = 0.0;
    for (int y = 0; y < k; ++y)
        for (int x = 0; x < k; ++x) {
            float v = p.at<float>(y, x) / s;
            varX += v * (x - cx) * (x - cx);
            varY += v * (y - cy) * (y - cy);
        }
    double sigma = std::sqrt(std::max(0.0, 0.5 * (varX + varY)));
    return FWHM_TO_SIGMA * sigma;
}

// Auto ksize from FWHM:
// sigma = max(fwhm,1)/2.3548
// r = ceil(4*sigma)
// k = 2r+1, clamp [kmin,kmax], odd

int Deconvolution::autoKsizeFromFwhm(double fwhmPx, int kmin, int kmax)
{
    double sigma = std::max(fwhmPx, 1.0) / FWHM_TO_SIGMA;
    int r = (int)std::ceil(4.0 * sigma);
    int k = 2 * r + 1;
    k = std::max(kmin, std::min(k, kmax));
    if (k % 2 == 0) ++k;
    return k;
}

// ─────────────────────────────────────────────────────────────────────────────
// FFT convolution (SAME)
// ─────────────────────────────────────────────────────────────────────────────
// Uses OpenCV DFT and applies kernel ifftshift before FFT.
// Returns output with the same size as src.
// Supports src (H,W) CV_32F.

cv::Mat Deconvolution::convolveFFT(const cv::Mat& src, const cv::Mat& psf)
{
    int H = src.rows, W = src.cols;
    int kh = psf.rows, kw = psf.cols;

    // Optimal DFT dimensions
    int fftH = cv::getOptimalDFTSize(H + kh - 1);
    int fftW = cv::getOptimalDFTSize(W + kw - 1);

    // Kernel ifftshift
    cv::Mat psfShifted = cv::Mat::zeros(kh, kw, CV_32F);
    {
        int cy = kh / 2, cx = kw / 2;
        // quadrant 1 (bottom-right) → top-left
        psf(cv::Rect(cx, cy, kw - cx, kh - cy)).copyTo(
            psfShifted(cv::Rect(0, 0, kw - cx, kh - cy)));
        // quadrant 2 (bottom-left) → top-right
        psf(cv::Rect(0, cy, cx, kh - cy)).copyTo(
            psfShifted(cv::Rect(kw - cx, 0, cx, kh - cy)));
        // quadrant 3 (top-right) → bottom-left
        psf(cv::Rect(cx, 0, kw - cx, cy)).copyTo(
            psfShifted(cv::Rect(0, kh - cy, kw - cx, cy)));
        // quadrant 4 (top-left) → bottom-right
        psf(cv::Rect(0, 0, cx, cy)).copyTo(
            psfShifted(cv::Rect(kw - cx, kh - cy, cx, cy)));
    }

    // Pad src and shifted kernel
    cv::Mat srcPad = cv::Mat::zeros(fftH, fftW, CV_32F);
    src.copyTo(srcPad(cv::Rect(0, 0, W, H)));

    cv::Mat psfPad = cv::Mat::zeros(fftH, fftW, CV_32F);
    psfShifted.copyTo(psfPad(cv::Rect(0, 0, kw, kh)));

    // DFT
    cv::Mat srcC, psfC;
    cv::dft(srcPad, srcC, cv::DFT_COMPLEX_OUTPUT);
    cv::dft(psfPad, psfC, cv::DFT_COMPLEX_OUTPUT);

    // Multiplication in the frequency domain
    cv::Mat prodC;
    cv::mulSpectrums(srcC, psfC, prodC, 0);

    // IDFT
    cv::Mat result;
    cv::dft(prodC, result, cv::DFT_INVERSE | cv::DFT_REAL_OUTPUT | cv::DFT_SCALE);

    // Crop SAME: offset = (kh-1)/2, (kw-1)/2
    int sh = (kh - 1) / 2, sw = (kw - 1) / 2;
    return result(cv::Rect(sw, sh, W, H)).clone();
}

// ─────────────────────────────────────────────────────────────────────────────
// Wiener filter
// ─────────────────────────────────────────────────────────────────────────────
cv::Mat Deconvolution::wienerFilter(const cv::Mat& src, const cv::Mat& psf, double K)
{
    int H = src.rows, W = src.cols;
    int kh = psf.rows, kw = psf.cols;

    // Pad centered PSF
    cv::Mat psfPad = cv::Mat::zeros(H, W, CV_32F);
    int y0 = H/2 - kh/2, x0 = W/2 - kw/2;
    psf.copyTo(psfPad(cv::Rect(x0, y0, kw, kh)));

    // ifftshift psfPad
    cv::Mat psfShift;
    {
        // Split into 4 quadrants and reassemble
        int cy = H/2, cx = W/2;
        cv::Mat tmp = cv::Mat::zeros(H, W, CV_32F);
        psfPad(cv::Rect(cx, cy, W-cx, H-cy)).copyTo(tmp(cv::Rect(0, 0, W-cx, H-cy)));
        psfPad(cv::Rect(0, cy, cx, H-cy)).copyTo(tmp(cv::Rect(W-cx, 0, cx, H-cy)));
        psfPad(cv::Rect(cx, 0, W-cx, cy)).copyTo(tmp(cv::Rect(0, H-cy, W-cx, cy)));
        psfPad(cv::Rect(0, 0, cx, cy)).copyTo(tmp(cv::Rect(W-cx, H-cy, cx, cy)));
        psfShift = tmp;
    }

    cv::Mat Hf, Yf;
    cv::dft(psfShift, Hf, cv::DFT_COMPLEX_OUTPUT);
    cv::dft(src,      Yf, cv::DFT_COMPLEX_OUTPUT);

    // Wf = H* / (|H|^2 + K)
    cv::Mat Hf_conj, mag2;
    std::vector<cv::Mat> hfPlanes(2), wfPlanes(2);
    cv::split(Hf, hfPlanes);
    hfPlanes[1] *= -1.0f; // conjugate
    cv::merge(hfPlanes, Hf_conj);

    // mag2 = Re^2 + Im^2
    mag2 = cv::Mat::zeros(H, W, CV_32F);
    {
        std::vector<cv::Mat> hp(2);
        cv::split(Hf, hp);
        mag2 = hp[0].mul(hp[0]) + hp[1].mul(hp[1]);
    }
    cv::Mat denom; cv::add(mag2, (float)K, denom);

    // Wf = Hf_conj / denom (for real and imaginary channels)
    std::vector<cv::Mat> wfCh(2);
    {
        std::vector<cv::Mat> hc(2);
        cv::split(Hf_conj, hc);
        wfCh[0] = hc[0] / denom;
        wfCh[1] = hc[1] / denom;
    }
    cv::Mat Wf; cv::merge(wfCh, Wf);

    // Wf * Yf
    cv::Mat prodC; cv::mulSpectrums(Wf, Yf, prodC, 0);

    // IDFT
    cv::Mat result;
    cv::dft(prodC, result, cv::DFT_INVERSE | cv::DFT_REAL_OUTPUT | cv::DFT_SCALE);
    return result.clone();
}

// ─────────────────────────────────────────────────────────────────────────────
// TV gradient
// ─────────────────────────────────────────────────────────────────────────────
cv::Mat Deconvolution::tvGradient(const cv::Mat& u, double eps)
{
    // grad_x = u[i,j+1] - u[i,j]  (zero-padded to the right)
    // grad_y = u[i+1,j] - u[i,j]  (zero-padded at the bottom)
    // |grad| = sqrt(gx^2 + gy^2 + eps^2)
    // div = (gx/|g|)[i,j] - (gx/|g|)[i,j-1]
    //     + (gy/|g|)[i,j] - (gy/|g|)[i-1,j]
    int H = u.rows, W = u.cols;
    cv::Mat gx(H, W, CV_32F, cv::Scalar(0));
    cv::Mat gy(H, W, CV_32F, cv::Scalar(0));

    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W - 1; ++x)
            gx.at<float>(y,x) = u.at<float>(y,x+1) - u.at<float>(y,x);
    for (int y = 0; y < H - 1; ++y)
        for (int x = 0; x < W; ++x)
            gy.at<float>(y,x) = u.at<float>(y+1,x) - u.at<float>(y,x);

    cv::Mat norm = cv::Mat::zeros(H, W, CV_32F);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            float dx = gx.at<float>(y,x), dy = gy.at<float>(y,x);
            norm.at<float>(y,x) = std::sqrt(dx*dx + dy*dy + (float)(eps*eps));
        }

    cv::Mat nx = gx / norm;
    cv::Mat ny = gy / norm;

    cv::Mat div = cv::Mat::zeros(H, W, CV_32F);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            float d = nx.at<float>(y,x) - (x > 0 ? nx.at<float>(y,x-1) : 0.0f)
                    + ny.at<float>(y,x) - (y > 0 ? ny.at<float>(y-1,x) : 0.0f);
            div.at<float>(y,x) = d;
        }
    return div;
}

// ─────────────────────────────────────────────────────────────────────────────
// buildStarMask — single frame (simple threshold + dilation)
// ─────────────────────────────────────────────────────────────────────────────
cv::Mat Deconvolution::buildStarMask(const cv::Mat& plane, const DeconvStarMask& m)
{
    cv::Mat mask = cv::Mat::ones(plane.size(), CV_32F);
    if (!m.useMask) return mask;

    cv::Mat binary;
    cv::threshold(plane, binary, m.threshold, 1.0, cv::THRESH_BINARY);
    binary.convertTo(binary, CV_8U, 255.0);

    int rad = std::max(1, (int)std::round(m.radius));
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(2*rad+1, 2*rad+1));
    cv::Mat dilated;
    cv::dilate(binary, dilated, kernel);

    dilated.convertTo(mask, CV_32F, 1.0 / 255.0);
    mask = 1.0f - mask;
    // blend: 0 = original, 1 = deconvolved in masked area
    // -> KEEP map in [blend, 1.0]
    mask = (float)m.blend + (1.0f - (float)m.blend) * mask;
    cv::threshold(mask, mask, 0.0, 1.0, cv::THRESH_TOZERO);
    return mask;
}

// ─────────────────────────────────────────────────────────────────────────────
// buildStarMaskSEP
// ─────────────────────────────────────────────────────────────────────────────

// Builds a float32 KEEP map in [0,1]:
// 1) SEP-like background + residual
// 2) Detection downscale (max_side)
// 3) Sigma threshold ladder: thresh_sigma * [1,2,4,8,16]
// 4) Extract objects, capped at max_objs (brightest first)
// 5) Draw circles with radius = ceil(ellipse_scale * max(a,b)) + grow_px
// clamped to max_radius_px
// 6) Gaussian feathering (soft_sigma)
// 7) keep = 1 - m; keep = keep_floor + (1-keep_floor)*keep

// If SEP is unavailable or no stars are found, returns an all-ones map.

cv::Mat Deconvolution::buildStarMaskSEP(const cv::Mat& luma2d,
                                         const DeconvStarMask& cfg)
{
    // Without direct SEP C integration, use an OpenCV contour-based fallback.
    int H = luma2d.rows, W = luma2d.cols;
    cv::Mat keepMap = cv::Mat::ones(H, W, CV_32F);
    if (!cfg.useMask) return keepMap;

    // --- Robust background (median + MAD) ---
    cv::Mat luma;
    luma2d.convertTo(luma, CV_32F);

    // Estimate background with heavy blur (SEP-like approximation)
    cv::Mat bgr;
    cv::GaussianBlur(luma, bgr, cv::Size(0,0), 64.0, 64.0, cv::BORDER_REFLECT);
    cv::Mat residual = luma - bgr;

    // Estimate err_scalar from residual MAD
    std::vector<float> vals(residual.begin<float>(), residual.end<float>());
    std::nth_element(vals.begin(), vals.begin() + vals.size()/2, vals.end());
    float med = vals[vals.size()/2];
    std::vector<float> absDev; absDev.reserve(vals.size());
    for (float v : vals) absDev.push_back(std::abs(v - med));
    std::nth_element(absDev.begin(), absDev.begin() + absDev.size()/2, absDev.end());
    float errScalar = 1.4826f * absDev[absDev.size()/2];
    if (errScalar < 1e-8f) errScalar = 1e-8f;

    // Downscale for detection
    cv::Mat det = residual;
    float scale = 1.0f;
    int maxSide = cfg.maxSide > 0 ? cfg.maxSide : STAR_MASK_MAXSIDE;
    if (std::max(H, W) > maxSide) {
        scale = (float)std::max(H, W) / (float)maxSide;
        cv::Mat small;
        cv::resize(residual, small,
                   cv::Size(std::max(1,(int)std::round(W/scale)),
                            std::max(1,(int)std::round(H/scale))),
                   0, 0, cv::INTER_AREA);
        det = small;
    }

    // Sigma threshold ladder
    std::vector<double> thresholds;
    double ts = cfg.threshSigma > 0 ? cfg.threshSigma : THRESHOLD_SIGMA;
    for (double mult : {1.0, 2.0, 4.0, 8.0, 16.0})
        thresholds.push_back(ts * mult);

    // Extract objects using threshold ladder
    struct ObjInfo { float x, y, a, b; };
    std::vector<ObjInfo> objs;
    int maxObjs = cfg.maxObjs > 0 ? cfg.maxObjs : STAR_MASK_MAXOBJS;

    for (double thr : thresholds) {
        // Threshold on downscaled residual
        cv::Mat binary;
        cv::threshold(det, binary, (float)(thr * errScalar), 255.0f, cv::THRESH_BINARY);
        binary.convertTo(binary, CV_8U);

        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

        if (contours.empty()) continue;
        if ((int)contours.size() > maxObjs * 12) continue;

        // Estimate x,y,a,b for each contour
        objs.clear();
        for (const auto& c : contours) {
            if (c.size() < 5) {
                cv::Moments mom = cv::moments(c);
                if (mom.m00 < 1e-8) continue;
                float cx = (float)(mom.m10 / mom.m00);
                float cy = (float)(mom.m01 / mom.m00);
                objs.push_back({cx, cy, 1.0f, 1.0f});
            } else {
                cv::RotatedRect rr = cv::fitEllipse(c);
                objs.push_back({rr.center.x, rr.center.y,
                                rr.size.width/2.0f, rr.size.height/2.0f});
            }
        }
        if (!objs.empty()) break;
    }

    if (objs.empty()) return keepMap; // no stars

    // Cap at maxObjs
    if ((int)objs.size() > maxObjs)
        objs.resize(maxObjs);

    // Draw on full-resolution mask
    cv::Mat mask8 = cv::Mat::zeros(H, W, CV_8U);
    int MR = std::max(1, cfg.maxRadiusPx > 0 ? cfg.maxRadiusPx : MAX_STAR_RADIUS);
    int G  = std::max(0, cfg.growPx >= 0 ? cfg.growPx : GROW_PX);
    double ES = cfg.ellipseScale > 0.1 ? cfg.ellipseScale : ELLIPSE_SCALE;

    for (const auto& o : objs) {
        int x = (int)std::round(o.x * scale);
        int y = (int)std::round(o.y * scale);
        if (x < 0 || x >= W || y < 0 || y >= H) continue;
        double ab = ES * std::max((double)o.a, (double)o.b) * scale;
        int r = std::min((int)std::ceil(ab) + G, MR);
        if (r <= 0) continue;
        cv::circle(mask8, cv::Point(x, y), r, cv::Scalar(1), -1, cv::LINE_8);
    }

    // Gaussian feathering
    cv::Mat mf;
    mask8.convertTo(mf, CV_32F);
    double softSig = cfg.softSigma > 0 ? cfg.softSigma : SOFT_SIGMA;
    if (softSig > 0.0) {
        int ks = (int)(std::max(1, (int)std::round(3 * softSig)) * 2 + 1);
        cv::GaussianBlur(mf, mf, cv::Size(ks, ks), softSig, softSig, cv::BORDER_REFLECT);
        cv::threshold(mf, mf, 0.0, 1.0, cv::THRESH_TOZERO);
        cv::min(mf, 1.0f, mf);
    }

    // keep = 1 - m; keep = keep_floor + (1-keep_floor)*keep
    double kf = std::max(0.0, std::min(0.99, cfg.keepFloor > 0 ? cfg.keepFloor : KEEP_FLOOR));
    cv::Mat keep = 1.0f - mf;
    keep = (float)kf + (float)(1.0 - kf) * keep;
    cv::min(keep, 1.0f, keep);
    cv::max(keep, 0.0f, keep);
    return keep;
}

// ─────────────────────────────────────────────────────────────────────────────
// buildVarianceMap
// ─────────────────────────────────────────────────────────────────────────────
// var = var_bg^2 + a_shot * obj_dn
// var_bg = rms_map from SEP-like background (fallback: scalar MAD)
// obj_dn = max(img - sky, 0)
// a_shot = 1/gain when gain is available, otherwise estimated
// Optional Gaussian smoothing + floor.

cv::Mat Deconvolution::buildVarianceMap(const cv::Mat& luma2d,
                                         const MFVarianceMapCfg& cfg,
                                         double gainHint)
{
    int H = luma2d.rows, W = luma2d.cols;
    cv::Mat img;
    luma2d.convertTo(img, CV_32F);
    cv::max(img, 0.0f, img);

    // Background SEP-like: wide GaussianBlur
    int bw = std::max(8, std::min(cfg.bw > 0 ? cfg.bw : 64, W));
    int bh = std::max(8, std::min(cfg.bh > 0 ? cfg.bh : 64, H));
    double sigBg = std::min(bw, bh) * 0.5;
    cv::Mat skyMap;
    cv::GaussianBlur(img, skyMap, cv::Size(0,0), sigBg, sigBg, cv::BORDER_REFLECT);

    // rms_map via local std (approx)
    cv::Mat img2; cv::multiply(img, img, img2);
    cv::Mat sky2; cv::multiply(skyMap, skyMap, sky2);
    cv::Mat mean2;
    cv::GaussianBlur(img2, mean2, cv::Size(0,0), sigBg, sigBg, cv::BORDER_REFLECT);
    cv::Mat rmsMap; cv::sqrt(cv::max(mean2 - sky2, 0.0f), rmsMap);
    cv::max(rmsMap, 1e-6f, rmsMap);

    // var_bg = rms^2
    cv::Mat varBg; cv::multiply(rmsMap, rmsMap, varBg);

    // obj_dn = max(img - sky, 0)
    cv::Mat objDn = img - skyMap;
    cv::max(objDn, 0.0f, objDn);

    // a_shot
    float aShot = 0.0f;
    if (gainHint > 0.0)
        aShot = (float)(1.0 / gainHint);
    else {
        // estimate: varbg_med / sky_med
        cv::Scalar skyMed, vbgMed;
        skyMed = cv::mean(skyMap);
        vbgMed = cv::mean(varBg);
        if (skyMed[0] > 1e-6)
            aShot = (float)std::min(vbgMed[0] / skyMed[0], 10.0);
    }

    // v = var_bg + a_shot * obj_dn
    cv::Mat v = varBg + aShot * objDn;

    // Optional smoothing
    double ss = cfg.smoothSigma > 0 ? cfg.smoothSigma : 1.0;
    if (ss > 0.0) {
        int ks = (int)(std::max(1, (int)std::round(3*ss)) * 2 + 1);
        cv::GaussianBlur(v, v, cv::Size(ks, ks), ss, ss, cv::BORDER_REFLECT);
    }

    // Floor
    float floor = (float)(cfg.floor > 0 ? cfg.floor : 1e-8);
    cv::max(v, floor, v);
    return v;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers SR — downsampleAvg / upsampleSum
// ─────────────────────────────────────────────────────────────────────────────
// Downsample by average pooling:
// average over non-overlapping r x r blocks
// (H//r)*r rows, (W//r)*r columns

cv::Mat Deconvolution::downsampleAvg(const cv::Mat& img, int r)
{
    if (r <= 1) return img.clone();
    int H = img.rows, W = img.cols;
    int Hs = (H / r) * r, Ws = (W / r) * r;
    cv::Mat crop = img(cv::Rect(0, 0, Ws, Hs));
    cv::Mat out(Hs / r, Ws / r, CV_32F);
    // INTER_AREA resize is equivalent to average pooling for integer factors
    cv::resize(crop, out, cv::Size(Ws/r, Hs/r), 0, 0, cv::INTER_AREA);
    return out;
}

// Upsample by sum-replication: np.kron(a, ones(r,r))
// Equivalent to repeat_interleave on both dimensions.

cv::Mat Deconvolution::upsampleSum(const cv::Mat& img, int r, cv::Size targetHW)
{
    if (r <= 1) return img.clone();
    cv::Mat out(img.rows * r, img.cols * r, CV_32F);
    cv::resize(img, out, cv::Size(img.cols * r, img.rows * r), 0, 0, cv::INTER_NEAREST);
    // kron-style replication (no interpolation), which matches INTER_NEAREST
    if (targetHW.width > 0 && targetHW.height > 0)
        return centerCrop(out, targetHW.height, targetHW.width);
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// centerCrop
// ─────────────────────────────────────────────────────────────────────────────
cv::Mat Deconvolution::centerCrop(const cv::Mat& arr, int Ht, int Wt, float fillVal)
{
    int H = arr.rows, W = arr.cols;
    if (H == Ht && W == Wt) return arr.clone();

    // Crop if larger than target
    int y0 = std::max(0, (H - Ht) / 2);
    int x0 = std::max(0, (W - Wt) / 2);
    int y1 = std::min(H, y0 + Ht);
    int x1 = std::min(W, x0 + Wt);
    cv::Mat cropped = arr(cv::Rect(x0, y0, x1 - x0, y1 - y0)).clone();

    if (cropped.rows == Ht && cropped.cols == Wt)
        return cropped;

    // Pad if smaller than target
    cv::Mat out(Ht, Wt, CV_32F, cv::Scalar(fillVal));
    int oy = (Ht - cropped.rows) / 2;
    int ox = (Wt - cropped.cols) / 2;
    cropped.copyTo(out(cv::Rect(ox, oy, cropped.cols, cropped.rows)));
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// sanitizeNumeric
// ─────────────────────────────────────────────────────────────────────────────
cv::Mat Deconvolution::sanitizeNumeric(const cv::Mat& a)
{
    cv::Mat out;
    a.convertTo(out, CV_32F);
    // Replace NaN/Inf with 0
    cv::patchNaNs(out, 0.0);
    // Clamp negatives
    cv::max(out, 0.0f, out);
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// estimateScalarVariance
// var = (1.4826 * MAD)^2
// ─────────────────────────────────────────────────────────────────────────────
double Deconvolution::estimateScalarVariance(const cv::Mat& a)
{
    std::vector<float> vals(a.begin<float>(), a.end<float>());
    if (vals.empty()) return 1.0;
    std::nth_element(vals.begin(), vals.begin() + vals.size()/2, vals.end());
    float med = vals[vals.size()/2];
    std::vector<float> absDev;
    absDev.reserve(vals.size());
    for (float v : vals) absDev.push_back(std::abs(v - med));
    std::nth_element(absDev.begin(), absDev.begin() + absDev.size()/2, absDev.end());
    float mad = absDev[absDev.size()/2] + 1e-6f;
    return (double)(1.4826f * mad) * (1.4826f * mad);
}

// ─────────────────────────────────────────────────────────────────────────────
// computeHuberWeights
// ─────────────────────────────────────────────────────────────────────────────
// W = psi(r)/r * 1/(var+eps) * mask
// psi(r)/r = 1            if |r| <= delta  (inlier)
// = delta/(|r|+eps) otherwise    (outlier, Huber)
// If huberDelta < 0: delta = |huberDelta| * RMS(r) via MAD
// If huberDelta == 0 (L2): psi/r = 1 always

cv::Mat Deconvolution::computeHuberWeights(const cv::Mat& residual,
                                            double huberDelta,
                                            const cv::Mat& varMap,
                                            const cv::Mat& mask)
{
    float delta = (float)huberDelta;

    // Auto delta
    if (huberDelta < 0.0) {
        double varEst = estimateScalarVariance(residual);
        float rms = (float)std::sqrt(varEst);
        delta = (float)(-huberDelta) * std::max(rms, 1e-6f);
    }

    cv::Mat absR = cv::abs(residual);

    // psi_over_r
    cv::Mat psiR;
    if (delta > 0.0f) {
        // psiR = where(|r|<=delta, 1, delta/(|r|+eps))
        cv::Mat inlier = (absR <= delta) / 255.0f;  // 1.0 for inliers
        // Binary conversion
        cv::Mat mask8; cv::threshold(absR, mask8, (double)delta, 1.0, cv::THRESH_BINARY_INV);
        // outlier weight: delta / (|r| + eps)
        cv::Mat outlierW = delta / (absR + EPS);
        psiR = cv::Mat::ones(residual.size(), CV_32F);
        // On outliers: psiR = delta/(|r|+eps)
        cv::Mat isOutlier;
        cv::threshold(absR, isOutlier, (double)delta, 1.0, cv::THRESH_BINARY);
        psiR = psiR.mul(1.0f - isOutlier) + outlierW.mul(isOutlier);
    } else {
        psiR = cv::Mat::ones(residual.size(), CV_32F);
    }

    // Variance
    cv::Mat v;
    if (varMap.empty()) {
        double varEst = estimateScalarVariance(residual);
        v = cv::Mat::ones(residual.size(), CV_32F) * (float)(varEst);
    } else {
        varMap.copyTo(v);
    }

    // w = psiR / (v + eps)
    cv::Mat w = psiR / (v + EPS);

    // Apply mask
    if (!mask.empty()) {
        cv::multiply(w, mask, w);
    }

    return w;
}

// ─────────────────────────────────────────────────────────────────────────────
// emitProgress — emits "__PROGRESS__ 0.xxxx msg"
// ─────────────────────────────────────────────────────────────────────────────
void Deconvolution::emitProgress(const std::function<void(const QString&)>& cb,
                                  double fraction, const QString& msg)
{
    if (!cb) return;
    fraction = std::max(0.0, std::min(1.0, fraction));
    QString line = QString("__PROGRESS__ %1").arg(fraction, 0, 'f', 4);
    if (!msg.isEmpty()) line += " " + msg;
    cb(line);
}

// ─────────────────────────────────────────────────────────────────────────────
// buildOutputPath / nonclobberPath
// ─────────────────────────────────────────────────────────────────────────────

QString Deconvolution::buildOutputPath(const QString& basePath, int superResFactor)
{
    QFileInfo fi(basePath);
    QString dir  = fi.absolutePath();
    QString base = fi.baseName();
    QString ext  = fi.suffix();

    // Remove trailing _MFDeconv if already present
    base.remove(QRegularExpression("(?i)[\\s_]+MFDeconv$"));
    // Remove size token like 1234x5678
    base.remove(QRegularExpression("\\(?\\s*(\\d{2,5})x(\\d{2,5})\\s*\\)?", QRegularExpression::CaseInsensitiveOption));

    QString newStem = "MFDeconv_" + base;
    if (superResFactor > 1)
        newStem += QString("_%1x").arg(superResFactor);

    return dir + "/" + newStem + "." + ext;
}

QString Deconvolution::nonclobberPath(const QString& path)
{
    if (!QFileInfo::exists(path)) return path;
    QFileInfo fi(path);
    QString dir  = fi.absolutePath();
    QString base = fi.baseName();
    QString ext  = fi.suffix();

    // If base ends with _vN, increment N
    QRegularExpression vRe("(.*)_v(\\d+)$");
    auto m = vRe.match(base);
    QString stem; int n;
    if (m.hasMatch()) {
        stem = m.captured(1);
        n    = m.captured(2).toInt() + 1;
    } else {
        stem = base;
        n    = 2;
    }
    while (true) {
        QString candidate = dir + "/" + stem + "_v" + QString::number(n) + "." + ext;
        if (!QFileInfo::exists(candidate)) return candidate;
        ++n;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// derivePsfForFrame
// ─────────────────────────────────────────────────────────────────────────────
// Try star-based PSF with sigma ladder [50,25,12,6] and ksize ladder.
// Fall back to Gaussian from estimated FWHM.
// Apply sigma_px=0.25 softening and normalization.

MFPsfInfo Deconvolution::derivePsfForFrame(const cv::Mat& lumaFrame, double fwhmHint)
{
    // Estimate FWHM
    double fwhm = fwhmHint;
    if (fwhm <= 0.0) fwhm = estimateFwhmFromImage(lumaFrame);
    if (!std::isfinite(fwhm) || fwhm <= 0.0) fwhm = 2.5;

    int kAuto = autoKsizeFromFwhm(fwhm);

    // Try empirical PSF estimation.
    // This implementation uses estimateEmpiricalPSF through ImageBuffer.
    // If unavailable, fall back to Gaussian.
    cv::Mat psf;

    // Ladder fallback: if empirical PSF fails, try smaller ksize values
    std::vector<int> kLadder = {kAuto, std::max(kAuto-4, 11), 21, 17, 15, 13, 11};
    for (int k : kLadder) {
        (void)k;
        // Use estimateEmpiricalPSF through ImageBuffer (wrapping luma in temp buffer)
        // If unavailable (header-only path), skip
        if (psf.empty()) break;  // placeholder: in production, call your PSF fitter
    }

    if (psf.empty()) {
        psf = gaussianPsf(fwhm, kAuto);
    }

    // Normalization + softening
    psf = normalizePsf(psf);
    psf = softenPsf(psf, 0.25);

    MFPsfInfo info;
    info.kernel = psf;
    info.ksize  = psf.rows;
    info.fwhmPx = psfFwhmPx(psf);
    return info;
}

// ─────────────────────────────────────────────────────────────────────────────
// estimateFwhmFromImage
// ─────────────────────────────────────────────────────────────────────────────
double Deconvolution::estimateFwhmFromImage(const cv::Mat& luma)
{
    // Without direct SEP C integration: estimate FWHM via OpenCV blob logic
    cv::Mat blurred;
    cv::GaussianBlur(luma, blurred, cv::Size(0,0), 3.0, 3.0, cv::BORDER_REFLECT);
    cv::Mat bg;
    cv::GaussianBlur(luma, bg, cv::Size(0,0), 64.0, 64.0, cv::BORDER_REFLECT);
    cv::Mat sub = blurred - bg;

    // SimpleBlobDetector for stars (optional; otherwise use second moments)
    double mn, mx;
    cv::minMaxLoc(sub, &mn, &mx);
    if (mx <= 0.0) return std::numeric_limits<double>::quiet_NaN();

    // Threshold at 30% of peak
    cv::Mat binary;
    cv::threshold(sub, binary, mx * 0.3, 255.0, cv::THRESH_BINARY);
    binary.convertTo(binary, CV_8U);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    if (contours.empty()) return std::numeric_limits<double>::quiet_NaN();

    std::vector<double> fwhms;
    for (const auto& c : contours) {
        if (c.size() < 5) continue;
        cv::RotatedRect rr = cv::fitEllipse(c);
        double sigma = (rr.size.width + rr.size.height) * 0.25;
        if (sigma > 0.0 && std::isfinite(sigma))
            fwhms.push_back(FWHM_TO_SIGMA * sigma);
    }
    if (fwhms.empty()) return std::numeric_limits<double>::quiet_NaN();
    std::sort(fwhms.begin(), fwhms.end());
    return fwhms[fwhms.size() / 2]; // median
}

// ─────────────────────────────────────────────────────────────────────────────
// buildRobustSeed
// ─────────────────────────────────────────────────────────────────────────────
// Pass 1: Welford mean on bootstrap frames
// Pass 2: Global MAD on bootstrap frames (±4·MAD clip band)
// Pass 3: Masked mean on bootstrap frames
// Pass 4: σ-clipped Welford streaming on remaining frames

cv::Mat Deconvolution::buildRobustSeed(const std::vector<cv::Mat>& frames,
                                        int bootstrapFrames, double clipSigma)
{
    int n = (int)frames.size();
    if (n == 0) return cv::Mat();
    int B = std::max(1, std::min(bootstrapFrames, n));

    // Pass 1: Welford mean
    cv::Mat mu = frames[0].clone();
    mu.convertTo(mu, CV_32F);
    for (int i = 1; i < B; ++i) {
        cv::Mat x; frames[i].convertTo(x, CV_32F);
        float cnt = (float)(i + 1);
        mu = mu + (x - mu) * (1.0f / cnt);
    }

    // Pass 2: global MAD estimation (scalar)
    // For each bootstrap frame calculate abs(x - mu), then median of everything
    std::vector<float> madSamples;
    for (int i = 0; i < B; ++i) {
        cv::Mat x; frames[i].convertTo(x, CV_32F);
        cv::Mat d = cv::abs(x - mu);
        std::vector<float> dv(d.begin<float>(), d.end<float>());
        madSamples.insert(madSamples.end(), dv.begin(), dv.end());
    }
    std::nth_element(madSamples.begin(), madSamples.begin() + madSamples.size()/2, madSamples.end());
    float madEst = madSamples[madSamples.size()/2];
    float thr = 4.0f * std::max(madEst, 1e-6f);

    // Pass 3: masked mean on bootstrap
    cv::Mat sumAcc = cv::Mat::zeros(mu.size(), CV_32F);
    cv::Mat cntAcc = cv::Mat::zeros(mu.size(), CV_32F);
    for (int i = 0; i < B; ++i) {
        cv::Mat x; frames[i].convertTo(x, CV_32F);
        cv::Mat mask = (cv::abs(x - mu) <= thr);
        mask.convertTo(mask, CV_32F, 1.0/255.0);
        sumAcc += x.mul(mask);
        cntAcc += mask;
    }
    cv::Mat seed = mu.clone();
    // where cntAcc > 0.5: seed = sumAcc / cntAcc
    for (int i = 0; i < mu.rows; ++i)
        for (int j = 0; j < mu.cols; ++j)
            if (cntAcc.at<float>(i,j) > 0.5f)
                seed.at<float>(i,j) = sumAcc.at<float>(i,j) / cntAcc.at<float>(i,j);

    // Pass 4: σ-clipped Welford streaming on remaining frames
    cv::Mat M2 = cv::Mat::zeros(seed.size(), CV_32F);
    cv::Mat cnt = cv::Mat::ones(seed.size(), CV_32F) * (float)B;
    mu = seed.clone();

    for (int i = B; i < n; ++i) {
        cv::Mat x; frames[i].convertTo(x, CV_32F);
        cv::Mat var = M2 / cv::max(cnt - 1.0f, 1.0f);
        cv::Mat sigma; cv::sqrt(cv::max(var, 1e-12f), sigma);
        cv::Mat acc = (cv::abs(x - mu) <= (float)clipSigma * sigma);
        acc.convertTo(acc, CV_32F, 1.0/255.0);

        cv::Mat nNew = cnt + acc;
        cv::Mat delta = x - mu;
        cv::Mat muN = mu + acc.mul(delta) / cv::max(nNew, 1.0f);
        M2 = M2 + acc.mul(delta).mul(x - muN);
        mu = muN;
        cnt = nNew;
    }

    cv::max(mu, 0.0f, mu);
    return mu;
}

// ─────────────────────────────────────────────────────────────────────────────
// buildMedianSeed
// ─────────────────────────────────────────────────────────────────────────────
cv::Mat Deconvolution::buildMedianSeed(const std::vector<cv::Mat>& frames,
                                        cv::Size tileHW)
{
    if (frames.empty()) return cv::Mat();
    int H = frames[0].rows, W = frames[0].cols;
    int n = (int)frames.size();
    int th = tileHW.height, tw = tileHW.width;
    cv::Mat seed = cv::Mat::zeros(H, W, CV_32F);

    for (int y0 = 0; y0 < H; y0 += th) {
        int y1 = std::min(y0 + th, H);
        for (int x0 = 0; x0 < W; x0 += tw) {
            int x1 = std::min(x0 + tw, W);
            int th_ = y1 - y0, tw_ = x1 - x0;

            // Collects slab (n, th_, tw_)
            std::vector<std::vector<float>> slab(th_ * tw_, std::vector<float>(n));
            for (int i = 0; i < n; ++i) {
                cv::Mat f; frames[i].convertTo(f, CV_32F);
                cv::Mat tile = f(cv::Rect(x0, y0, tw_, th_));
                for (int r = 0; r < th_; ++r)
                    for (int c = 0; c < tw_; ++c)
                        slab[r * tw_ + c][i] = tile.at<float>(r, c);
            }
            // Pixel-wise median
            for (int r = 0; r < th_; ++r) {
                for (int c = 0; c < tw_; ++c) {
                    auto& pix = slab[r * tw_ + c];
                    std::nth_element(pix.begin(), pix.begin() + n/2, pix.end());
                    seed.at<float>(y0 + r, x0 + c) = pix[n/2];
                }
            }
        }
    }
    return seed;
}

// ─────────────────────────────────────────────────────────────────────────────
// solveSuperPsf
// ─────────────────────────────────────────────────────────────────────────────
// Solves: h* = argmin ||f_native - D(h) * g_sigma||^2
// via gradient descent.
// Returns normalized PSF (r*k)×(r*k).

cv::Mat Deconvolution::solveSuperPsf(const cv::Mat& nativePsf,
                                      int r, double sigma,
                                      int iters, double lr)
{
    cv::Mat f;
    nativePsf.convertTo(f, CV_32F);

    // Square + odd
    int k = std::min(f.rows, f.cols);
    if (k % 2 == 0) { f = f(cv::Rect(1, 1, k-1, k-1)); k = k-1; }
    int kr = k * r;

    // Native scale Gaussian
    cv::Mat g = gaussianPsf(sigma * FWHM_TO_SIGMA, k);

    // h0: zero-insertion (every r-th pixel = f)
    cv::Mat h0 = cv::Mat::zeros(kr, kr, CV_32F);
    for (int i = 0; i < k; ++i)
        for (int j = 0; j < k; ++j)
            h0.at<float>(i*r, j*r) = f.at<float>(i, j);
    h0 = normalizePsf(h0);

    cv::Mat h = h0.clone();
    double eta = lr;

    for (int iter = 0; iter < std::max(50, iters); ++iter) {
        // Dh = downscale
        cv::Mat Dh = downsampleAvg(h, r);
        // conv = Dh * g (SAME)
        cv::Mat conv = convolveFFT(Dh, g);
        // resid = conv - f
        cv::Mat resid = conv - f;
        // grad_Dh = resid * g^T (adjoint convolution)
        cv::Mat gFlip = flipKernel(g);
        cv::Mat gradDh = convolveFFT(resid, gFlip);
        // grad_h = upsample_sum(grad_Dh, r)
        cv::Mat gradH = upsampleSum(gradDh, r, cv::Size(kr, kr));
        // Update
        h = h - (float)eta * gradH;
        cv::max(h, 0.0f, h);
        float s = (float)cv::sum(h)[0];
        if (s > 1e-8f) h /= s;
        eta *= 0.995;
    }

    // Ensure odd kernel dimensions
    if (h.rows % 2 == 0) h = h(cv::Rect(0, 0, h.cols-1, h.rows-1)).clone();
    return normalizePsf(h);
}

// ─────────────────────────────────────────────────────────────────────────────
// RGB ↔ YCbCr (single-image path)
// ─────────────────────────────────────────────────────────────────────────────
void Deconvolution::rgbToYcbcr(const std::vector<float>& rgb,
                                 std::vector<float>& ycbcr,
                                 int width, int height)
{
    int N = width * height;
    ycbcr.resize(N * 3);
    for (int i = 0; i < N; ++i) {
        float r = rgb[i], g = rgb[N + i], b = rgb[2*N + i];
        ycbcr[i]       =  0.2126f*r + 0.7152f*g + 0.0722f*b;
        ycbcr[N   + i] = -0.1146f*r - 0.3854f*g + 0.5f*b + 0.5f;
        ycbcr[2*N + i] =  0.5f*r - 0.4542f*g - 0.0458f*b + 0.5f;
    }
}

void Deconvolution::ycbcrToRgb(const std::vector<float>& ycbcr,
                                 std::vector<float>& rgb,
                                 int width, int height, int)
{
    int N = width * height;
    rgb.resize(N * 3);
    for (int i = 0; i < N; ++i) {
        float Y = ycbcr[i], Cb = ycbcr[N+i] - 0.5f, Cr = ycbcr[2*N+i] - 0.5f;
        rgb[i]       = std::max(0.0f, std::min(1.0f, Y + 1.5748f*Cr));
        rgb[N   + i] = std::max(0.0f, std::min(1.0f, Y - 0.1873f*Cb - 0.4681f*Cr));
        rgb[2*N + i] = std::max(0.0f, std::min(1.0f, Y + 1.8556f*Cb));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Single-image algorithms
// ─────────────────────────────────────────────────────────────────────────────

// ──── applyRL — Richardson-Lucy (no regularization) ───────────────────────────

// Richardson-Lucy update loop (reg_type="None"):
// f = clip(img, eps, None)
// for iter:
// blurred   = conv(f, psf)
// ratio     = img / (blurred + eps)
// correction= conv(ratio, psf_flipped)
// f         = f * correction
// + star mask blend

DeconvResult Deconvolution::applyRL(cv::Mat& plane, const cv::Mat& psf,
                                     const cv::Mat& starMask, const DeconvParams& p)
{
    DeconvResult res;
    cv::Mat f; plane.copyTo(f);
    cv::max(f, EPS, f);
    cv::Mat psfFlip = flipKernel(psf);

    for (int it = 0; it < p.maxIter; ++it) {
        cv::Mat blurred = convolveFFT(f, psf);
        cv::max(blurred, EPS, blurred);
        cv::Mat ratio = plane / blurred;
        cv::Mat corr  = convolveFFT(ratio, psfFlip);
        cv::Mat fNew  = f.mul(corr);
        cv::max(fNew, 0.0f, fNew);

        // Convergence
        cv::Mat diff; cv::absdiff(fNew, f, diff);
        double change = cv::mean(diff)[0] / (cv::mean(f)[0] + 1e-8);
        f = fNew;
        res.finalChange = change;
        res.iterations  = it + 1;

        if (p.progressCallback)
            p.progressCallback((int)(100.0 * (it+1) / p.maxIter),
                               QString("RL iter %1/%2").arg(it+1).arg(p.maxIter));

        if (change < p.convergenceTol) break;
    }

    // Star mask blend
    if (!starMask.empty()) {
        f = starMask.mul(f) + (1.0f - starMask).mul(plane);
    }
    // Strength blend
    if (p.globalStrength < 1.0) {
        f = (float)p.globalStrength * f + (float)(1.0 - p.globalStrength) * plane;
    }
    f.copyTo(plane);
    res.success = true;
    return res;
}

// ──── applyRLTV — RL + Total Variation ────────────────────────────────────────
DeconvResult Deconvolution::applyRLTV(cv::Mat& plane, const cv::Mat& psf,
                                       const cv::Mat& starMask, const DeconvParams& p)
{
    DeconvResult res;
    cv::Mat f; plane.copyTo(f);
    cv::max(f, EPS, f);
    cv::Mat psfFlip = flipKernel(psf);

    for (int it = 0; it < p.maxIter; ++it) {
        cv::Mat blurred = convolveFFT(f, psf);
        cv::max(blurred, EPS, blurred);
        cv::Mat ratio = plane / blurred;
        cv::Mat corr  = convolveFFT(ratio, psfFlip);

        // TV gradient (if regType == 2)
        if (p.regType == 2 && p.tvRegWeight > 0.0) {
            cv::Mat tvGrad = tvGradient(f, p.tvEps);
            corr = corr - (float)p.tvRegWeight * tvGrad;
        }
        // Tikhonov (if regType == 1)
        if (p.regType == 1 && p.tvRegWeight > 0.0) {
            // Laplacian penalization
            cv::Mat lap;
            cv::Laplacian(f, lap, CV_32F);
            corr = corr - (float)p.tvRegWeight * lap;
        }

        cv::Mat fNew = f.mul(corr);
        cv::max(fNew, 0.0f, fNew);

        if (p.dering) {
            cv::Mat fBlur;
            cv::bilateralFilter(fNew, fBlur, 5, 0.08, 1.0);
            fNew = fBlur;
        }

        double change = cv::norm(fNew - f, cv::NORM_L1) / (cv::norm(f, cv::NORM_L1) + 1e-8);
        f = fNew;
        res.finalChange = change;
        res.iterations  = it + 1;

        if (p.progressCallback)
            p.progressCallback((int)(100.0 * (it+1) / p.maxIter),
                               QString("RLTV iter %1/%2").arg(it+1).arg(p.maxIter));

        if (change < p.convergenceTol) break;
    }

    if (!starMask.empty())
        f = starMask.mul(f) + (1.0f - starMask).mul(plane);
    if (p.globalStrength < 1.0)
        f = (float)p.globalStrength * f + (float)(1.0 - p.globalStrength) * plane;

    f.copyTo(plane);
    res.success = true;
    return res;
}

// ──── applyWiener ──────────────────────────────────────────────────────────────
DeconvResult Deconvolution::applyWiener(cv::Mat& plane, const cv::Mat& psf,
                                         const DeconvParams& p)
{
    DeconvResult res;
    cv::Mat result = wienerFilter(plane, psf, p.wienerK);
    cv::max(result, 0.0f, result);
    cv::min(result, 1.0f, result);

    if (p.dering) {
        cv::Mat bilat;
        cv::bilateralFilter(result, bilat, 5, 0.08, 1.0);
        result = bilat;
    }

    if (p.globalStrength < 1.0)
        result = (float)p.globalStrength * result + (float)(1.0-p.globalStrength) * plane;

    result.copyTo(plane);
    res.success    = true;
    res.iterations = 1;
    return res;
}

// ──── applyVanCittert ─────────────────────────────────────────────────────────
// f = img
// for iter:
// conv = f * psf (SAME)
// f = f + relaxation * (img - conv)

DeconvResult Deconvolution::applyVanCittert(cv::Mat& plane, const cv::Mat& psf,
                                              const DeconvParams& p)
{
    DeconvResult res;
    cv::Mat f; plane.copyTo(f);

    for (int it = 0; it < p.maxIter; ++it) {
        cv::Mat conv = convolveFFT(f, psf);
        cv::Mat fNew = f + (float)p.vcRelax * (plane - conv);
        cv::max(fNew, 0.0f, fNew);
        cv::min(fNew, 1.0f, fNew);

        double change = cv::norm(fNew - f, cv::NORM_L1) / (cv::norm(f, cv::NORM_L1) + 1e-8);
        f = fNew;
        res.finalChange = change;
        res.iterations  = it + 1;

        if (change < p.convergenceTol) break;
    }

    if (p.globalStrength < 1.0)
        f = (float)p.globalStrength * f + (float)(1.0-p.globalStrength) * plane;
    f.copyTo(plane);
    res.success = true;
    return res;
}

// ──── applyLarsonSekanina ─────────────────────────────────────────────────────

// Rotational gradient filter:
// r2 = r + radial_step  (or r if radial_step <= 0)
// theta2 = (theta + angular_step) mod 2π
// J = bilinear_interpolate(gray, y2, x2)
// if Divide: B = clip(gray * (median(J)/(J+eps)), 0, 1)
// if Subtract: B = clip(normalize(gray - J), 0, 1)
// blend SoftLight or Screen

DeconvResult Deconvolution::applyLarsonSekanina(cv::Mat& plane, const DeconvParams& p)
{
    DeconvResult res;
    int H = plane.rows, W = plane.cols;
    float cx = (p.lsCenterX != 0.0 ? (float)p.lsCenterX : W / 2.0f);
    float cy = (p.lsCenterY != 0.0 ? (float)p.lsCenterY : H / 2.0f);
    double dtheta = (p.lsAngularStep / 180.0) * M_PI;

    // Polar coordinates
    cv::Mat xs(H, W, CV_32F), ys(H, W, CV_32F);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            xs.at<float>(y,x) = (float)x;
            ys.at<float>(y,x) = (float)y;
        }
    cv::Mat dx = xs - cx, dy = ys - cy;
    cv::Mat r(H, W, CV_32F), theta(H, W, CV_32F);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            float ddx = dx.at<float>(y,x), ddy = dy.at<float>(y,x);
            r.at<float>(y,x)     = std::sqrt(ddx*ddx + ddy*ddy);
            float th = std::atan2(ddy, ddx);
            if (th < 0) th += (float)(2*M_PI);
            theta.at<float>(y,x) = th;
        }

    // Rotated coordinates
    cv::Mat r2 = (p.lsRadialStep > 0) ? r + (float)p.lsRadialStep : r.clone();
    cv::Mat theta2(H, W, CV_32F);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            theta2.at<float>(y,x) = std::fmod(theta.at<float>(y,x) + (float)dtheta, (float)(2*M_PI));

    cv::Mat x2(H, W, CV_32F);
    cv::Mat y2(H, W, CV_32F);

    // Compute remap coordinates
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            x2.at<float>(y,x) = cx + r2.at<float>(y,x) * std::cos(theta2.at<float>(y,x));
            y2.at<float>(y,x) = cy + r2.at<float>(y,x) * std::sin(theta2.at<float>(y,x));
        }

    // Bilinear interpolation of plane at (y2, x2)
    cv::Mat J(H, W, CV_32F, cv::Scalar(0));
    cv::remap(plane, J, x2, y2, cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0));

    cv::Mat B;
    if (p.lsOp == LSOperator::Divide) {
        cv::Scalar medJ = cv::mean(J);
        float medV = (medJ[0] > 0) ? (float)medJ[0] : 1e-6f;
        B = plane * (medV / (J + EPS));
        cv::min(B, 1.0f, B); cv::max(B, 0.0f, B);
    } else {
        B = plane - J;
        cv::max(B, 0.0f, B);
        double maxV; cv::minMaxLoc(B, nullptr, &maxV);
        if (maxV > 0) B /= (float)maxV;
    }

    // Blend
    cv::Mat blended;
    if (p.lsBlend == LSBlendMode::Screen) {
        blended = (plane + B) - plane.mul(B);
    } else {
        blended = (1.0f - 2.0f * B).mul(plane.mul(plane)) + 2.0f * B.mul(plane);
    }
    cv::max(blended, 0.0f, blended);
    cv::min(blended, 1.0f, blended);

    if (p.globalStrength < 1.0)
        blended = (float)p.globalStrength * blended + (float)(1.0-p.globalStrength) * plane;
    blended.copyTo(plane);
    res.success = true;
    res.iterations = 1;
    return res;
}

// ─────────────────────────────────────────────────────────────────────────────
// buildPSF - build synthetic PSF
// ─────────────────────────────────────────────────────────────────────────────
cv::Mat Deconvolution::buildPSF(const DeconvParams& p, int kernelSize)
{
    if (!p.customKernel.empty()) return p.customKernel.clone();
    if (kernelSize <= 0) kernelSize = autoKsizeFromFwhm(p.psfFWHM);
    return gaussianPsf(p.psfFWHM, kernelSize);
}

// ─────────────────────────────────────────────────────────────────────────────
// estimateFWHM / estimateEmpiricalPSF (single-image wrappers)
// ─────────────────────────────────────────────────────────────────────────────
double Deconvolution::estimateFWHM(const ImageBuffer& buf)
{
    const int w = buf.width();
    const int h = buf.height();
    if (w == 0 || h == 0) return 3.0;
    const std::vector<float>& src = buf.data();
    const int c = buf.channels();
    cv::Mat plane;
    if (c == 1) {
        plane = cv::Mat(h, w, CV_32F, const_cast<float*>(src.data()));
    } else {
        cv::Mat interleaved(h, w, CV_32FC(c), const_cast<float*>(src.data()));
        std::vector<cv::Mat> planes;
        cv::split(interleaved, planes);
        plane = planes[1 % c]; // Use green if RGB, or first channel if other
    }
    return estimateFwhmFromImage(plane);
}

cv::Mat Deconvolution::estimateEmpiricalPSF(const ImageBuffer& buf, int kernelSize)
{
    const int w = buf.width();
    const int h = buf.height();
    if (w == 0 || h == 0) return cv::Mat();
    const std::vector<float>& src = buf.data();
    const int c = buf.channels();
    cv::Mat plane;
    if (c == 1) {
        plane = cv::Mat(h, w, CV_32F, const_cast<float*>(src.data()));
    } else {
        cv::Mat interleaved(h, w, CV_32FC(c), const_cast<float*>(src.data()));
        std::vector<cv::Mat> planes;
        cv::split(interleaved, planes);
        plane = planes[1 % c]; // Use green if RGB
    }
    if (kernelSize <= 0) kernelSize = 31;
    // Wrapper: call derivePsfForFrame
    MFPsfInfo info = derivePsfForFrame(plane);
    if (info.kernel.empty()) return gaussianPsf(3.0, kernelSize);
    // Resize if needed
    if (info.kernel.rows != kernelSize) {
        cv::Mat padded = cv::Mat::zeros(kernelSize, kernelSize, CV_32F);
        int k = info.kernel.rows;
        int off = (kernelSize - k) / 2;
        if (off >= 0)
            info.kernel.copyTo(padded(cv::Rect(off, off, k, k)));
        else
            info.kernel(cv::Rect(-off, -off, kernelSize, kernelSize)).copyTo(padded);
        return normalizePsf(padded);
    }
    return info.kernel;
}

// ─────────────────────────────────────────────────────────────────────────────
// apply - single-image entry point
// ─────────────────────────────────────────────────────────────────────────────
DeconvResult Deconvolution::apply(ImageBuffer& buf, const DeconvParams& p)
{
    DeconvResult res;
    const int W = buf.width();
    const int H = buf.height();
    const int C = buf.channels();
    if (W == 0 || H == 0) {
        res.errorMsg = "Empty ImageBuffer";
        return res;
    }

    std::vector<float>& data = buf.data();
    cv::Mat psf = buildPSF(p, p.kernelSize);

    auto processPlane = [&](cv::Mat& plane) -> DeconvResult {
        cv::Mat mask = buildStarMask(plane, p.starMask);
        switch (p.algo) {
        case DeconvAlgorithm::RichardsonLucy:
            return (p.regType == 0) ? applyRL(plane, psf, mask, p)
                                    : applyRLTV(plane, psf, mask, p);
        case DeconvAlgorithm::RLTV:      return applyRLTV(plane, psf, mask, p);
        case DeconvAlgorithm::Wiener:    return applyWiener(plane, psf, p);
        case DeconvAlgorithm::VanCittert:return applyVanCittert(plane, psf, p);
        case DeconvAlgorithm::LarsonSekanina: return applyLarsonSekanina(plane, p);
        default: { DeconvResult r; r.errorMsg = "Unknown algorithm"; return r; }
        }
    };

    if (C == 1) {
        cv::Mat plane(H, W, CV_32F, data.data());
        return processPlane(plane);
    }

    // Multi-channel: luminance-only or per-channel
    if (p.luminanceOnly && C >= 3) {
        // Convert to YCbCr, process Y, convert back
        std::vector<float> ycbcr;
        rgbToYcbcr(data, ycbcr, W, H, C);
        cv::Mat Y(H, W, CV_32F, ycbcr.data());
        DeconvResult r = processPlane(Y);
        ycbcrToRgb(ycbcr, data, W, H, C);
        return r;
    }

    // Per-channel processing using interleaved Mat
    cv::Mat interleavedImg(H, W, CV_32FC(C), data.data());
    std::vector<cv::Mat> planes;
    cv::split(interleavedImg, planes);

    DeconvResult last;
    for (int c = 0; c < C; ++c) {
        last = processPlane(planes[c]);
    }
    
    cv::merge(planes, interleavedImg);
    return last;
}

// ─────────────────────────────────────────────────────────────────────────────
// applyMultiFrame — Multi-Frame Deconvolution
// Multi-Frame Deconvolution
// ─────────────────────────────────────────────────────────────────────────────
MFDeconvResult Deconvolution::applyMultiFrame(const MFDeconvParams& p)
{
    MFDeconvResult result;
    auto& cb = p.statusCallback;
    auto emitPct = [&](double frac, const QString& msg) {
        emitProgress(cb, frac, msg);
    };

    // ── Validation ───────────────────────────────────────────────────────
    if (p.framePaths.empty()) {
        result.errorMsg = "No input frames specified.";
        return result;
    }
    if (p.outputPath.isEmpty()) {
        result.errorMsg = "Output path not specified.";
        return result;
    }

    int nFrames = (int)p.framePaths.size();
    if (cb) cb(QString("MFDeconv: loading %1 aligned frames...").arg(nFrames));
    emitPct(0.02, "scanning");

    // ── Determine common size (centered intersection) ─────────────────────
    // Production flow should load each FITS and infer H,W.
    // Placeholder: all frames must have the same size.
    // Common-size logic for aligned input frames.
    int Ht = -1, Wt = -1;
    // [INTEGRATION]: load each FITS frame and compute min(H), min(W).
    // Current placeholder assumes all frames have identical dimensions
    // and uses the first frame as reference.

    emitPct(0.05, "preparing");

    // ── Frame loading ──────────────────────────────────────────────────────
    // [INTEGRATION]: load each frame as CV_32F.
    // Placeholder list used here.
    std::vector<cv::Mat> frames;
    frames.reserve(nFrames);
    // for (const auto& path : p.framePaths) {
    //     cv::Mat f = loadFitsFrame(path, Ht, Wt, p.colorMode);
    //     frames.push_back(f);
    // }

    if (frames.empty()) {
        result.errorMsg = "Unable to load input frames.";
        return result;
    }
    Ht = frames[0].rows;
    Wt = frames[0].cols;

    // ── Per-frame PSF (auto-derived) ─────────────────────────────────────
    if (cb) cb("MFDeconv: measuring per-frame PSF...");
    emitPct(0.08, "PSF computation");

    std::vector<cv::Mat> psfs;
    psfs.reserve(nFrames);
    for (int i = 0; i < nFrames; ++i) {
        MFPsfInfo info = derivePsfForFrame(frames[i]);
        psfs.push_back(info.kernel);
        if (cb)
            cb(QString("MFDeconv: PSF%1: ksize=%2 | FWHM≈%3px")
               .arg(i+1).arg(info.ksize).arg(info.fwhmPx, 0, 'f', 2));
    }

    // ── Super-Resolution: lift PSF ────────────────────────────────────────
    int r = std::max(1, p.superResFactor);
    if (r > 1) {
        if (cb) cb(QString("MFDeconv: super-resolution r=%1 with sigma=%2 - solving SR PSF...")
                   .arg(r).arg(p.srSigma));
        for (int i = 0; i < nFrames; ++i) {
            cv::Mat srPsf = solveSuperPsf(psfs[i], r, p.srSigma,
                                           p.srPsfOptIters, p.srPsfOptLr);
            if (srPsf.rows % 2 == 0)
                srPsf = srPsf(cv::Rect(0, 0, srPsf.cols-1, srPsf.rows-1)).clone();
            psfs[i] = srPsf;
            if (cb)
                     cb(QString("  SR-PSF%1: -> %2x%2 (sum=%3)")
                   .arg(i+1).arg(srPsf.rows).arg(cv::sum(srPsf)[0], 0, 'f', 6));
        }
    }

    std::vector<cv::Mat> flipPsfs;
    flipPsfs.reserve(nFrames);
    for (const auto& psf : psfs) flipPsfs.push_back(flipKernel(psf));

    emitPct(0.20, "PSF Ready");

    // ── Star masks ────────────────────────────────────────────────────────
    std::vector<cv::Mat> starMasks;
    if (p.useStarMasks) {
        if (cb) cb("MFDeconv: computing star mask per frame...");
        // Optional reference mask
        cv::Mat refMask;
        if (!p.starMaskRefPath.isEmpty()) {
            // [INTEGRATION]: load reference frame
            // cv::Mat refFrame = loadFitsFrame(p.starMaskRefPath, Ht, Wt, "luma");
            // refMask = buildStarMaskSEP(refFrame, p.starMaskCfg);
        }
        for (int i = 0; i < nFrames; ++i) {
            if (!refMask.empty())
                starMasks.push_back(refMask.clone());
            else
                starMasks.push_back(buildStarMaskSEP(frames[i], p.starMaskCfg));
        }
    }

    // ── Variance maps ─────────────────────────────────────────────────────
    std::vector<cv::Mat> varMaps;
    if (p.useVarianceMaps) {
        if (cb) cb("MFDeconv: computing variance map per frame...");
        for (int i = 0; i < nFrames; ++i)
            varMaps.push_back(buildVarianceMap(frames[i], p.varmapCfg));
    }

    // ── Seed ──────────────────────────────────────────────────────────────
    emitPct(0.25, "Calculating Seed Image...");
    if (cb) cb(p.seedMode == MFSeedMode::Median ?
               "MFDeconv: Building median seed (tiled, streaming)…" :
               "MFDeconv: Building robust seed (bootstrap + sigma-clip)...");

    cv::Mat seedNative;
    if (p.seedMode == MFSeedMode::Median) {
        seedNative = buildMedianSeed(frames, cv::Size(256, 256));
    } else {
        seedNative = buildRobustSeed(frames, p.bootstrapFrames, p.clipSigma);
    }

    // Lift seed for SR
    cv::Mat x;
    if (r > 1) {
        x = upsampleSum(seedNative / (float)(r*r), r, cv::Size(Wt*r, Ht*r));
    } else {
        x = seedNative.clone();
    }
    if (x.empty()) { result.errorMsg = "Seed computation failed."; return result; }
    emitPct(0.40, "Seed ready");

    int Hs = x.rows, Ws = x.cols;

    // ── Background telemetry ──────────────────────────────────────────────
    if (cb) cb("MFDeconv: Calculating Backgrounds and MADs…");
    {
        cv::Mat& y0 = frames[0];
        // cv::Scalar med = cv::mean(y0); // unused
        std::vector<float> v(y0.begin<float>(), y0.end<float>());
        std::nth_element(v.begin(), v.begin()+v.size()/2, v.end());
        float medV = v[v.size()/2];
        std::vector<float> absD; for (float a : v) absD.push_back(std::abs(a - medV));
        std::nth_element(absD.begin(), absD.begin()+absD.size()/2, absD.end());
        float bgEst = 1.4826f * absD[absD.size()/2];
        if (cb)
            cb(QString("MFDeconv: color_mode=%1, huber_delta=%2 (bg RMS~%3)")
               .arg(p.colorMode).arg(p.huberDelta).arg(bgEst, 0, 'f', 3));
    }

    // ── Allocate accumulators ────────────────────────────────────────────
    cv::Mat num = cv::Mat::zeros(Hs, Ws, CV_32F);
    cv::Mat den = cv::Mat::zeros(Hs, Ws, CV_32F);

    // ── EarlyStopper ──────────────────────────────────────────────────────
    MFEarlyStopper early(p.earlyStop, cb);

    bool earlyStop = false;
    int  usedIters = 0;
    int  maxIters  = std::max(1, p.maxIters);

    double relax  = p.relax;   // default 0.7
    double kappa  = p.kappa;   // default 2.0
    bool   rhoL2  = (p.rho == MFLossType::L2);
    double huberD = rhoL2 ? 0.0 : p.huberDelta;

    if (cb) cb("Starting First Multiplicative Iteration…");

    // ── Iterative loop ────────────────────────────────────────────────────
    for (int it = 1; it <= maxIters; ++it) {
        num.setTo(0.0f);
        den.setTo(0.0f);

        // ── Per frame ─────────────────────────────────────────────────────
        for (int fi = 0; fi < nFrames; ++fi) {
            const cv::Mat& y_nat = frames[fi];                     // native frame
            const cv::Mat& mask  = (!starMasks.empty()) ? starMasks[fi]
                                                        : cv::Mat();
            const cv::Mat& vmap  = (!varMaps.empty())   ? varMaps[fi]
                                                        : cv::Mat();
            const cv::Mat& psf   = psfs[fi];
            const cv::Mat& psfT  = flipPsfs[fi];

            // Prediction: convolution on SR grid, then downsample to native grid
            cv::Mat predSuper = convolveFFT(x, psf);
            cv::Mat predLow   = (r > 1) ? downsampleAvg(predSuper, r) : predSuper.clone();

            // Robust weights (Huber or L2)
            cv::Mat residual = y_nat - predLow;
            cv::Mat wmap;
            if (rhoL2) {
                wmap = cv::Mat::ones(y_nat.size(), CV_32F);
                if (!mask.empty()) cv::multiply(wmap, mask, wmap);
                if (!vmap.empty()) wmap = wmap / (vmap + EPS);
            } else {
                wmap = computeHuberWeights(residual, huberD, vmap, mask);
            }
            wmap = cv::abs(wmap);  // safety: weights always >= 0

            // Adjoint backproject
            cv::Mat upY, upPred;
            if (r > 1) {
                upY    = upsampleSum(wmap.mul(y_nat),    r, cv::Size(Ws, Hs));
                upPred = upsampleSum(wmap.mul(predLow),  r, cv::Size(Ws, Hs));
            } else {
                upY    = wmap.mul(y_nat);
                upPred = wmap.mul(predLow);
            }

            // Accumulate
            num += convolveFFT(upY,    psfT);
            den += convolveFFT(upPred, psfT);
        }

        // ── Multiplicative step ──────────────────────────────────────────
        // ratio = num / (den + eps)
        // neutral pixels: ratio = 1
        // upd = clamp(ratio, 1/kappa, kappa)
        // x_next = clamp(x * upd, 0)
        cv::Mat denSafe = den + EPS;
        cv::Mat ratio   = num / denSafe;

        // Neutral check (|den| < 1e-12 && |num| < 1e-12) → ratio = 1
        cv::Mat neutralMask = (cv::abs(den) < 1e-12f) & (cv::abs(num) < 1e-12f);
        ratio.setTo(1.0f, neutralMask);

        // Clamp ratio in [1/kappa, kappa]
        cv::Mat upd;
        cv::min(ratio, (float)kappa, upd);
        cv::max(upd, (float)(1.0/kappa), upd);

        cv::Mat xNext = x.mul(upd);
        cv::max(xNext, 0.0f, xNext);

        // Calculate stats for EarlyStopper
        cv::Mat updDiff = cv::abs(upd - 1.0f);
        std::vector<float> udv(updDiff.begin<float>(), updDiff.end<float>());
        std::nth_element(udv.begin(), udv.begin()+udv.size()/2, udv.end());
        double um = udv[udv.size()/2];

        cv::Mat xDiff = cv::abs(xNext - x);
        std::vector<float> xdv(xDiff.begin<float>(), xDiff.end<float>());
        std::nth_element(xdv.begin(), xdv.begin()+xdv.size()/2, xdv.end());
        std::vector<float> xav(x.begin<float>(), x.end<float>());
        std::nth_element(xav.begin(), xav.begin()+xav.size()/2, xav.end());
        double xMed = xav[xav.size()/2];
        double rc = xdv[xdv.size()/2] / (xMed + 1e-8);

        // Early stop check
        if (early.step(it, maxIters, um, rc)) {
            x = xNext.clone();
            usedIters   = it;
            earlyStop   = true;
            if (cb) cb(QString("MFDeconv: Iteration %1/%2 (early stop)").arg(it).arg(maxIters));
            break;
        }

        // Relaxation: x = (1-relax)*x + relax*x_next
        x = (float)(1.0 - relax) * x + (float)relax * xNext;

        // Save intermediate outputs
        if (p.saveIntermediate && (it % std::max(1, p.saveEvery) == 0)) {
            // [INTEGRATION]: save x as FITS in iter_dir
            // saveFitsIter(x, iterDir, it, p.colorMode);
        }

        double frac = 0.25 + 0.70 * ((double)it / maxIters);
        emitPct(frac, QString("Iteration %1/%2").arg(it).arg(maxIters));
        if (cb) cb(QString("Iter %1/%2").arg(it).arg(maxIters));
    }

    if (!earlyStop) usedIters = maxIters;

    // Save final result
    emitPct(0.97, "saving");

    // [INTEGRATION]: save x as FITS with MF_ITERS, MF_ESTOP, etc. headers
    // saveFitsResult(x, p.outputPath, usedIters, earlyStop, r, p);

    QString outPath = buildOutputPath(p.outputPath, r);
    outPath = nonclobberPath(outPath);

    if (cb) {
        QString msg = QString("MFDeconv saved: %1  (iters used: %2%3)")
            .arg(outPath).arg(usedIters)
            .arg(earlyStop ? ", early stop" : "");
        cb(msg);
    }
    emitPct(1.0, "done");

    result.success        = true;
    result.itersUsed      = usedIters;
    result.earlyStopped   = earlyStop;
    result.superResFactor = r;
    result.outputPath     = outPath;
    return result;
}

*/