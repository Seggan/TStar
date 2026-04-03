#include "StarRecompositionRunner.h"

#include <algorithm>
#include <cmath>

// ----------------------------------------------------------------------------
// Constructor
// ----------------------------------------------------------------------------
StarRecompositionRunner::StarRecompositionRunner(QObject* parent)
    : QObject(parent)
{}

// ----------------------------------------------------------------------------
// run
//
// Applies a Generalised Hyperbolic Stretch (GHS) to the star layer, then
// blends it with the starless background using the Screen formula:
//   output = starless + star - starless * star
//
// Screen blend is commutative, associative and always produces a result
// >= both inputs, making it safe for combining astronomical layers.
// ----------------------------------------------------------------------------
bool StarRecompositionRunner::run(const ImageBuffer& starless,
                                   const ImageBuffer& stars,
                                   ImageBuffer&       output,
                                   const StarRecompositionParams& params,
                                   QString* errorMsg)
{
    if (starless.width() != stars.width() || starless.height() != stars.height()) {
        if (errorMsg)
            *errorMsg = "Dimension mismatch between starless and star layer images.";
        return false;
    }

    const int w      = starless.width();
    const int h      = starless.height();
    const int c      = starless.channels();
    const int starsC = stars.channels();

    output.resize(w, h, std::max(c, starsC));

    // Apply GHS stretch to a working copy of the star layer.
    ImageBuffer stretchedStars = stars;
    stretchedStars.applyGHS(params.ghs);

    const size_t numPixels = static_cast<size_t>(w) * h;
    const int    outC      = output.channels();

    const float* sllData = starless.data().data();
    const float* strData = stretchedStars.data().data();
    float*       outData = output.data().data();

    // Screen blend: output = A + B - A * B
    for (size_t i = 0; i < numPixels; ++i) {
        for (int ch = 0; ch < outC; ++ch) {
            float sll = 0.0f;
            if      (c == 3) sll = sllData[i * 3 + ch];
            else if (c == 1) sll = sllData[i];

            float str = 0.0f;
            if      (starsC == 3) str = strData[i * 3 + ch];
            else if (starsC == 1) str = strData[i];

            float val = sll + str - (sll * str);
            outData[i * outC + ch] = std::max(0.0f, std::min(1.0f, val));
        }
    }

    output.setMetadata(starless.metadata());
    return true;
}