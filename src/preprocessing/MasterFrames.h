/**
 * @file MasterFrames.h
 * @brief Master calibration frame management.
 *
 * Provides facilities for loading, caching, creating, and validating
 * master calibration frames (bias, dark, flat, dark-for-flat) used
 * during the image preprocessing pipeline.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#ifndef MASTER_FRAMES_H
#define MASTER_FRAMES_H

#include "PreprocessingTypes.h"
#include "../ImageBuffer.h"
#include "../stacking/StackingTypes.h"

#include <QString>
#include <QStringList>
#include <memory>
#include <unordered_map>

namespace Preprocessing {

/**
 * @brief Manages the lifecycle of master calibration frames.
 *
 * Master frames are loaded from FITS files, cached in memory for
 * repeated use during batch calibration, and can be created from
 * sets of individual calibration sub-frames via integrated stacking.
 *
 * The class is non-copyable; pass by reference or pointer.
 */
class MasterFrames {
public:
    MasterFrames()  = default;
    ~MasterFrames() = default;

    // Non-copyable, non-assignable.
    MasterFrames(const MasterFrames&)            = delete;
    MasterFrames& operator=(const MasterFrames&) = delete;

    // ========================================================================
    //  Loading and Access
    // ========================================================================

    /**
     * @brief Load a master frame from a FITS file.
     * @param type Type of master frame (Bias, Dark, Flat, DarkFlat).
     * @param path Filesystem path to the master FITS file.
     * @return true if the file was loaded and parsed successfully.
     */
    bool load(MasterType type, const QString& path);

    /**
     * @brief Query whether a particular master frame is loaded and valid.
     */
    bool isLoaded(MasterType type) const;

    /**
     * @brief Retrieve a read-only pointer to a loaded master frame.
     * @return Pointer to the ImageBuffer, or nullptr if not loaded.
     */
    const ImageBuffer* get(MasterType type) const;

    /**
     * @brief Retrieve a mutable pointer to a loaded master frame.
     */
    ImageBuffer* get(MasterType type);

    /**
     * @brief Retrieve computed statistics for a loaded master frame.
     */
    const MasterStats& stats(MasterType type) const;

    /**
     * @brief Unload a single master frame, releasing its memory.
     */
    void unload(MasterType type);

    /**
     * @brief Unload all master frames.
     */
    void clear();

    // ========================================================================
    //  Creation (Stacking Individual Sub-Frames)
    // ========================================================================

    /**
     * @brief Create a master bias by stacking individual bias sub-frames.
     * @param files     List of bias sub-frame file paths.
     * @param output    Destination path for the resulting master bias FITS.
     * @param method    Pixel combination method (default: Mean).
     * @param rejection Outlier rejection algorithm (default: Winsorised).
     * @param sigmaLow  Low sigma clipping threshold.
     * @param sigmaHigh High sigma clipping threshold.
     * @param progress  Optional progress callback.
     * @return true on success.
     */
    bool createMasterBias(const QStringList& files,
                          const QString& output,
                          Stacking::Method    method    = Stacking::Method::Mean,
                          Stacking::Rejection rejection = Stacking::Rejection::Winsorized,
                          float sigmaLow  = 3.0f,
                          float sigmaHigh = 3.0f,
                          ProgressCallback progress = nullptr);

    /**
     * @brief Create a master dark by stacking individual dark sub-frames.
     * @param files      List of dark sub-frame file paths.
     * @param output     Destination path for the resulting master dark FITS.
     * @param masterBias Optional master bias to subtract before stacking.
     * @param method     Pixel combination method.
     * @param rejection  Outlier rejection algorithm.
     * @param sigmaLow   Low sigma clipping threshold.
     * @param sigmaHigh  High sigma clipping threshold.
     * @param progress   Optional progress callback.
     * @return true on success.
     */
    bool createMasterDark(const QStringList& files,
                          const QString& output,
                          const QString& masterBias = QString(),
                          Stacking::Method    method    = Stacking::Method::Mean,
                          Stacking::Rejection rejection = Stacking::Rejection::Winsorized,
                          float sigmaLow  = 3.0f,
                          float sigmaHigh = 3.0f,
                          ProgressCallback progress = nullptr);

    /**
     * @brief Create a master flat by stacking individual flat sub-frames.
     * @param files      List of flat sub-frame file paths.
     * @param output     Destination path for the resulting master flat FITS.
     * @param masterBias Optional master bias to subtract before stacking.
     * @param masterDark Optional master dark to subtract before stacking.
     * @param method     Pixel combination method.
     * @param rejection  Outlier rejection algorithm.
     * @param sigmaLow   Low sigma clipping threshold.
     * @param sigmaHigh  High sigma clipping threshold.
     * @param progress   Optional progress callback.
     * @return true on success.
     */
    bool createMasterFlat(const QStringList& files,
                          const QString& output,
                          const QString& masterBias = QString(),
                          const QString& masterDark = QString(),
                          Stacking::Method    method    = Stacking::Method::Mean,
                          Stacking::Rejection rejection = Stacking::Rejection::Winsorized,
                          float sigmaLow  = 3.0f,
                          float sigmaHigh = 3.0f,
                          ProgressCallback progress = nullptr);

    // ========================================================================
    //  Validation
    // ========================================================================

    /**
     * @brief Validate that all loaded masters are dimensionally compatible
     *        with a target light frame.
     * @param target The light frame to check against.
     * @return An empty string if compatible, or a human-readable error message.
     */
    QString validateCompatibility(const ImageBuffer& target) const;

    /**
     * @brief Check whether the master dark temperature is close enough
     *        to the target temperature.
     * @param targetTemp Target sensor temperature in degrees Celsius.
     * @param tolerance  Acceptable difference in degrees (default: 5.0).
     * @return true if within tolerance, or if temperature metadata is absent.
     */
    bool checkDarkTemperature(double targetTemp, double tolerance = 5.0) const;

    /**
     * @brief Check whether the master dark exposure matches the target.
     * @param targetExposure Target exposure time in seconds.
     * @param tolerance      Relative tolerance as a fraction (0.1 = 10%).
     * @return true if within tolerance, or if exposure metadata is absent.
     */
    bool checkDarkExposure(double targetExposure, double tolerance = 0.1) const;

private:
    // -- Internal storage ----------------------------------------------------

    /**
     * @brief Container for a single master frame and its associated data.
     */
    struct MasterData {
        std::unique_ptr<ImageBuffer> buffer;
        MasterStats stats;
        QString     path;
    };

    std::unordered_map<int, MasterData> m_masters;

    // -- Helpers -------------------------------------------------------------

    /** @brief Compute descriptive statistics for a loaded master frame. */
    void computeStats(MasterType type);

    /** @brief Convert a MasterType enum to an integer map key. */
    static int typeIndex(MasterType type) { return static_cast<int>(type); }
};

} // namespace Preprocessing

#endif // MASTER_FRAMES_H