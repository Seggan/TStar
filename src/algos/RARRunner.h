#ifndef RARRUNNER_H
#define RARRUNNER_H

#include <QObject>
#include <QString>
#include <atomic>

#include "ImageBuffer.h"

// ----------------------------------------------------------------------------
// RARParams
// Configuration for the AI-based Residual Aberration Removal worker process.
// ----------------------------------------------------------------------------
struct RARParams {
    QString modelPath;              // Absolute path to the ONNX model file
    int     patchSize = 512;        // Tile size fed to the model (pixels)
    int     overlap   = 64;         // Overlap between adjacent tiles (pixels)
    QString provider  = "CPU";      // Inference provider: CPU, CUDA, or DirectML
};

// ----------------------------------------------------------------------------
// RARRunner
// Launches an external Python worker process that performs AI-based aberration
// removal on the supplied ImageBuffer. Image data is exchanged via temporary
// TIFF (input) and raw float32 binary (output) files to avoid precision loss.
//
// Cancellation is supported through the cancel() slot, which sets an atomic
// flag checked in the wait loop.
// ----------------------------------------------------------------------------
class RARRunner : public QObject {
    Q_OBJECT

public:
    explicit RARRunner(QObject* parent = nullptr);

    // Execute the aberration removal pipeline synchronously.
    // Blocks the calling thread until the worker process finishes.
    // Returns false and populates errorMsg on failure or cancellation.
    bool run(const ImageBuffer& input,
             ImageBuffer&       output,
             const RARParams&   params,
             QString*           errorMsg);

signals:
    void processOutput(const QString& text);

public slots:
    // Signal the blocking wait loop to terminate and kill the worker process.
    void cancel() { m_stop = true; }

private:
    std::atomic<bool> m_stop{false};
};

#endif // RARRUNNER_H