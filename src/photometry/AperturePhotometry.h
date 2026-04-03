#pragma once

// =============================================================================
// AperturePhotometry.h
//
// Aperture photometry module providing PSF fitting and circular aperture
// flux measurement with sky annulus background subtraction.
//
// Structures:
//   ApertureConfig   - Photometry configuration parameters
//   PhotometryResult - Measured flux, magnitude, and error
//   PSFResult        - Point spread function fitting output
//
// Class:
//   AperturePhotometry - Combines PSF centroiding with aperture flux extraction
// =============================================================================

#include <vector>
#include <cmath>
#include <memory>

#include "PsfFitter.h"

// =============================================================================
// ApertureConfig - Aperture photometry configuration parameters
// =============================================================================

struct ApertureConfig {
    double aperture            = 8.0;   // Fixed aperture radius (pixels)
    double inner               = 14.0;  // Sky annulus inner radius (pixels)
    double outer               = 24.0;  // Sky annulus outer radius (pixels)
    double auto_aperture_factor = 2.0;  // Multiplier applied to FWHM for automatic aperture sizing
    bool   force_radius        = false; // When true, use fixed aperture; otherwise derive from FWHM
    double gain                = 1.0;   // Detector gain (e-/ADU) for noise estimation
    double minval              = 0.0;   // Minimum valid pixel value
    double maxval              = 1.0;   // Maximum valid pixel value (saturation ceiling)
};

// =============================================================================
// PhotometryResult - Output of aperture photometry measurement
// =============================================================================

struct PhotometryResult {
    double mag       = 0.0;   // Instrumental magnitude (-2.5 * log10(flux))
    double mag_error = 0.0;   // Magnitude uncertainty (1.0857 * noise / signal)
    double snr       = 0.0;   // Signal-to-noise ratio in decibels
    double flux      = 0.0;   // Linear flux: 10^(-0.4 * mag)
    bool   valid     = false; // True if measurement passed all validity checks
};

// =============================================================================
// PSFResult - Point spread function fitting output
//
// Wraps the low-level PsfStar result with absolute image coordinates
// and provides a compatibility layer for upstream callers.
// =============================================================================

struct PSFResult {
    double x0         = 0.0;  // Centroid X in absolute image coordinates
    double y0         = 0.0;  // Centroid Y in absolute image coordinates
    double fwhmx      = 0.0;  // Full width at half maximum along X (pixels)
    double fwhmy      = 0.0;  // Full width at half maximum along Y (pixels)
    double amplitude  = 0.0;  // Peak amplitude above background (A from PSF fit)
    double background = 0.0;  // Local background level (B from PSF fit)
    double rmse       = 0.0;  // Root mean square error of the PSF fit residual
    double beta       = -1.0; // Moffat beta parameter (-1 indicates Gaussian profile)
    bool   valid      = false;

    std::shared_ptr<PsfStar> psf; // Full PSF fit result; may be null if fallback was used
};

// =============================================================================
// AperturePhotometry - PSF fitting and aperture flux measurement
// =============================================================================

class AperturePhotometry {
public:
    AperturePhotometry();

    // Set the configuration for subsequent measurements.
    void setConfig(const ApertureConfig& cfg);

    // Fit a 2-D PSF model to a star at the given integer pixel position.
    // Returns sub-pixel centroid, FWHM, and amplitude in a PSFResult.
    PSFResult fitPSF(const float* data, int width, int height, int channels,
                     int channel, int starX, int starY, int boxRadius = 10);

    // Perform circular aperture photometry using a previously obtained PSFResult
    // for the centroid position. Sky background is estimated from an annulus.
    PhotometryResult measure(const float* data, int width, int height, int channels,
                             int channel, const PSFResult& psf);

    // Convenience method: fit PSF then measure aperture photometry in one call.
    PhotometryResult measureStar(const float* data, int width, int height, int channels,
                                 int channel, int starX, int starY);

private:
    ApertureConfig m_config;

    // Compute a robust location estimate using the Hampel M-estimator.
    // Optionally returns the standard error of the mean via stdev.
    double robustMean(std::vector<double>& data, double* stdev = nullptr);

    // Convert integrated signal intensity to instrumental magnitude.
    static double getMagnitude(double intensity);

    // Compute magnitude error and signal-to-noise ratio from noise components.
    static double getMagError(double intensity, double area, int nsky,
                              double skysig, double gain, double* snr = nullptr);
};