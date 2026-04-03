/*
 * SPCC.h - Spectrophotometric Color Calibration
 *
 * Algorithm overview:
 *   1. Load all SEDs, filters, and sensor QE curves from tstar_data.fits
 *      (FITS binary tables, CTYPE = "SED" | "FILTER" | "SENSOR").
 *   2. Build a common wavelength grid (3000-11000 Angstrom, 1 Angstrom step)
 *      matching the reference Python implementation grid.
 *   3. Interpolate each curve onto that grid; combine as:
 *      T_sys = T_filter * T_QE * T_LP1 * T_LP2.
 *   4. Detect stars with aperture photometry (background-subtracted annulus method).
 *   5. For every matched SIMBAD star: integrate its Pickles SED against T_sys_R/G/B
 *      to obtain expected flux ratios (S_R/S_G, S_B/S_G).
 *   6. Gaia XP fallback: for stars lacking a Pickles match, use cached Gaia XP spectra
 *      integrated against the same throughput curves.
 *   7. Fit measured vs. expected ratios using three competing models (slope-only, affine,
 *      quadratic); select the model with the lowest RMS fractional residual.
 *   8. Apply per-pixel polynomial color mapping:
 *      R' = poly_R(R/G) * G, B' = poly_B(B/G) * G,
 *      anchored around the median pivot of each channel.
 *   9. Optionally remove a chromatic gradient (poly2 / poly3 / RBF surface fit in
 *      differential-magnitude space, clamped to +/-0.05 mag peak).
 */

#pragma once
#ifndef SPCC_H
#define SPCC_H

#include <vector>
#include <array>
#include <functional>
#include <memory>
#include <unordered_map>
#include <atomic>
#include <cstring>

#include <QString>
#include <QStringList>

#include "ImageBuffer.h"

// Forward declarations
struct CatalogStar;

// =============================================================================
// Wavelength grid constants
// Matches Python: wl_grid = np.arange(3000, 11001) [Angstroms, 1 Angstrom step]
// =============================================================================

static constexpr int    WL_GRID_MIN_AA   = 3000;   ///< Grid start wavelength (Angstrom)
static constexpr int    WL_GRID_MAX_AA   = 11000;  ///< Grid end wavelength (Angstrom)
static constexpr int    WL_GRID_LEN      = 8001;   ///< Total grid points: (11000 - 3000) + 1

/// Gaia XP sampled grid parameters (336-1020 nm, 2 nm step), used for XP-data convolution.
static constexpr int    XPSAMPLED_LEN      = 343;
static constexpr double XPSAMPLED_MIN_NM   = 336.0;
static constexpr double XPSAMPLED_MAX_NM   = 1020.0;
static constexpr double XPSAMPLED_STEP_NM  = 2.0;

// =============================================================================
// SPCCType - data type enumeration for database entries
// =============================================================================

enum SPCCType
{
    MONO_SENSOR = 1,  ///< Monochrome camera sensor QE curve
    OSC_SENSOR  = 2,  ///< OSC (color) camera sensor QE curve
    MONO_FILTER = 3,  ///< Narrowband or broadband filter (monochrome system)
    OSC_FILTER  = 4,  ///< Filter designed for OSC cameras
    OSC_LPF     = 5,  ///< Low-pass / IR-cut filter for OSC cameras
    WB_REF      = 6   ///< Stellar SED or white-balance reference spectrum
};

// =============================================================================
// SPCCObject - one spectral curve entry loaded from tstar_data.fits or JSON
//
// Arrays x (wavelength in Angstrom) and y (flux or throughput) are populated
// eagerly at load time and stored as std::vector<double>.
// =============================================================================

struct SPCCObject
{
    QString name;                   ///< EXTNAME value from the FITS header (or JSON "name" field)
    QString model;                  ///< Same as name; retained for API symmetry
    int     type          = 0;      ///< SPCCType enum value
    bool    arrays_loaded = false;  ///< True when x and y have been populated

    std::vector<double> x;  ///< Wavelength array in Angstrom (converted from raw units on load)
    std::vector<double> y;  ///< Flux [SED] or fractional throughput [FILTER / SENSOR]
};

// =============================================================================
// SPCCDataStore - complete in-memory spectral database
// =============================================================================

struct SPCCDataStore
{
    std::vector<SPCCObject> sed_list;     ///< CTYPE = "SED"    (Pickles library + galaxy templates)
    std::vector<SPCCObject> filter_list;  ///< CTYPE = "FILTER" (band-pass and LP filters)
    std::vector<SPCCObject> sensor_list;  ///< CTYPE = "SENSOR" (camera QE curves)
};

// =============================================================================
// PicklesMatch - result of mapping a SIMBAD spectral type to a Pickles SED name
// =============================================================================

struct PicklesMatch
{
    QString sedName;      ///< Matched Pickles SED EXTNAME; empty string if no match was found
    bool    valid = false;
};

// =============================================================================
// StarRecord - one SIMBAD star with image-plane coordinates and spectral info
// =============================================================================

struct StarRecord
{
    // Image-plane position (SEP centroid output)
    double x_img  = 0.0;  ///< Pixel column
    double y_img  = 0.0;  ///< Pixel row

    // Astrometric position
    double ra     = 0.0;  ///< ICRS right ascension (degrees)
    double dec    = 0.0;  ///< ICRS declination (degrees)

    // Source shape used for aperture radius scaling
    double semi_a = 1.5;  ///< SEP semi-major axis (pixels)

    // Spectral classification
    QString sp_type;        ///< Raw SIMBAD spectral type string (e.g. "G2V", "K0III")
    QString sp_clean;       ///< Leading letter class extracted from sp_type (e.g. "G")
    QString pickles_match;  ///< Best-matching Pickles SED EXTNAME

    // Gaia DR3 cross-match data
    qint64  gaia_source_id = 0;
    double  gaia_bp_rp     = std::numeric_limits<double>::quiet_NaN();  ///< Gaia BP-RP color index
    double  gaia_gmag      = std::numeric_limits<double>::quiet_NaN();  ///< Gaia G-band magnitude
};

// =============================================================================
// PhotometryResult - output of background-subtracted annulus aperture photometry
//
// Mirrors Python measure_star_rgb_photometry():
//   mu_bg    = ann_sum / ann_area
//   star_sum = raw_sum - mu_bg * ap_area
// =============================================================================

struct PhotometryResult
{
    double R_star = 0.0;   ///< Background-subtracted star flux, red channel
    double G_star = 0.0;   ///< Background-subtracted star flux, green channel
    double B_star = 0.0;   ///< Background-subtracted star flux, blue channel

    double R_bg   = 0.0;   ///< Estimated background level per pixel, red channel
    double G_bg   = 0.0;   ///< Estimated background level per pixel, green channel
    double B_bg   = 0.0;   ///< Estimated background level per pixel, blue channel

    bool   valid  = false; ///< True only when all three star fluxes are finite and positive
};

// =============================================================================
// EnrichedMatch - one successfully processed star from the main matching loop
// =============================================================================

struct EnrichedMatch
{
    // Image position
    double x_img, y_img;

    // Measured star-only flux per channel (from aperture photometry)
    double R_meas, G_meas, B_meas;

    // Expected synthetic flux integrals (SED convolved with T_sys per channel)
    double S_star_R, S_star_G, S_star_B;

    // Color ratios used for model fitting
    double exp_RG,  exp_BG;   ///< Expected ratios: S_R/S_G and S_B/S_G
    double meas_RG, meas_BG;  ///< Measured ratios: R_meas/G_meas and B_meas/G_meas

    // Aperture geometry used for this star
    double r_ap, r_in, r_out;

    bool used_gaia = false;   ///< True if Gaia XP spectrum was used instead of Pickles SED
};

// =============================================================================
// CalibrationModel - selected polynomial model and its fitted coefficients
//
// Coefficient layout: coeff[0]*x^2 + coeff[1]*x + coeff[2]
//   slope-only:  coeff = {0, slope, 0}
//   affine:      coeff = {0, slope, intercept}
//   quadratic:   coeff = {a, b, c}
// =============================================================================

enum ModelKind
{
    MODEL_SLOPE_ONLY = 0,
    MODEL_AFFINE     = 1,
    MODEL_QUADRATIC  = 2
};

struct CalibrationModel
{
    ModelKind kind      = MODEL_AFFINE;

    double coeff_R[3]   = {0.0, 1.0, 0.0};  ///< Polynomial coefficients for the R/G ratio model
    double coeff_B[3]   = {0.0, 1.0, 0.0};  ///< Polynomial coefficients for the B/G ratio model
    double rms_total    = 0.0;               ///< Combined RMS fractional residual for model selection
};

// =============================================================================
// GradientSurface - per-channel multiplicative correction surface
//
// Computed in differential-magnitude space and converted to linear scale factors.
// Applied as: corrected[ch] = img[ch] / scale[ch]
// =============================================================================

struct GradientSurface
{
    std::vector<float> R, G, B;  ///< Per-pixel multiplicative scale maps, size = width * height
    int  width  = 0;
    int  height = 0;
    bool valid  = false;
};

// =============================================================================
// SPCCParams - all user-configurable inputs from the dialog
// =============================================================================

struct SPCCParams
{
    // -------------------------------------------------------------------------
    // Equipment selections (names must match EXTNAME values in tstar_data.fits)
    // -------------------------------------------------------------------------
    QString whiteRef;    ///< Reference SED EXTNAME (e.g. "A0V", "G2V")
    QString rFilter;     ///< Filter EXTNAME for the R channel; "(None)" = flat unity response
    QString gFilter;     ///< Filter EXTNAME for the G channel
    QString bFilter;     ///< Filter EXTNAME for the B channel
    QString sensor;      ///< Sensor QE EXTNAME; "(None)" = flat unity response
    QString lpFilter1;   ///< LP / IR-cut filter 1 EXTNAME; "(None)" = flat unity response
    QString lpFilter2;   ///< LP / IR-cut filter 2 EXTNAME; "(None)" = flat unity response

    // -------------------------------------------------------------------------
    // Processing options
    // -------------------------------------------------------------------------
    QString bgMethod;              ///< Background model: "None" | "Simple" | "Poly2" | "Poly3" | "RBF"
    QString dataPath;              ///< Directory containing tstar_data.fits and the JSON database

    double  sepThreshold  = 5.0;  ///< SEP source detection threshold in sigma above background
    int     maxStars      = 300;  ///< Maximum number of SEP detections to process
    bool    gaiaFallback  = true; ///< Use Gaia XP spectra for stars without a Pickles match
    bool    useFullMatrix = true; ///< Estimate full 3x3 correction matrix; false = diagonal only
    bool    linearMode    = true; ///< True = apply global per-channel multipliers;
                                  ///< False = apply non-linear polynomial per pixel

    // -------------------------------------------------------------------------
    // Optional chromatic gradient extraction
    // -------------------------------------------------------------------------
    bool    runGradient      = false;    ///< Enable gradient surface computation and application
    QString gradientMethod   = "poly3"; ///< Surface fit method: "poly2" | "poly3" | "rbf"

    // -------------------------------------------------------------------------
    // Runtime callbacks
    // -------------------------------------------------------------------------
    std::function<void(int, const QString&)> progressCallback;  ///< Progress reporter: (percent, message)
    std::atomic<bool>*                       cancelFlag = nullptr; ///< Set to true to request cancellation
};

// =============================================================================
// SPCCResult - output bundle returned by the calibration pipeline
// =============================================================================

struct SPCCResult
{
    bool    success   = false;  ///< True when calibration completed without error
    QString error_msg;          ///< Human-readable error description (non-empty on failure)
    QString log_msg;            ///< Informational log text for display in the dialog

    int     stars_found = 0;    ///< Total stars received from the catalog / star-list input
    int     stars_used  = 0;    ///< Stars that survived photometry quality cuts and ratio fitting
    double  residual    = 0.0;  ///< Combined RMS fractional residual of the chosen model

    // Scale factors evaluated at the reference point (x = 1.0) for display
    double scaleR = 1.0;
    double scaleG = 1.0;
    double scaleB = 1.0;

    // 3x3 correction matrix (diagonal elements only when !useFullMatrix)
    double corrMatrix[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

    // White-balance multipliers retained for backward compatibility
    double white_balance_k[3] = {1.0, 1.0, 1.0};

    // Calibrated output image (shares pixel buffer with the modified working copy)
    std::shared_ptr<ImageBuffer> modifiedBuffer = nullptr;

    // Per-star diagnostic data for the residual scatter plot
    struct DiagStar
    {
        double x_img, y_img;
        double meas_RG, meas_BG;
        double exp_RG,  exp_BG;
        bool   is_inlier = true;
    };
    std::vector<DiagStar> diagnostics;

    CalibrationModel model;  ///< The polynomial model that was selected and applied
};

// =============================================================================
// XPSampled - Gaia XP-sampled spectral container
//
// Covers 336-1020 nm at 2 nm steps (343 samples).
// Used when integrating Gaia XP spectra against the system throughput curves.
// =============================================================================

struct XPSampled
{
    double wl[XPSAMPLED_LEN]   = {};  ///< Wavelength grid in nm
    double flux[XPSAMPLED_LEN] = {};  ///< Spectral flux values

    XPSampled()
    {
        for (int i = 0; i < XPSAMPLED_LEN; ++i)
            wl[i] = XPSAMPLED_MIN_NM + i * XPSAMPLED_STEP_NM;
    }
};

// =============================================================================
// SPCC - Spectrophotometric Color Calibration
//
// All public methods are static. The dialog invokes calibrateWithStarList()
// (or the legacy calibrateWithCatalog() wrapper) via QtConcurrent::run().
// =============================================================================

class SPCC
{
public:

    // =========================================================================
    // Database I/O
    // =========================================================================

    /// Load all binary-table HDUs from <dataPath>/tstar_data.fits into an SPCCDataStore.
    /// CTYPE header key routes each HDU:
    ///   "SED"    -> out.sed_list
    ///   "FILTER" -> out.filter_list
    ///   "SENSOR" -> out.sensor_list
    /// Wavelengths are converted to Angstrom when they appear to be in nm.
    static bool loadTStarFits(const QString& dataPath, SPCCDataStore& out);

    /// Load the TStar JSON database from a directory tree.
    /// Expected subdirectories: mono_filters, mono_sensors, osc_filters, osc_sensors, wb_refs.
    static bool loadTStarDatabase(const QString& dbPath, SPCCDataStore& out);

    /// Return a sorted list of SED names in the data store (for populating combo boxes).
    static QStringList availableSEDs(const SPCCDataStore& store);

    /// Return a sorted list of filter names in the data store.
    static QStringList availableFilters(const SPCCDataStore& store);

    /// Return a sorted list of sensor names in the data store.
    static QStringList availableSensors(const SPCCDataStore& store);

    // =========================================================================
    // Main calibration entry points
    // =========================================================================

    /// Full calibration pipeline. Called from SPCCDialog via QtConcurrent::run().
    /// @param buf         Input image (RGB float32 in [0,1]).
    /// @param params      User-chosen equipment selections and processing options.
    /// @param starRecords SIMBAD star list with pixel coordinates and spectral types,
    ///                    produced by the "Fetch Stars" step in the dialog.
    static SPCCResult calibrateWithStarList(const ImageBuffer& buf,
                                            const SPCCParams&  params,
                                            const std::vector<StarRecord>& starRecords);

    /// Legacy entry point retained for compatibility with existing dialog code.
    /// Converts CatalogStar records to StarRecords and delegates to calibrateWithStarList().
    static SPCCResult calibrateWithCatalog(const ImageBuffer& buf,
                                           const SPCCParams&  params,
                                           const std::vector<CatalogStar>& stars);

    // =========================================================================
    // Spectral utilities (public for diagnostics and the spectrum viewer)
    // =========================================================================

    /// Interpolate a raw spectral curve onto the common wavelength grid using linear
    /// interpolation. Points outside the input wavelength range are set to 0.
    /// Mirrors Python: np.interp(wl_grid, wl_o, tp_o, left=0.0, right=0.0).
    /// @param wl_aa  Input wavelengths in Angstrom (arbitrary sampling, must be sorted ascending).
    /// @param vals   Throughput or flux values corresponding to wl_aa.
    /// @param out    Output array of length WL_GRID_LEN (3000-11000 Angstrom, 1 Angstrom step).
    static void interpolateToGrid(const std::vector<double>& wl_aa,
                                  const std::vector<double>& vals,
                                  double out[WL_GRID_LEN]);

    /// Trapezoidal integral of the element-wise product f[i] * g[i] on the common grid
    /// (uniform 1 Angstrom spacing).
    /// Mirrors Python: np.trapz(f * T_sys, x=wl_grid).
    static double trapz(const double f[WL_GRID_LEN], const double g[WL_GRID_LEN]);

    /// Build the combined system throughput curve:
    ///   T_sys = T_filter * T_sensor * T_LP1 * T_LP2
    /// Any component whose name is "(None)" or empty is treated as a flat response of 1.
    /// The channel string (e.g. "Red") is used to resolve per-channel curve variants.
    static void buildSystemThroughput(const SPCCDataStore& store,
                                      const QString& filterName,
                                      const QString& sensorName,
                                      const QString& lp1Name,
                                      const QString& lp2Name,
                                      const QString& channel,
                                      double T_sys[WL_GRID_LEN]);

    // =========================================================================
    // Pickles spectral type matching
    // =========================================================================

    /// Map a SIMBAD spectral type string to a ranked list of matching Pickles SED names.
    /// Matching is performed in four priority tiers (letter + digit + luminosity,
    /// letter + digit, letter + luminosity nearest digit, letter only).
    /// Returns an empty list when no match can be found.
    static QStringList picklesMatchForSimbad(const QString&    simbadSp,
                                             const QStringList& availableSEDs);

    /// Infer an approximate Pickles Main Sequence spectral type (e.g. "G2V")
    /// from a Gaia DR3 BP-RP color index.
    static QString inferTypeFromBpRp(double bp_rp);

    /// Approximate the Gaia BP-RP color index corresponding to a given spectral type string.
    static double bpRpFromType(const QString& spec);

    // =========================================================================
    // Aperture photometry
    // =========================================================================

    /// Background-subtracted circular aperture photometry on an RGB float32 image.
    /// Mirrors Python measure_star_rgb_photometry():
    ///   mu_bg    = ann_sum / ann_area
    ///   star_sum = raw_sum - mu_bg * ap_area
    /// @param img_float  Packed RGB float32 image, row-major, values in [0, 1].
    ///                   Layout: R0 G0 B0 R1 G1 B1 ...
    /// @param width      Image width in pixels.
    /// @param height     Image height in pixels.
    /// @param cx, cy     Aperture centre coordinates (pixels).
    /// @param r          Aperture radius (pixels).
    /// @param r_in       Inner annulus radius (pixels).
    /// @param r_out      Outer annulus radius (pixels).
    static PhotometryResult aperturePhotometry(const float* img_float,
                                               int    width, int height,
                                               double cx,    double cy,
                                               double r,     double r_in, double r_out);

    // =========================================================================
    // Calibration model selection
    // =========================================================================

    /// Fit three competing polynomial models to the (measured, expected) color ratio pairs
    /// and return the model with the lowest combined RMS fractional residual.
    /// Models evaluated:
    ///   0 - slope-only:  meas = slope * exp
    ///   1 - affine:      meas = slope * exp + intercept
    ///   2 - quadratic:   meas = a * exp^2 + b * exp + c  (requires n >= 6)
    /// Mirrors the Python model-selection block in run_spcc().
    static CalibrationModel fitColorModel(const std::vector<double>& meas_RG,
                                          const std::vector<double>& exp_RG,
                                          const std::vector<double>& meas_BG,
                                          const std::vector<double>& exp_BG);

    // =========================================================================
    // Image correction
    // =========================================================================

    /// Apply the chosen polynomial color model to every pixel of the image in-place.
    /// Correction formula:
    ///   R' = pivot_avg + (R - pivot_R) * mR
    ///   B' = pivot_avg + (B - pivot_B) * mB
    ///   G' = pivot_avg + (G - pivot_G)
    /// Multipliers are derived from the model coefficients and clamped to [0.25, 4.0].
    /// Output values are clamped to [0, 1].
    static void applyColorModel(float* img_float,
                                int    width, int height,
                                const CalibrationModel& model,
                                double pivot_R, double pivot_G, double pivot_B);

    // =========================================================================
    // Chromatic gradient extraction and removal
    // =========================================================================

    /// Compute a per-channel multiplicative correction surface using the
    /// differential-magnitude approach. The surface peak amplitude is clamped
    /// to max_allowed_mag magnitudes (default 0.05 mag).
    /// Mirrors Python run_gradient_extraction().
    static GradientSurface computeGradientSurface(
            const float* img_float,
            int width, int height,
            const std::vector<EnrichedMatch>& matches,
            const double T_sys_R[WL_GRID_LEN],
            const double T_sys_G[WL_GRID_LEN],
            const double T_sys_B[WL_GRID_LEN],
            const QString& method,
            double max_allowed_mag = 0.05);

    /// Apply a gradient correction surface to img_float in-place.
    /// corrected[ch] = img[ch] / max(scale[ch], 1e-8), clamped to [0, 1].
    static void applyGradientSurface(float* img_float, int width, int height,
                                     const GradientSurface& surf);

    // =========================================================================
    // Miscellaneous utilities
    // =========================================================================

    /// Detect whether the wavelength array is in nm and convert to Angstrom in-place.
    /// Conversion is applied when the median value falls in the range [250, 2000].
    /// Mirrors Python _ensure_angstrom().
    static bool detectAndConvertToAngstrom(std::vector<double>& wl);

private:

    // =========================================================================
    // Internal helpers (not part of the public API)
    // =========================================================================

    /// Compute the combined RMS fractional residual: sqrt(mean(((pred / exp) - 1)^2)).
    static double rmsFrac(const std::vector<double>& pred,
                          const std::vector<double>& exp_vals);

    /// Evaluate a second-degree polynomial: coeff[0]*x^2 + coeff[1]*x + coeff[2].
    static inline double polyEval(const double coeff[3], double x)
    {
        return coeff[0] * x * x + coeff[1] * x + coeff[2];
    }

    /// Least-squares fit of a quadratic polynomial y = a*x^2 + b*x + c.
    /// Returns false when fewer than three data points are provided.
    static bool polyFit2(const std::vector<double>& x,
                         const std::vector<double>& y,
                         double& a, double& b, double& c);

    /// Fit a second-degree polynomial surface z = f(x, y) to scattered point data
    /// and evaluate it on the full pixel grid.
    static std::vector<double> fitPoly2Surface(
            const std::vector<std::array<double, 2>>& pts,
            const std::vector<double>& vals,
            int width, int height);

    /// Fit a third-degree polynomial surface z = f(x, y) to scattered point data
    /// and evaluate it on the full pixel grid.
    static std::vector<double> fitPoly3Surface(
            const std::vector<std::array<double, 2>>& pts,
            const std::vector<double>& vals,
            int width, int height);

    /// Trapezoidal integral on a generic non-uniform 1D grid.
    static double trapzGeneric(const double* f, const double* x, int n);

    /// Compute the median of a vector (operates on a copy; safe to call with temporaries).
    static double vectorMedian(std::vector<double> v);

    /// Linear search for a curve by name in a list. Returns nullptr if not found.
    static const SPCCObject* findCurve(const std::vector<SPCCObject>& list,
                                       const QString& name);
};

#endif // SPCC_H