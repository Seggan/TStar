#ifndef WCSSOLVER_H
#define WCSSOLVER_H

// ============================================================================
// WcsSolver
//
// Computes FITS-standard WCS parameters (CRPIX, CRVAL, CD matrix) from
// the converged affine transform produced by NativePlateSolver.
// ============================================================================

#include "TriangleMatcher.h"

#include <cmath>
#include <iostream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class WcsSolver {
public:
    // Arcseconds to degrees conversion factor
    static constexpr double ARCSEC_TO_DEG = 1.0 / 3600.0;

    /**
     * Computes WCS parameters from a converged plate-solve solution.
     *
     * After the convergence loop in matchCatalog:
     *   - raConverged, decConverged = converged projection centre
     *   - trans = final affine transform (centred pixels -> tangent plane arcsec)
     *
     * WCS output:
     *   CRPIX = image centre (FITS 1-indexed convention)
     *   CRVAL = converged (RA, Dec) -- the projection centre itself
     *   CD    = linear coefficients of the transform (arcsec -> degrees)
     *
     * After convergence, trans.x00 and trans.y00 approach zero, meaning
     * the tangent-plane origin coincides with the image centre. Therefore
     * CRVAL = (raConverged, decConverged) directly, with no additional
     * de-projection required.
     *
     * Coordinate convention:
     *   The plate solver defines centred pixel coordinates as:
     *     ms.x = buffer_x - W/2   (same direction as FITS X)
     *     ms.y = H/2 - buffer_y   (increases northward, same as eta)
     *
     *   The TRANS maps (ms.x, ms.y) -> (xi, eta) in arcsec.
     *   Both ms.y and eta increase northward, so no sign flip is needed
     *   when converting TRANS terms to the CD matrix:
     *     CD1_1 = trans.x10 / 3600
     *     CD1_2 = trans.x01 / 3600
     *     CD2_1 = trans.y10 / 3600
     *     CD2_2 = trans.y01 / 3600
     *
     * @return True on success, false if the CD matrix is singular.
     */
    static bool computeWcs(const GenericTrans& trans,
                           double raConverged, double decConverged,
                           int imageWidth, int imageHeight,
                           double& crpix1, double& crpix2,
                           double& crval1, double& crval2,
                           double cd[2][2])
    {
        // CRPIX: image centre in FITS convention (1-indexed)
        crpix1 = imageWidth  * 0.5 + 0.5;
        crpix2 = imageHeight * 0.5 + 0.5;

        // CD matrix: convert transform coefficients from arcsec/px to deg/px
        cd[0][0] = trans.x10 * ARCSEC_TO_DEG;  // CD1_1
        cd[0][1] = trans.x01 * ARCSEC_TO_DEG;  // CD1_2
        cd[1][0] = trans.y10 * ARCSEC_TO_DEG;  // CD2_1
        cd[1][1] = trans.y01 * ARCSEC_TO_DEG;  // CD2_2

        // Verify the matrix is non-singular
        double det = cd[0][0] * cd[1][1] - cd[0][1] * cd[1][0];
        if (std::abs(det) < 1e-20) {
            std::cerr << "WcsSolver: Singular CD matrix (det="
                      << det << ")" << std::endl;
            return false;
        }

        // CRVAL: the converged projection centre
        crval1 = raConverged;
        crval2 = decConverged;

        return true;
    }
};

#endif // WCSSOLVER_H