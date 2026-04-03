#pragma once
#ifndef SENSORPROFILE_H
#define SENSORPROFILE_H

// ============================================================================
// SensorProfile.h
// Sensor characterization profiles for photometry, calibration, and
// noise modeling. Includes a built-in database of common sensor types.
// ============================================================================

#include <QString>
#include <map>
#include <vector>

/**
 * @brief Physical and electronic characteristics of an imaging sensor.
 *
 * Used for:
 *   - Aperture photometry (gain, read noise, full well capacity)
 *   - Color calibration (per-channel quantum efficiency curves)
 *   - Noise modeling (dark current, temperature compensation)
 *   - Deconvolution parameter estimation
 */
struct SensorProfile {
    // -- Identification -------------------------------------------------------
    QString name;                       ///< e.g. "Canon EOS R7", "ZWO ASI533MM-Pro"
    QString type;                       ///< "DSLR", "CCD", "CMOS", "eMOS"

    // -- Sensor geometry ------------------------------------------------------
    double pixelWidth   = 0.0;          ///< Pixel pitch in microns (horizontal)
    double pixelHeight  = 0.0;          ///< Pixel pitch in microns (vertical)
    int    sensorWidth  = 0;            ///< Active sensor width in pixels
    int    sensorHeight = 0;            ///< Active sensor height in pixels

    // -- Photometric parameters -----------------------------------------------
    double gain         = 0.0;          ///< Conversion gain (e-/ADU)
    double readNoise    = 0.0;          ///< Read noise (RMS electrons)
    double darkCurrent  = 0.0;          ///< Dark current (e-/s at reference temperature)
    double fullWell     = 0.0;          ///< Full well capacity (electrons)
    double maxADU       = 65535.0;      ///< Maximum ADU value (e.g. 65535 for 16-bit)

    // -- Quantum efficiency curves (optional) ---------------------------------
    std::map<int, double> qeRed;        ///< Wavelength (nm) -> QE [0,1] for red channel
    std::map<int, double> qeGreen;      ///< Wavelength (nm) -> QE [0,1] for green channel
    std::map<int, double> qeBlue;       ///< Wavelength (nm) -> QE [0,1] for blue channel

    // -- Temperature compensation ---------------------------------------------
    double tempRefC             = 25.0; ///< Reference temperature for dark current (Celsius)
    double darkCurrentGradient  = 0.0;  ///< Dark current change rate per degree Celsius

    // -- Serialization --------------------------------------------------------
    bool isCustom = false;              ///< true for user-defined profiles
};

/**
 * @brief Singleton database of built-in and user-registered sensor profiles.
 */
class SensorProfileDatabase {
public:
    static SensorProfileDatabase& instance();

    /**
     * @brief Load a profile by name.
     * @param name Profile name to look up.
     * @param out  Output profile (set to default if not found).
     * @return true if the named profile was found.
     */
    bool loadProfile(const QString& name, SensorProfile& out);

    /** Register a custom sensor profile. */
    void registerProfile(const SensorProfile& profile);

    /** Get the names of all available profiles. */
    std::vector<QString> listProfiles() const;

    /** Get a generic CCD profile with conservative default parameters. */
    static SensorProfile getDefault();

private:
    SensorProfileDatabase() = default;

    std::map<QString, SensorProfile> m_profiles;

    /** Populate the database with built-in sensor profiles. */
    void initBuiltins();
};

#endif // SENSORPROFILE_H