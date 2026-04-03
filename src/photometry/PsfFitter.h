#pragma once
#ifndef PSFFITTER_H
#define PSFFITTER_H

// =============================================================================
// PsfFitter.h
//
// Point Spread Function (PSF) fitting module using GSL nonlinear least squares.
//
// Supported models:
//   Gaussian - 7-parameter 2-D elliptical Gaussian with rotation
//              Parameters: B, A, x0, y0, SX, fc, alpha
//   Moffat   - 8-parameter 2-D elliptical Moffat with free beta exponent
//              Parameters: B, A, x0, y0, SX, fc, alpha, fbeta
//
// Parameter encoding uses trigonometric transforms to enforce bounds:
//   SX    = |fit[4]|                                  (positive definite)
//   r     = 0.5 * (cos(fit[5]) + 1)                  (roundness in [0, 1])
//   SY    = r^2 * SX
//   alpha = fit[6]                                    (rotation angle, radians)
//   beta  = BETA_MAX * 0.5 * (cos(fit[7]) + 1)       (Moffat beta in [0, BETA_MAX])
// =============================================================================

#include <cmath>
#include <vector>
#include <cstddef>

// =============================================================================
// Mathematical constants
// =============================================================================

#define PSF_2_SQRT_2_LOG2    2.35482004503      // 2 * sqrt(2 * ln(2)), Gaussian FWHM/sigma ratio
#define PSF_INV_4_LOG2       0.360673760222241  // 1 / (4 * ln(2)), inverse conversion
#define PSF_MOFFAT_BETA_MAX  10.0               // Upper bound for Moffat beta parameter

// =============================================================================
// PSF profile type selector
// =============================================================================

enum class PsfProfile {
    Gaussian, // 7-parameter fit: B A x0 y0 SX fc alpha
    Moffat    // 8-parameter fit: B A x0 y0 SX fc alpha fbeta
};

// =============================================================================
// PSF fitting error codes
// =============================================================================

enum class PsfError {
    OK              = 0,
    Alloc           = 3,   // Memory allocation failure
    Unsupported     = 4,   // Unsupported profile or configuration
    Diverged        = 5,   // Solver diverged or produced invalid results
    OutOfWindow     = 6,   // Star position outside fitting window
    InnerTooSmall   = 7,   // Inner aperture too small
    ApertureTooSmall= 8,   // Aperture region too small
    TooFewBgPix     = 9,   // Insufficient background pixels
    MeanFailed      = 10,  // Background mean estimation failed
    InvalidStdErr   = 11,  // Invalid standard error in covariance
    InvalidPixVal   = 12,  // Invalid pixel value encountered
    WindowTooSmall  = 13,  // Fitting window has fewer pixels than parameters
    InvalidImage    = 14,  // Input data buffer is too small
    FluxRatio       = 15   // Flux ratio out of acceptable range
};

// =============================================================================
// PsfStar - Result of a single PSF fit
// =============================================================================

struct PsfStar {
    PsfProfile profile = PsfProfile::Gaussian;

    // Fitted model parameters
    double B     = 0.0;   // Background level
    double A     = 0.0;   // Peak amplitude above background
    double x0    = 0.0;   // Centroid X (local box coordinates)
    double y0    = 0.0;   // Centroid Y (local box coordinates)
    double sx    = 0.0;   // Sigma X (Gaussian) or scale radius Ro_X (Moffat)
    double sy    = 0.0;   // Sigma Y (Gaussian) or scale radius Ro_Y (Moffat)
    double fwhmx = 0.0;   // Full width at half maximum along X (pixels)
    double fwhmy = 0.0;   // Full width at half maximum along Y (pixels)
    double angle = 0.0;   // Rotation angle of the major axis (degrees)
    double rmse  = 0.0;   // Root mean square fit residual
    double beta  = -1.0;  // Moffat beta exponent (-1 indicates Gaussian)
    double mag   = 0.0;   // Instrumental magnitude estimate

    // Relative uncertainties from covariance matrix diagonal
    double B_err    = 0.0, A_err    = 0.0;
    double x_err    = 0.0, y_err    = 0.0;
    double sx_err   = 0.0, sy_err   = 0.0;
    double ang_err  = 0.0, beta_err = 0.0;

    // Absolute position in full-image coordinates (set by the caller)
    double xpos = 0.0;
    double ypos = 0.0;

    bool has_saturated = false; // True if saturated pixels were present in the box
    int  R     = 0;             // Box half-radius used for the fit
    int  layer = 0;             // Image layer/channel index
};

// =============================================================================
// PsfFitData - Internal data structure passed to the GSL solver
// =============================================================================

struct PsfFitData {
    size_t  n;      // Number of unmasked (valid) pixels
    double* y;      // Observed pixel values for valid pixels (length n)
    size_t  NbRows; // Box height (rows)
    size_t  NbCols; // Box width (columns)
    double  rmse;   // Updated by the residual function after each evaluation
    int*    mask;   // Pixel mask: 1 = valid, 0 = excluded (saturated)
};

// =============================================================================
// PsfFitter - Static PSF fitting interface
// =============================================================================

class PsfFitter {
public:
    PsfFitter()  = default;
    ~PsfFitter() = default;

    // -------------------------------------------------------------------------
    // fit() - Fit a 2-D PSF model to a data sub-image.
    //
    // Parameters:
    //   data        : Row-major pixel array of size NbRows * NbCols
    //   NbRows      : Sub-image height
    //   NbCols      : Sub-image width
    //   background  : Pre-estimated background value
    //   sat         : Saturation level; pixels >= sat are excluded from the fit
    //   convergence : Iteration scaling factor (1 = default, 2 = relaxed)
    //   fromPeaker  : If true, the center pixel is assumed to be the peak
    //   profile     : PSF model to fit (Gaussian or Moffat)
    //   error       : Optional output error code
    //
    // Returns:
    //   Heap-allocated PsfStar on success; nullptr on failure.
    //   Caller is responsible for deallocation (use PsfFitter::free()).
    // -------------------------------------------------------------------------

    static PsfStar* fit(const double* data,
                        size_t NbRows, size_t NbCols,
                        double background, double sat,
                        int convergence          = 1,
                        bool fromPeaker          = true,
                        PsfProfile profile       = PsfProfile::Gaussian,
                        PsfError* error          = nullptr);

    // Convenience wrapper accepting a std::vector for the data buffer.
    static PsfStar* fitMatrix(const std::vector<double>& data,
                              size_t NbRows, size_t NbCols,
                              double background, double sat,
                              int convergence    = 1,
                              bool fromPeaker    = true,
                              PsfProfile profile = PsfProfile::Gaussian,
                              PsfError* error    = nullptr);

    // Deallocate a heap-allocated PsfStar returned by fit() or fitMatrix().
    static void free(PsfStar* s) { delete s; }

    // FWHM conversion utilities
    static double fwhm_from_s(double s, double beta, PsfProfile profile);
    static double s_from_fwhm(double fwhm, double beta, PsfProfile profile);

private:
    // Estimate initial parameter values from the data sub-image.
    static bool initParams(const double* data, size_t NbRows, size_t NbCols,
                           double bg, bool fromPeaker,
                           double* A_out, double* x0_out, double* y0_out,
                           double* fwhmX_out, double* fwhmY_out,
                           double* angle_out);

    // Compute an approximate instrumental magnitude from summed flux.
    static double getMag(const double* data, size_t NbRows, size_t NbCols,
                         double B);
};

#endif // PSFFITTER_H