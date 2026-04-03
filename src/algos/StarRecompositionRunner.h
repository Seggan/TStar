#ifndef STARRECOMPOSITIONRUNNER_H
#define STARRECOMPOSITIONRUNNER_H

#include <QObject>

#include "../ImageBuffer.h"

// ----------------------------------------------------------------------------
// StarRecompositionParams
// Parameters controlling how the star layer is stretched before recomposition.
// ----------------------------------------------------------------------------
struct StarRecompositionParams {
    ImageBuffer::GHSParams ghs;  // Generalised Hyperbolic Stretch parameters
                                 // applied to the star layer prior to blending.
};

// ----------------------------------------------------------------------------
// StarRecompositionRunner
// Recombines a starless background image with a star layer using a Screen
// blend mode (A + B - A*B), which is additive and prevents clipping.
//
// The star layer is stretched via GHS before blending to allow independent
// control of star brightness relative to the background.
// ----------------------------------------------------------------------------
class StarRecompositionRunner : public QObject {
    Q_OBJECT

public:
    explicit StarRecompositionRunner(QObject* parent = nullptr);

    // Combine starless and stars into output using Screen blend.
    // Both inputs must have identical width and height; channels may differ
    // (mono/RGB combinations are handled). Returns false on dimension mismatch.
    bool run(const ImageBuffer& starless,
             const ImageBuffer& stars,
             ImageBuffer&       output,
             const StarRecompositionParams& params,
             QString* errorMsg = nullptr);

signals:
    void processOutput(const QString& msg);
};

#endif // STARRECOMPOSITIONRUNNER_H