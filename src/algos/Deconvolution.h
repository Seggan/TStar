#pragma once
/*
 * Deconvolution.h  —  Image deconvolution for astrophotography
 *
 * Implemented algorithms:
 * 1. Richardson-Lucy (RL), with optional L2/TV regularization
 * 2. Wiener / MMSE deconvolution
 * 3. Van Cittert deconvolution
 * 4. Larson-Sekanina rotational gradient filtering
 *
 * Multi-frame deconvolution includes per-frame PSF estimation, star masks,
 * variance maps, robust/median seed generation, robust weighting,
 * optional super-resolution, and progress reporting.
 */

#ifndef DECONVOLUTION_H
#define DECONVOLUTION_H

#include <vector>
#include <functional>
#include <string>
#include <QString>
#include "ImageBuffer.h"
#include <opencv2/core.hpp>

// ─── PSF source ──────────────────────────────────────────────────────────────
enum class PSFSource {
    Gaussian,    ///< synthetic isotropic Gaussian
    Moffat,      ///< synthetic Moffat profile
    Disk,        ///< synthetic Disk profile
    Airy,        ///< synthetic Airy profile
    Custom       ///< user-supplied kernel image
};

// ─── Algorithm selection ──────────────────────────────────────────────────────
enum class DeconvAlgorithm {
    RichardsonLucy,   ///< standard RL (no regularisation)
    RLTV,             ///< RL + Total Variation regulariser
    Wiener,           ///< single-step Wiener / MMSE
    VanCittert,       ///< Van Cittert
    LarsonSekanina    ///< Larson-Sekanina
};

enum class LSOperator {
    Divide,
    Subtract
};

enum class LSBlendMode {
    SoftLight,
    Screen
};

// ─── Loss function (MFDeconv rho) ───────────────────────────────────────────
enum class MFLossType {
    Huber,   ///< rho="huber" — Huber loss with adaptive or fixed delta
    L2       ///< rho="l2"   — psi/r=1 (flat weights)
};

// ─── Seed mode ────────────────────────────────────────────────────────────────
enum class MFSeedMode {
    Robust,  ///< Bootstrap mean + +/-4*MAD clip + sigma-clipped Welford streaming (default)
    Median   ///< Exact tiled median (RAM-bounded)
};

// ─── PSF FFT cache mode (CPU path) ───────────────────────────────────────────
enum class MFPsfFftCache {
    Ram,   ///< Precompute and keep in RAM
    Disk,  ///< Use disk memmap to reduce RAM
    None   ///< Recompute per frame and per iteration
};

// ─── Mask to protect bright stars from ringing ───────────────────────────────
struct DeconvStarMask {
    bool   useMask       = true;
    double threshold     = 0.85;    ///< pixel value threshold (single-frame mode)
    double radius        = 2.0;     ///< dilation radius (pixels) (single-frame mode)
    double blend         = 0.5;     ///< blend factor 0=original, 1=deconvolved

    // ── SEP parameters ──────────────────────────────────────────────────────
    double threshSigma   = 2.0;     ///< star detection threshold in sigma (THRESHOLD_SIGMA)
    int    maxObjs       = 2000;    ///< max detected objects (STAR_MASK_MAXOBJS)
    int    growPx        = 8;       ///< ellipse dilation in pixels (GROW_PX)
    double ellipseScale  = 1.2;     ///< scale factor around each detected star (ELLIPSE_SCALE)
    double softSigma     = 2.0;     ///< Gaussian feathering sigma (SOFT_SIGMA)
    int    maxRadiusPx   = 16;      ///< max per-star radius in pixels (MAX_STAR_RADIUS)
    double keepFloor     = 0.20;    ///< minimum KEEP map value (KEEP_FLOOR)
    int    maxSide       = 2048;    ///< max image side for detection downscale (STAR_MASK_MAXSIDE)
};

// ─── Variance map config ──────────────────────────────────────────────────────
struct MFVarianceMapCfg {
    int    bw            = 64;      ///< SEP background box width
    int    bh            = 64;      ///< SEP background box height
    double smoothSigma   = 1.0;     ///< Gaussian sigma for map smoothing
    double floor         = 1e-8;    ///< minimum floor to avoid divide-by-zero
};

// ─── EarlyStopper config ─────────────────────────────────────────────────────
struct MFEarlyStopCfg {
    double tolUpdFloor   = 2e-4;    ///< absolute floor on EMA update_magnitude
    double tolRelFloor   = 5e-4;    ///< absolute floor on EMA rel_change
    double earlyFrac     = 0.40;    ///< adaptive threshold fraction of baseline
    double emaAlpha      = 0.5;     ///< EMA weight (1=instant, 0=no update)
    int    patience      = 2;       ///< consecutive under-threshold iterations before stop
    int    minIters      = 3;       ///< minimum iterations before early-stop checks
};

// ─── Deconvolution parameters (single frame) ────────────────────────────────
struct DeconvParams {
    // ── Global ──────────────────────────────────────────────────────────
    double      globalStrength    = 1.0;

    // ── Algorithm ───────────────────────────────────────────────────────
    DeconvAlgorithm algo          = DeconvAlgorithm::RichardsonLucy;
    bool        luminanceOnly     = true;

    // ── PSF definition ──────────────────────────────────────────────────
    PSFSource   psfSource         = PSFSource::Gaussian;
    double      psfFWHM           = 3.0;
    double      psfBeta           = 2.0;
    double      psfAngle          = 0.0;
    double      psfRoundness      = 1.0;
    cv::Mat     customKernel;

    // ── Iterations (RL / VC) ────────────────────────────────────────────
    int         maxIter           = 30;
    double      convergenceTol    = 1e-4;

    // ── RLTV / Tikhonov / Dering ────────────────────────────────────────
    int         regType           = 0;          ///< 0=None, 1=Tikhonov, 2=TV
    bool        dering            = true;
    double      tvRegWeight       = 0.01;
    double      tvEps             = 1e-4;

    // ── Wiener ──────────────────────────────────────────────────────────
    double      wienerK           = 0.01;

    // ── Van Cittert ─────────────────────────────────────────────────────
    double      vcRelax           = 0.0;

    // ── Airy Disk ───────────────────────────────────────────────────────
    double      airyWavelength    = 700.0;
    double      airyAperture      = 100.0;
    double      airyFocalLen      = 1200.0;
    double      airyPixelSize     = 4.63;
    double      airyObstruction   = 0.3;

    // ── Larson-Sekanina ─────────────────────────────────────────────────
    double      lsRadialStep      = 0.0;
    double      lsAngularStep     = 1.0;
    double      lsCenterX         = 0.0;
    double      lsCenterY         = 0.0;
    LSOperator  lsOp              = LSOperator::Divide;
    LSBlendMode lsBlend           = LSBlendMode::SoftLight;

    // ── Star protection (single-frame mode) ─────────────────────────────
    DeconvStarMask starMask;

    // ── Border handling ─────────────────────────────────────────────────
    int         borderPad         = 32;
    int         kernelSize        = 0;

    std::function<void(int, const QString&)> progressCallback;
};

// ─── Multi-Frame Deconvolution parameters ────────────────────────────────────
struct MFDeconvParams {
    // ── Frame list ──────────────────────────────────────────────────────
    std::vector<QString> framePaths;  ///< paths of aligned FITS frames
    QString              outputPath;  ///< base output path

    // ── Iteration ──────────────────────────────────────────────────────
    int         maxIters          = 20;     ///< max iterations (iters=)
    int         minIters          = 3;      ///< min iterations before early stop
    double      kappa             = 2.0;    ///< clamp ratio num/den in [1/kappa, kappa]
    double      relax             = 0.7;    ///< blend: x = (1-relax)*x_old + relax*x_new

    // ── Loss / robust weights ───────────────────────────────────────────
    MFLossType  rho               = MFLossType::Huber;
    double      huberDelta        = 0.0;    ///< >0 absolute, <0 auto (|delta|*RMS), 0=flat

    // ── Color ───────────────────────────────────────────────────────────
    //   "luma"       -> process ITU-R BT.709 luminance
    //   "perchannel" -> process R, G, B independently
    QString     colorMode         = "luma";

    // ── Seed ────────────────────────────────────────────────────────────
    MFSeedMode  seedMode          = MFSeedMode::Robust;
    int         bootstrapFrames   = 20;     ///< frames per bootstrap (robust seed)
    double      clipSigma         = 5.0;    ///< sigma-clip Welford streaming

    // ── Star masks ──────────────────────────────────────────────────────
    bool        useStarMasks      = false;
    DeconvStarMask starMaskCfg;
    QString     starMaskRefPath;            ///< if not empty, calculate mask from this frame

    // ── Variance maps ───────────────────────────────────────────────────
    bool        useVarianceMaps   = false;
    MFVarianceMapCfg varmapCfg;

    // ── Early stopping ──────────────────────────────────────────────────
    MFEarlyStopCfg earlyStop;

    // ── PSF ─────────────────────────────────────────────────────────────
    //   Per-frame auto-derived PSF (ksize ladder + sigma ladder)
    //   Core constants:
    //     sigma_ladder = [50.0, 25.0, 12.0, 6.0]
    //     ksize min=11, max=51 (±4σ rule, come _auto_ksize_from_fwhm)
    //     softening sigma_px=0.25

    // ── Super-Resolution ────────────────────────────────────────────────
    int         superResFactor    = 1;      ///< 1 = native; 2 = double resolution
    double      srSigma           = 1.1;    ///< Gaussian sigma for SR-PSF solver
    int         srPsfOptIters     = 250;    ///< SR-PSF solver iterations
    double      srPsfOptLr        = 0.1;    ///< learning rate solver SR-PSF

    // ── Intermediates ───────────────────────────────────────────────────
    bool        saveIntermediate  = false;
    int         saveEvery         = 1;      ///< save every N iterations

    // ── Scratch / memory ────────────────────────────────────────────────
    bool        scratchToDisk     = false;  ///< use memmap for large scratch buffers
    QString     scratchDir;                 ///< memmap directory
    int         memmapThresholdMb = 512;    ///< size threshold for memmap

    // ── PSF FFT cache (CPU path) ─────────────────────────────────────────
    MFPsfFftCache psfFftCache     = MFPsfFftCache::Ram;
    bool        fftReuseAcrossIters = true;

    // ── Low-memory mode ──────────────────────────────────────────────────
    bool        lowMem            = false;  ///< reduce LRU cache, disable prefetch

    // ── Batch frames ─────────────────────────────────────────────────────
    int         batchFrames       = -1;     ///< -1 = auto (based on resolution)

    // ── Progress callback ────────────────────────────────────────────────
    //   Emits "__PROGRESS__ 0.xxxx msg"
    std::function<void(const QString&)> statusCallback;
    std::function<void(int, const QString&)> progressCallback; ///< (percent 0-100, msg)
};

// ─── Result (single frame) ─────────────────────────────────────────────────
struct DeconvResult {
    bool    success     = false;
    int     iterations  = 0;
    double  finalChange = 0.0;
    QString errorMsg;
};

// ─── Multi-Frame Deconvolution Result ────────────────────────────────────────
struct MFDeconvResult {
    bool    success         = false;
    int     itersUsed       = 0;       ///< MF_ITERS
    bool    earlyStopped    = false;   ///< MF_ESTOP
    int     superResFactor  = 1;       ///< MF_SR
    QString outputPath;                ///< saved file path
    QString errorMsg;
};

// ─── EarlyStopper ───────────────────────────────────────────────────────────
/**
 * Early stopping logic:
 *   - EMA of update_magnitude (um) and rel_change (rc)
 *   - Adaptive thresholds: tol = max(tol_floor, early_frac * base_value)
 *   - Counts consecutive "small" iterations (patience) before stopping
 */
class MFEarlyStopper {
public:
    explicit MFEarlyStopper(const MFEarlyStopCfg& cfg,
                            std::function<void(const QString&)> statusCb = nullptr);

    /// Call once per iteration. Returns true when processing should stop.
    /// @param it      current iteration (1-based)
    /// @param maxIters  planned maximum iterations
    /// @param um      update_magnitude = median(|upd - 1|)
    /// @param rc      rel_change = median(|x_new - x_old|) / median(|x_old|)
    bool step(int it, int maxIters, double um, double rc);

    void reset();

private:
    MFEarlyStopCfg m_cfg;
    std::function<void(const QString&)> m_statusCb;

    double m_emaUm      = -1.0;
    double m_emaRc      = -1.0;
    double m_baseUm     = -1.0;
    double m_baseRc     = -1.0;
    int    m_earlyCnt   = 0;
};

// ─── PSF utilities ──────────────────────────────────────────────────────────
struct MFPsfInfo {
    cv::Mat  kernel;        ///< float32, normalized, sum=1
    int      ksize;         ///< odd kernel side length
    double   fwhmPx;        ///< estimated FWHM in pixels
};

// ─── Main class ──────────────────────────────────────────────────────────────
class Deconvolution {
public:
    // ──── Single image ─────────────────────────────────────────────────
    static DeconvResult apply(ImageBuffer& buf, const DeconvParams& p);

    static cv::Mat buildPSF(const DeconvParams& p, int kernelSize);
    static double  estimateFWHM(const ImageBuffer& buf);
    static cv::Mat estimateEmpiricalPSF(const ImageBuffer& buf, int kernelSize = 31);
    static cv::Mat buildStarMask(const cv::Mat& plane, const DeconvStarMask& m);

    // ──── Multi-Frame Deconvolution ────────────────────────────────────────
    /**
     * Run MFDeconv on a sequence of aligned FITS frames.
     * Includes:
     *   - per-frame PSF estimation (ksize ladder, sigma ladder, Gaussian fallback)
     *   - PSF softening (Gaussian, sigma_px=0.25)
     *   - robust or median seed
     *   - star mask (float KEEP map with Gaussian feathering)
     *   - variance map (var_bg + shot noise, optional Gaussian smoothing)
     *   - MM/RL iteration with Huber or L2, relax=0.7, kappa clamping
     *   - EMA-based early stopping with patience
     *   - super-resolution for r>1 (SR-PSF solver, adjoint up/downsampling)
     *   - optional intermediate saves
     *   - progress callback via "__PROGRESS__ 0.xxxx msg"
     */
    static MFDeconvResult applyMultiFrame(const MFDeconvParams& p);

    // ──── PSF auto-detection for MFDeconv (ksize + sigma ladder) ───────────
    /**
     * Derive a PSF from a single mono float32 image.
     * Tries det_sigma in [50,25,12,6] and descending ksize values
     * (max_stars=80). If detection fails, returns a Gaussian from FWHM.
     * Applies sigma_px=0.25 softening and normalization.
     */
    static MFPsfInfo derivePsfForFrame(const cv::Mat& lumaFrame,
                                       double fwhmHint = 0.0);

    // ──── SEP star mask for MFDeconv ───────────────────────────────────────
    /**
     * Builds a float32 KEEP map in [0,1] via SEP detection.
     * Uses detection downscale (max_side), threshold ladder,
     * circle/ellipse drawing, Gaussian feathering, and keep_floor.
     * If SEP is unavailable, returns an all-ones map.
     */
    static cv::Mat buildStarMaskSEP(const cv::Mat& luma2d,
                                    const DeconvStarMask& cfg);

    // ──── Variance map for MFDeconv ────────────────────────────────────────
    /**
     * Builds a float32 variance map: var = var_bg^2 + a_shot * obj_dn.
     * Uses SEP for background map; falls back to scalar MAD.
     * Uses header gain when available, otherwise an estimate.
     * Supports floor and optional Gaussian smoothing.
     */
    static cv::Mat buildVarianceMap(const cv::Mat& luma2d,
                                    const MFVarianceMapCfg& cfg,
                                    double gainHint = 0.0);

    // ──── Seed image ────────────────────────────────────────────────────────
    /**
     * Robust seed strategy:
     *   1) Welford mean over bootstrapFrames
     *   2) Global MAD estimate for +/-4*MAD clipping
     *   3) Masked mean over bootstrap frames
     *   4) Sigma-clipped Welford streaming over remaining frames
     * Accepts preloaded frames (cv::Mat CHW float32).
     */
    static cv::Mat buildRobustSeed(const std::vector<cv::Mat>& frames,
                                   int bootstrapFrames = 20,
                                   double clipSigma    = 5.0);

    /**
     * Median seed strategy:
     * exact tiled median, RAM-bounded. Accepts preloaded frame list.
     */
    static cv::Mat buildMedianSeed(const std::vector<cv::Mat>& frames,
                                   cv::Size tileHW = cv::Size(256, 256));

private:
    // ──── Single image algorithms ───────────────────────────────────────
    static DeconvResult applyWiener      (cv::Mat& plane, const cv::Mat& psf, const DeconvParams& p);
    static DeconvResult applyRL          (cv::Mat& plane, const cv::Mat& psf, const cv::Mat& starMask, const DeconvParams& p);
    static DeconvResult applyRLTV        (cv::Mat& plane, const cv::Mat& psf, const cv::Mat& starMask, const DeconvParams& p);
    static DeconvResult applyVanCittert  (cv::Mat& plane, const cv::Mat& psf, const DeconvParams& p);
    static DeconvResult applyLarsonSekanina(cv::Mat& plane, const DeconvParams& p);

    static void rgbToYcbcr(const std::vector<float>& rgb, std::vector<float>& ycbcr, int w, int h, int c);
    static void ycbcrToRgb(const std::vector<float>& ycbcr, std::vector<float>& rgb, int w, int h, int c);

    // ──── Frequency / convolution helpers ────────────────────────────────
    /**
     * SAME FFT convolution.
     * Applies ifftshift on the kernel before FFT.
     * Returns output with the same size as src.
     */
    static cv::Mat convolveFFT(const cv::Mat& src, const cv::Mat& psf);

    /**
     * Frequency-domain Wiener filter.
     */
    static cv::Mat wienerFilter(const cv::Mat& src, const cv::Mat& psf, double K);

    /**
     * TV sub-gradient.
     */
    static cv::Mat tvGradient(const cv::Mat& u, double eps);

    // ──── MFDeconv helpers ───────────────────────────────────────────────
    /**
     * Estimate FWHM from PSF second moment.
     * FWHM ~= 2.3548 * sigma.
     */
    static double psfFwhmPx(const cv::Mat& psf);

    /**
     * Estimate image FWHM via second moment.
     * Uses SEP detection when available; falls back to NaN.
     */
    static double estimateFwhmFromImage(const cv::Mat& luma);

    /**
     * Auto-ksize from FWHM: covers +/-4 sigma, odd size, clamped to [kmin,kmax].
     */
    static int autoKsizeFromFwhm(double fwhmPx, int kmin = 11, int kmax = 51);

    /**
     * Normalized synthetic Gaussian PSF.
     * sigma = max(fwhm, 1.0) / 2.3548
     */
    static cv::Mat gaussianPsf(double fwhmPx, int ksize);

    /**
     * PSF softening: convolution with Gaussian sigma=sigma_px.
     */
    static cv::Mat softenPsf(const cv::Mat& psf, double sigmaPx = 0.25);

    /**
     * PSF normalization: clip negatives and divide by sum.
     */
    static cv::Mat normalizePsf(const cv::Mat& psf);

    /**
     * Flip kernel for convolution.
     */
    static cv::Mat flipKernel(const cv::Mat& psf);

    /**
     * Per-pixel robust Huber weights.
     *   psi_over_r = 1 se |r| <= delta, else delta / (|r| + eps)
     *   w = psi_over_r / (var + eps) * mask
     * If huberDelta < 0: delta = |huberDelta| * RMS(r) via MAD.
     * If huberDelta == 0 (L2): psi_over_r = 1.
     */
    static cv::Mat computeHuberWeights(const cv::Mat& residual,
                                        double huberDelta,
                                        const cv::Mat& varMap,
                                        const cv::Mat& mask);

    /**
     * Robust scalar variance estimate via MAD.
     * var = (1.4826 * MAD)^2
     */
    static double estimateScalarVariance(const cv::Mat& a);

    /**
     * Downscale by average pooling.
     * Works on 2D (H,W) or per-channel data.
     */
    static cv::Mat downsampleAvg(const cv::Mat& img, int r);

    /**
     * Upsample by sum-replication (adjoint of downsampleAvg).
     */
    static cv::Mat upsampleSum(const cv::Mat& img, int r, cv::Size targetHW = cv::Size());

    /**
     * SR-PSF solver: solves h* = argmin ||f_native - D(h)*g_sigma||^2
     * via gradient descent.
     */
    static cv::Mat solveSuperPsf(const cv::Mat& nativePsf,
                                   int r,
                                   double sigma      = 1.1,
                                   int    iters      = 250,
                                   double lr         = 0.1);

    /**
     * Center-crop or center-pad a frame to (Ht, Wt).
     */
    static cv::Mat centerCrop(const cv::Mat& arr, int Ht, int Wt, float fillVal = 0.0f);

    /**
     * Sanitize NaN/Inf, clip to [0, inf), and ensure contiguous float32 data.
     */
    static cv::Mat sanitizeNumeric(const cv::Mat& a);

    /**
     * Build canonical output path.
     * Format: MFDeconv_<base>[_<HxW>][_<secs>s][_2x].<ext>
     */
    static QString buildOutputPath(const QString& basePath, int superResFactor);

    /**
     * Non-clobber versioning.
     * If file exists, appends _v2, _v3, ...
     */
    static QString nonclobberPath(const QString& path);

    /**
     * Emit a progress string in the format:
     * "__PROGRESS__ 0.xxxx msg"
     */
    static void emitProgress(const std::function<void(const QString&)>& cb,
                              double fraction,
                              const QString& msg = QString());
};

#endif // DECONVOLUTION_H