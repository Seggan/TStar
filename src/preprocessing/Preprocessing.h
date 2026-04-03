/**
 * @file Preprocessing.h
 * @brief Image calibration engine and worker thread for the preprocessing pipeline.
 *
 * Declares PreprocessingEngine (single / batch image calibration) and
 * PreprocessingWorker (QThread wrapper for asynchronous batch processing).
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#ifndef PREPROCESSING_H
#define PREPROCESSING_H

#include "PreprocessingTypes.h"
#include "MasterFrames.h"
#include "../ImageBuffer.h"

#include <QObject>
#include <QThread>

#include <atomic>
#include <memory>
#include <vector>
#include <QPoint>

namespace Preprocessing {

// ============================================================================
//  Calibration Statistics
// ============================================================================

/**
 * @brief Accumulated statistics from a calibration run.
 */
struct CalibrationStats {
    int    imagesProcessed      = 0;
    int    imagesFailed         = 0;
    double avgBackground        = 0.0;
    double avgDarkOptimK        = 1.0;   ///< Running average of the dark optimisation factor K
    int    hotPixelsCorrected   = 0;
    int    coldPixelsCorrected  = 0;
};

// ============================================================================
//  Preprocessing Engine
// ============================================================================

/**
 * @brief Core engine for the image calibration pipeline.
 *
 * Executes the standard CCD/CMOS calibration sequence:
 *   1. Bias subtraction (master file or synthetic constant)
 *   2. Dark subtraction (with optional exposure-ratio scaling
 *      or numerical optimisation)
 *   3. Flat-field division
 *   4. Sensor-specific artefact fixes (banding, bad lines, X-Trans AF)
 *   5. Cosmetic correction (hot / cold pixel repair)
 *   6. CFA demosaicing (debayering)
 *
 * Supports both single-image and multi-threaded batch processing.
 */
class PreprocessingEngine : public QObject {
    Q_OBJECT

public:
    explicit PreprocessingEngine(QObject* parent = nullptr);
    ~PreprocessingEngine() override;

    // ========================================================================
    //  Configuration
    // ========================================================================

    /**
     * @brief Apply a complete set of preprocessing parameters.
     *
     * This also triggers loading of master frames and pre-computation
     * of derived data such as flat normalisation and deviant-pixel maps.
     */
    void setParams(const PreprocessParams& params);

    /** @brief Read-only access to the current parameters. */
    const PreprocessParams& params() const { return m_params; }

    /** @brief Mutable access to the master frames manager. */
    MasterFrames& masters() { return m_masters; }

    /** @brief Read-only access to the master frames manager. */
    const MasterFrames& masters() const { return m_masters; }

    // ========================================================================
    //  Single-Image Processing
    // ========================================================================

    /**
     * @brief Run the full calibration pipeline on a single in-memory image.
     * @param input  Source image (unmodified).
     * @param output Calibrated result.
     * @return true if all enabled calibration steps completed successfully.
     */
    bool preprocessImage(const ImageBuffer& input, ImageBuffer& output);

    /**
     * @brief Load, calibrate, and save a single FITS file.
     * @param inputPath  Path to the source FITS file.
     * @param outputPath Destination path for the calibrated output.
     * @return true on success.
     */
    bool preprocessFile(const QString& inputPath, const QString& outputPath);

    // ========================================================================
    //  Batch Processing
    // ========================================================================

    /**
     * @brief Calibrate a list of files in parallel.
     * @param inputFiles List of source file paths.
     * @param outputDir  Directory in which to write calibrated outputs.
     * @param progress   Optional progress callback.
     * @return Number of files successfully processed.
     */
    int preprocessBatch(const QStringList& inputFiles,
                        const QString& outputDir,
                        ProgressCallback progress = nullptr);

    /** @brief Retrieve statistics accumulated during the last run. */
    const CalibrationStats& lastStats() const { return m_stats; }

    // ========================================================================
    //  Cancellation
    // ========================================================================

    /** @brief Request cancellation of the current operation. */
    void requestCancel() { m_cancelled = true; }

    /** @brief Query whether cancellation has been requested. */
    bool isCancelled() const { return m_cancelled.load(); }

signals:
    void progressChanged(const QString& message, double progress);
    void logMessage(const QString& message, const QString& color);
    void imageProcessed(const QString& path, bool success);
    void finished(bool success);

private:
    // ========================================================================
    //  Calibration Steps
    // ========================================================================

    /**
     * @brief Subtract bias from an image (synthetic constant or master frame).
     */
    bool subtractBias(ImageBuffer& image);

    /**
     * @brief Subtract the master dark, optionally scaling by exposure ratio
     *        or numerical optimisation.
     * @param image [in/out] Image to calibrate.
     * @param K     [out]    Scale factor actually applied.
     * @return true on success.
     */
    bool subtractDark(ImageBuffer& image, double& K);

    /**
     * @brief Detect and replace cosmetic defects (hot / cold pixels).
     * @param image         [in/out] Image to correct.
     * @param hotCorrected  [out]    Count of hot pixels corrected.
     * @param coldCorrected [out]    Count of cold pixels corrected.
     */
    void applyCosmeticCorrection(ImageBuffer& image,
                                 int& hotCorrected, int& coldCorrected);

    /**
     * @brief Demosaic a single-channel CFA image to three-channel RGB.
     */
    bool debayer(ImageBuffer& image);

    // ========================================================================
    //  Debayering Algorithm Wrappers
    // ========================================================================

    bool debayerBilinear(ImageBuffer& image);
    bool debayerVNG(ImageBuffer& image);
    bool debayerSuperpixel(ImageBuffer& image);

    /**
     * @brief Determine the Bayer colour (0 = R, 1 = G, 2 = B) at a pixel.
     */
    int getBayerColor(int x, int y) const;

    // ========================================================================
    //  Internal State
    // ========================================================================

    PreprocessParams       m_params;
    MasterFrames           m_masters;
    CalibrationStats       m_stats;
    std::atomic<bool>      m_cancelled{false};
    std::vector<QPoint>    m_deviantPixels;       ///< Defect map derived from master dark
    double                 m_flatNormalization = 1.0; ///< Cached flat normalisation factor

    /**
     * @brief Append a HISTORY record to the image FITS header.
     */
    void addHistory(ImageBuffer& image, const QString& message);
};

// ============================================================================
//  Worker Thread for Batch Preprocessing
// ============================================================================

/**
 * @brief QThread wrapper that runs PreprocessingEngine::preprocessBatch
 *        on a background thread, relaying progress and log signals.
 */
class PreprocessingWorker : public QThread {
    Q_OBJECT

public:
    PreprocessingWorker(const PreprocessParams& params,
                        const QStringList& files,
                        const QString& outputDir,
                        QObject* parent = nullptr);

    void run() override;
    void requestCancel();

    /** @brief Access to the engine's accumulated statistics. */
    const CalibrationStats& stats() const { return m_engine.lastStats(); }

signals:
    void progressChanged(const QString& message, double progress);
    void logMessage(const QString& message, const QString& color);
    void imageProcessed(const QString& path, bool success);
    void finished(bool success);

private:
    PreprocessingEngine m_engine;
    QStringList         m_files;
    QString             m_outputDir;
};

} // namespace Preprocessing

#endif // PREPROCESSING_H