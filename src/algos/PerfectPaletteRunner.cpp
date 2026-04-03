#include "PerfectPaletteRunner.h"

#include <algorithm>
#include <cmath>
#include <numeric>

// ----------------------------------------------------------------------------
// Constructor
// ----------------------------------------------------------------------------
PerfectPaletteRunner::PerfectPaletteRunner(QObject* parent)
    : QObject(parent)
{}

// ----------------------------------------------------------------------------
// applyStatisticalStretch
//
// Performs a two-stage stretch on each channel of the supplied buffer:
//   1. Black point clipping: BP = max(min_value, median - 2.8 * stddev)
//      Rescales the channel so that BP maps to 0 and 1.0 remains at 1.0.
//   2. Midtone Transfer Function (MTF): computes the midtone parameter 'm'
//      that maps the post-clip median to the requested targetMedian, then
//      applies the rational MTF curve to the channel.
//
// Statistics are gathered in a single parallel O(N) pass to avoid sorting.
// The median is obtained via ImageBuffer::getChannelMedian (histogram-based).
// ----------------------------------------------------------------------------
void PerfectPaletteRunner::applyStatisticalStretch(ImageBuffer& buffer, float targetMedian)
{
    if (!buffer.isValid()) return;

    const int    w         = buffer.width();
    const int    h         = buffer.height();
    const int    c         = buffer.channels();
    const size_t numPixels = static_cast<size_t>(w) * h;
    float*       data      = buffer.data().data();

    for (int ch = 0; ch < c; ++ch) {

        // --- Step 1: Gather channel statistics ---

        // Approximate median via histogram stepping for large-image performance.
        float med = buffer.getChannelMedian(ch);

        // Compute the minimum value and accumulate sum / sum-of-squares in
        // parallel, using OpenMP reductions. O(N), no sort required.
        float  minVal  = 1.0f;
        double sum     = 0.0;
        double sq_sum  = 0.0;

        #pragma omp parallel for reduction(min:minVal) reduction(+:sum, sq_sum)
        for (size_t i = 0; i < numPixels; ++i) {
            float v = data[i * c + ch];
            if (v < minVal) minVal = v;
            sum    += v;
            sq_sum += static_cast<double>(v) * v;
        }

        const float mean   = static_cast<float>(sum / numPixels);
        const float stdDev = std::sqrt(std::max(0.0, sq_sum / numPixels - static_cast<double>(mean) * mean));

        // --- Step 2: Black point clipping ---

        // BP = max(min_value, median - 2.8 * stddev)
        float bp    = std::max(minVal, med - 2.8f * stdDev);
        float denom = 1.0f - bp;
        if (std::abs(denom) < 1e-6f) denom = 1e-6f;

        // Rescale: new_value = clamp((value - bp) / (1 - bp), 0, 1)
        #pragma omp parallel for
        for (size_t i = 0; i < numPixels; ++i) {
            float val         = (data[i * c + ch] - bp) / denom;
            data[i * c + ch]  = std::max(0.0f, std::min(1.0f, val));
        }

        // --- Step 3: MTF application ---

        // Because the black-point transform is monotonic we can compute the
        // new median analytically rather than re-scanning the buffer.
        float newMed = (med - bp) / denom;
        newMed = std::max(0.0f, std::min(1.0f, newMed));

        if (newMed > 0.0f && newMed < 1.0f) {

            // Solve for the MTF midtone parameter 'm' such that MTF(newMed, m) = targetMedian.
            float m = (newMed * (targetMedian - 1.0f))
                    / (newMed * (2.0f * targetMedian - 1.0f) - targetMedian);
            m = std::max(0.0001f, std::min(0.9999f, m));

            const float m_1  = m - 1.0f;
            const float m2_1 = 2.0f * m - 1.0f;

            // Apply MTF: f(x, m) = (m - 1) * x / ((2m - 1) * x - m)
            #pragma omp parallel for
            for (size_t i = 0; i < numPixels; ++i) {
                float x   = data[i * c + ch];
                float den = m2_1 * x - m;
                if (std::abs(den) < 1e-9f) den = 1e-9f;
                data[i * c + ch] = (m_1 * x) / den;
            }
        }
    }
}

// ----------------------------------------------------------------------------
// run
//
// Validates inputs, optionally stretches and scales each channel, resolves
// missing channels by substitution, then dispatches to the appropriate
// palette mapping function.
// ----------------------------------------------------------------------------
bool PerfectPaletteRunner::run(const ImageBuffer* ha,
                               const ImageBuffer* oiii,
                               const ImageBuffer* sii,
                               ImageBuffer&       output,
                               const PerfectPaletteParams& params,
                               QString* errorMsg)
{
    // OIII is mandatory; at least one of Ha or SII must also be provided.
    if (!oiii || (!ha && !sii)) {
        if (errorMsg) *errorMsg = "OIII and either Ha or SII are required.";
        return false;
    }

    // Determine output dimensions from the first available input.
    int w = 0, h = 0;
    if      (ha)   { w = ha->width();   h = ha->height();   }
    else if (oiii) { w = oiii->width(); h = oiii->height(); }
    else if (sii)  { w = sii->width();  h = sii->height();  }

    output.resize(w, h, 3);

    // Create working copies so the originals are not modified.
    ImageBuffer fHa, fOiii, fSii;
    if (ha)   fHa   = *ha;
    if (oiii) fOiii = *oiii;
    if (sii)  fSii  = *sii;

    // Optional per-channel statistical stretch.
    if (params.applyStatisticalStretch) {
        if (ha)   applyStatisticalStretch(fHa,   params.targetMedian);
        if (oiii) applyStatisticalStretch(fOiii, params.targetMedian);
        if (sii)  applyStatisticalStretch(fSii,  params.targetMedian);
    }

    // Apply per-channel intensity multipliers if they differ from unity.
    if (params.haFactor   != 1.0f && fHa.isValid())   fHa.multiply(params.haFactor);
    if (params.oiiiFactor != 1.0f && fOiii.isValid()) fOiii.multiply(params.oiiiFactor);
    if (params.siiFactor  != 1.0f && fSii.isValid())  fSii.multiply(params.siiFactor);

    // Substitute missing channels: if Ha is absent use SII, and vice versa.
    if (!ha  && sii) fHa  = fSii;
    if (!sii && ha)  fSii = fHa;

    // Propagate metadata from the first available source.
    ImageBuffer::Metadata outMeta;
    if      (ha)   outMeta = ha->metadata();
    else if (oiii) outMeta = oiii->metadata();
    else if (sii)  outMeta = sii->metadata();

    // Dispatch to the selected palette mapping.
    if      (params.paletteName == "SHO")       mapSHO      (fHa, fOiii, fSii, output);
    else if (params.paletteName == "HOO")       mapGeneric  (fHa, fOiii, fOiii, output);
    else if (params.paletteName == "HSO")       mapGeneric  (fHa, fSii,  fOiii, output);
    else if (params.paletteName == "HOS")       mapGeneric  (fHa, fOiii, fSii,  output);
    else if (params.paletteName == "OSS")       mapGeneric  (fOiii, fSii,  fSii,  output);
    else if (params.paletteName == "OHH")       mapGeneric  (fOiii, fHa,   fHa,   output);
    else if (params.paletteName == "OSH")       mapGeneric  (fOiii, fSii,  fHa,   output);
    else if (params.paletteName == "OHS")       mapGeneric  (fOiii, fHa,   fSii,  output);
    else if (params.paletteName == "HSS")       mapGeneric  (fHa,   fSii,  fSii,  output);
    else if (params.paletteName == "Foraxx")    mapForaxx   (fHa, fOiii, fSii, output);
    else if (params.paletteName == "Realistic1") mapRealistic1(fHa, fOiii, fSii, output);
    else if (params.paletteName == "Realistic2") mapRealistic2(fHa, fOiii, fSii, output);
    else {
        // Default fallback to the standard Hubble (SHO) palette.
        mapSHO(fHa, fOiii, fSii, output);
    }

    output.setMetadata(outMeta);
    return true;
}

// ----------------------------------------------------------------------------
// mapSHO
// Hubble palette: R = SII, G = Ha, B = OIII
// ----------------------------------------------------------------------------
void PerfectPaletteRunner::mapSHO(const ImageBuffer& ha,
                                   const ImageBuffer& oiii,
                                   const ImageBuffer& sii,
                                   ImageBuffer& out)
{
    mapGeneric(sii, ha, oiii, out);
}

// ----------------------------------------------------------------------------
// mapGeneric
// Assigns the three supplied single-channel buffers directly to R, G, B.
// ----------------------------------------------------------------------------
void PerfectPaletteRunner::mapGeneric(const ImageBuffer& rCh,
                                       const ImageBuffer& gCh,
                                       const ImageBuffer& bCh,
                                       ImageBuffer& out)
{
    const int    w    = out.width();
    const int    h    = out.height();
    float*       data = out.data().data();

    #pragma omp parallel for
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
        data[i * 3 + 0] = rCh.getPixelFlat(i, 0);
        data[i * 3 + 1] = gCh.getPixelFlat(i, 0);
        data[i * 3 + 2] = bCh.getPixelFlat(i, 0);
    }
}

// ----------------------------------------------------------------------------
// mapForaxx
// Non-linear palette that blends Ha and SII into the red channel based on
// the local OIII intensity, producing a transition between warm and cool hues.
//
// R = t * SII + (1 - t) * Ha,  where t = oVal^(1 - oVal)
// G = (Ha * OIII)^(1 - Ha*OIII) * Ha + (1 - ...) * OIII
// B = OIII
// ----------------------------------------------------------------------------
void PerfectPaletteRunner::mapForaxx(const ImageBuffer& ha,
                                      const ImageBuffer& oiii,
                                      const ImageBuffer& sii,
                                      ImageBuffer& out)
{
    const int    w    = out.width();
    const int    h    = out.height();
    float*       data = out.data().data();

    #pragma omp parallel for
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
        const float hVal = ha.getPixelFlat(i, 0);
        const float oVal = oiii.getPixelFlat(i, 0);
        const float sVal = sii.getPixelFlat(i, 0);

        // Red: blend SII and Ha based on OIII-driven exponent.
        float t = std::pow(std::max(1e-6f, oVal), 1.0f - std::max(1e-6f, oVal));
        float r = t * sVal + (1.0f - t) * hVal;

        // Green: non-linear mix of Ha and OIII via product exponent.
        float t2 = hVal * oVal;
        float g  = std::pow(t2, 1.0f - t2) * hVal + (1.0f - std::pow(t2, 1.0f - t2)) * oVal;

        // Blue: direct OIII assignment.
        float b = oVal;

        data[i * 3 + 0] = r;
        data[i * 3 + 1] = g;
        data[i * 3 + 2] = b;
    }
}

// ----------------------------------------------------------------------------
// mapRealistic1
// Blends channels with fixed coefficients to approximate natural star colours:
//   R = 0.5 * (Ha + SII)
//   G = 0.3 * Ha + 0.7 * OIII
//   B = 0.9 * OIII + 0.1 * Ha
// ----------------------------------------------------------------------------
void PerfectPaletteRunner::mapRealistic1(const ImageBuffer& ha,
                                          const ImageBuffer& oiii,
                                          const ImageBuffer& sii,
                                          ImageBuffer& out)
{
    const int    w    = out.width();
    const int    h    = out.height();
    float*       data = out.data().data();

    #pragma omp parallel for
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
        const float hVal = ha.getPixelFlat(i, 0);
        const float oVal = oiii.getPixelFlat(i, 0);
        const float sVal = sii.getPixelFlat(i, 0);

        data[i * 3 + 0] = (hVal + sVal) * 0.5f;
        data[i * 3 + 1] = 0.3f * hVal + 0.7f * oVal;
        data[i * 3 + 2] = 0.9f * oVal + 0.1f * hVal;
    }
}

// ----------------------------------------------------------------------------
// mapRealistic2
// Alternative realistic blend emphasising SII in the red channel:
//   R = 0.7 * Ha + 0.3 * SII
//   G = 0.3 * SII + 0.7 * OIII
//   B = OIII
// ----------------------------------------------------------------------------
void PerfectPaletteRunner::mapRealistic2(const ImageBuffer& ha,
                                          const ImageBuffer& oiii,
                                          const ImageBuffer& sii,
                                          ImageBuffer& out)
{
    const int    w    = out.width();
    const int    h    = out.height();
    float*       data = out.data().data();

    #pragma omp parallel for
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
        const float hVal = ha.getPixelFlat(i, 0);
        const float oVal = oiii.getPixelFlat(i, 0);
        const float sVal = sii.getPixelFlat(i, 0);

        data[i * 3 + 0] = 0.7f * hVal + 0.3f * sVal;
        data[i * 3 + 1] = 0.3f * sVal + 0.7f * oVal;
        data[i * 3 + 2] = oVal;
    }
}