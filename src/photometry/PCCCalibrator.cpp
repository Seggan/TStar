// =============================================================================
// PCCCalibrator.cpp
//
// Implementation of the Photometric Color Calibration engine.
//
// Color model: converts stellar effective temperature (from B-V or Gaia
// Teff) to linear sRGB via the Planckian locus (CIE xyY) and the standard
// sRGB color matrix.  Calibration factors are derived by comparing measured
// image flux ratios against catalog-predicted color ratios using robust
// linear regression (repeated median fit).
// =============================================================================

#include "PCCCalibrator.h"
#include "AperturePhotometry.h"

#include <cmath>
#include <algorithm>

#include <QString>
#include <QStringList>

#include <gsl/gsl_fit.h>
#include <gsl/gsl_statistics.h>

#include "../core/RobustStatistics.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// =============================================================================
// Unit conversion constants
// =============================================================================

static constexpr double DEG2RAD = M_PI / 180.0;
static constexpr double RAD2DEG = 180.0 / M_PI;

// =============================================================================
// Construction
// =============================================================================

PCCCalibrator::PCCCalibrator()
{
    // Initialize to identity WCS (no transformation).
    m_crval1 = 0.0; m_crval2 = 0.0;
    m_crpix1 = 0.0; m_crpix2 = 0.0;
    m_cd11   = 1.0; m_cd12   = 0.0;
    m_cd21   = 0.0; m_cd22   = 1.0;
}

// =============================================================================
// WCS configuration
// =============================================================================

void PCCCalibrator::setWCS(double crval1, double crval2,
                           double crpix1, double crpix2,
                           double cd1_1, double cd1_2,
                           double cd2_1, double cd2_2)
{
    m_crval1 = crval1; m_crval2 = crval2;
    m_crpix1 = crpix1; m_crpix2 = crpix2;
    m_cd11   = cd1_1;  m_cd12   = cd1_2;
    m_cd21   = cd2_1;  m_cd22   = cd2_2;
}

// =============================================================================
// SIP distortion coefficient parsing
//
// Accepts a map of string keys in the form "X_i_j" where X is one of
// {A, B, AP, BP} and i, j are polynomial power indices.
// =============================================================================

void PCCCalibrator::setSIP(int a_order, int b_order,
                           int ap_order, int bp_order,
                           const std::map<std::string, double>& coeffs)
{
    m_sipOrderA  = a_order;
    m_sipOrderB  = b_order;
    m_sipOrderAP = ap_order;
    m_sipOrderBP = bp_order;

    m_sipA.clear();  m_sipB.clear();
    m_sipAP.clear(); m_sipBP.clear();

    for (const auto& [key, val] : coeffs) {
        QString k = QString::fromStdString(key);
        QStringList parts = k.split('_');

        if (parts.size() >= 3) {
            bool ok1, ok2;
            int i = parts[1].toInt(&ok1);
            int j = parts[2].toInt(&ok2);

            if (ok1 && ok2) {
                if      (parts[0] == "A")  m_sipA[{i, j}]  = val;
                else if (parts[0] == "B")  m_sipB[{i, j}]  = val;
                else if (parts[0] == "AP") m_sipAP[{i, j}] = val;
                else if (parts[0] == "BP") m_sipBP[{i, j}] = val;
            }
        }
    }

    m_useSip = (!m_sipA.empty()  || !m_sipB.empty() ||
                !m_sipAP.empty() || !m_sipBP.empty());
}

// =============================================================================
// SIP polynomial evaluation
//
// Computes sum_ij( coeff[i,j] * u^i * v^j ) for all stored coefficients.
// =============================================================================

double PCCCalibrator::calculateSIP(
    double u, double v,
    const std::map<std::pair<int,int>, double>& coeffs,
    [[maybe_unused]] int order) const
{
    double sum = 0.0;

    for (const auto& [p, val] : coeffs) {
        double term = val;

        if      (p.first == 1) term *= u;
        else if (p.first > 1)  term *= std::pow(u, p.first);

        if      (p.second == 1) term *= v;
        else if (p.second > 1)  term *= std::pow(v, p.second);

        sum += term;
    }

    return sum;
}

// =============================================================================
// Color temperature and CIE colorimetry utilities
// =============================================================================

// Convert Johnson B-V color index to effective temperature (Kelvin).
// Ballesteros (2012) formula for main-sequence stars.
static float BV_to_T(float bv)
{
    return 4600.0f * (1.0f / (0.92f * bv + 1.7f) + 1.0f / (0.92f * bv + 0.62f));
}

// Compute CIE 1931 chromaticity (x, y) from color temperature (Kelvin)
// using Kim's cubic spline approximation for the Planckian locus.
// Reference: https://en.wikipedia.org/wiki/Planckian_locus
static void temp_to_xyY(double t, double& x, double& y)
{
    // CIE x chromaticity coordinate
    if (t < 1667.0) {
        x = 0.0;
    } else if (t < 4000.0) {
        x = (-0.2661239e9 / (t*t*t)) - (0.2343589e6 / (t*t))
            + (0.8776956e3 / t) + 0.179910;
    } else if (t < 25000.0) {
        x = (-3.0258469e9 / (t*t*t)) + (2.1070379e6 / (t*t))
            + (0.2226347e3 / t) + 0.240390;
    } else {
        x = 0.0;
    }

    // CIE y chromaticity coordinate
    if (t < 1667.0) {
        y = 0.0;
    } else if (t < 2222.0) {
        y = (-1.1063814*x*x*x) - (1.34811020*x*x) + (2.18555832*x) - 0.20219683;
    } else if (t < 4000.0) {
        y = (-0.9549476*x*x*x) - (1.37418593*x*x) + (2.09137015*x) - 0.16748867;
    } else if (t < 25000.0) {
        y = (3.0817580*x*x*x) - (5.87338670*x*x) + (3.75112997*x) - 0.37001483;
    } else {
        y = 0.0;
    }
}

// Convert CIE chromaticity (x, y) to CIE XYZ with Y = 1 (white point).
static void xyY_to_XYZ(double x, double y, double& X, double& Y_out, double& Z)
{
    if (y < 1e-9) {
        X = Y_out = Z = 0.0;
        return;
    }
    Y_out = 1.0;
    X     = x / y;
    Z     = (1.0 - x - y) / y;
}

// Convert CIE XYZ to linear sRGB using the standard sRGB matrix (D65 illuminant).
static void XYZ_to_sRGB(double X, double Y, double Z,
                         float& r, float& g, float& b)
{
    r = static_cast<float>( 3.2404542 * X - 1.5371385 * Y - 0.4985314 * Z);
    g = static_cast<float>(-0.9692660 * X + 1.8760108 * Y + 0.0415560 * Z);
    b = static_cast<float>( 0.0556434 * X - 0.2040259 * Y + 1.0572252 * Z);
}

// Convert color temperature to normalized linear sRGB via the Planckian locus.
// The output is clamped to [0, 1] and normalized so that the brightest
// channel equals 1.0.
static void temp_to_rgb(float T, float& r, float& g, float& b)
{
    double x = 0.0, y = 0.0;
    double X = 0.0, Y = 0.0, Z = 0.0;

    temp_to_xyY(static_cast<double>(T), x, y);

    if (x == 0.0 && y == 0.0) {
        r = g = b = 1.0f;
        return;
    }

    xyY_to_XYZ(x, y, X, Y, Z);
    XYZ_to_sRGB(X, Y, Z, r, g, b);

    // Clamp negative values (out-of-gamut temperatures)
    r = std::max(0.0f, r);
    g = std::max(0.0f, g);
    b = std::max(0.0f, b);

    // Normalize the brightest channel to unity
    float mx = std::max({r, g, b});
    if (mx > 1e-6f) {
        r /= mx;
        g /= mx;
        b /= mx;
    }
}

// =============================================================================
// Forward WCS: pixel coordinates to world coordinates (RA, Dec)
//
// Implements the FITS TAN (gnomonic) projection with optional SIP
// distortion correction.
// =============================================================================

void PCCCalibrator::pixelToWorld(double x, double y, double& ra, double& dec)
{
    // Step 1: Pixel to intermediate world coordinates (degrees).
    // FITS CRPIX uses 1-based indexing: offset = pixel - crpix + 1.
    double u = x - m_crpix1 + 1.0;
    double v = y - m_crpix2 + 1.0;

    // Apply forward SIP distortion if available
    if (m_useSip) {
        double f_uv = calculateSIP(u, v, m_sipA, m_sipOrderA);
        double g_uv = calculateSIP(u, v, m_sipB, m_sipOrderB);
        u += f_uv;
        v += g_uv;
    }

    // Apply CD matrix to obtain projection plane coordinates (degrees)
    double xi  = m_cd11 * u + m_cd12 * v;
    double eta = m_cd21 * u + m_cd22 * v;

    // Step 2: Gnomonic de-projection to spherical coordinates.
    double xi_rad  = xi  * DEG2RAD;
    double eta_rad = eta * DEG2RAD;

    double crval1_rad = m_crval1 * DEG2RAD;
    double crval2_rad = m_crval2 * DEG2RAD;

    double rho = std::sqrt(xi_rad * xi_rad + eta_rad * eta_rad);
    if (rho == 0.0) {
        ra  = m_crval1;
        dec = m_crval2;
        return;
    }

    double c = std::atan(rho);

    double sin_dec = std::cos(c) * std::sin(crval2_rad)
                   + (eta_rad * std::sin(c) * std::cos(crval2_rad)) / rho;
    double dec_rad = std::asin(std::clamp(sin_dec, -1.0, 1.0));

    double y_term  = rho * std::cos(crval2_rad) * std::cos(c)
                   - eta_rad * std::sin(crval2_rad) * std::sin(c);
    double x_term  = xi_rad * std::sin(c);
    double ra_diff = std::atan2(x_term, y_term);
    double ra_rad  = crval1_rad + ra_diff;

    ra  = ra_rad  * RAD2DEG;
    dec = dec_rad * RAD2DEG;

    // Normalize RA to [0, 360)
    while (ra < 0.0)    ra += 360.0;
    while (ra >= 360.0) ra -= 360.0;
}

// =============================================================================
// Inverse WCS: world coordinates (RA, Dec) to pixel coordinates
//
// Uses inverse gnomonic projection and either inverse SIP (AP/BP)
// coefficients or fixed-point iteration to invert forward SIP (A/B).
// =============================================================================

void PCCCalibrator::worldToPixel(double ra, double dec, double& x, double& y)
{
    double ra_rad     = ra  * DEG2RAD;
    double dec_rad    = dec * DEG2RAD;
    double crval1_rad = m_crval1 * DEG2RAD;
    double crval2_rad = m_crval2 * DEG2RAD;

    double cos_dec  = std::cos(dec_rad);
    double sin_dec  = std::sin(dec_rad);
    double cos_dec0 = std::cos(crval2_rad);
    double sin_dec0 = std::sin(crval2_rad);
    double delta_ra = ra_rad - crval1_rad;
    double cos_dra  = std::cos(delta_ra);

    double denom = sin_dec * sin_dec0 + cos_dec * cos_dec0 * cos_dra;
    if (std::abs(denom) < 1e-10) {
        x = y = -1.0;
        return;
    }

    // Gnomonic projection plane coordinates (radians -> degrees)
    double xi  = (cos_dec * std::sin(delta_ra)) / denom * RAD2DEG;
    double eta = (sin_dec * cos_dec0 - cos_dec * sin_dec0 * cos_dra) / denom * RAD2DEG;

    // Invert the CD matrix
    double det = m_cd11 * m_cd22 - m_cd12 * m_cd21;
    if (std::abs(det) < 1e-15) {
        x = y = -1.0;
        return;
    }

    double u = ( m_cd22 * xi - m_cd12 * eta) / det;
    double v = (-m_cd21 * xi + m_cd11 * eta) / det;

    // Apply inverse SIP distortion
    if (m_useSip && (!m_sipAP.empty() || !m_sipBP.empty())) {
        // Direct inverse coefficients (AP/BP) are available
        double f_uv = calculateSIP(u, v, m_sipAP, m_sipOrderAP);
        double g_uv = calculateSIP(u, v, m_sipBP, m_sipOrderBP);
        u += f_uv;
        v += g_uv;

    } else if (m_useSip && (!m_sipA.empty() || !m_sipB.empty())) {
        // No inverse coefficients: use fixed-point iteration to invert A/B.
        // Solve for (u0, v0) such that u0 + A(u0, v0) = u, v0 + B(u0, v0) = v.
        double u0 = u, v0 = v;
        for (int iter = 0; iter < 10; ++iter) {
            double du = calculateSIP(u0, v0, m_sipA, m_sipOrderA);
            double dv = calculateSIP(u0, v0, m_sipB, m_sipOrderB);
            double u_new = u - du;
            double v_new = v - dv;

            if (std::abs(u_new - u0) < 1e-6 && std::abs(v_new - v0) < 1e-6)
                break;

            u0 = u_new;
            v0 = v_new;
        }
        u = u0;
        v = v0;
    }

    // Convert back to pixel coordinates (FITS 1-based to 0-based)
    x = u + m_crpix1 - 1.0;
    y = v + m_crpix2 - 1.0;
}

// =============================================================================
// Legacy calibration using pre-detected star lists
//
// For each green-channel star, finds spatially coincident red and blue
// counterparts, projects the position to (RA, Dec), cross-matches against
// the catalog, and accumulates per-channel correction factors K = Tr / flux.
// Factors are combined via robust mean and normalized.
// =============================================================================

PCCResult PCCCalibrator::calibrate(
    const std::vector<DetectedStar>& starsR,
    const std::vector<DetectedStar>& starsG,
    const std::vector<DetectedStar>& starsB,
    const std::vector<CatalogStar>&  catalog,
    [[maybe_unused]] int width,
    [[maybe_unused]] int height)
{
    std::vector<float> kR_list, kG_list, kB_list;

    // Cross-match tolerance: 0.005 deg (~18 arcsec) to accommodate residual
    // astrometric distortion not fully captured by the WCS model.
    const double matchRadiusDeg = 0.005;

    // Iterate over green-channel stars as the reference channel.
    for (const auto& sg : starsG) {
        if (sg.saturated) continue;

        // Find matching red and blue stars within 2.5 pixels (accounts for
        // chromatic aberration and slight inter-channel registration offsets).
        const DetectedStar* sr = nullptr;
        const DetectedStar* sb = nullptr;

        for (const auto& s : starsR) {
            if (!s.saturated &&
                std::abs(s.x - sg.x) < 2.5 &&
                std::abs(s.y - sg.y) < 2.5) {
                sr = &s;
                break;
            }
        }
        for (const auto& s : starsB) {
            if (!s.saturated &&
                std::abs(s.x - sg.x) < 2.5 &&
                std::abs(s.y - sg.y) < 2.5) {
                sb = &s;
                break;
            }
        }

        if (!sr || !sb || sr->flux <= 0.0 || sg.flux <= 0.0 || sb->flux <= 0.0)
            continue;

        // Project the star position to sky coordinates
        double ra, dec;
        pixelToWorld(sg.x, sg.y, ra, dec);

        // Cross-match against the catalog
        for (const auto& cat : catalog) {
            // Quick declination pre-filter
            if (std::abs(cat.dec - dec) > matchRadiusDeg) continue;

            // Right ascension difference with cos(dec) correction and wrap-around handling
            double dRA = std::abs(cat.ra - ra);
            if (dRA > 180.0) dRA = 360.0 - dRA;
            double cosDec  = std::cos(dec * DEG2RAD);
            double dRA_sky = dRA * cosDec;
            if (dRA_sky > matchRadiusDeg) continue;

            double dist = std::sqrt(dRA_sky * dRA_sky
                                  + (cat.dec - dec) * (cat.dec - dec));
            if (dist >= matchRadiusDeg) continue;

            // Compute catalog-predicted RGB from B-V color temperature
            float Tr, Tg, Tb;
            temp_to_rgb(BV_to_T(static_cast<float>(cat.B_V)), Tr, Tg, Tb);

            if (Tr > 0.0f && Tg > 0.0f && Tb > 0.0f) {
                // Correction factor: K = catalog_color / measured_flux
                float kR = (1.0f / static_cast<float>(sr->flux)) * Tr;
                float kG = (1.0f / static_cast<float>(sg.flux)) * Tg;
                float kB = (1.0f / static_cast<float>(sb->flux)) * Tb;

                if (!std::isnan(kR) && !std::isnan(kG) && !std::isnan(kB) &&
                    !std::isinf(kR) && !std::isinf(kG) && !std::isinf(kB))
                {
                    kR_list.push_back(kR);
                    kG_list.push_back(kG);
                    kB_list.push_back(kB);
                }
            }

            break; // Accept only the first matching catalog star
        }
    }

    // Require a minimum number of matched stars for statistical reliability
    size_t minStars = std::min({kR_list.size(), kG_list.size(), kB_list.size()});
    if (minStars < 10) return {false, 1.0, 1.0, 1.0};

    // Combine correction factors using a robust mean estimator
    float kr = RobustStatistics::standardRobustMean(kR_list);
    float kg = RobustStatistics::standardRobustMean(kG_list);
    float kb = RobustStatistics::standardRobustMean(kB_list);

    if (kr <= 0.0f || kg <= 0.0f || kb <= 0.0f)
        return {false, 1.0, 1.0, 1.0};

    // Normalize so the largest factor equals 1.0
    float maxK = std::max({kr, kg, kb});

    PCCResult res;
    res.valid    = true;
    res.R_factor = kr / maxK;
    res.G_factor = kg / maxK;
    res.B_factor = kb / maxK;

    return res;
}

// =============================================================================
// Aperture photometry calibration
//
// Performs per-channel aperture photometry on each catalog star position,
// computes image color ratios (R/G, B/G), and fits them against catalog-
// predicted ratios using repeated median regression. The calibration factors
// are derived from the fit evaluated at the G2V solar white reference.
// =============================================================================

PCCResult PCCCalibrator::calibrateWithAperture(
    const ImageBuffer& image,
    const std::vector<CatalogStar>& catalogStars)
{
    if (image.channels() < 3 || catalogStars.empty())
        return {false, 1.0, 1.0, 1.0};

    const float* data = image.data().data();
    int width    = image.width();
    int height   = image.height();
    int channels = image.channels();

    // Configure aperture photometry with standard defaults
    AperturePhotometry photometry;
    ApertureConfig config;
    config.aperture            = 10.0;
    config.inner               = 20.0;
    config.outer               = 30.0;
    config.auto_aperture_factor = 4.0;
    config.force_radius        = true;
    config.minval              = 0.0;
    config.maxval              = 1.0;
    photometry.setConfig(config);

    // Accumulate catalog and image color ratio pairs for robust fitting
    std::vector<double> CatRG, ImgRG;
    std::vector<double> CatBG, ImgBG;

    for (const auto& cat : catalogStars) {
        // Check for external cancellation
        if (m_cancelFlag && m_cancelFlag->load())
            return {false, 1.0, 1.0, 1.0};

        // Convert catalog sky position to image pixel coordinates
        double px, py;
        worldToPixel(cat.ra, cat.dec, px, py);

        // Skip stars too close to the image border for reliable photometry
        if (px < 15 || py < 15 || px >= width - 15 || py >= height - 15)
            continue;

        int starX = static_cast<int>(px);
        int starY = static_cast<int>(py);

        // Measure aperture photometry in each RGB channel
        PhotometryResult photR = photometry.measureStar(
            data, width, height, channels, 0, starX, starY);
        PhotometryResult photG = photometry.measureStar(
            data, width, height, channels, 1, starX, starY);
        PhotometryResult photB = photometry.measureStar(
            data, width, height, channels, 2, starX, starY);

        if (!photR.valid || !photG.valid || !photB.valid) continue;
        if (photR.flux <= 0.0 || photG.flux <= 0.0 || photB.flux <= 0.0) continue;

        // Determine the catalog-predicted color temperature
        float Tr, Tg, Tb;
        double temp;
        if (cat.teff > 1000.0) {
            temp = cat.teff;
        } else {
            float bv = std::clamp(static_cast<float>(cat.B_V), -0.4f, 2.0f);
            temp = 4600.0 * (1.0 / (0.92 * bv + 1.7) + 1.0 / (0.92 * bv + 0.62));
        }
        temp_to_rgb(static_cast<float>(temp), Tr, Tg, Tb);

        if (Tr <= 0.0f || Tg <= 0.0f || Tb <= 0.0f) continue;

        // Compute measured and predicted color ratios relative to the green channel
        double img_rg = photR.flux / photG.flux;
        double img_bg = photB.flux / photG.flux;
        double cat_rg = static_cast<double>(Tr) / Tg;
        double cat_bg = static_cast<double>(Tb) / Tg;

        if (!std::isnan(img_rg) && !std::isnan(cat_rg)) {
            CatRG.push_back(cat_rg);
            ImgRG.push_back(img_rg);
        }
        if (!std::isnan(img_bg) && !std::isnan(cat_bg)) {
            CatBG.push_back(cat_bg);
            ImgBG.push_back(img_bg);
        }
    }

    // Require a minimum number of matched stars
    size_t minStars = std::min(CatRG.size(), CatBG.size());
    if (minStars < 5) return {false, 1.0, 1.0, 1.0};

    // --- Robust linear fit: ImageRatio = slope * CatalogRatio + intercept ---

    double slopeRG = 0.0, iceptRG = 0.0, sigmaRG = 0.0;
    double slopeBG = 0.0, iceptBG = 0.0, sigmaBG = 0.0;

    bool fitRG = RobustStatistics::repeatedMedianFit(
        CatRG, ImgRG, slopeRG, iceptRG, sigmaRG);
    bool fitBG = RobustStatistics::repeatedMedianFit(
        CatBG, ImgBG, slopeBG, iceptBG, sigmaBG);

    if (!fitRG || !fitBG) return {false, 1.0, 1.0, 1.0};

    // --- Evaluate the fit at the G2V solar white reference (T = 5778 K) ---
    //
    // The white reference defines what "neutral" means. For a G2V star,
    // the Planckian locus gives R/G ~1.13 and B/G ~0.93, not 1:1:1.

    float wr_r, wr_g, wr_b;
    temp_to_rgb(5778.0f, wr_r, wr_g, wr_b);

    double Wr = (wr_g > 1e-6f) ? static_cast<double>(wr_r / wr_g) : 1.0;
    double Wb = (wr_g > 1e-6f) ? static_cast<double>(wr_b / wr_g) : 1.0;

    double pred_rg = iceptRG + slopeRG * Wr;
    double pred_bg = iceptBG + slopeBG * Wb;

    if (pred_rg <= 0.0 || pred_bg <= 0.0)
        return {false, 1.0, 1.0, 1.0};

    // Correction factors: invert the predicted image ratio at the white reference
    float kr = static_cast<float>(1.0 / pred_rg);
    float kg = 1.0f;
    float kb = static_cast<float>(1.0 / pred_bg);

    // Normalize to [0, 1] range
    float maxK = std::max({kr, kg, kb});

    PCCResult res;
    res.valid    = true;
    res.R_factor = kr / maxK;
    res.G_factor = kg / maxK;
    res.B_factor = kb / maxK;

    // Store diagnostic data for visualization
    res.CatRG   = CatRG;
    res.ImgRG   = ImgRG;
    res.CatBG   = CatBG;
    res.ImgBG   = ImgBG;
    res.slopeRG = slopeRG;
    res.iceptRG = iceptRG;
    res.slopeBG = slopeBG;
    res.iceptBG = iceptBG;

    // Populate polynomial fields for compatibility with higher-order models
    res.polyRG[0] = 0.0;  res.polyRG[1] = slopeRG;  res.polyRG[2] = iceptRG;
    res.polyBG[0] = 0.0;  res.polyBG[1] = slopeBG;  res.polyBG[2] = iceptBG;
    res.isQuadratic = false;

    return res;
}