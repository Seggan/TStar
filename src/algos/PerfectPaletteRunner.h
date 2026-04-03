#ifndef PERFECTPALETTERUNNER_H
#define PERFECTPALETTERUNNER_H

#include <QObject>
#include <vector>
#include <map>

#include "../ImageBuffer.h"

// ----------------------------------------------------------------------------
// PerfectPaletteParams
// Parameters controlling the narrowband palette mapping operation.
// ----------------------------------------------------------------------------
struct PerfectPaletteParams {
    QString paletteName           = "SHO";   // Target palette (SHO, HOO, Foraxx, etc.)
    float   haFactor              = 1.0f;    // H-alpha intensity multiplier
    float   oiiiFactor            = 1.0f;    // OIII intensity multiplier
    float   siiFactor             = 1.0f;    // SII intensity multiplier
    bool    applyStatisticalStretch = true;  // Apply auto-stretch before mapping
    float   targetMedian          = 0.25f;   // Target median for statistical stretch
};

// ----------------------------------------------------------------------------
// PerfectPaletteRunner
// Combines narrowband emission channels (Ha, OIII, SII) into an RGB image
// using one of several predefined palette mappings. Optionally applies a
// statistical stretch to each input channel before combination.
// ----------------------------------------------------------------------------
class PerfectPaletteRunner : public QObject {
    Q_OBJECT

public:
    explicit PerfectPaletteRunner(QObject* parent = nullptr);

    // Combine narrowband channels into the output RGB buffer.
    // At minimum, oiii and one of ha or sii must be provided.
    // Returns false and populates errorMsg on failure.
    bool run(const ImageBuffer* ha,
             const ImageBuffer* oiii,
             const ImageBuffer* sii,
             ImageBuffer&       output,
             const PerfectPaletteParams& params,
             QString* errorMsg = nullptr);

    // Apply a two-step statistical stretch (black point + MTF) to a buffer in-place.
    // Uses a histogram-based median and a parallel O(N) stddev pass for performance.
    static void applyStatisticalStretch(ImageBuffer& buffer, float targetMedian = 0.25f);

signals:
    void processOutput(const QString& msg);

private:
    // Palette mapping functions. Each maps the three input channels to RGB.
    void mapSHO      (const ImageBuffer& ha, const ImageBuffer& oiii, const ImageBuffer& sii, ImageBuffer& out);
    void mapGeneric  (const ImageBuffer& rCh, const ImageBuffer& gCh, const ImageBuffer& bCh, ImageBuffer& out);
    void mapForaxx   (const ImageBuffer& ha, const ImageBuffer& oiii, const ImageBuffer& sii, ImageBuffer& out);
    void mapRealistic1(const ImageBuffer& ha, const ImageBuffer& oiii, const ImageBuffer& sii, ImageBuffer& out);
    void mapRealistic2(const ImageBuffer& ha, const ImageBuffer& oiii, const ImageBuffer& sii, ImageBuffer& out);
};

#endif // PERFECTPALETTERUNNER_H