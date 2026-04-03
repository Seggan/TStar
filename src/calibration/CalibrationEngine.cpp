/**
 * @file CalibrationEngine.cpp
 * @brief Implementation of C++ calibration interface
 *
 * Provides Qt/ImageBuffer integration for the C calibration primitives.
 * Handles multi-channel extraction, CFA pattern detection from metadata,
 * and thread-safe result aggregation.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#include "CalibrationEngine.h"
#include "CalibrationC.h"
#include "../stacking/Statistics.h"

#include <cmath>
#include <algorithm>
#include <vector>
#include <mutex>

#include <omp.h>

namespace Calibration {

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

/**
 * @brief Extract CFA pattern code from image metadata
 *
 * Parses the BAYERPAT header to determine the CFA pattern type.
 * Returns -1 for non-CFA images (multi-channel or no pattern).
 *
 * @param image    Image buffer with metadata
 * @param channels Number of image channels
 * @return CFA pattern code: -1=none, 0=RGGB, 1=BGGR, 2=GRBG, 3=GBRG, 4=XTrans
 */
static int extractCfaPattern(const ImageBuffer& image, int channels)
{
    /* Non-mono images don't have CFA patterns */
    if (channels != 1) {
        return -1;
    }

    QString bayerPattern = image.getHeaderValue("BAYERPAT");

    /* Check for X-Trans pattern */
    if (bayerPattern.contains("Trans", Qt::CaseInsensitive)) {
        return 4;
    }

    /* Default to RGGB for mono images with unknown pattern */
    return 0;
}

/**
 * @brief Convert BayerPattern enum to C interface pattern code
 *
 * @param pattern Preprocessing BayerPattern enum value
 * @return Integer pattern code for C functions
 */
static int bayerPatternToCode(Preprocessing::BayerPattern pattern)
{
    switch (pattern) {
        case Preprocessing::BayerPattern::RGGB:  return 0;
        case Preprocessing::BayerPattern::BGGR:  return 1;
        case Preprocessing::BayerPattern::GRBG:  return 2;
        case Preprocessing::BayerPattern::GBRG:  return 3;
        case Preprocessing::BayerPattern::XTrans: return 4;
        default: return -1;
    }
}

/* ============================================================================
 * Dark Frame Calibration
 * ============================================================================ */

float CalibrationEngine::findOptimalDarkScale(const ImageBuffer& light,
                                              const ImageBuffer& masterDark,
                                              const Preprocessing::DarkOptimParams& params)
{
    const int width = light.width();
    const int height = light.height();
    const int channels = light.channels();

    const float* lightData = light.data().data();
    const float* darkData = masterDark.data().data();

    /* 
     * Use center 512x512 region for optimization.
     * This avoids edge effects and provides a representative sample.
     * ROI size is clamped to image dimensions for small images.
     */
    int roiSize = std::min({512, width, height});
    int roiX = std::max(0, (width - roiSize) / 2);
    int roiY = std::max(0, (height - roiSize) / 2);

    return find_optimal_dark_scale_c(
        lightData, darkData,
        width, height, channels,
        params.K_min, params.K_max,
        params.tolerance, params.maxIterations,
        roiX, roiY, roiSize, roiSize
    );
}

/* ============================================================================
 * Flat Field Calibration
 * ============================================================================ */

double CalibrationEngine::computeFlatNormalization(const ImageBuffer& masterFlat)
{
    return compute_flat_normalization_c(
        masterFlat.data().data(),
        masterFlat.width(),
        masterFlat.height(),
        masterFlat.channels()
    );
}

bool CalibrationEngine::applyFlat(ImageBuffer& light,
                                  const ImageBuffer& masterFlat,
                                  double normalization)
{
    /* Validate normalization factor to prevent division issues */
    if (normalization < 1e-6) {
        return false;
    }

    const size_t totalPixels = static_cast<size_t>(
        light.width() * light.height() * light.channels()
    );

    float* imageData = light.data().data();
    const float* flatData = masterFlat.data().data();

    apply_flat_c(
        imageData, flatData,
        totalPixels,
        static_cast<float>(normalization),
        omp_get_max_threads()
    );

    return true;
}

/* ============================================================================
 * Sensor Artifact Correction
 * ============================================================================ */

void CalibrationEngine::fixBanding(ImageBuffer& image)
{
    const int width = image.width();
    const int height = image.height();
    const int channels = image.channels();

    float* data = image.data().data();
    int cfaPattern = extractCfaPattern(image, channels);

    fix_banding_c(data, width, height, channels, cfaPattern, omp_get_max_threads());
}

void CalibrationEngine::fixBadLines(ImageBuffer& image)
{
    const int width = image.width();
    const int height = image.height();
    const int channels = image.channels();

    float* data = image.data().data();
    int cfaPattern = extractCfaPattern(image, channels);

    fix_bad_lines_c(data, width, height, channels, cfaPattern, omp_get_max_threads());
}

void CalibrationEngine::fixXTransArtifacts(ImageBuffer& image)
{
    /* Extract camera model and pattern from image metadata */
    QString model = image.getHeaderValue("INSTRUME");
    QString pattern = image.getHeaderValue("BAYERPAT");

    fix_xtrans_c(
        image.data().data(),
        image.width(),
        image.height(),
        pattern.toLatin1().constData(),
        model.toLatin1().constData(),
        omp_get_max_threads()
    );
}

/* ============================================================================
 * CFA Processing
 * ============================================================================ */

void CalibrationEngine::equalizeCFAChannels(ImageBuffer& flat,
                                            Preprocessing::BayerPattern pattern)
{
    /* CFA equalization only applies to single-channel (mosaic) images */
    if (flat.channels() != 1) {
        return;
    }

    const int width = flat.width();
    const int height = flat.height();
    float* data = flat.data().data();

    int patternCode = bayerPatternToCode(pattern);

    if (patternCode >= 0) {
        equalize_cfa_c(data, width, height, patternCode, omp_get_max_threads());
    }
}

/* ============================================================================
 * Bad Pixel Detection
 * ============================================================================ */

std::vector<QPoint> CalibrationEngine::findDeviantPixels(const ImageBuffer& dark,
                                                         float hotSigma,
                                                         float coldSigma)
{
    const int width = dark.width();
    const int height = dark.height();
    const int channels = dark.channels();
    const float* data = dark.data().data();

    std::vector<QPoint> deviantPixels;
    std::mutex resultMutex;

    /* Maximum deviant pixels per channel (safety limit to prevent memory issues) */
    const int MAX_DEVIANT_PER_CHANNEL = 500000;

    /* Process each channel independently */
    for (int c = 0; c < channels; ++c) {

        /*
         * The C function expects a contiguous single-plane buffer.
         * For single-channel images, we can use the data directly.
         * For multi-channel (interleaved RGB), we must extract the channel.
         */
        std::vector<float> channelBuffer;
        const float* layerPtr = nullptr;

        if (channels == 1) {
            /* Single channel: use data directly */
            layerPtr = data;
        } else {
            /* Multi-channel: extract this channel into contiguous buffer */
            channelBuffer.resize(static_cast<size_t>(width) * height);

            #pragma omp parallel for
            for (int i = 0; i < width * height; ++i) {
                channelBuffer[i] = data[i * channels + c];
            }

            layerPtr = channelBuffer.data();
        }

        /* Allocate output arrays for detected pixel coordinates */
        int* outX = static_cast<int*>(malloc(MAX_DEVIANT_PER_CHANNEL * sizeof(int)));
        int* outY = static_cast<int*>(malloc(MAX_DEVIANT_PER_CHANNEL * sizeof(int)));

        if (!outX || !outY) {
            free(outX);
            free(outY);
            continue;
        }

        /*
         * CFA mode selection:
         * - Single channel images: enable CFA-aware detection (2x2 phase split)
         * - Multi-channel images: treat each plane independently
         */
        int cfaMode = (channels == 1) ? 0 : -1;

        int count = find_deviant_pixels_c(
            layerPtr, width, height,
            hotSigma, coldSigma,
            outX, outY,
            MAX_DEVIANT_PER_CHANNEL,
            cfaMode
        );

        /* Aggregate results (thread-safe) */
        if (count > 0) {
            std::lock_guard<std::mutex> lock(resultMutex);

            for (int i = 0; i < count; ++i) {
                deviantPixels.push_back(QPoint(outX[i], outY[i]));
            }
        }

        free(outX);
        free(outY);
    }

    /* 
     * Sort and remove duplicate coordinates.
     * Duplicates can occur when the same pixel is detected in multiple channels.
     */
    std::sort(deviantPixels.begin(), deviantPixels.end(),
        [](const QPoint& a, const QPoint& b) {
            if (a.y() != b.y()) return a.y() < b.y();
            return a.x() < b.x();
        }
    );

    deviantPixels.erase(
        std::unique(deviantPixels.begin(), deviantPixels.end()),
        deviantPixels.end()
    );

    return deviantPixels;
}

}  // namespace Calibration