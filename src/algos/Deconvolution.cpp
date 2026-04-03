// /**
//  * @file Deconvolution.cpp
//  * @brief Implementation of single-image and multi-frame deconvolution algorithms.
//  *
//  * This file contains all computational routines for PSF generation,
//  * FFT-based convolution, iterative deconvolution algorithms, star masking,
//  * variance map construction, seed generation, super-resolution PSF solving,
//  * and the multi-frame deconvolution pipeline.
//  */
// 
// #include "Deconvolution.h"
// 
// #include <cmath>
// #include <algorithm>
// #include <numeric>
// #include <stdexcept>
// #include <random>
// 
// #include <QFileInfo>
// #include <QDir>
// #include <QRegularExpression>
// 
// #include <opencv2/imgproc.hpp>
// #include <opencv2/core.hpp>
// 
// #ifdef _OPENMP
// #include <omp.h>
// #endif
// 
// /* =========================================================================
//  * Compile-time constants
//  * ========================================================================= */
// 
// /** Numerical floor to prevent division by zero. */
// static constexpr float  EPS             = 1e-6f;
// 
// /** Conversion factor: FWHM = 2.3548 * sigma (for a Gaussian profile). */
// static constexpr double FWHM_TO_SIGMA   = 2.3548;
// 
// /* -- Default star-mask detection parameters ------------------------------- */
// static constexpr int    STAR_MASK_MAXSIDE   = 2048;
// static constexpr int    STAR_MASK_MAXOBJS   = 2000;
// static constexpr double THRESHOLD_SIGMA     = 2.0;
// static constexpr double KEEP_FLOOR          = 0.20;
// static constexpr int    GROW_PX             = 8;
// static constexpr int    MAX_STAR_RADIUS     = 16;
// static constexpr double SOFT_SIGMA          = 2.0;
// static constexpr double ELLIPSE_SCALE       = 1.2;

// /* =========================================================================
//  * MFEarlyStopper implementation
//  * ========================================================================= */
// 
// MFEarlyStopper::MFEarlyStopper(const MFEarlyStopCfg& cfg,
//                                std::function<void(const QString&)> statusCb)
//     : m_cfg(cfg)
//     , m_statusCb(statusCb)
// {
//     if (m_statusCb) {
//         m_statusCb(QString(
//             "MFDeconv early-stop config: "
//             "tol_upd_floor=%1, tol_rel_floor=%2, "
//             "early_frac=%3, ema_alpha=%4, "
//             "patience=%5, min_iters=%6")
//             .arg(cfg.tolUpdFloor).arg(cfg.tolRelFloor)
//             .arg(cfg.earlyFrac).arg(cfg.emaAlpha)
//             .arg(cfg.patience).arg(cfg.minIters));
//     }
// }
// 
// void MFEarlyStopper::reset()
// {
//     m_emaUm    = -1.0;
//     m_emaRc    = -1.0;
//     m_baseUm   = -1.0;
//     m_baseRc   = -1.0;
//     m_earlyCnt = 0;
// }
// 
// bool MFEarlyStopper::step(int it, int maxIters, double um, double rc)
// {
//     /* --- Update exponential moving averages ------------------------------- */
//     if (it == 1 || m_emaUm < 0.0) {
//         m_emaUm  = um;
//         m_emaRc  = rc;
//         m_baseUm = um;
//         m_baseRc = rc;
//     } else {
//         const double a = m_cfg.emaAlpha;
//         m_emaUm = a * um + (1.0 - a) * m_emaUm;
//         m_emaRc = a * rc + (1.0 - a) * m_emaRc;
//     }
// 
//     /* --- Compute adaptive convergence thresholds ------------------------- */
//     const double bUm  = (m_baseUm > 0.0) ? m_baseUm : um;
//     const double bRc  = (m_baseRc > 0.0) ? m_baseRc : rc;
//     const double tolUm = std::max(m_cfg.tolUpdFloor, m_cfg.earlyFrac * bUm);
//     const double tolRc = std::max(m_cfg.tolRelFloor, m_cfg.earlyFrac * bRc);
// 
//     const bool small = (m_emaUm < tolUm) || (m_emaRc < tolRc);
// 
//     /* --- Diagnostic logging ---------------------------------------------- */
//     if (m_statusCb) {
//         m_statusCb(QString(
//             "MFDeconv iter %1/%2: "
//             "um=%3, rc=%4 | ema_um=%5, ema_rc=%6 | "
//             "tol_um=%7, tol_rc=%8 | small=%9")
//             .arg(it).arg(maxIters)
//             .arg(um,       0, 'e', 3)
//             .arg(rc,       0, 'e', 3)
//             .arg(m_emaUm,  0, 'e', 3)
//             .arg(m_emaRc,  0, 'e', 3)
//             .arg(tolUm,    0, 'e', 3)
//             .arg(tolRc,    0, 'e', 3)
//             .arg(small ? "True" : "False"));
//     }
// 
//     /* --- Patience counter ------------------------------------------------ */
//     if (small && it >= m_cfg.minIters) {
//         ++m_earlyCnt;
//         if (m_statusCb) {
//             m_statusCb(QString("MFDeconv iter %1: early-stop candidate (%2/%3)")
//                 .arg(it).arg(m_earlyCnt).arg(m_cfg.patience));
//         }
//     } else {
//         if (m_earlyCnt > 0 && m_statusCb) {
//             m_statusCb(QString("MFDeconv iter %1: early-stop streak reset").arg(it));
//         }
//         m_earlyCnt = 0;
//     }
// 
//     /* --- Trigger early stop when patience is exhausted -------------------- */
//     if (m_earlyCnt >= m_cfg.patience) {
//         if (m_statusCb) {
//             m_statusCb(QString(
//                 "MFDeconv early-stop TRIGGERED: "
//                 "ema_um=%1 < %2 or ema_rc=%3 < %4 "
//                 "for %5 consecutive iters")
//                 .arg(m_emaUm, 0, 'e', 3).arg(tolUm, 0, 'e', 3)
//                 .arg(m_emaRc, 0, 'e', 3).arg(tolRc, 0, 'e', 3)
//                 .arg(m_cfg.patience));
//         }
//         return true;
//     }
// 
//     return false;
// }

// /* =========================================================================
//  * PSF Generation and Utilities
//  * ========================================================================= */

// /**
//  * @brief Generate a normalized isotropic Gaussian PSF.
//  * @param fwhmPx  Full Width at Half Maximum in pixels.
//  * @param ksize   Kernel side length (must be odd).
//  * @return CV_32F matrix normalized so that the sum equals 1.
//  */
// cv::Mat Deconvolution::gaussianPsf(double fwhmPx, int ksize)
// {
//     const double sigma = std::max(fwhmPx, 1.0) / FWHM_TO_SIGMA;
//     const int r = (ksize - 1) / 2;

//     cv::Mat g(ksize, ksize, CV_32F);
//     float sum = 0.0f;

//     for (int y = -r; y <= r; ++y) {
//         for (int x = -r; x <= r; ++x) {
//             const float val = std::exp(
//                 -(float)(x * x + y * y) / (float)(2.0 * sigma * sigma));
//             g.at<float>(y + r, x + r) = val;
//             sum += val;
//         }
//     }

//     g /= (sum + EPS);
//     return g;
// }

// /**
//  * @brief Normalize a PSF kernel: clip negatives and divide by sum.
//  */
// cv::Mat Deconvolution::normalizePsf(const cv::Mat& psf)
// {
//     cv::Mat out;
//     cv::max(psf, 0.0, out);

//     const float s = (float)cv::sum(out)[0];
//     if (std::isfinite(s) && s > 1e-6f) {
//         out /= s;
//     }

//     return out;
// }

// /**
//  * @brief Soften a PSF by convolving it with a small Gaussian.
//  * @param psf     Input PSF kernel.
//  * @param sigmaPx Standard deviation of the softening Gaussian in pixels.
//  */
// cv::Mat Deconvolution::softenPsf(const cv::Mat& psf, double sigmaPx)
// {
//     if (sigmaPx <= 0.0) return psf.clone();

//     const int r  = std::max(1, (int)std::round(3.0 * sigmaPx));
//     const int ks = 2 * r + 1;
//     cv::Mat g = gaussianPsf(sigmaPx * FWHM_TO_SIGMA, ks);

//     return convolveFFT(psf, g);
// }

// /**
//  * @brief Flip a kernel by 180 degrees for adjoint (transposed) convolution.
//  */
// cv::Mat Deconvolution::flipKernel(const cv::Mat& psf)
// {
//     cv::Mat flipped;
//     cv::flip(psf, flipped, -1);
//     return flipped;
// }

// /**
//  * @brief Estimate the FWHM of a PSF kernel using second-moment analysis.
//  * @return FWHM in pixels, or NaN if the kernel is degenerate.
//  */
// double Deconvolution::psfFwhmPx(const cv::Mat& psf)
// {
//     cv::Mat p;
//     cv::max(psf, 0.0f, p);
//     p.convertTo(p, CV_32F);

//     const float s = (float)cv::sum(p)[0];
//     if (s <= EPS) return std::numeric_limits<double>::quiet_NaN();

//     const int k = p.rows;

//     /* Compute the intensity-weighted centroid. */
//     double cx = 0.0, cy = 0.0;
//     for (int y = 0; y < k; ++y) {
//         for (int x = 0; x < k; ++x) {
//             const float v = p.at<float>(y, x) / s;
//             cx += v * x;
//             cy += v * y;
//         }
//     }

//     /* Compute second moments (variance) around the centroid. */
//     double varX = 0.0, varY = 0.0;
//     for (int y = 0; y < k; ++y) {
//         for (int x = 0; x < k; ++x) {
//             const float v = p.at<float>(y, x) / s;
//             varX += v * (x - cx) * (x - cx);
//             varY += v * (y - cy) * (y - cy);
//         }
//     }

//     const double sigma = std::sqrt(std::max(0.0, 0.5 * (varX + varY)));
//     return FWHM_TO_SIGMA * sigma;
// }

// /**
//  * @brief Compute an appropriate odd kernel size that covers +/-4 sigma.
//  */
// int Deconvolution::autoKsizeFromFwhm(double fwhmPx, int kmin, int kmax)
// {
//     const double sigma = std::max(fwhmPx, 1.0) / FWHM_TO_SIGMA;
//     const int r = (int)std::ceil(4.0 * sigma);
//     int k = 2 * r + 1;
//     k = std::max(kmin, std::min(k, kmax));
//     if (k % 2 == 0) ++k;
//     return k;
// }

// /* =========================================================================
//  * FFT Convolution (SAME output size)
//  * ========================================================================= */

// /**
//  * @brief Perform SAME-size convolution using the DFT.
//  *
//  * The kernel is ifftshifted before transformation so that its center
//  * maps to the DC component.  The output has the same dimensions as @p src.
//  *
//  * @param src  Input image (CV_32F, single-channel).
//  * @param psf  Convolution kernel (CV_32F, single-channel).
//  * @return Convolution result with the same size as @p src.
//  */
// cv::Mat Deconvolution::convolveFFT(const cv::Mat& src, const cv::Mat& psf)
// {
//     const int H = src.rows, W = src.cols;
//     const int kh = psf.rows, kw = psf.cols;

//     /* Determine optimal DFT dimensions for performance. */
//     const int fftH = cv::getOptimalDFTSize(H + kh - 1);
//     const int fftW = cv::getOptimalDFTSize(W + kw - 1);

//     /* Apply ifftshift to the kernel (swap quadrants around the center). */
//     cv::Mat psfShifted = cv::Mat::zeros(kh, kw, CV_32F);
//     {
//         const int cy = kh / 2, cx = kw / 2;

        // Bottom-right -> top-left
//         psf(cv::Rect(cx, cy, kw - cx, kh - cy))
//             .copyTo(psfShifted(cv::Rect(0, 0, kw - cx, kh - cy)));
        // Bottom-left -> top-right
//         psf(cv::Rect(0, cy, cx, kh - cy))
//             .copyTo(psfShifted(cv::Rect(kw - cx, 0, cx, kh - cy)));
        // Top-right -> bottom-left
//         psf(cv::Rect(cx, 0, kw - cx, cy))
//             .copyTo(psfShifted(cv::Rect(0, kh - cy, kw - cx, cy)));
        // Top-left -> bottom-right
//         psf(cv::Rect(0, 0, cx, cy))
//             .copyTo(psfShifted(cv::Rect(kw - cx, kh - cy, cx, cy)));
//     }

//     /* Zero-pad both the source and the shifted kernel to DFT size. */
//     cv::Mat srcPad = cv::Mat::zeros(fftH, fftW, CV_32F);
//     src.copyTo(srcPad(cv::Rect(0, 0, W, H)));

//     cv::Mat psfPad = cv::Mat::zeros(fftH, fftW, CV_32F);
//     psfShifted.copyTo(psfPad(cv::Rect(0, 0, kw, kh)));

//     /* Forward DFT. */
//     cv::Mat srcC, psfC;
//     cv::dft(srcPad, srcC, cv::DFT_COMPLEX_OUTPUT);
//     cv::dft(psfPad, psfC, cv::DFT_COMPLEX_OUTPUT);

//     /* Element-wise multiplication in the frequency domain. */
//     cv::Mat prodC;
//     cv::mulSpectrums(srcC, psfC, prodC, 0);

//     /* Inverse DFT to obtain the spatial-domain result. */
//     cv::Mat result;
//     cv::dft(prodC, result,
//             cv::DFT_INVERSE | cv::DFT_REAL_OUTPUT | cv::DFT_SCALE);

//     /* Crop to SAME size relative to the kernel center. */
//     const int sh = (kh - 1) / 2;
//     const int sw = (kw - 1) / 2;
//     return result(cv::Rect(sw, sh, W, H)).clone();
// }

// /* =========================================================================
//  * Wiener Filter
//  * ========================================================================= */

// /**
//  * @brief Apply a frequency-domain Wiener filter.
//  *
//  * Computes: X = H* / (|H|^2 + K) * Y
//  * where H is the PSF OTF and Y is the observed image spectrum.
//  *
//  * @param src  Observed (blurred) image.
//  * @param psf  PSF kernel.
//  * @param K    Noise-to-signal power ratio (regularization constant).
//  */
// cv::Mat Deconvolution::wienerFilter(const cv::Mat& src, const cv::Mat& psf, double K)
// {
//     const int H = src.rows, W = src.cols;
//     const int kh = psf.rows, kw = psf.cols;

//     /* Center the PSF in a full-size canvas. */
//     cv::Mat psfPad = cv::Mat::zeros(H, W, CV_32F);
//     const int y0 = H / 2 - kh / 2;
//     const int x0 = W / 2 - kw / 2;
//     psf.copyTo(psfPad(cv::Rect(x0, y0, kw, kh)));

//     /* Apply ifftshift to the centered PSF. */
//     cv::Mat psfShift;
//     {
//         const int cy = H / 2, cx = W / 2;
//         cv::Mat tmp = cv::Mat::zeros(H, W, CV_32F);
//         psfPad(cv::Rect(cx, cy, W - cx, H - cy)).copyTo(tmp(cv::Rect(0,      0,      W - cx, H - cy)));
//         psfPad(cv::Rect(0,  cy, cx,     H - cy)).copyTo(tmp(cv::Rect(W - cx, 0,      cx,     H - cy)));
//         psfPad(cv::Rect(cx, 0,  W - cx, cy    )).copyTo(tmp(cv::Rect(0,      H - cy, W - cx, cy)));
//         psfPad(cv::Rect(0,  0,  cx,     cy    )).copyTo(tmp(cv::Rect(W - cx, H - cy, cx,     cy)));
//         psfShift = tmp;
//     }

//     /* DFT of PSF and observed image. */
//     cv::Mat Hf, Yf;
//     cv::dft(psfShift, Hf, cv::DFT_COMPLEX_OUTPUT);
//     cv::dft(src,      Yf, cv::DFT_COMPLEX_OUTPUT);

//     /* Compute conjugate of H and |H|^2. */
//     std::vector<cv::Mat> hfPlanes(2);
//     cv::split(Hf, hfPlanes);
//     hfPlanes[1] *= -1.0f;   // Conjugate: negate imaginary part.

//     cv::Mat Hf_conj;
//     cv::merge(hfPlanes, Hf_conj);

//     cv::Mat mag2;
//     {
//         std::vector<cv::Mat> hp(2);
//         cv::split(Hf, hp);
//         mag2 = hp[0].mul(hp[0]) + hp[1].mul(hp[1]);
//     }

//     /* Wiener filter transfer function: W = H* / (|H|^2 + K). */
//     cv::Mat denom;
//     cv::add(mag2, (float)K, denom);

//     std::vector<cv::Mat> wfCh(2);
//     {
//         std::vector<cv::Mat> hc(2);
//         cv::split(Hf_conj, hc);
//         wfCh[0] = hc[0] / denom;
//         wfCh[1] = hc[1] / denom;
//     }
//     cv::Mat Wf;
//     cv::merge(wfCh, Wf);

//     /* Apply filter in the frequency domain: result = IDFT(W * Y). */
//     cv::Mat prodC;
//     cv::mulSpectrums(Wf, Yf, prodC, 0);

//     cv::Mat result;
//     cv::dft(prodC, result,
//             cv::DFT_INVERSE | cv::DFT_REAL_OUTPUT | cv::DFT_SCALE);

//     return result.clone();
// }

// /* =========================================================================
//  * Total Variation Gradient
//  * ========================================================================= */

// /**
//  * @brief Compute the TV (Total Variation) sub-gradient of an image.
//  *
//  * Uses forward differences for the gradient and backward differences
//  * for the divergence, with Neumann boundary conditions.
//  *
//  * @param u   Input image (CV_32F).
//  * @param eps Small constant to avoid division by zero in the gradient norm.
//  * @return TV divergence map (same size as @p u).
//  */
// cv::Mat Deconvolution::tvGradient(const cv::Mat& u, double eps)
// {
//     const int H = u.rows, W = u.cols;

//     cv::Mat gx(H, W, CV_32F, cv::Scalar(0));
//     cv::Mat gy(H, W, CV_32F, cv::Scalar(0));

//     /* Forward differences for the gradient. */
//     for (int y = 0; y < H; ++y)
//         for (int x = 0; x < W - 1; ++x)
//             gx.at<float>(y, x) = u.at<float>(y, x + 1) - u.at<float>(y, x);

//     for (int y = 0; y < H - 1; ++y)
//         for (int x = 0; x < W; ++x)
//             gy.at<float>(y, x) = u.at<float>(y + 1, x) - u.at<float>(y, x);

//     /* Gradient magnitude with regularization. */
//     cv::Mat norm = cv::Mat::zeros(H, W, CV_32F);
//     for (int y = 0; y < H; ++y)
//         for (int x = 0; x < W; ++x) {
//             const float dx = gx.at<float>(y, x);
//             const float dy = gy.at<float>(y, x);
//             norm.at<float>(y, x) = std::sqrt(dx * dx + dy * dy + (float)(eps * eps));
//         }

//     /* Normalized gradient components. */
//     cv::Mat nx = gx / norm;
//     cv::Mat ny = gy / norm;

//     /* Backward divergence. */
//     cv::Mat div = cv::Mat::zeros(H, W, CV_32F);
//     for (int y = 0; y < H; ++y)
//         for (int x = 0; x < W; ++x) {
//             const float d =
//                 nx.at<float>(y, x) - (x > 0 ? nx.at<float>(y, x - 1) : 0.0f) +
//                 ny.at<float>(y, x) - (y > 0 ? ny.at<float>(y - 1, x) : 0.0f);
//             div.at<float>(y, x) = d;
//         }

//     return div;
// }

// /* =========================================================================
//  * Star Mask Construction
//  * ========================================================================= */

// /**
//  * @brief Build a simple threshold + dilation star mask (single-frame mode).
//  *
//  * Returns a KEEP map where 1 = fully deconvolved, blend..1 = mixed.
//  */
// cv::Mat Deconvolution::buildStarMask(const cv::Mat& plane, const DeconvStarMask& m)
// {
//     cv::Mat mask = cv::Mat::ones(plane.size(), CV_32F);
//     if (!m.useMask) return mask;

//     /* Binary threshold on pixel intensity. */
//     cv::Mat binary;
//     cv::threshold(plane, binary, m.threshold, 1.0, cv::THRESH_BINARY);
//     binary.convertTo(binary, CV_8U, 255.0);

//     /* Morphological dilation to cover the full stellar profile. */
//     const int rad = std::max(1, (int)std::round(m.radius));
//     cv::Mat kernel = cv::getStructuringElement(
//         cv::MORPH_ELLIPSE, cv::Size(2 * rad + 1, 2 * rad + 1));
//     cv::Mat dilated;
//     cv::dilate(binary, dilated, kernel);

//     /* Convert to float KEEP map. */
//     dilated.convertTo(mask, CV_32F, 1.0 / 255.0);
//     mask = 1.0f - mask;

//     /* Apply blend factor: KEEP ranges from [blend, 1.0]. */
//     mask = (float)m.blend + (1.0f - (float)m.blend) * mask;
//     cv::threshold(mask, mask, 0.0, 1.0, cv::THRESH_TOZERO);

//     return mask;
// }

// /**
//  * @brief Build a star-protection KEEP map using SEP-style contour detection.
//  *
//  * Pipeline:
//  *  1. Robust background estimation (Gaussian blur + MAD).
//  *  2. Detection downscale for performance.
//  *  3. Multi-level sigma threshold ladder.
//  *  4. Contour extraction, capped at maxObjs.
//  *  5. Circle drawing with scaled radii.
//  *  6. Gaussian feathering.
//  *  7. KEEP map = keep_floor + (1 - keep_floor) * (1 - mask).
//  *
//  * Returns an all-ones map if no stars are detected.
//  */
// cv::Mat Deconvolution::buildStarMaskSEP(const cv::Mat& luma2d,
//                                         const DeconvStarMask& cfg)
// {
//     const int H = luma2d.rows, W = luma2d.cols;
//     cv::Mat keepMap = cv::Mat::ones(H, W, CV_32F);
//     if (!cfg.useMask) return keepMap;

//     /* --- Robust background estimation ------------------------------------ */
//     cv::Mat luma;
//     luma2d.convertTo(luma, CV_32F);

//     cv::Mat bgr;
//     cv::GaussianBlur(luma, bgr, cv::Size(0, 0), 64.0, 64.0, cv::BORDER_REFLECT);
//     cv::Mat residual = luma - bgr;

//     /* Noise level via Median Absolute Deviation (MAD). */
//     std::vector<float> vals(residual.begin<float>(), residual.end<float>());
//     std::nth_element(vals.begin(), vals.begin() + vals.size() / 2, vals.end());
//     const float med = vals[vals.size() / 2];

//     std::vector<float> absDev;
//     absDev.reserve(vals.size());
//     for (float v : vals) absDev.push_back(std::abs(v - med));
//     std::nth_element(absDev.begin(), absDev.begin() + absDev.size() / 2, absDev.end());

//     float errScalar = 1.4826f * absDev[absDev.size() / 2];
//     if (errScalar < 1e-8f) errScalar = 1e-8f;

//     /* --- Detection downscale --------------------------------------------- */
//     cv::Mat det = residual;
//     float scale = 1.0f;
//     const int maxSide = cfg.maxSide > 0 ? cfg.maxSide : STAR_MASK_MAXSIDE;

//     if (std::max(H, W) > maxSide) {
//         scale = (float)std::max(H, W) / (float)maxSide;
//         cv::Mat small;
//         cv::resize(residual, small,
//                    cv::Size(std::max(1, (int)std::round(W / scale)),
//                             std::max(1, (int)std::round(H / scale))),
//                    0, 0, cv::INTER_AREA);
//         det = small;
//     }

//     /* --- Sigma threshold ladder ------------------------------------------ */
//     const double ts = cfg.threshSigma > 0 ? cfg.threshSigma : THRESHOLD_SIGMA;
//     std::vector<double> thresholds;
//     for (double mult : {1.0, 2.0, 4.0, 8.0, 16.0})
//         thresholds.push_back(ts * mult);

//     /* --- Object extraction via contour analysis -------------------------- */
//     struct ObjInfo { float x, y, a, b; };
//     std::vector<ObjInfo> objs;
//     const int maxObjs = cfg.maxObjs > 0 ? cfg.maxObjs : STAR_MASK_MAXOBJS;

//     for (double thr : thresholds) {
//         cv::Mat binary;
//         cv::threshold(det, binary, (float)(thr * errScalar), 255.0f, cv::THRESH_BINARY);
//         binary.convertTo(binary, CV_8U);

//         std::vector<std::vector<cv::Point>> contours;
//         cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

//         if (contours.empty()) continue;
//         if ((int)contours.size() > maxObjs * 12) continue;

//         objs.clear();
//         for (const auto& c : contours) {
//             if (c.size() < 5) {
//                 cv::Moments mom = cv::moments(c);
//                 if (mom.m00 < 1e-8) continue;
//                 const float cx_ = (float)(mom.m10 / mom.m00);
//                 const float cy_ = (float)(mom.m01 / mom.m00);
//                 objs.push_back({cx_, cy_, 1.0f, 1.0f});
//             } else {
//                 cv::RotatedRect rr = cv::fitEllipse(c);
//                 objs.push_back({rr.center.x, rr.center.y,
//                                 rr.size.width / 2.0f, rr.size.height / 2.0f});
//             }
//         }
//         if (!objs.empty()) break;
//     }

//     if (objs.empty()) return keepMap;

//     /* Cap the number of detected objects. */
//     if ((int)objs.size() > maxObjs)
//         objs.resize(maxObjs);

//     /* --- Draw circles on the full-resolution mask ------------------------ */
//     cv::Mat mask8 = cv::Mat::zeros(H, W, CV_8U);
//     const int MR = std::max(1, cfg.maxRadiusPx > 0 ? cfg.maxRadiusPx : MAX_STAR_RADIUS);
//     const int G  = std::max(0, cfg.growPx >= 0 ? cfg.growPx : GROW_PX);
//     const double ES = cfg.ellipseScale > 0.1 ? cfg.ellipseScale : ELLIPSE_SCALE;

//     for (const auto& o : objs) {
//         const int x = (int)std::round(o.x * scale);
//         const int y = (int)std::round(o.y * scale);
//         if (x < 0 || x >= W || y < 0 || y >= H) continue;

//         const double ab = ES * std::max((double)o.a, (double)o.b) * scale;
//         const int r = std::min((int)std::ceil(ab) + G, MR);
//         if (r <= 0) continue;

//         cv::circle(mask8, cv::Point(x, y), r, cv::Scalar(1), -1, cv::LINE_8);
//     }

//     /* --- Gaussian feathering ---------------------------------------------- */
//     cv::Mat mf;
//     mask8.convertTo(mf, CV_32F);
//     const double softSig = cfg.softSigma > 0 ? cfg.softSigma : SOFT_SIGMA;

//     if (softSig > 0.0) {
//         const int ks = (int)(std::max(1, (int)std::round(3 * softSig)) * 2 + 1);
//         cv::GaussianBlur(mf, mf, cv::Size(ks, ks), softSig, softSig, cv::BORDER_REFLECT);
//         cv::threshold(mf, mf, 0.0, 1.0, cv::THRESH_TOZERO);
//         cv::min(mf, 1.0f, mf);
//     }

//     /* --- Construct final KEEP map ---------------------------------------- */
//     const double kf = std::max(0.0, std::min(0.99,
//         cfg.keepFloor > 0 ? cfg.keepFloor : KEEP_FLOOR));
//     cv::Mat keep = 1.0f - mf;
//     keep = (float)kf + (float)(1.0 - kf) * keep;
//     cv::min(keep, 1.0f, keep);
//     cv::max(keep, 0.0f, keep);

//     return keep;
// }

// /* =========================================================================
//  * Variance Map Construction
//  * ========================================================================= */

// /**
//  * @brief Build a per-pixel variance map combining read noise and shot noise.
//  *
//  * var(x, y) = var_bg(x, y) + a_shot * max(img(x, y) - sky(x, y), 0)
//  *
//  * @param luma2d   Luminance image (CV_32F).
//  * @param cfg      Variance map configuration.
//  * @param gainHint Camera gain (e-/ADU). If <= 0, it is estimated from the data.
//  */
// cv::Mat Deconvolution::buildVarianceMap(const cv::Mat& luma2d,
//                                         const MFVarianceMapCfg& cfg,
//                                         double gainHint)
// {
//     const int H = luma2d.rows, W = luma2d.cols;

//     cv::Mat img;
//     luma2d.convertTo(img, CV_32F);
//     cv::max(img, 0.0f, img);

//     /* --- Background estimation via wide Gaussian blur -------------------- */
//     const int bw = std::max(8, std::min(cfg.bw > 0 ? cfg.bw : 64, W));
//     const int bh = std::max(8, std::min(cfg.bh > 0 ? cfg.bh : 64, H));
//     const double sigBg = std::min(bw, bh) * 0.5;

//     cv::Mat skyMap;
//     cv::GaussianBlur(img, skyMap, cv::Size(0, 0), sigBg, sigBg, cv::BORDER_REFLECT);

//     /* --- Local RMS map (approximated via second-moment subtraction) ------- */
//     cv::Mat img2;
//     cv::multiply(img, img, img2);

//     cv::Mat sky2;
//     cv::multiply(skyMap, skyMap, sky2);

//     cv::Mat mean2;
//     cv::GaussianBlur(img2, mean2, cv::Size(0, 0), sigBg, sigBg, cv::BORDER_REFLECT);

//     cv::Mat rmsMap;
//     cv::sqrt(cv::max(mean2 - sky2, 0.0f), rmsMap);
//     cv::max(rmsMap, 1e-6f, rmsMap);

//     /* Background variance = RMS^2. */
//     cv::Mat varBg;
//     cv::multiply(rmsMap, rmsMap, varBg);

//     /* Object signal above sky. */
//     cv::Mat objDn = img - skyMap;
//     cv::max(objDn, 0.0f, objDn);

//     /* --- Shot noise coefficient ------------------------------------------ */
//     float aShot = 0.0f;
//     if (gainHint > 0.0) {
//         aShot = (float)(1.0 / gainHint);
//     } else {
//         cv::Scalar skyMed = cv::mean(skyMap);
//         cv::Scalar vbgMed = cv::mean(varBg);
//         if (skyMed[0] > 1e-6)
//             aShot = (float)std::min(vbgMed[0] / skyMed[0], 10.0);
//     }

//     /* --- Combined variance ----------------------------------------------- */
//     cv::Mat v = varBg + aShot * objDn;

//     /* Optional spatial smoothing. */
//     const double ss = cfg.smoothSigma > 0 ? cfg.smoothSigma : 1.0;
//     if (ss > 0.0) {
//         const int ks = (int)(std::max(1, (int)std::round(3 * ss)) * 2 + 1);
//         cv::GaussianBlur(v, v, cv::Size(ks, ks), ss, ss, cv::BORDER_REFLECT);
//     }

//     /* Apply minimum floor. */
//     const float floor = (float)(cfg.floor > 0 ? cfg.floor : 1e-8);
//     cv::max(v, floor, v);

//     return v;
// }

// /* =========================================================================
//  * Super-Resolution Helpers
//  * ========================================================================= */

// /**
//  * @brief Downsample an image by average pooling with integer factor @p r.
//  *
//  * Crops input to the nearest multiple of @p r before pooling.
//  */
// cv::Mat Deconvolution::downsampleAvg(const cv::Mat& img, int r)
// {
//     if (r <= 1) return img.clone();

//     const int H = img.rows, W = img.cols;
//     const int Hs = (H / r) * r;
//     const int Ws = (W / r) * r;

//     cv::Mat crop = img(cv::Rect(0, 0, Ws, Hs));
//     cv::Mat out(Hs / r, Ws / r, CV_32F);

//     /* INTER_AREA is equivalent to average pooling for integer factors. */
//     cv::resize(crop, out, cv::Size(Ws / r, Hs / r), 0, 0, cv::INTER_AREA);
//     return out;
// }

// /**
//  * @brief Upsample by nearest-neighbor replication (Kronecker product with ones).
//  *
//  * This is the adjoint operator of @ref downsampleAvg for the multiplicative
//  * RL update in super-resolution mode.
//  */
// cv::Mat Deconvolution::upsampleSum(const cv::Mat& img, int r, cv::Size targetHW)
// {
//     if (r <= 1) return img.clone();

//     cv::Mat out(img.rows * r, img.cols * r, CV_32F);
//     cv::resize(img, out, cv::Size(img.cols * r, img.rows * r), 0, 0, cv::INTER_NEAREST);

//     if (targetHW.width > 0 && targetHW.height > 0)
//         return centerCrop(out, targetHW.height, targetHW.width);
//     return out;
// }

// /* =========================================================================
//  * General Utility Functions
//  * ========================================================================= */

// /**
//  * @brief Center-crop or center-pad a matrix to the specified target size.
//  */
// cv::Mat Deconvolution::centerCrop(const cv::Mat& arr, int Ht, int Wt, float fillVal)
// {
//     const int H = arr.rows, W = arr.cols;
//     if (H == Ht && W == Wt) return arr.clone();

//     /* Crop if the source is larger than the target. */
//     const int y0 = std::max(0, (H - Ht) / 2);
//     const int x0 = std::max(0, (W - Wt) / 2);
//     const int y1 = std::min(H, y0 + Ht);
//     const int x1 = std::min(W, x0 + Wt);
//     cv::Mat cropped = arr(cv::Rect(x0, y0, x1 - x0, y1 - y0)).clone();

//     if (cropped.rows == Ht && cropped.cols == Wt)
//         return cropped;

//     /* Pad if the source is smaller than the target. */
//     cv::Mat out(Ht, Wt, CV_32F, cv::Scalar(fillVal));
//     const int oy = (Ht - cropped.rows) / 2;
//     const int ox = (Wt - cropped.cols) / 2;
//     cropped.copyTo(out(cv::Rect(ox, oy, cropped.cols, cropped.rows)));
//     return out;
// }

// /**
//  * @brief Sanitize numeric values: replace NaN/Inf with zero, clamp negatives.
//  */
// cv::Mat Deconvolution::sanitizeNumeric(const cv::Mat& a)
// {
//     cv::Mat out;
//     a.convertTo(out, CV_32F);
//     cv::patchNaNs(out, 0.0);
//     cv::max(out, 0.0f, out);
//     return out;
// }

// /**
//  * @brief Estimate a robust scalar variance from flat data via MAD.
//  *
//  * var = (1.4826 * MAD)^2, where MAD is the Median Absolute Deviation.
//  */
// double Deconvolution::estimateScalarVariance(const cv::Mat& a)
// {
//     std::vector<float> vals(a.begin<float>(), a.end<float>());
//     if (vals.empty()) return 1.0;

//     std::nth_element(vals.begin(), vals.begin() + vals.size() / 2, vals.end());
//     const float med = vals[vals.size() / 2];

//     std::vector<float> absDev;
//     absDev.reserve(vals.size());
//     for (float v : vals) absDev.push_back(std::abs(v - med));

//     std::nth_element(absDev.begin(), absDev.begin() + absDev.size() / 2, absDev.end());
//     const float mad = absDev[absDev.size() / 2] + 1e-6f;

//     return (double)(1.4826f * mad) * (1.4826f * mad);
// }

// /* =========================================================================
//  * Huber Weights
//  * ========================================================================= */

// /**
//  * @brief Compute per-pixel robust weights using the Huber loss function.
//  *
//  * The weight for each pixel is:
//  *   w = psi(r) / r * 1 / (var + eps) * mask
//  *
//  * where psi(r)/r = 1 for |r| <= delta (inlier), and
//  *       psi(r)/r = delta / (|r| + eps) otherwise (outlier, downweighted).
//  *
//  * @param residual  Pixel-wise residual (observed - predicted).
//  * @param huberDelta  >0: fixed threshold, <0: auto (|delta| * RMS), 0: L2 (all 1s).
//  * @param varMap    Per-pixel variance map (may be empty for scalar estimate).
//  * @param mask      Star-protection mask (may be empty).
//  */
// cv::Mat Deconvolution::computeHuberWeights(const cv::Mat& residual,
//                                            double huberDelta,
//                                            const cv::Mat& varMap,
//                                            const cv::Mat& mask)
// {
//     float delta = (float)huberDelta;

//     /* Automatic delta from residual statistics. */
//     if (huberDelta < 0.0) {
//         const double varEst = estimateScalarVariance(residual);
//         const float rms = (float)std::sqrt(varEst);
//         delta = (float)(-huberDelta) * std::max(rms, 1e-6f);
//     }

//     const cv::Mat absR = cv::abs(residual);

//     /* --- Compute psi(r) / r ---------------------------------------------- */
//     cv::Mat psiR;
//     if (delta > 0.0f) {
//         cv::Mat isOutlier;
//         cv::threshold(absR, isOutlier, (double)delta, 1.0, cv::THRESH_BINARY);

//         cv::Mat outlierW = delta / (absR + EPS);

//         psiR = cv::Mat::ones(residual.size(), CV_32F);
//         psiR = psiR.mul(1.0f - isOutlier) + outlierW.mul(isOutlier);
//     } else {
//         /* L2 mode: uniform weights. */
//         psiR = cv::Mat::ones(residual.size(), CV_32F);
//     }

//     /* --- Incorporate variance map ---------------------------------------- */
//     cv::Mat v;
//     if (varMap.empty()) {
//         const double varEst = estimateScalarVariance(residual);
//         v = cv::Mat::ones(residual.size(), CV_32F) * (float)(varEst);
//     } else {
//         varMap.copyTo(v);
//     }

//     cv::Mat w = psiR / (v + EPS);

//     /* --- Apply star-protection mask -------------------------------------- */
//     if (!mask.empty()) {
//         cv::multiply(w, mask, w);
//     }

//     return w;
// }

// /* =========================================================================
//  * Progress and File Path Utilities
//  * ========================================================================= */

// /**
//  * @brief Emit a structured progress string through a callback.
//  *
//  * Format: "__PROGRESS__ 0.xxxx optional_message"
//  */
// void Deconvolution::emitProgress(const std::function<void(const QString&)>& cb,
//                                  double fraction, const QString& msg)
// {
//     if (!cb) return;
//     fraction = std::max(0.0, std::min(1.0, fraction));
//     QString line = QString("__PROGRESS__ %1").arg(fraction, 0, 'f', 4);
//     if (!msg.isEmpty()) line += " " + msg;
//     cb(line);
// }

// /**
//  * @brief Construct the canonical output file path for MFDeconv results.
//  */
// QString Deconvolution::buildOutputPath(const QString& basePath, int superResFactor)
// {
//     QFileInfo fi(basePath);
//     QString dir  = fi.absolutePath();
//     QString base = fi.baseName();
//     QString ext  = fi.suffix();

//     /* Strip any existing MFDeconv suffix to prevent duplication. */
//     base.remove(QRegularExpression("(?i)[\\s_]+MFDeconv$"));
//     base.remove(QRegularExpression(
//         "\\(?\\s*(\\d{2,5})x(\\d{2,5})\\s*\\)?",
//         QRegularExpression::CaseInsensitiveOption));

//     QString newStem = "MFDeconv_" + base;
//     if (superResFactor > 1)
//         newStem += QString("_%1x").arg(superResFactor);

//     return dir + "/" + newStem + "." + ext;
// }

// /**
//  * @brief Append a version suffix (_v2, _v3, ...) to avoid overwriting files.
//  */
// QString Deconvolution::nonclobberPath(const QString& path)
// {
//     if (!QFileInfo::exists(path)) return path;

//     QFileInfo fi(path);
//     QString dir  = fi.absolutePath();
//     QString base = fi.baseName();
//     QString ext  = fi.suffix();

//     QRegularExpression vRe("(.*)_v(\\d+)$");
//     auto m = vRe.match(base);

//     QString stem;
//     int n;
//     if (m.hasMatch()) {
//         stem = m.captured(1);
//         n    = m.captured(2).toInt() + 1;
//     } else {
//         stem = base;
//         n    = 2;
//     }

//     while (true) {
//         QString candidate = dir + "/" + stem + "_v" + QString::number(n) + "." + ext;
//         if (!QFileInfo::exists(candidate)) return candidate;
//         ++n;
//     }
// }

// /* =========================================================================
//  * PSF Derivation for Multi-Frame Mode
//  * ========================================================================= */

// /**
//  * @brief Derive a PSF kernel from a single luminance frame.
//  *
//  * Attempts empirical PSF extraction through a kernel-size ladder.
//  * If empirical extraction is unavailable, falls back to a Gaussian
//  * PSF based on the estimated FWHM.  Applies light softening (sigma = 0.25 px)
//  * and normalization.
//  */
// MFPsfInfo Deconvolution::derivePsfForFrame(const cv::Mat& lumaFrame, double fwhmHint)
// {
//     /* Estimate or validate the FWHM. */
//     double fwhm = fwhmHint;
//     if (fwhm <= 0.0) fwhm = estimateFwhmFromImage(lumaFrame);
//     if (!std::isfinite(fwhm) || fwhm <= 0.0) fwhm = 2.5;

//     const int kAuto = autoKsizeFromFwhm(fwhm);

//     /* Attempt empirical PSF (placeholder for production integration). */
//     cv::Mat psf;
//     std::vector<int> kLadder = {kAuto, std::max(kAuto - 4, 11), 21, 17, 15, 13, 11};
//     for (int k : kLadder) {
//         (void)k;
//         if (psf.empty()) break;
//     }

//     /* Fallback to synthetic Gaussian. */
//     if (psf.empty()) {
//         psf = gaussianPsf(fwhm, kAuto);
//     }

//     /* Post-processing: normalize and apply light softening. */
//     psf = normalizePsf(psf);
//     psf = softenPsf(psf, 0.25);

//     MFPsfInfo info;
//     info.kernel = psf;
//     info.ksize  = psf.rows;
//     info.fwhmPx = psfFwhmPx(psf);
//     return info;
// }

// /**
//  * @brief Estimate the seeing FWHM from a luminance image.
//  *
//  * Uses contour-based blob detection and ellipse fitting as a fallback
//  * when direct SEP integration is unavailable.
//  *
//  * @return Median FWHM in pixels, or NaN if estimation fails.
//  */
// double Deconvolution::estimateFwhmFromImage(const cv::Mat& luma)
// {
//     /* Light blur to suppress noise; heavy blur for background. */
//     cv::Mat blurred;
//     cv::GaussianBlur(luma, blurred, cv::Size(0, 0), 3.0, 3.0, cv::BORDER_REFLECT);

//     cv::Mat bg;
//     cv::GaussianBlur(luma, bg, cv::Size(0, 0), 64.0, 64.0, cv::BORDER_REFLECT);

//     cv::Mat sub = blurred - bg;

//     double mn, mx;
//     cv::minMaxLoc(sub, &mn, &mx);
//     if (mx <= 0.0) return std::numeric_limits<double>::quiet_NaN();

//     /* Threshold at 30% of peak to isolate stellar cores. */
//     cv::Mat binary;
//     cv::threshold(sub, binary, mx * 0.3, 255.0, cv::THRESH_BINARY);
//     binary.convertTo(binary, CV_8U);

//     std::vector<std::vector<cv::Point>> contours;
//     cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
//     if (contours.empty()) return std::numeric_limits<double>::quiet_NaN();

//     /* Collect FWHM estimates from ellipse fits. */
//     std::vector<double> fwhms;
//     for (const auto& c : contours) {
//         if (c.size() < 5) continue;
//         cv::RotatedRect rr = cv::fitEllipse(c);
//         const double sigma = (rr.size.width + rr.size.height) * 0.25;
//         if (sigma > 0.0 && std::isfinite(sigma))
//             fwhms.push_back(FWHM_TO_SIGMA * sigma);
//     }

//     if (fwhms.empty()) return std::numeric_limits<double>::quiet_NaN();

//     std::sort(fwhms.begin(), fwhms.end());
//     return fwhms[fwhms.size() / 2];
// }

// /* =========================================================================
//  * Seed Image Construction
//  * ========================================================================= */

// /**
//  * @brief Build a robust seed image using a four-pass strategy.
//  *
//  * Pass 1: Welford online mean over bootstrap frames.
//  * Pass 2: Global MAD estimation for clipping threshold (+/- 4 * MAD).
//  * Pass 3: Masked (clipped) mean over bootstrap frames.
//  * Pass 4: Sigma-clipped Welford streaming over the remaining frames.
//  */
// cv::Mat Deconvolution::buildRobustSeed(const std::vector<cv::Mat>& frames,
//                                        int bootstrapFrames, double clipSigma)
// {
//     const int n = (int)frames.size();
//     if (n == 0) return cv::Mat();
//     const int B = std::max(1, std::min(bootstrapFrames, n));

//     /* --- Pass 1: Welford mean over bootstrap frames ---------------------- */
//     cv::Mat mu = frames[0].clone();
//     mu.convertTo(mu, CV_32F);
//     for (int i = 1; i < B; ++i) {
//         cv::Mat x;
//         frames[i].convertTo(x, CV_32F);
//         const float cnt = (float)(i + 1);
//         mu = mu + (x - mu) * (1.0f / cnt);
//     }

//     /* --- Pass 2: Global MAD estimation ----------------------------------- */
//     std::vector<float> madSamples;
//     for (int i = 0; i < B; ++i) {
//         cv::Mat x;
//         frames[i].convertTo(x, CV_32F);
//         cv::Mat d = cv::abs(x - mu);
//         std::vector<float> dv(d.begin<float>(), d.end<float>());
//         madSamples.insert(madSamples.end(), dv.begin(), dv.end());
//     }
//     std::nth_element(madSamples.begin(),
//                      madSamples.begin() + madSamples.size() / 2,
//                      madSamples.end());
//     const float madEst = madSamples[madSamples.size() / 2];
//     const float thr = 4.0f * std::max(madEst, 1e-6f);

//     /* --- Pass 3: Masked mean over bootstrap frames ----------------------- */
//     cv::Mat sumAcc = cv::Mat::zeros(mu.size(), CV_32F);
//     cv::Mat cntAcc = cv::Mat::zeros(mu.size(), CV_32F);
//     for (int i = 0; i < B; ++i) {
//         cv::Mat x;
//         frames[i].convertTo(x, CV_32F);
//         cv::Mat maskMat = (cv::abs(x - mu) <= thr);
//         maskMat.convertTo(maskMat, CV_32F, 1.0 / 255.0);
//         sumAcc += x.mul(maskMat);
//         cntAcc += maskMat;
//     }

//     cv::Mat seed = mu.clone();
//     for (int i = 0; i < mu.rows; ++i)
//         for (int j = 0; j < mu.cols; ++j)
//             if (cntAcc.at<float>(i, j) > 0.5f)
//                 seed.at<float>(i, j) = sumAcc.at<float>(i, j) / cntAcc.at<float>(i, j);

//     /* --- Pass 4: Sigma-clipped Welford streaming over remaining frames --- */
//     cv::Mat M2  = cv::Mat::zeros(seed.size(), CV_32F);
//     cv::Mat cnt = cv::Mat::ones(seed.size(), CV_32F) * (float)B;
//     mu = seed.clone();

//     for (int i = B; i < n; ++i) {
//         cv::Mat x;
//         frames[i].convertTo(x, CV_32F);
//         cv::Mat var = M2 / cv::max(cnt - 1.0f, 1.0f);
//         cv::Mat sigma;
//         cv::sqrt(cv::max(var, 1e-12f), sigma);
//         cv::Mat acc = (cv::abs(x - mu) <= (float)clipSigma * sigma);
//         acc.convertTo(acc, CV_32F, 1.0 / 255.0);

//         cv::Mat nNew  = cnt + acc;
//         cv::Mat delta = x - mu;
//         cv::Mat muN   = mu + acc.mul(delta) / cv::max(nNew, 1.0f);
//         M2  = M2 + acc.mul(delta).mul(x - muN);
//         mu  = muN;
//         cnt = nNew;
//     }

//     cv::max(mu, 0.0f, mu);
//     return mu;
// }

// /**
//  * @brief Build a median seed image via tiled pixel-wise median.
//  *
//  * Processes the image in tiles to bound memory usage.
//  */
// cv::Mat Deconvolution::buildMedianSeed(const std::vector<cv::Mat>& frames,
//                                        cv::Size tileHW)
// {
//     if (frames.empty()) return cv::Mat();

//     const int H = frames[0].rows, W = frames[0].cols;
//     const int n  = (int)frames.size();
//     const int th = tileHW.height, tw = tileHW.width;

//     cv::Mat seed = cv::Mat::zeros(H, W, CV_32F);

//     for (int y0 = 0; y0 < H; y0 += th) {
//         const int y1  = std::min(y0 + th, H);
//         for (int x0 = 0; x0 < W; x0 += tw) {
//             const int x1   = std::min(x0 + tw, W);
//             const int th_  = y1 - y0;
//             const int tw_  = x1 - x0;

//             /* Collect pixel stacks for the current tile. */
//             std::vector<std::vector<float>> slab(th_ * tw_, std::vector<float>(n));
//             for (int i = 0; i < n; ++i) {
//                 cv::Mat f;
//                 frames[i].convertTo(f, CV_32F);
//                 cv::Mat tile = f(cv::Rect(x0, y0, tw_, th_));
//                 for (int r = 0; r < th_; ++r)
//                     for (int c = 0; c < tw_; ++c)
//                         slab[r * tw_ + c][i] = tile.at<float>(r, c);
//             }

//             /* Compute the pixel-wise median. */
//             for (int r = 0; r < th_; ++r) {
//                 for (int c = 0; c < tw_; ++c) {
//                     auto& pix = slab[r * tw_ + c];
//                     std::nth_element(pix.begin(), pix.begin() + n / 2, pix.end());
//                     seed.at<float>(y0 + r, x0 + c) = pix[n / 2];
//                 }
//             }
//         }
//     }

//     return seed;
// }

// /* =========================================================================
//  * Super-Resolution PSF Solver
//  * ========================================================================= */

// /**
//  * @brief Solve for the super-resolution PSF via gradient descent.
//  *
//  * Minimizes: ||f_native - D(h * g_sigma)||^2
//  * where D is the downsampling operator and g_sigma is a native-scale Gaussian.
//  *
//  * @param nativePsf  Native-resolution PSF to lift.
//  * @param r          Super-resolution factor.
//  * @param sigma      Gaussian sigma for the native-scale blur kernel.
//  * @param iters      Number of gradient descent iterations.
//  * @param lr         Initial learning rate.
//  * @return Normalized super-resolution PSF of size (r*k) x (r*k).
//  */
// cv::Mat Deconvolution::solveSuperPsf(const cv::Mat& nativePsf,
//                                      int r, double sigma,
//                                      int iters, double lr)
// {
//     cv::Mat f;
//     nativePsf.convertTo(f, CV_32F);

//     /* Ensure the kernel is square with odd dimensions. */
//     int k = std::min(f.rows, f.cols);
//     if (k % 2 == 0) {
//         f = f(cv::Rect(1, 1, k - 1, k - 1));
//         k = k - 1;
//     }
//     const int kr = k * r;

//     /* Native-scale Gaussian for the forward model. */
//     cv::Mat g = gaussianPsf(sigma * FWHM_TO_SIGMA, k);

//     /* Initialize h via zero-insertion: place native PSF at every r-th pixel. */
//     cv::Mat h0 = cv::Mat::zeros(kr, kr, CV_32F);
//     for (int i = 0; i < k; ++i)
//         for (int j = 0; j < k; ++j)
//             h0.at<float>(i * r, j * r) = f.at<float>(i, j);
//     h0 = normalizePsf(h0);

//     cv::Mat h = h0.clone();
//     double eta = lr;

//     /* Gradient descent loop. */
//     for (int iter = 0; iter < std::max(50, iters); ++iter) {
//         /* Forward model: downsample(h) * g. */
//         cv::Mat Dh   = downsampleAvg(h, r);
//         cv::Mat conv = convolveFFT(Dh, g);
//         cv::Mat resid = conv - f;

//         /* Adjoint gradient: upsample(convolve(resid, g_flipped)). */
//         cv::Mat gFlip  = flipKernel(g);
//         cv::Mat gradDh = convolveFFT(resid, gFlip);
//         cv::Mat gradH  = upsampleSum(gradDh, r, cv::Size(kr, kr));

//         /* Update with projection to non-negative, normalized simplex. */
//         h = h - (float)eta * gradH;
//         cv::max(h, 0.0f, h);
//         const float s = (float)cv::sum(h)[0];
//         if (s > 1e-8f) h /= s;

//         eta *= 0.995;
//     }

//     /* Ensure odd kernel dimensions. */
//     if (h.rows % 2 == 0)
//         h = h(cv::Rect(0, 0, h.cols - 1, h.rows - 1)).clone();

//     return normalizePsf(h);
// }

// /* =========================================================================
//  * Color Space Conversion (RGB <-> YCbCr, ITU-R BT.709)
//  * ========================================================================= */

// /**
//  * @brief Convert planar RGB to planar YCbCr using BT.709 coefficients.
//  *
//  * Layout: data[0..N-1] = R, data[N..2N-1] = G, data[2N..3N-1] = B.
//  */
// void Deconvolution::rgbToYcbcr(const std::vector<float>& rgb,
//                                std::vector<float>& ycbcr,
//                                int width, int height)
// {
//     const int N = width * height;
//     ycbcr.resize(N * 3);

//     for (int i = 0; i < N; ++i) {
//         const float r = rgb[i];
//         const float g = rgb[N + i];
//         const float b = rgb[2 * N + i];

//         ycbcr[i]         =  0.2126f * r + 0.7152f * g + 0.0722f * b;
//         ycbcr[N + i]     = -0.1146f * r - 0.3854f * g + 0.5f    * b + 0.5f;
//         ycbcr[2 * N + i] =  0.5f    * r - 0.4542f * g - 0.0458f * b + 0.5f;
//     }
// }

// /**
//  * @brief Convert planar YCbCr back to planar RGB using BT.709 coefficients.
//  */
// void Deconvolution::ycbcrToRgb(const std::vector<float>& ycbcr,
//                                std::vector<float>& rgb,
//                                int width, int height, int)
// {
//     const int N = width * height;
//     rgb.resize(N * 3);

//     for (int i = 0; i < N; ++i) {
//         const float Y  = ycbcr[i];
//         const float Cb = ycbcr[N + i] - 0.5f;
//         const float Cr = ycbcr[2 * N + i] - 0.5f;

//         rgb[i]         = std::max(0.0f, std::min(1.0f, Y + 1.5748f * Cr));
//         rgb[N + i]     = std::max(0.0f, std::min(1.0f, Y - 0.1873f * Cb - 0.4681f * Cr));
//         rgb[2 * N + i] = std::max(0.0f, std::min(1.0f, Y + 1.8556f * Cb));
//     }
// }

// /* =========================================================================
//  * Single-Image Deconvolution Algorithms
//  * ========================================================================= */

// /**
//  * @brief Richardson-Lucy deconvolution without regularization.
//  *
//  * Iterative multiplicative update:
//  *   blurred    = conv(f, PSF)
//  *   ratio      = observed / (blurred + eps)
//  *   correction = conv(ratio, PSF^T)
//  *   f_new      = f * correction
//  */
// DeconvResult Deconvolution::applyRL(cv::Mat& plane, const cv::Mat& psf,
//                                     const cv::Mat& starMask, const DeconvParams& p)
// {
//     DeconvResult res;
//     cv::Mat f;
//     plane.copyTo(f);
//     cv::max(f, EPS, f);

//     cv::Mat psfFlip = flipKernel(psf);

//     for (int it = 0; it < p.maxIter; ++it) {
//         cv::Mat blurred = convolveFFT(f, psf);
//         cv::max(blurred, EPS, blurred);

//         cv::Mat ratio = plane / blurred;
//         cv::Mat corr  = convolveFFT(ratio, psfFlip);
//         cv::Mat fNew  = f.mul(corr);
//         cv::max(fNew, 0.0f, fNew);

//         /* Measure relative convergence. */
//         cv::Mat diff;
//         cv::absdiff(fNew, f, diff);
//         const double change = cv::mean(diff)[0] / (cv::mean(f)[0] + 1e-8);

//         f = fNew;
//         res.finalChange = change;
//         res.iterations  = it + 1;

//         if (p.progressCallback)
//             p.progressCallback((int)(100.0 * (it + 1) / p.maxIter),
//                                QString("RL iter %1/%2").arg(it + 1).arg(p.maxIter));

//         if (change < p.convergenceTol) break;
//     }

//     /* Apply star-mask blending. */
//     if (!starMask.empty())
//         f = starMask.mul(f) + (1.0f - starMask).mul(plane);

//     /* Apply global strength blending. */
//     if (p.globalStrength < 1.0)
//         f = (float)p.globalStrength * f + (float)(1.0 - p.globalStrength) * plane;

//     f.copyTo(plane);
//     res.success = true;
//     return res;
// }

// /**
//  * @brief Richardson-Lucy with optional Total Variation or Tikhonov regularization.
//  */
// DeconvResult Deconvolution::applyRLTV(cv::Mat& plane, const cv::Mat& psf,
//                                       const cv::Mat& starMask, const DeconvParams& p)
// {
//     DeconvResult res;
//     cv::Mat f;
//     plane.copyTo(f);
//     cv::max(f, EPS, f);

//     cv::Mat psfFlip = flipKernel(psf);

//     for (int it = 0; it < p.maxIter; ++it) {
//         cv::Mat blurred = convolveFFT(f, psf);
//         cv::max(blurred, EPS, blurred);

//         cv::Mat ratio = plane / blurred;
//         cv::Mat corr  = convolveFFT(ratio, psfFlip);

//         /* Total Variation regularization term. */
//         if (p.regType == 2 && p.tvRegWeight > 0.0) {
//             cv::Mat tvGrad = tvGradient(f, p.tvEps);
//             corr = corr - (float)p.tvRegWeight * tvGrad;
//         }

//         /* Tikhonov (Laplacian) regularization term. */
//         if (p.regType == 1 && p.tvRegWeight > 0.0) {
//             cv::Mat lap;
//             cv::Laplacian(f, lap, CV_32F);
//             corr = corr - (float)p.tvRegWeight * lap;
//         }

//         cv::Mat fNew = f.mul(corr);
//         cv::max(fNew, 0.0f, fNew);

//         /* Optional bilateral de-ringing filter. */
//         if (p.dering) {
//             cv::Mat fBlur;
//             cv::bilateralFilter(fNew, fBlur, 5, 0.08, 1.0);
//             fNew = fBlur;
//         }

//         const double change = cv::norm(fNew - f, cv::NORM_L1)
//                             / (cv::norm(f, cv::NORM_L1) + 1e-8);

//         f = fNew;
//         res.finalChange = change;
//         res.iterations  = it + 1;

//         if (p.progressCallback)
//             p.progressCallback((int)(100.0 * (it + 1) / p.maxIter),
//                                QString("RLTV iter %1/%2").arg(it + 1).arg(p.maxIter));

//         if (change < p.convergenceTol) break;
//     }

//     if (!starMask.empty())
//         f = starMask.mul(f) + (1.0f - starMask).mul(plane);

//     if (p.globalStrength < 1.0)
//         f = (float)p.globalStrength * f + (float)(1.0 - p.globalStrength) * plane;

//     f.copyTo(plane);
//     res.success = true;
//     return res;
// }

// /**
//  * @brief Single-step Wiener deconvolution with optional de-ringing.
//  */
// DeconvResult Deconvolution::applyWiener(cv::Mat& plane, const cv::Mat& psf,
//                                         const DeconvParams& p)
// {
//     DeconvResult res;
//     cv::Mat result = wienerFilter(plane, psf, p.wienerK);
//     cv::max(result, 0.0f, result);
//     cv::min(result, 1.0f, result);

//     if (p.dering) {
//         cv::Mat bilat;
//         cv::bilateralFilter(result, bilat, 5, 0.08, 1.0);
//         result = bilat;
//     }

//     if (p.globalStrength < 1.0)
//         result = (float)p.globalStrength * result
//                + (float)(1.0 - p.globalStrength) * plane;

//     result.copyTo(plane);
//     res.success    = true;
//     res.iterations = 1;
//     return res;
// }

// /**
//  * @brief Van Cittert iterative deconvolution.
//  *
//  * Update rule: f_new = f + relaxation * (observed - conv(f, PSF))
//  */
// DeconvResult Deconvolution::applyVanCittert(cv::Mat& plane, const cv::Mat& psf,
//                                             const DeconvParams& p)
// {
//     DeconvResult res;
//     cv::Mat f;
//     plane.copyTo(f);

//     for (int it = 0; it < p.maxIter; ++it) {
//         cv::Mat conv = convolveFFT(f, psf);
//         cv::Mat fNew = f + (float)p.vcRelax * (plane - conv);
//         cv::max(fNew, 0.0f, fNew);
//         cv::min(fNew, 1.0f, fNew);

//         const double change = cv::norm(fNew - f, cv::NORM_L1)
//                             / (cv::norm(f, cv::NORM_L1) + 1e-8);

//         f = fNew;
//         res.finalChange = change;
//         res.iterations  = it + 1;

//         if (change < p.convergenceTol) break;
//     }

//     if (p.globalStrength < 1.0)
//         f = (float)p.globalStrength * f + (float)(1.0 - p.globalStrength) * plane;

//     f.copyTo(plane);
//     res.success = true;
//     return res;
// }

// /**
//  * @brief Larson-Sekanina rotational gradient filter.
//  *
//  * Computes a rotationally-shifted version of the image and combines it
//  * with the original via division or subtraction, then blends using
//  * Screen or SoftLight compositing.
//  */
// DeconvResult Deconvolution::applyLarsonSekanina(cv::Mat& plane, const DeconvParams& p)
// {
//     DeconvResult res;
//     const int H = plane.rows, W = plane.cols;

//     const float cx = (p.lsCenterX != 0.0 ? (float)p.lsCenterX : W / 2.0f);
//     const float cy = (p.lsCenterY != 0.0 ? (float)p.lsCenterY : H / 2.0f);
//     const double dtheta = (p.lsAngularStep / 180.0) * M_PI;

//     /* --- Compute polar coordinates --------------------------------------- */
//     cv::Mat xs(H, W, CV_32F), ys(H, W, CV_32F);
//     for (int y = 0; y < H; ++y)
//         for (int x = 0; x < W; ++x) {
//             xs.at<float>(y, x) = (float)x;
//             ys.at<float>(y, x) = (float)y;
//         }

//     cv::Mat dx = xs - cx, dy = ys - cy;
//     cv::Mat r(H, W, CV_32F), theta(H, W, CV_32F);
//     for (int y = 0; y < H; ++y)
//         for (int x = 0; x < W; ++x) {
//             const float ddx = dx.at<float>(y, x);
//             const float ddy = dy.at<float>(y, x);
//             r.at<float>(y, x) = std::sqrt(ddx * ddx + ddy * ddy);
//             float th = std::atan2(ddy, ddx);
//             if (th < 0) th += (float)(2 * M_PI);
//             theta.at<float>(y, x) = th;
//         }

//     /* --- Compute rotated / shifted coordinates --------------------------- */
//     cv::Mat r2 = (p.lsRadialStep > 0)
//                ? r + (float)p.lsRadialStep
//                : r.clone();

//     cv::Mat theta2(H, W, CV_32F);
//     for (int y = 0; y < H; ++y)
//         for (int x = 0; x < W; ++x)
//             theta2.at<float>(y, x) = std::fmod(
//                 theta.at<float>(y, x) + (float)dtheta, (float)(2 * M_PI));

//     cv::Mat x2(H, W, CV_32F), y2(H, W, CV_32F);
//     for (int y = 0; y < H; ++y)
//         for (int x = 0; x < W; ++x) {
//             x2.at<float>(y, x) = cx + r2.at<float>(y, x) * std::cos(theta2.at<float>(y, x));
//             y2.at<float>(y, x) = cy + r2.at<float>(y, x) * std::sin(theta2.at<float>(y, x));
//         }

//     /* --- Remap the image using bilinear interpolation --------------------- */
//     cv::Mat J(H, W, CV_32F, cv::Scalar(0));
//     cv::remap(plane, J, x2, y2, cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0));

//     /* --- Apply the selected operator (Divide or Subtract) ---------------- */
//     cv::Mat B;
//     if (p.lsOp == LSOperator::Divide) {
//         cv::Scalar medJ = cv::mean(J);
//         const float medV = (medJ[0] > 0) ? (float)medJ[0] : 1e-6f;
//         B = plane * (medV / (J + EPS));
//         cv::min(B, 1.0f, B);
//         cv::max(B, 0.0f, B);
//     } else {
//         B = plane - J;
//         cv::max(B, 0.0f, B);
//         double maxV;
//         cv::minMaxLoc(B, nullptr, &maxV);
//         if (maxV > 0) B /= (float)maxV;
//     }

//     /* --- Compositing blend ----------------------------------------------- */
//     cv::Mat blended;
//     if (p.lsBlend == LSBlendMode::Screen) {
//         blended = (plane + B) - plane.mul(B);
//     } else {
//         /* SoftLight blend. */
//         blended = (1.0f - 2.0f * B).mul(plane.mul(plane)) + 2.0f * B.mul(plane);
//     }
//     cv::max(blended, 0.0f, blended);
//     cv::min(blended, 1.0f, blended);

//     if (p.globalStrength < 1.0)
//         blended = (float)p.globalStrength * blended
//                 + (float)(1.0 - p.globalStrength) * plane;

//     blended.copyTo(plane);
//     res.success    = true;
//     res.iterations = 1;
//     return res;
// }

// /* =========================================================================
//  * PSF Construction (Public API)
//  * ========================================================================= */

// /**
//  * @brief Build or retrieve the PSF kernel for single-image deconvolution.
//  */
// cv::Mat Deconvolution::buildPSF(const DeconvParams& p, int kernelSize)
// {
//     if (!p.customKernel.empty()) return p.customKernel.clone();
//     if (kernelSize <= 0) kernelSize = autoKsizeFromFwhm(p.psfFWHM);
//     return gaussianPsf(p.psfFWHM, kernelSize);
// }

// /* =========================================================================
//  * ImageBuffer-Based FWHM and Empirical PSF Wrappers
//  * ========================================================================= */

// /**
//  * @brief Estimate the seeing FWHM from an ImageBuffer.
//  */
// double Deconvolution::estimateFWHM(const ImageBuffer& buf)
// {
//     const int w = buf.width();
//     const int h = buf.height();
//     if (w == 0 || h == 0) return 3.0;

//     const std::vector<float>& src = buf.data();
//     const int c = buf.channels();

//     cv::Mat plane;
//     if (c == 1) {
//         plane = cv::Mat(h, w, CV_32F, const_cast<float*>(src.data()));
//     } else {
//         cv::Mat interleaved(h, w, CV_32FC(c), const_cast<float*>(src.data()));
//         std::vector<cv::Mat> planes;
//         cv::split(interleaved, planes);
//         plane = planes[1 % c];
//     }

//     return estimateFwhmFromImage(plane);
// }

// /**
//  * @brief Estimate an empirical PSF kernel from an ImageBuffer.
//  */
// cv::Mat Deconvolution::estimateEmpiricalPSF(const ImageBuffer& buf, int kernelSize)
// {
//     const int w = buf.width();
//     const int h = buf.height();
//     if (w == 0 || h == 0) return cv::Mat();

//     const std::vector<float>& src = buf.data();
//     const int c = buf.channels();

//     cv::Mat plane;
//     if (c == 1) {
//         plane = cv::Mat(h, w, CV_32F, const_cast<float*>(src.data()));
//     } else {
//         cv::Mat interleaved(h, w, CV_32FC(c), const_cast<float*>(src.data()));
//         std::vector<cv::Mat> planes;
//         cv::split(interleaved, planes);
//         plane = planes[1 % c];
//     }

//     if (kernelSize <= 0) kernelSize = 31;

//     MFPsfInfo info = derivePsfForFrame(plane);
//     if (info.kernel.empty()) return gaussianPsf(3.0, kernelSize);

//     /* Resize to the requested kernel size if necessary. */
//     if (info.kernel.rows != kernelSize) {
//         cv::Mat padded = cv::Mat::zeros(kernelSize, kernelSize, CV_32F);
//         const int k   = info.kernel.rows;
//         const int off = (kernelSize - k) / 2;
//         if (off >= 0)
//             info.kernel.copyTo(padded(cv::Rect(off, off, k, k)));
//         else
//             info.kernel(cv::Rect(-off, -off, kernelSize, kernelSize)).copyTo(padded);
//         return normalizePsf(padded);
//     }

//     return info.kernel;
// }

// /* =========================================================================
//  * Single-Image Entry Point
//  * ========================================================================= */

// /**
//  * @brief Apply single-image deconvolution to an ImageBuffer.
//  *
//  * Dispatches to the appropriate algorithm based on DeconvParams::algo.
//  * Supports luminance-only processing for multi-channel images.
//  */
// DeconvResult Deconvolution::apply(ImageBuffer& buf, const DeconvParams& p)
// {
//     DeconvResult res;
//     const int W = buf.width();
//     const int H = buf.height();
//     const int C = buf.channels();

//     if (W == 0 || H == 0) {
//         res.errorMsg = "Empty ImageBuffer";
//         return res;
//     }

//     std::vector<float>& data = buf.data();
//     cv::Mat psf = buildPSF(p, p.kernelSize);

//     /* Lambda to process a single grayscale plane. */
//     auto processPlane = [&](cv::Mat& plane) -> DeconvResult {
//         cv::Mat mask = buildStarMask(plane, p.starMask);
//         switch (p.algo) {
//             case DeconvAlgorithm::RichardsonLucy:
//                 return (p.regType == 0) ? applyRL(plane, psf, mask, p)
//                                         : applyRLTV(plane, psf, mask, p);
//             case DeconvAlgorithm::RLTV:
//                 return applyRLTV(plane, psf, mask, p);
//             case DeconvAlgorithm::Wiener:
//                 return applyWiener(plane, psf, p);
//             case DeconvAlgorithm::VanCittert:
//                 return applyVanCittert(plane, psf, p);
//             case DeconvAlgorithm::LarsonSekanina:
//                 return applyLarsonSekanina(plane, p);
//             default: {
//                 DeconvResult r;
//                 r.errorMsg = "Unknown algorithm";
//                 return r;
//             }
//         }
//     };

//     /* --- Single-channel input -------------------------------------------- */
//     if (C == 1) {
//         cv::Mat plane(H, W, CV_32F, data.data());
//         return processPlane(plane);
//     }

//     /* --- Multi-channel: luminance-only mode ------------------------------- */
//     if (p.luminanceOnly && C >= 3) {
//         std::vector<float> ycbcr;
//         rgbToYcbcr(data, ycbcr, W, H, C);
//         cv::Mat Y(H, W, CV_32F, ycbcr.data());
//         DeconvResult r = processPlane(Y);
//         ycbcrToRgb(ycbcr, data, W, H, C);
//         return r;
//     }

//     /* --- Multi-channel: per-channel processing --------------------------- */
//     cv::Mat interleavedImg(H, W, CV_32FC(C), data.data());
//     std::vector<cv::Mat> planes;
//     cv::split(interleavedImg, planes);

//     DeconvResult last;
//     for (int c = 0; c < C; ++c) {
//         last = processPlane(planes[c]);
//     }

//     cv::merge(planes, interleavedImg);
//     return last;
// }

// /* =========================================================================
//  * Multi-Frame Deconvolution Pipeline
//  * ========================================================================= */

// /**
//  * @brief Execute the full multi-frame deconvolution pipeline.
//  *
//  * This is the main entry point for multi-frame processing.  It orchestrates
//  * frame loading, PSF estimation, star masking, variance mapping, seed
//  * construction, the iterative multiplicative update loop, early stopping,
//  * and output serialization.
//  */
// MFDeconvResult Deconvolution::applyMultiFrame(const MFDeconvParams& p)
// {
//     MFDeconvResult result;
//     auto& cb = p.statusCallback;

//     auto emitPct = [&](double frac, const QString& msg) {
//         emitProgress(cb, frac, msg);
//     };

//     /* --- Input validation ------------------------------------------------ */
//     if (p.framePaths.empty()) {
//         result.errorMsg = "No input frames specified.";
//         return result;
//     }
//     if (p.outputPath.isEmpty()) {
//         result.errorMsg = "Output path not specified.";
//         return result;
//     }

//     const int nFrames = (int)p.framePaths.size();
//     if (cb) cb(QString("MFDeconv: loading %1 aligned frames...").arg(nFrames));
//     emitPct(0.02, "scanning");

//     /* --- Determine common frame dimensions ------------------------------- */
//     int Ht = -1, Wt = -1;
    // [INTEGRATION]: Load each FITS frame to determine min(H), min(W).
    // Placeholder: assumes all frames share identical dimensions.
//     emitPct(0.05, "preparing");

//     /* --- Load frames ----------------------------------------------------- */
//     std::vector<cv::Mat> frames;
//     frames.reserve(nFrames);
    // [INTEGRATION]: Load each frame as CV_32F here.

//     if (frames.empty()) {
//         result.errorMsg = "Unable to load input frames.";
//         return result;
//     }

//     Ht = frames[0].rows;
//     Wt = frames[0].cols;

//     /* --- Per-frame PSF estimation ---------------------------------------- */
//     if (cb) cb("MFDeconv: measuring per-frame PSF...");
//     emitPct(0.08, "PSF computation");

//     std::vector<cv::Mat> psfs;
//     psfs.reserve(nFrames);
//     for (int i = 0; i < nFrames; ++i) {
//         MFPsfInfo info = derivePsfForFrame(frames[i]);
//         psfs.push_back(info.kernel);
//         if (cb) {
//             cb(QString("MFDeconv: PSF%1: ksize=%2 | FWHM~%3px")
//                 .arg(i + 1).arg(info.ksize).arg(info.fwhmPx, 0, 'f', 2));
//         }
//     }

//     /* --- Super-resolution PSF lifting ------------------------------------ */
//     const int r = std::max(1, p.superResFactor);
//     if (r > 1) {
//         if (cb) {
//             cb(QString("MFDeconv: super-resolution r=%1 with sigma=%2 - solving SR PSF...")
//                 .arg(r).arg(p.srSigma));
//         }
//         for (int i = 0; i < nFrames; ++i) {
//             cv::Mat srPsf = solveSuperPsf(psfs[i], r, p.srSigma,
//                                           p.srPsfOptIters, p.srPsfOptLr);
//             if (srPsf.rows % 2 == 0)
//                 srPsf = srPsf(cv::Rect(0, 0, srPsf.cols - 1, srPsf.rows - 1)).clone();
//             psfs[i] = srPsf;
//             if (cb) {
//                 cb(QString("  SR-PSF%1: -> %2x%2 (sum=%3)")
//                     .arg(i + 1).arg(srPsf.rows)
//                     .arg(cv::sum(srPsf)[0], 0, 'f', 6));
//             }
//         }
//     }

//     /* Pre-compute flipped PSFs for adjoint convolution. */
//     std::vector<cv::Mat> flipPsfs;
//     flipPsfs.reserve(nFrames);
//     for (const auto& psf : psfs)
//         flipPsfs.push_back(flipKernel(psf));

//     emitPct(0.20, "PSF Ready");

//     /* --- Star masks ------------------------------------------------------ */
//     std::vector<cv::Mat> starMasks;
//     if (p.useStarMasks) {
//         if (cb) cb("MFDeconv: computing star mask per frame...");
//         cv::Mat refMask;
        // [INTEGRATION]: Load reference mask frame if specified.

//         for (int i = 0; i < nFrames; ++i) {
//             if (!refMask.empty())
//                 starMasks.push_back(refMask.clone());
//             else
//                 starMasks.push_back(buildStarMaskSEP(frames[i], p.starMaskCfg));
//         }
//     }

//     /* --- Variance maps --------------------------------------------------- */
//     std::vector<cv::Mat> varMaps;
//     if (p.useVarianceMaps) {
//         if (cb) cb("MFDeconv: computing variance map per frame...");
//         for (int i = 0; i < nFrames; ++i)
//             varMaps.push_back(buildVarianceMap(frames[i], p.varmapCfg));
//     }

//     /* --- Seed image construction ----------------------------------------- */
//     emitPct(0.25, "Calculating Seed Image...");
//     if (cb) {
//         cb(p.seedMode == MFSeedMode::Median
//             ? "MFDeconv: Building median seed (tiled, streaming)..."
//             : "MFDeconv: Building robust seed (bootstrap + sigma-clip)...");
//     }

//     cv::Mat seedNative;
//     if (p.seedMode == MFSeedMode::Median) {
//         seedNative = buildMedianSeed(frames, cv::Size(256, 256));
//     } else {
//         seedNative = buildRobustSeed(frames, p.bootstrapFrames, p.clipSigma);
//     }

//     /* Lift seed to super-resolution grid if needed. */
//     cv::Mat x;
//     if (r > 1) {
//         x = upsampleSum(seedNative / (float)(r * r), r, cv::Size(Wt * r, Ht * r));
//     } else {
//         x = seedNative.clone();
//     }

//     if (x.empty()) {
//         result.errorMsg = "Seed computation failed.";
//         return result;
//     }
//     emitPct(0.40, "Seed ready");

//     const int Hs = x.rows, Ws = x.cols;

//     /* --- Background telemetry -------------------------------------------- */
//     if (cb) cb("MFDeconv: Calculating Backgrounds and MADs...");
//     {
//         cv::Mat& y0 = frames[0];
//         std::vector<float> v(y0.begin<float>(), y0.end<float>());
//         std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
//         const float medV = v[v.size() / 2];

//         std::vector<float> absD;
//         for (float a : v) absD.push_back(std::abs(a - medV));
//         std::nth_element(absD.begin(), absD.begin() + absD.size() / 2, absD.end());
//         const float bgEst = 1.4826f * absD[absD.size() / 2];

//         if (cb) {
//             cb(QString("MFDeconv: color_mode=%1, huber_delta=%2 (bg RMS~%3)")
//                 .arg(p.colorMode).arg(p.huberDelta).arg(bgEst, 0, 'f', 3));
//         }
//     }

//     /* --- Allocate accumulators -------------------------------------------- */
//     cv::Mat num = cv::Mat::zeros(Hs, Ws, CV_32F);
//     cv::Mat den = cv::Mat::zeros(Hs, Ws, CV_32F);

//     /* --- Early stopping monitor ------------------------------------------ */
//     MFEarlyStopper early(p.earlyStop, cb);
//     bool earlyStop  = false;
//     int  usedIters  = 0;
//     const int maxIters = std::max(1, p.maxIters);

//     const double relax = p.relax;
//     const double kappa = p.kappa;
//     const bool   rhoL2 = (p.rho == MFLossType::L2);
//     const double huberD = rhoL2 ? 0.0 : p.huberDelta;

//     if (cb) cb("Starting First Multiplicative Iteration...");

//     /* === Iterative multiplicative update loop ============================= */
//     for (int it = 1; it <= maxIters; ++it) {
//         num.setTo(0.0f);
//         den.setTo(0.0f);

//         /* --- Per-frame forward/adjoint pass ------------------------------ */
//         for (int fi = 0; fi < nFrames; ++fi) {
//             const cv::Mat& y_nat = frames[fi];
//             const cv::Mat& mask  = (!starMasks.empty()) ? starMasks[fi] : cv::Mat();
//             const cv::Mat& vmap  = (!varMaps.empty())   ? varMaps[fi]   : cv::Mat();
//             const cv::Mat& psf_  = psfs[fi];
//             const cv::Mat& psfT  = flipPsfs[fi];

//             /* Forward prediction: convolve on SR grid, downsample. */
//             cv::Mat predSuper = convolveFFT(x, psf_);
//             cv::Mat predLow   = (r > 1) ? downsampleAvg(predSuper, r) : predSuper.clone();

//             /* Compute robust weights. */
//             cv::Mat residual = y_nat - predLow;
//             cv::Mat wmap;
//             if (rhoL2) {
//                 wmap = cv::Mat::ones(y_nat.size(), CV_32F);
//                 if (!mask.empty()) cv::multiply(wmap, mask, wmap);
//                 if (!vmap.empty()) wmap = wmap / (vmap + EPS);
//             } else {
//                 wmap = computeHuberWeights(residual, huberD, vmap, mask);
//             }
//             wmap = cv::abs(wmap);

//             /* Adjoint back-projection. */
//             cv::Mat upY, upPred;
//             if (r > 1) {
//                 upY    = upsampleSum(wmap.mul(y_nat),    r, cv::Size(Ws, Hs));
//                 upPred = upsampleSum(wmap.mul(predLow),  r, cv::Size(Ws, Hs));
//             } else {
//                 upY    = wmap.mul(y_nat);
//                 upPred = wmap.mul(predLow);
//             }

//             num += convolveFFT(upY,    psfT);
//             den += convolveFFT(upPred, psfT);
//         }

//         /* --- Multiplicative update --------------------------------------- */
//         cv::Mat denSafe = den + EPS;
//         cv::Mat ratio   = num / denSafe;

//         /* Neutral pixel handling: where both num and den are negligible. */
//         cv::Mat neutralMask = (cv::abs(den) < 1e-12f) & (cv::abs(num) < 1e-12f);
//         ratio.setTo(1.0f, neutralMask);

//         /* Clamp the update ratio to [1/kappa, kappa]. */
//         cv::Mat upd;
//         cv::min(ratio, (float)kappa, upd);
//         cv::max(upd,   (float)(1.0 / kappa), upd);

//         cv::Mat xNext = x.mul(upd);
//         cv::max(xNext, 0.0f, xNext);

//         /* --- Convergence statistics for early stopping ------------------- */
//         cv::Mat updDiff = cv::abs(upd - 1.0f);
//         std::vector<float> udv(updDiff.begin<float>(), updDiff.end<float>());
//         std::nth_element(udv.begin(), udv.begin() + udv.size() / 2, udv.end());
//         const double um = udv[udv.size() / 2];

//         cv::Mat xDiff = cv::abs(xNext - x);
//         std::vector<float> xdv(xDiff.begin<float>(), xDiff.end<float>());
//         std::nth_element(xdv.begin(), xdv.begin() + xdv.size() / 2, xdv.end());

//         std::vector<float> xav(x.begin<float>(), x.end<float>());
//         std::nth_element(xav.begin(), xav.begin() + xav.size() / 2, xav.end());
//         const double xMed = xav[xav.size() / 2];
//         const double rc   = xdv[xdv.size() / 2] / (xMed + 1e-8);

//         /* Check for early stopping. */
//         if (early.step(it, maxIters, um, rc)) {
//             x         = xNext.clone();
//             usedIters = it;
//             earlyStop = true;
//             if (cb) cb(QString("MFDeconv: Iteration %1/%2 (early stop)").arg(it).arg(maxIters));
//             break;
//         }

//         /* Apply relaxation blend. */
//         x = (float)(1.0 - relax) * x + (float)relax * xNext;

//         /* Optionally save intermediate results. */
//         if (p.saveIntermediate && (it % std::max(1, p.saveEvery) == 0)) {
            // [INTEGRATION]: Save x as FITS in iteration directory.
//         }

//         const double frac = 0.25 + 0.70 * ((double)it / maxIters);
//         emitPct(frac, QString("Iteration %1/%2").arg(it).arg(maxIters));
//         if (cb) cb(QString("Iter %1/%2").arg(it).arg(maxIters));
//     }

//     if (!earlyStop) usedIters = maxIters;

//     /* --- Save final result ----------------------------------------------- */
//     emitPct(0.97, "saving");
    // [INTEGRATION]: Save x as FITS with appropriate header keywords.

//     QString outPath = buildOutputPath(p.outputPath, r);
//     outPath = nonclobberPath(outPath);

//     if (cb) {
//         cb(QString("MFDeconv saved: %1 (iters used: %2%3)")
//             .arg(outPath).arg(usedIters)
//             .arg(earlyStop ? ", early stop" : ""));
//     }

//     emitPct(1.0, "done");

//     result.success        = true;
//     result.itersUsed      = usedIters;
//     result.earlyStopped   = earlyStop;
//     result.superResFactor = r;
//     result.outputPath     = outPath;
//     return result;
// }
