#ifndef PCCRESULT_H
#define PCCRESULT_H

// =============================================================================
// PCCResult.h
//
// Result structure for Photometric Color Calibration (PCC).
//
// Contains per-channel scaling factors that transform measured instrumental
// fluxes into a photometrically balanced color representation, along with
// diagnostic data for visualization of the calibration fit.
// =============================================================================

#include <vector>

struct PCCResult {
    bool   valid    = false; // True if the calibration produced usable factors
    double R_factor = 1.0;   // Red channel scaling factor (normalized)
    double G_factor = 1.0;   // Green channel scaling factor (normalized)
    double B_factor = 1.0;   // Blue channel scaling factor (normalized)

    // Background neutralization offsets (applied before scaling)
    float bg_r = 0.0f;
    float bg_g = 0.0f;
    float bg_b = 0.0f;

    // Scatter data for diagnostic distribution plots
    std::vector<double> CatRG, ImgRG; // Catalog vs. image R/G color ratios
    std::vector<double> CatBG, ImgBG; // Catalog vs. image B/G color ratios

    // Linear fit parameters for visualization (slope and intercept)
    double slopeRG = 1.0, iceptRG = 0.0;
    double slopeBG = 1.0, iceptBG = 0.0;

    // Polynomial coefficients for extended models (a*x^2 + b*x + c)
    // Used by SPCC and higher-order PCC calibration paths.
    double polyRG[3]   = {0.0, 1.0, 0.0};
    double polyBG[3]   = {0.0, 1.0, 0.0};
    bool   isQuadratic = false;
};

#endif // PCCRESULT_H