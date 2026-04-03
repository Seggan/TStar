/**
 * @file CalibrationEngine.h
 * @brief C++ interface for astronomical image calibration operations
 *
 * Provides high-level calibration functions that wrap the C implementation
 * with Qt/ImageBuffer integration. Handles multi-channel image processing,
 * CFA pattern detection, and coordinate output for bad pixel maps.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#ifndef CALIBRATION_ENGINE_H
#define CALIBRATION_ENGINE_H

#include "../ImageBuffer.h"
#include "../preprocessing/PreprocessingTypes.h"

#include <QString>
#include <QPoint>
#include <vector>

namespace Calibration {

/**
 * @class CalibrationEngine
 * @brief Core calibration functions for astronomical image processing
 *
 * This class provides static methods for image calibration operations
 * including dark optimization, flat field application, bad pixel detection,
 * and sensor artifact correction.
 */
class CalibrationEngine {
public:
    /* ========================================================================
     * Dark Frame Calibration
     * ======================================================================== */

    /**
     * @brief Find optimal dark frame scaling factor
     *
     * Uses golden section search to find the K value that minimizes
     * noise in (Light - K * Dark). This compensates for temperature
     * differences between dark and light frames.
     *
     * @param light      Light frame to calibrate
     * @param masterDark Master dark frame
     * @param params     Optimization parameters (K range, tolerance, iterations)
     * @return Optimal scaling factor K
     */
    static float findOptimalDarkScale(const ImageBuffer& light,
                                      const ImageBuffer& masterDark,
                                      const Preprocessing::DarkOptimParams& params);

    /**
     * @brief Evaluate calibration noise for a specific dark scaling factor
     *
     * @param light      Light frame
     * @param masterDark Master dark frame
     * @param k          Dark scaling factor to evaluate
     * @param rect       Region of interest for noise measurement
     * @return Noise metric value (lower is better)
     */
    static float evaluateCalibratedNoise(const ImageBuffer& light,
                                         const ImageBuffer& masterDark,
                                         float k,
                                         const QRect& rect);

    /* ========================================================================
     * Flat Field Calibration
     * ======================================================================== */

    /**
     * @brief Compute normalization factor for a master flat field
     *
     * Calculates the mean pixel value of the central 1/3 region,
     * avoiding vignetting at the edges.
     *
     * @param masterFlat Master flat frame
     * @return Normalization factor (mean of center region)
     */
    static double computeFlatNormalization(const ImageBuffer& masterFlat);

    /**
     * @brief Apply flat field correction to a light frame
     *
     * Divides light frame by normalized flat field to correct for
     * optical vignetting and pixel-to-pixel sensitivity variations.
     *
     * @param light         Light frame to correct (modified in-place)
     * @param masterFlat    Master flat frame
     * @param normalization Normalization factor from computeFlatNormalization()
     * @return true on success, false if normalization is invalid
     */
    static bool applyFlat(ImageBuffer& light,
                          const ImageBuffer& masterFlat,
                          double normalization);

    /* ========================================================================
     * Sensor Artifact Correction
     * ======================================================================== */

    /**
     * @brief Fix CCD/CMOS banding artifacts
     *
     * Removes horizontal row-to-row brightness variations common in
     * Canon and other DSLR sensors. Uses robust statistics to avoid
     * contamination from stars and nebulae.
     *
     * @param image Image to correct (modified in-place)
     */
    static void fixBanding(ImageBuffer& image);

    /**
     * @brief Fix completely bad sensor lines
     *
     * Detects and interpolates entire rows or columns that are
     * defective (dead lines, hot lines).
     *
     * @param image Image to correct (modified in-place)
     */
    static void fixBadLines(ImageBuffer& image);

    /**
     * @brief Fix X-Trans sensor autofocus pixel artifacts
     *
     * Fujifilm X-Trans sensors have embedded phase-detect AF pixels
     * that can cause artifacts. This function detects and interpolates
     * outlier pixels using same-color neighbors.
     *
     * @param image Image to correct (modified in-place)
     */
    static void fixXTransArtifacts(ImageBuffer& image);

    /* ========================================================================
     * CFA Processing
     * ======================================================================== */

    /**
     * @brief Equalize CFA channel response in a master flat
     *
     * Normalizes R, G, B channel means to match, preventing color cast
     * in calibrated images. Essential for CFA flat fields.
     *
     * @param flat    Master flat image (modified in-place)
     * @param pattern Bayer/X-Trans pattern of the sensor
     */
    static void equalizeCFAChannels(ImageBuffer& flat,
                                    Preprocessing::BayerPattern pattern);

    /* ========================================================================
     * Bad Pixel Detection
     * ======================================================================== */

    /**
     * @brief Detect deviant (hot/cold) pixels from a master dark
     *
     * Uses MAD-based sigma clipping to identify outlier pixels.
     * The resulting coordinates can be used to build a bad pixel map.
     *
     * @param dark       Master dark frame
     * @param hotSigma   Sigma threshold for hot pixel detection
     * @param coldSigma  Sigma threshold for cold pixel detection
     * @return Vector of deviant pixel coordinates
     */
    static std::vector<QPoint> findDeviantPixels(const ImageBuffer& dark,
                                                 float hotSigma,
                                                 float coldSigma);
};

}  // namespace Calibration

#endif  // CALIBRATION_ENGINE_H