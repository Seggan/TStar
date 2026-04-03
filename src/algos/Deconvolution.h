// #pragma once
// /**
//  * @file Deconvolution.h
//  * @brief Image deconvolution engine for astrophotography.
//  *
//  * Provides both single-image and multi-frame deconvolution algorithms:
//  *   - Richardson-Lucy (RL) with optional Tikhonov / Total Variation regularization
//  *   - Wiener / MMSE frequency-domain deconvolution
//  *   - Van Cittert iterative deconvolution
//  *   - Larson-Sekanina rotational gradient filtering
//  *
//  * Multi-frame mode adds per-frame PSF estimation, star masking, variance
//  * weighting, robust / median seed generation, optional super-resolution,
//  * EMA-based early stopping, and structured progress reporting.
//  */
// 
// #ifndef DECONVOLUTION_H
// #define DECONVOLUTION_H

// #include <vector>
// #include <functional>
// #include <string>
// 
// #include <QString>
// #include <opencv2/core.hpp>
// 
// #include "ImageBuffer.h"

// /* =========================================================================
//  * Enumerations
//  * ========================================================================= */
// 
// /**
//  * @brief Selects how the Point Spread Function (PSF) kernel is generated.
//  */
// enum class PSFSource {
//     Gaussian,           ///< Isotropic Gaussian kernel.
//     Moffat,             ///< Moffat profile kernel.
//     Disk,               ///< Uniform disk kernel.
//     Airy,               ///< Airy pattern kernel.
//     Custom              ///< User-supplied kernel image.
// };

// /**
//  * @brief Selects the single-image deconvolution algorithm.
//  */
// enum class DeconvAlgorithm {
//     RichardsonLucy,     ///< Standard Richardson-Lucy (no regularization).
//     RLTV,               ///< Richardson-Lucy with Total Variation regularizer.
//     Wiener,             ///< Single-step Wiener / MMSE filter.
//     VanCittert,         ///< Van Cittert iterative method.
//     LarsonSekanina      ///< Larson-Sekanina rotational gradient filter.
// };

// /**
//  * @brief Operator used in the Larson-Sekanina filter.
//  */
// enum class LSOperator {
//     Divide,
//     Subtract
// };
// 
// /**
//  * @brief Blending mode applied after the Larson-Sekanina filter.
//  */
// enum class LSBlendMode {
//     SoftLight,
//     Screen
// };

// /**
//  * @brief Loss function for multi-frame deconvolution weighting.
//  */
// enum class MFLossType {
//     Huber,              ///< Huber loss with adaptive or fixed delta.
//     L2                  ///< Flat L2 weights (psi/r = 1).
// };
// 
// /**
//  * @brief Seed initialization strategy for multi-frame deconvolution.
//  */
// enum class MFSeedMode {
//     Robust,             ///< Bootstrap mean + MAD clipping + sigma-clipped Welford.
//     Median              ///< Exact tiled median (RAM-bounded).
// };
// 
// /**
//  * @brief PSF FFT caching strategy on the CPU path.
//  */
// enum class MFPsfFftCache {
//     Ram,                ///< Precompute and retain in RAM.
//     Disk,               ///< Memory-map to disk to reduce RAM pressure.
//     None                ///< Recompute per frame and per iteration.
// };

// /* =========================================================================
//  * Configuration Structures
//  * ========================================================================= */
// 
// /**
//  * @brief Parameters for building a star-protection mask.
//  *
//  * The mask is a float32 KEEP map in [0, 1] that protects bright stars
//  * from deconvolution ringing.  Values near 1 retain the deconvolved
//  * result; values near 0 preserve the original data.
//  */
// struct DeconvStarMask {
//     bool   useMask      = true;
// 
//     /* -- Simple threshold mode (single-frame) ----------------------------- */
//     double threshold    = 0.85;     ///< Pixel value above which stars are detected.
//     double radius       = 2.0;      ///< Morphological dilation radius (pixels).
//     double blend        = 0.5;      ///< 0 = keep original, 1 = keep deconvolved.
// 
//     /* -- SEP-style detection parameters ----------------------------------- */
//     double threshSigma  = 2.0;      ///< Detection threshold in background sigma units.
//     int    maxObjs      = 2000;     ///< Maximum detected objects.
//     int    growPx       = 8;        ///< Ellipse dilation in pixels.
//     double ellipseScale = 1.2;      ///< Scale factor around each detected star.
//     double softSigma    = 2.0;      ///< Gaussian feathering sigma.
//     int    maxRadiusPx  = 16;       ///< Maximum per-star mask radius in pixels.
//     double keepFloor    = 0.20;     ///< Minimum KEEP map value.
//     int    maxSide      = 2048;     ///< Max image side for detection downscale.
// };

// /**
//  * @brief Configuration for per-frame variance map construction.
//  */
// struct MFVarianceMapCfg {
//     int    bw           = 64;       ///< Background estimation box width.
//     int    bh           = 64;       ///< Background estimation box height.
//     double smoothSigma  = 1.0;      ///< Gaussian sigma for map smoothing.
//     double floor        = 1e-8;     ///< Minimum floor to prevent division by zero.
// };
// 
// /**
//  * @brief Configuration for EMA-based early stopping in multi-frame deconvolution.
//  */
// struct MFEarlyStopCfg {
//     double tolUpdFloor  = 2e-4;     ///< Absolute floor on EMA update magnitude.
//     double tolRelFloor  = 5e-4;     ///< Absolute floor on EMA relative change.
//     double earlyFrac    = 0.40;     ///< Adaptive threshold as fraction of baseline.
//     double emaAlpha     = 0.5;      ///< EMA smoothing weight (1 = instant).
//     int    patience     = 2;        ///< Consecutive small iterations before stop.
//     int    minIters     = 3;        ///< Minimum iterations before early-stop checks.
// };

// /**
//  * @brief Full parameter set for single-image deconvolution.
//  */
// struct DeconvParams {
//     /* -- Global ----------------------------------------------------------- */
//     double          globalStrength  = 1.0;
// 
//     /* -- Algorithm selection ---------------------------------------------- */
//     DeconvAlgorithm algo            = DeconvAlgorithm::RichardsonLucy;
//     bool            luminanceOnly   = true;
// 
//     /* -- PSF definition --------------------------------------------------- */
//     PSFSource       psfSource       = PSFSource::Gaussian;
//     double          psfFWHM         = 3.0;
//     double          psfBeta         = 2.0;
//     double          psfAngle        = 0.0;
//     double          psfRoundness    = 1.0;
//     cv::Mat         customKernel;
// 
//     /* -- Iteration control (RL / Van Cittert) ------------------------------ */
//     int             maxIter         = 30;
//     double          convergenceTol  = 1e-4;
// 
//     /* -- Regularization (RLTV / Tikhonov / de-ringing) -------------------- */
//     int             regType         = 0;    ///< 0 = None, 1 = Tikhonov, 2 = TV.
//     bool            dering          = true;
//     double          tvRegWeight     = 0.01;
//     double          tvEps           = 1e-4;
// 
//     /* -- Wiener ----------------------------------------------------------- */
//     double          wienerK         = 0.01;
// 
//     /* -- Van Cittert ------------------------------------------------------- */
//     double          vcRelax         = 0.0;
// 
//     /* -- Airy disk PSF parameters ----------------------------------------- */
//     double          airyWavelength  = 700.0;
//     double          airyAperture    = 100.0;
//     double          airyFocalLen    = 1200.0;
//     double          airyPixelSize   = 4.63;
//     double          airyObstruction = 0.3;
// 
//     /* -- Larson-Sekanina -------------------------------------------------- */
//     double          lsRadialStep    = 0.0;
//     double          lsAngularStep   = 1.0;
//     double          lsCenterX       = 0.0;
//     double          lsCenterY       = 0.0;
//     LSOperator      lsOp            = LSOperator::Divide;
//     LSBlendMode     lsBlend         = LSBlendMode::SoftLight;
// 
//     /* -- Star mask (single-frame mode) ------------------------------------ */
//     DeconvStarMask  starMask;
// 
//     /* -- Border handling -------------------------------------------------- */
//     int             borderPad       = 32;
//     int             kernelSize      = 0;
// 
//     /* -- Progress callback ------------------------------------------------ */
//     std::function<void(int, const QString&)> progressCallback;
// };

// /**
//  * @brief Full parameter set for multi-frame deconvolution.
//  */
// struct MFDeconvParams {
//     /* -- Frame list -------------------------------------------------------- */
//     std::vector<QString> framePaths;        ///< Paths to aligned FITS frames.
//     QString              outputPath;        ///< Base output file path.
// 
//     /* -- Iteration control ------------------------------------------------ */
//     int             maxIters            = 20;
//     int             minIters            = 3;
//     double          kappa               = 2.0;  ///< Clamp ratio in [1/kappa, kappa].
//     double          relax               = 0.7;  ///< Relaxation blend factor.
// 
//     /* -- Loss / robust weights -------------------------------------------- */
//     MFLossType      rho                 = MFLossType::Huber;
//     double          huberDelta          = 0.0;  ///< >0 fixed, <0 auto, 0 flat.
// 
//     /* -- Color processing mode -------------------------------------------- */
//     QString         colorMode           = "luma"; ///< "luma" or "perchannel".
// 
//     /* -- Seed ------------------------------------------------------------- */
//     MFSeedMode      seedMode            = MFSeedMode::Robust;
//     int             bootstrapFrames     = 20;
//     double          clipSigma           = 5.0;
// 
//     /* -- Star masks ------------------------------------------------------- */
//     bool            useStarMasks        = false;
//     DeconvStarMask  starMaskCfg;
//     QString         starMaskRefPath;            ///< Optional reference frame path.
// 
//     /* -- Variance maps ---------------------------------------------------- */
//     bool            useVarianceMaps     = false;
//     MFVarianceMapCfg varmapCfg;
// 
//     /* -- Early stopping --------------------------------------------------- */
//     MFEarlyStopCfg  earlyStop;
// 
//     /* -- Super-resolution ------------------------------------------------- */
//     int             superResFactor      = 1;    ///< 1 = native, 2 = 2x resolution.
//     double          srSigma             = 1.1;
//     int             srPsfOptIters       = 250;
//     double          srPsfOptLr          = 0.1;
// 
//     /* -- Intermediate saves ----------------------------------------------- */
//     bool            saveIntermediate    = false;
//     int             saveEvery           = 1;
// 
//     /* -- Scratch / memory ------------------------------------------------- */
//     bool            scratchToDisk       = false;
//     QString         scratchDir;
//     int             memmapThresholdMb   = 512;
// 
//     /* -- PSF FFT cache ---------------------------------------------------- */
//     MFPsfFftCache   psfFftCache         = MFPsfFftCache::Ram;
//     bool            fftReuseAcrossIters = true;
// 
//     /* -- Low-memory mode -------------------------------------------------- */
//     bool            lowMem              = false;
// 
//     /* -- Batch frames ----------------------------------------------------- */
//     int             batchFrames         = -1;   ///< -1 = auto.
// 
//     /* -- Progress callbacks ----------------------------------------------- */
//     std::function<void(const QString&)>       statusCallback;
//     std::function<void(int, const QString&)>  progressCallback;
// };

// /* =========================================================================
//  * Result Structures
//  * ========================================================================= */
// 
// /**
//  * @brief Result of a single-image deconvolution operation.
//  */
// struct DeconvResult {
//     bool    success     = false;
//     int     iterations  = 0;
//     double  finalChange = 0.0;
//     QString errorMsg;
// };
// 
// /**
//  * @brief Result of a multi-frame deconvolution operation.
//  */
// struct MFDeconvResult {
//     bool    success         = false;
//     int     itersUsed       = 0;
//     bool    earlyStopped    = false;
//     int     superResFactor  = 1;
//     QString outputPath;
//     QString errorMsg;
// };

// /* =========================================================================
//  * MFEarlyStopper
//  * ========================================================================= */
// 
// /**
//  * @brief EMA-based early stopping monitor for multi-frame deconvolution.
//  *
//  * Tracks exponential moving averages of the update magnitude and relative
//  * change.  When both metrics remain below adaptive thresholds for
//  * @c patience consecutive iterations, the iteration loop is terminated.
//  */
// class MFEarlyStopper {
// public:
//     explicit MFEarlyStopper(const MFEarlyStopCfg& cfg,
//                             std::function<void(const QString&)> statusCb = nullptr);
// 
//     /**
//      * @brief Evaluate convergence after a single iteration.
//      * @param it       Current iteration number (1-based).
//      * @param maxIters Planned maximum number of iterations.
//      * @param um       Update magnitude: median(|update_ratio - 1|).
//      * @param rc       Relative change: median(|x_new - x_old|) / median(|x_old|).
//      * @return @c true if the early-stop criterion has been met.
//      */
//     bool step(int it, int maxIters, double um, double rc);
// 
//     /** @brief Reset all internal accumulators to their initial state. */
//     void reset();
// 
// private:
//     MFEarlyStopCfg                          m_cfg;
//     std::function<void(const QString&)>     m_statusCb;
// 
//     double  m_emaUm     = -1.0;
//     double  m_emaRc     = -1.0;
//     double  m_baseUm    = -1.0;
//     double  m_baseRc    = -1.0;
//     int     m_earlyCnt  = 0;
// };

// /* =========================================================================
//  * PSF Information
//  * ========================================================================= */
// 
// /**
//  * @brief Encapsulates a derived PSF kernel and its measured properties.
//  */
// struct MFPsfInfo {
//     cv::Mat kernel;     ///< Float32 kernel, normalized so sum = 1.
//     int     ksize;      ///< Odd kernel side length.
//     double  fwhmPx;     ///< Estimated FWHM in pixels.
// };

// /* =========================================================================
//  * Deconvolution Engine
//  * ========================================================================= */
// 
// /**
//  * @brief Static utility class implementing all deconvolution algorithms.
//  */
// class Deconvolution {
// public:
//     /* -- Single-image public API ------------------------------------------ */
// 
//     /** @brief Apply single-image deconvolution in-place. */
//     static DeconvResult apply(ImageBuffer& buf, const DeconvParams& p);
// 
//     /** @brief Construct or retrieve the PSF kernel for the given parameters. */
//     static cv::Mat buildPSF(const DeconvParams& p, int kernelSize);
// 
//     /** @brief Estimate the seeing FWHM from an ImageBuffer. */
//     static double estimateFWHM(const ImageBuffer& buf);
// 
//     /** @brief Estimate an empirical PSF from stellar profiles in an ImageBuffer. */
//     static cv::Mat estimateEmpiricalPSF(const ImageBuffer& buf, int kernelSize = 31);
// 
//     /** @brief Build a simple threshold-based star protection mask. */
//     static cv::Mat buildStarMask(const cv::Mat& plane, const DeconvStarMask& m);
// 
//     /* -- Multi-frame public API ------------------------------------------- */
// 
//     /**
//      * @brief Execute multi-frame deconvolution on a set of aligned frames.
//      *
//      * Performs per-frame PSF estimation, optional star masking and variance
//      * weighting, multiplicative RL iteration with robust weights, optional
//      * super-resolution, EMA early stopping, and structured progress output.
//      */
//     static MFDeconvResult applyMultiFrame(const MFDeconvParams& p);
// 
//     /**
//      * @brief Derive a PSF kernel from a single luminance frame.
//      *
//      * Attempts empirical extraction; falls back to a Gaussian PSF based
//      * on the estimated FWHM.  Applies light softening and normalization.
//      */
//     static MFPsfInfo derivePsfForFrame(const cv::Mat& lumaFrame,
//                                        double fwhmHint = 0.0);
// 
//     /**
//      * @brief Build a star-protection KEEP map using SEP-style detection.
//      *
//      * Returns a float32 map in [0, 1] with Gaussian feathering.
//      * Falls back to an all-ones map if no stars are detected.
//      */
//     static cv::Mat buildStarMaskSEP(const cv::Mat& luma2d,
//                                     const DeconvStarMask& cfg);
// 
//     /**
//      * @brief Build a per-pixel variance map for weighted deconvolution.
//      *
//      * Combines background RMS and Poisson shot noise.
//      */
//     static cv::Mat buildVarianceMap(const cv::Mat& luma2d,
//                                    const MFVarianceMapCfg& cfg,
//                                    double gainHint = 0.0);
// 
//     /**
//      * @brief Build a robust seed image from multiple frames.
//      *
//      * Four-pass strategy: Welford bootstrap mean, global MAD estimation,
//      * masked mean refinement, sigma-clipped Welford streaming.
//      */
//     static cv::Mat buildRobustSeed(const std::vector<cv::Mat>& frames,
//                                    int bootstrapFrames = 20,
//                                    double clipSigma = 5.0);
// 
//     /**
//      * @brief Build a median seed image via tiled pixel-wise median.
//      */
//     static cv::Mat buildMedianSeed(const std::vector<cv::Mat>& frames,
//                                    cv::Size tileHW = cv::Size(256, 256));
// 
// private:
//     /* -- Single-image algorithm implementations --------------------------- */
// 
//     static DeconvResult applyRL(cv::Mat& plane, const cv::Mat& psf,
//                                 const cv::Mat& starMask, const DeconvParams& p);
// 
//     static DeconvResult applyRLTV(cv::Mat& plane, const cv::Mat& psf,
//                                   const cv::Mat& starMask, const DeconvParams& p);
// 
//     static DeconvResult applyWiener(cv::Mat& plane, const cv::Mat& psf,
//                                     const DeconvParams& p);
// 
//     static DeconvResult applyVanCittert(cv::Mat& plane, const cv::Mat& psf,
//                                         const DeconvParams& p);
// 
//     static DeconvResult applyLarsonSekanina(cv::Mat& plane, const DeconvParams& p);
// 
//     /* -- Color space conversion ------------------------------------------- */
// 
//     static void rgbToYcbcr(const std::vector<float>& rgb,
//                            std::vector<float>& ycbcr,
//                            int w, int h, int c);
// 
//     static void ycbcrToRgb(const std::vector<float>& ycbcr,
//                            std::vector<float>& rgb,
//                            int w, int h, int c);
// 
//     /* -- Frequency-domain helpers ----------------------------------------- */
// 
//     /** @brief SAME-size FFT convolution with ifftshift on the kernel. */
//     static cv::Mat convolveFFT(const cv::Mat& src, const cv::Mat& psf);
// 
//     /** @brief Frequency-domain Wiener filter. */
//     static cv::Mat wienerFilter(const cv::Mat& src, const cv::Mat& psf, double K);
// 
//     /** @brief Compute the Total Variation sub-gradient of an image. */
//     static cv::Mat tvGradient(const cv::Mat& u, double eps);
// 
//     /* -- PSF utilities ---------------------------------------------------- */
// 
//     /** @brief Estimate FWHM from a PSF kernel via second-moment analysis. */
//     static double psfFwhmPx(const cv::Mat& psf);
// 
//     /** @brief Estimate the seeing FWHM from a luminance image. */
//     static double estimateFwhmFromImage(const cv::Mat& luma);
// 
//     /** @brief Compute an appropriate odd kernel size from a FWHM value. */
//     static int autoKsizeFromFwhm(double fwhmPx, int kmin = 11, int kmax = 51);
// 
//     /** @brief Generate a normalized isotropic Gaussian PSF. */
//     static cv::Mat gaussianPsf(double fwhmPx, int ksize);
// 
//     /** @brief Soften a PSF by convolving with a small Gaussian. */
//     static cv::Mat softenPsf(const cv::Mat& psf, double sigmaPx = 0.25);
// 
//     /** @brief Normalize a PSF: clip negatives, divide by sum. */
//     static cv::Mat normalizePsf(const cv::Mat& psf);
// 
//     /** @brief Flip a kernel (180-degree rotation) for adjoint convolution. */
//     static cv::Mat flipKernel(const cv::Mat& psf);
// 
//     /* -- Robust statistics ------------------------------------------------ */
// 
//     /**
//      * @brief Compute per-pixel Huber weights for robust deconvolution.
//      *
//      * When huberDelta < 0, delta is set to |huberDelta| * RMS(residual).
//      * When huberDelta == 0, all weights are unity (L2 loss).
//      */
//     static cv::Mat computeHuberWeights(const cv::Mat& residual,
//                                        double huberDelta,
//                                        const cv::Mat& varMap,
//                                        const cv::Mat& mask);
// 
//     /** @brief Robust scalar variance estimate: var = (1.4826 * MAD)^2. */
//     static double estimateScalarVariance(const cv::Mat& a);
// 
//     /* -- Super-resolution helpers ----------------------------------------- */
// 
//     /** @brief Downsample by average pooling with integer factor @p r. */
//     static cv::Mat downsampleAvg(const cv::Mat& img, int r);
// 
//     /** @brief Upsample by nearest-neighbor replication (adjoint of downsampleAvg). */
//     static cv::Mat upsampleSum(const cv::Mat& img, int r,
//                                cv::Size targetHW = cv::Size());
// 
//     /**
//      * @brief Solve for the super-resolution PSF via gradient descent.
//      *
//      * Minimizes ||f_native - D(h) * g_sigma||^2 where D is the
//      * downsampling operator and g_sigma is a native-scale Gaussian.
//      */
//     static cv::Mat solveSuperPsf(const cv::Mat& nativePsf, int r,
//                                  double sigma = 1.1,
//                                  int iters = 250,
//                                  double lr = 0.1);
// 
//     /* -- General utilities ------------------------------------------------ */
// 
//     /** @brief Center-crop or center-pad a matrix to the target dimensions. */
//     static cv::Mat centerCrop(const cv::Mat& arr, int Ht, int Wt,
//                               float fillVal = 0.0f);
// 
//     /** @brief Replace NaN/Inf with zero and clamp negatives. */
//     static cv::Mat sanitizeNumeric(const cv::Mat& a);
// 
//     /** @brief Build the canonical output file path for MFDeconv results. */
//     static QString buildOutputPath(const QString& basePath, int superResFactor);
// 
//     /** @brief Append a version suffix to avoid overwriting existing files. */
//     static QString nonclobberPath(const QString& path);
// 
//     /** @brief Emit a structured progress string via a callback. */
//     static void emitProgress(const std::function<void(const QString&)>& cb,
//                              double fraction,
//                              const QString& msg = QString());
// };
// 
// #endif // DECONVOLUTION_H