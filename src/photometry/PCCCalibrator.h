#ifndef PCCCALIBRATOR_H
#define PCCCALIBRATOR_H

// =============================================================================
// PCCCalibrator.h
//
// Photometric Color Calibration (PCC) engine.
//
// Computes per-channel correction factors by matching measured stellar
// photometry against catalog reference colors. Supports two calibration
// paths:
//   1. Legacy: uses pre-detected star lists with flux values (DetectedStar)
//   2. Aperture: performs PSF + aperture photometry on raw image data
//
// WCS (World Coordinate System) transformation handles FITS TAN projection
// with optional SIP distortion polynomials for accurate pixel-to-sky mapping.
// =============================================================================

#include <vector>
#include <map>
#include <atomic>

#include "StarDetector.h"
#include "CatalogClient.h"
#include "../ImageBuffer.h"
#include "PCCResult.h"

class PCCCalibrator {
public:
    PCCCalibrator();

    // Legacy calibration using pre-extracted per-channel star lists.
    PCCResult calibrate(const std::vector<DetectedStar>& imageStarsR,
                        const std::vector<DetectedStar>& imageStarsG,
                        const std::vector<DetectedStar>& imageStarsB,
                        const std::vector<CatalogStar>&  catalogStars,
                        int width, int height);

    // Aperture photometry calibration operating directly on image pixel data.
    PCCResult calibrateWithAperture(const ImageBuffer& image,
                                    const std::vector<CatalogStar>& catalogStars);

    // Set the linear WCS parameters (FITS TAN projection).
    void setWCS(double crval1, double crval2,
                double crpix1, double crpix2,
                double cd1_1, double cd1_2,
                double cd2_1, double cd2_2);

    // Set SIP (Simple Imaging Polynomial) distortion coefficients.
    void setSIP(int a_order, int b_order,
                int ap_order, int bp_order,
                const std::map<std::string, double>& coeffs);

    // Provide an external cancellation flag for long-running calibration.
    void setCancelFlag(std::atomic<bool>* flag) { m_cancelFlag = flag; }

private:
    // --- WCS parameters (FITS TAN gnomonic projection) ---

    double m_crval1, m_crval2;  // Reference sky coordinates (RA, Dec in degrees)
    double m_crpix1, m_crpix2;  // Reference pixel coordinates (1-indexed FITS convention)
    double m_cd11, m_cd12;      // CD matrix elements: pixel-to-degree linear transform
    double m_cd21, m_cd22;

    // --- SIP distortion model ---

    bool m_useSip = false;
    int  m_sipOrderA  = 0, m_sipOrderB  = 0;
    int  m_sipOrderAP = 0, m_sipOrderBP = 0;

    std::map<std::pair<int,int>, double> m_sipA;   // Forward distortion A coefficients
    std::map<std::pair<int,int>, double> m_sipB;   // Forward distortion B coefficients
    std::map<std::pair<int,int>, double> m_sipAP;  // Inverse distortion AP coefficients
    std::map<std::pair<int,int>, double> m_sipBP;  // Inverse distortion BP coefficients

    // Evaluate a SIP polynomial for the given intermediate coordinates (u, v).
    double calculateSIP(double u, double v,
                        const std::map<std::pair<int,int>, double>& coeffs,
                        int order) const;

    // Forward WCS: pixel (x, y) to world (RA, Dec) using TAN + SIP.
    void pixelToWorld(double x, double y, double& ra, double& dec);

    // Inverse WCS: world (RA, Dec) to pixel (x, y) using TAN + inverse SIP.
    void worldToPixel(double ra, double dec, double& x, double& y);

    // External cancellation flag (may be null).
    std::atomic<bool>* m_cancelFlag = nullptr;
};

#endif // PCCCALIBRATOR_H