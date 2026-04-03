#ifndef STARSTRETCHRUNNER_H
#define STARSTRETCHRUNNER_H

#include <QObject>
#include <QString>

#include "ImageBuffer.h"

// ----------------------------------------------------------------------------
// StarStretchParams
// Configuration for the star-specific stretch and colour operations.
// ----------------------------------------------------------------------------
struct StarStretchParams {
    float stretchAmount = 5.0f;   // Pixel Math stretch strength: 0.0 to 8.0
    float colorBoost    = 1.0f;   // Saturation multiplier:       0.0 to 2.0
    bool  scnr          = false;  // Apply SCNR green removal when true
};

// ----------------------------------------------------------------------------
// StarStretchRunner
// Applies a dedicated processing pipeline optimised for star layers:
//   1. Pixel Math stretch (rational function with base-3 exponent).
//   2. Saturation boost (simple mean-based method).
//   3. Optional SCNR (Subtractive Chromatic Noise Reduction) for green removal.
//   4. Mask blending if a mask is attached to the buffer.
// ----------------------------------------------------------------------------
class StarStretchRunner : public QObject {
    Q_OBJECT

public:
    explicit StarStretchRunner(QObject* parent = nullptr);

    // Apply the star stretch pipeline. Input is copied to output before processing.
    // Returns false only on hard errors (currently always returns true).
    bool run(const ImageBuffer& input,
             ImageBuffer&       output,
             const StarStretchParams& params,
             QString* errorMsg = nullptr);

signals:
    void processOutput(const QString& msg);
    void progressValue(int percent);
};

#endif // STARSTRETCHRUNNER_H