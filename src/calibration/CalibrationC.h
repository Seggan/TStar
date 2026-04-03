/**
 * @file CalibrationC.h
 * @brief C interface for high-performance image calibration primitives
 *
 * This header provides C-linkage functions for astronomical image calibration,
 * including bias/dark/flat correction, bad pixel detection, banding removal,
 * CFA handling, and debayering. All functions are designed for multi-threaded
 * execution using OpenMP.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#ifndef CALIBRATION_C_H
#define CALIBRATION_C_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Basic Calibration Operations
 * ============================================================================ */

/**
 * @brief Subtract master bias frame from an image (in-place)
 *
 * @param image   Image data to calibrate (modified in-place)
 * @param bias    Master bias frame (same dimensions as image)
 * @param size    Total number of pixels (width * height * channels)
 * @param threads Number of OpenMP threads to use
 */
void subtract_bias_c(float* image, const float* bias, size_t size, int threads);

/**
 * @brief Subtract scaled master dark frame with pedestal offset (in-place)
 *
 * Formula: image[i] = (image[i] - k * dark[i]) + pedestal
 *
 * @param image    Image data to calibrate (modified in-place)
 * @param dark     Master dark frame (same dimensions as image)
 * @param size     Total number of pixels (width * height * channels)
 * @param k        Dark scaling factor (typically 1.0 or optimized value)
 * @param pedestal Pedestal value to add after subtraction (prevents negative values)
 * @param threads  Number of OpenMP threads to use
 */
void subtract_dark_c(float* image, const float* dark, size_t size,
                     float k, float pedestal, int threads);

/**
 * @brief Apply master flat field correction (in-place)
 *
 * Divides image by normalized flat field. Pixels with flat values below
 * epsilon threshold are clamped to prevent division artifacts.
 *
 * @param image         Image data to calibrate (modified in-place)
 * @param flat          Master flat frame (same dimensions as image)
 * @param size          Total number of pixels (width * height * channels)
 * @param normalization Flat field normalization factor (mean of flat center)
 * @param threads       Number of OpenMP threads to use
 */
void apply_flat_c(float* image, const float* flat, size_t size,
                  float normalization, int threads);

/* ============================================================================
 * Bad Pixel Detection and Correction
 * ============================================================================ */

/**
 * @brief Detect deviant (hot/cold) pixels in a master dark frame
 *
 * Uses median absolute deviation (MAD) based sigma clipping to identify
 * outlier pixels. For CFA images, statistics are computed per color phase
 * to avoid cross-contamination between color channels.
 *
 * @param dark       Master dark frame data (single plane)
 * @param width      Image width in pixels
 * @param height     Image height in pixels
 * @param hot_sigma  Sigma threshold for hot pixel detection (e.g., 5.0)
 * @param cold_sigma Sigma threshold for cold pixel detection (e.g., 5.0)
 * @param out_x      Output array for X coordinates of detected pixels
 * @param out_y      Output array for Y coordinates of detected pixels
 * @param max_output Maximum number of pixels to output (array capacity)
 * @param cfa_pattern CFA pattern type: -1=None, 0=RGGB, 1=BGGR, 2=GRBG, 3=GBRG, 4=XTrans
 *
 * @return Number of deviant pixels found (up to max_output)
 *
 * @note This function processes a single plane of data. For multi-channel images,
 *       the caller must extract each channel into a contiguous buffer before calling.
 */
int find_deviant_pixels_c(const float* dark, int width, int height,
                          float hot_sigma, float cold_sigma,
                          int* out_x, int* out_y, int max_output,
                          int cfa_pattern);

/**
 * @brief Apply cosmetic correction to fix hot/cold pixels (in-place)
 *
 * Supports two modes:
 * 1. Master map mode: Uses pre-computed bad pixel coordinates
 * 2. Sigma clipping mode: Auto-detects and fixes outliers on-the-fly
 *
 * @param image        Image data to correct (modified in-place)
 * @param width        Image width in pixels
 * @param height       Image height in pixels
 * @param channels     Number of color channels (1 for mono/CFA, 3 for RGB)
 * @param bad_pixels_x Array of X coordinates for known bad pixels (may be NULL)
 * @param bad_pixels_y Array of Y coordinates for known bad pixels (may be NULL)
 * @param bad_count    Number of entries in bad pixel arrays
 * @param hot_sigma    Sigma threshold for hot pixel auto-detection (0 to disable)
 * @param cold_sigma   Sigma threshold for cold pixel auto-detection (0 to disable)
 * @param cfa_pattern  CFA pattern for proper neighbor selection (-1 for non-CFA)
 * @param hot_fixed    Output: number of hot pixels corrected (may be NULL)
 * @param cold_fixed   Output: number of cold pixels corrected (may be NULL)
 * @param threads      Number of OpenMP threads to use
 */
void apply_cosmetic_correction_c(float* image, int width, int height, int channels,
                                 const int* bad_pixels_x, const int* bad_pixels_y,
                                 int bad_count,
                                 float hot_sigma, float cold_sigma,
                                 int cfa_pattern,
                                 int* hot_fixed, int* cold_fixed,
                                 int threads);

/* ============================================================================
 * Sensor Artifact Correction
 * ============================================================================ */

/**
 * @brief Remove horizontal banding artifacts from CCD/CMOS images (in-place)
 *
 * Corrects row-to-row brightness variations common in Canon and other DSLR sensors.
 * Uses 1st quartile (25th percentile) to estimate background level, avoiding
 * contamination from stars and nebulae.
 *
 * @param image       Image data to correct (modified in-place)
 * @param width       Image width in pixels
 * @param height      Image height in pixels
 * @param channels    Number of color channels (1 for mono/CFA, 3 for RGB interleaved)
 * @param cfa_pattern CFA pattern type (-1 for non-CFA or already debayered)
 * @param threads     Number of OpenMP threads to use
 */
void fix_banding_c(float* image, int width, int height, int channels,
                   int cfa_pattern, int threads);

/**
 * @brief Fix completely bad rows/columns in sensor data (in-place)
 *
 * Detects and interpolates entire rows or columns that are defective,
 * such as dead sensor lines or hot columns. Uses high-pass residual
 * analysis to identify outlier lines.
 *
 * @param image       Image data to correct (modified in-place)
 * @param width       Image width in pixels
 * @param height      Image height in pixels
 * @param channels    Number of color channels
 * @param cfa_pattern CFA pattern type (-1 for non-CFA)
 * @param threads     Number of OpenMP threads to use
 */
void fix_bad_lines_c(float* image, int width, int height, int channels,
                     int cfa_pattern, int threads);

/**
 * @brief Fix X-Trans sensor autofocus pixel artifacts (in-place)
 *
 * Fujifilm X-Trans sensors have embedded phase-detect AF pixels that
 * can cause artifacts. This function detects and interpolates outlier
 * pixels using same-color neighbors in the 6x6 X-Trans pattern.
 *
 * @param image       Image data to correct (modified in-place)
 * @param width       Image width in pixels
 * @param height      Image height in pixels
 * @param bay_pattern Bayer/X-Trans pattern string from metadata (36 chars for X-Trans)
 * @param model_name  Camera model name for pattern detection fallback
 * @param threads     Number of OpenMP threads to use
 */
void fix_xtrans_c(float* image, int width, int height,
                  const char* bay_pattern, const char* model_name,
                  int threads);

/* ============================================================================
 * CFA and Debayering Operations
 * ============================================================================ */

/**
 * @brief Equalize CFA channel response for flat field frames (in-place)
 *
 * Normalizes the response of R, G, B channels in a CFA flat field to have
 * equal average values. This prevents color cast in calibrated images.
 * Supports both 2x2 Bayer patterns and 6x6 X-Trans patterns.
 *
 * @param image        Flat field image data (modified in-place, single channel)
 * @param width        Image width in pixels
 * @param height       Image height in pixels
 * @param pattern_type CFA pattern: 0=RGGB, 1=BGGR, 2=GRBG, 3=GBRG, 4=XTrans
 * @param threads      Number of OpenMP threads to use
 */
void equalize_cfa_c(float* image, int width, int height,
                    int pattern_type, int threads);

/**
 * @brief Perform bilinear debayering (demosaicing)
 *
 * Converts single-channel CFA data to 3-channel RGB using bilinear interpolation.
 * This is a fast algorithm suitable for preview and intermediate processing.
 *
 * @param src     Source CFA image (single channel, width * height floats)
 * @param dst     Destination RGB image (interleaved, width * height * 3 floats)
 * @param width   Image width in pixels
 * @param height  Image height in pixels
 * @param r_row   Row parity of red pixels (0 or 1)
 * @param r_col   Column parity of red pixels (0 or 1)
 * @param b_row   Row parity of blue pixels (0 or 1)
 * @param b_col   Column parity of blue pixels (0 or 1)
 * @param threads Number of OpenMP threads to use
 */
void debayer_bilinear_c(const float* src, float* dst, int width, int height,
                        int r_row, int r_col, int b_row, int b_col, int threads);

/* ============================================================================
 * Calibration Parameter Optimization
 * ============================================================================ */

/**
 * @brief Compute flat field normalization factor
 *
 * Calculates the mean pixel value of the central 1/3 region of the flat field.
 * This value is used to normalize flat field division.
 *
 * @param flat     Master flat frame data
 * @param width    Image width in pixels
 * @param height   Image height in pixels
 * @param channels Number of color channels
 *
 * @return Normalization factor (mean of center region), or 1.0 on error
 */
double compute_flat_normalization_c(const float* flat, int width, int height,
                                    int channels);

/**
 * @brief Find optimal dark frame scaling factor using golden section search
 *
 * Determines the optimal scaling factor K for dark subtraction by minimizing
 * noise in the calibrated result. Uses golden section optimization over
 * a region of interest.
 *
 * @param light     Light frame data
 * @param dark      Master dark frame data
 * @param width     Image width in pixels
 * @param height    Image height in pixels
 * @param channels  Number of color channels
 * @param k_min     Minimum K value to search (e.g., 0.5)
 * @param k_max     Maximum K value to search (e.g., 1.5)
 * @param tolerance Convergence tolerance for optimization
 * @param max_iters Maximum number of iterations
 * @param roi_x     ROI X offset for noise evaluation
 * @param roi_y     ROI Y offset for noise evaluation
 * @param roi_w     ROI width for noise evaluation
 * @param roi_h     ROI height for noise evaluation
 *
 * @return Optimal dark scaling factor K
 */
float find_optimal_dark_scale_c(const float* light, const float* dark,
                                int width, int height, int channels,
                                float k_min, float k_max,
                                float tolerance, int max_iters,
                                int roi_x, int roi_y, int roi_w, int roi_h);

#ifdef __cplusplus
}
#endif

#endif /* CALIBRATION_C_H */