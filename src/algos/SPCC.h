#pragma once
/*
 * SPCC.h  —  Spectrophotometric Color Calibration
 *
 * Calibrates the white balance of a linear, demosaiced astrophotograph by
 * matching its measured star colours to synthetic photometric predictions
 * derived from a spectral library convolved with the camera's spectral
 * response (QE) curves.
 *
 * Algorithm outline
 * ─────────────────
 * 1. Detect and photometrically measure stars in the image
 *    (aperture photometry, ADU sums in each channel).
 * 2. Cross-match against an embedded APASS / Gaia DR3 colour catalogue
 *    (sparse plate-solved positions, or optional manual WCS).
 * 3. For each matched star compute predicted channel ratios:
 *       ratio_R = ∫ S(λ)·T_R(λ) dλ / ∫ S(λ)·T_G(λ) dλ
 *       ratio_B = ∫ S(λ)·T_B(λ) dλ / ∫ S(λ)·T_G(λ) dλ
 *    where S(λ) is a Pickles library spectrum chosen by star B-V colour.
 * 4. Compute a 3×3 (or diagonal) colour-correction matrix C by least-squares
 *    minimisation of ‖C · i_meas − i_pred‖².
 * 5. Optionally apply a white-reference correction (solar type G2V).
 * 6. Apply C to every pixel in the image.
 *
 * External data required (shipped with TStar):
 *   data/spcc/pickles_spectra.bin     — 131 Pickles UVK library spectra
 *   data/spcc/filter_responses.bin    — camera/filter response curves
 *   data/spcc/gaia_bv_catalogue.bin   — sparse Gaia colour catalogue
 *
 *  Author: TStar project
 */

#ifndef SPCC_H
#define SPCC_H

#include <vector>
#include <array>
#include <QString>
#include <QStringList>
#include "ImageBuffer.h"

// ─── Spectral response curve ──────────────────────────────────────────────────
struct SpectralResponse {
    QString    name;                   ///< e.g. "Canon EOS Ra", "Generic DSLR"
    std::vector<double> wavelength;   ///< nm
    std::vector<double> r, g, b;      ///< QE curves (0..1)
    double wl_min = 380.0, wl_max = 780.0, wl_step = 5.0;
};

// ─── Pickles stellar spectrum ─────────────────────────────────────────────────
struct PicklesSpectrum {
    double bv;                         ///< B-V colour index
    std::vector<double> flux;          ///< flux values (same grid as SpectralResponse)
};

// ─── Matched star record ──────────────────────────────────────────────────────
struct SPCCStar {
    double xImg, yImg;                 ///< image pixel position (subpixel)
    double r_adu, g_adu, b_adu;        ///< measured flux in each channel (ADU)
    double bv;                         ///< catalogue B-V (Gaia BP-RP → B-V approx)
    double pred_r, pred_g, pred_b;     ///< predicted flux ratios (theory)
    bool   used = false;               ///< included in final solve
};

// ─── SPCC parameters ─────────────────────────────────────────────────────────
struct SPCCParams {
    QString  cameraProfile    = "Generic DSLR";  ///< name from filter_responses.bin
    QString  filterProfile    = "Luminance";
    bool     useFullMatrix    = false;   ///< true=3×3, false=diagonal (2-scalar)
    double   minSNR           = 20.0;   ///< minimum star SNR to use
    int      maxStars         = 200;    ///< maximum stars to cross-match
    double   apertureR        = 4.0;    ///< aperture radius in pixels
    bool     limitMagnitude   = true;
    double   magLimit         = 13.5;   ///< catalogue faint limit
    bool     neutralBackground= true;   ///< subtract background before calibration
    bool     solarReference   = true;   ///< normalise to G2V (solar white point)
    QString  dataPath;                  ///< path to data/spcc/ directory
};

// ─── SPCC result ─────────────────────────────────────────────────────────────
struct SPCCResult {
    bool     success    = false;
    int      starsUsed  = 0;            ///< number of stars in final solve
    int      starsFound = 0;            ///< total stars detected
    double   residual   = 0.0;          ///< RMS residual of the colour correction
    double   corrMatrix[3][3] = {};     ///< applied 3×3 colour matrix
    double   scaleR = 1.0, scaleG = 1.0, scaleB = 1.0; ///< diagonal scales
    QString  errorMsg;
    QString  logMsg;                    ///< rich info for sidebar logger

    // Per-star diagnostics
    std::vector<SPCCStar> stars;
};

// ─── SPCC mirror configuration ─────────────────────────────────────────────────
struct SPCCMirror {
    QString url;
    QString description;
};

// ─── Main SPCC class ──────────────────────────────────────────────────────────
class SPCC {
public:
    // ── Data loading ──────────────────────────────────────────────────────
    static bool loadPicklesLibrary  (const QString& path,
                                     std::vector<PicklesSpectrum>& out);
    static bool loadSpectralResponse(const QString& path, const QString& name,
                                     SpectralResponse& out,
                                     const QString& filterName = QString());
    static bool loadGaiaCatalogue   (const QString& path,
                                     std::vector<std::array<double,3>>& radecBV);

    // ── Calibrate ─────────────────────────────────────────────────────────
    static SPCCResult calibrate(ImageBuffer& buf, const SPCCParams& p);

    // ── Utilities ─────────────────────────────────────────────────────────

    /// Predict R/G and B/G channel ratios for a star of given B-V
    static void predictRatios(double bv,
                               const std::vector<PicklesSpectrum>& lib,
                               const SpectralResponse& resp,
                               double& out_r, double& out_g, double& out_b);

    /// Convert Gaia BP-RP to approximate B-V
    static double bprpToBV(double bprp);

    /// List available camera profiles from filter_responses.bin
    static QStringList availableCameraProfiles(const QString& dataPath);
    static QStringList availableFilterProfiles (const QString& dataPath);

    /// Get hardcoded SPCC data mirrors
    static std::vector<SPCCMirror> getDataMirrors();

private:
    // ── Internal helpers ──────────────────────────────────────────────────

    struct ApertureResult { double r_adu, g_adu, b_adu, snr; };
    static ApertureResult aperturePhotometry(const ImageBuffer& buf,
                                             double cx, double cy, double radius);

    static bool solveColourMatrix(const std::vector<SPCCStar>& stars,
                                  bool fullMatrix,
                                  double mat[3][3]);

    static void applyColourMatrix(ImageBuffer& buf, const double mat[3][3]);

    static const PicklesSpectrum& picklesByBV(double bv,
                                              const std::vector<PicklesSpectrum>& lib);
};

#endif // SPCC_H
