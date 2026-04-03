#ifndef STARNETRUNNER_H
#define STARNETRUNNER_H

#include <QString>
#include <QObject>
#include <QPointer>
#include <QThread>
#include <vector>
#include <atomic>

#include "ImageBuffer.h"

// ----------------------------------------------------------------------------
// StarNetParams
// Configuration for the StarNet++ star removal pass.
// ----------------------------------------------------------------------------
struct StarNetParams {
    bool  isLinear     = false;   // True if the input is in linear (unstretched) space
    bool  generateMask = false;   // Reserved: generate a star mask output
    int   stride       = 256;     // Tile stride used by StarNet++
    float upsample     = 1.0f;    // Scale factor before processing (reserved)
    bool  useGpu       = true;    // Prefer GPU inference when available
};

// ----------------------------------------------------------------------------
// StarNetWorker
// Performs the star removal operation in a dedicated QThread.
// Communication with the owner StarNetRunner happens through Qt signals.
//
// The worker:
//   - Optionally auto-stretches linear input before calling StarNet++.
//   - Pads the image to a multiple of 32 pixels for network stability.
//   - Invokes StarNet++ as an external process via QProcess.
//   - Converts the TIFF output to raw float32 via a Python bridge script.
//   - Inverts the auto-stretch on the starless result if applied.
//   - Crops and restores the original dimensions.
// ----------------------------------------------------------------------------
class StarNetWorker : public QObject {
    Q_OBJECT

public:
    explicit StarNetWorker(QObject* parent = nullptr);

public slots:
    // Entry point invoked via QMetaObject::invokeMethod from StarNetRunner::run().
    void process(const ImageBuffer& input, const StarNetParams& params);

    // Set the cancellation flag; the process() wait loop checks this flag.
    void cancel() { m_stop = true; }

signals:
    // Emitted when processing completes (success or failure).
    // errorMsg is empty on success.
    void finished(const ImageBuffer& output, const QString& errorMsg);

    // Forwarded stdout from child processes for display in the UI.
    void processOutput(const QString& text);

private:
    // Midtone Transfer Function parameters computed per channel.
    struct MTFParams {
        float s[3];  // Shadow clipping point (black point)
        float m[3];  // MTF midtone parameter
        float h[3];  // Highlight clipping point (white point)
    };

    // Compute MTF parameters from the image histogram using median + MAD statistics.
    MTFParams computeMtfParams(const ImageBuffer& img);

    // Apply the forward MTF stretch in-place.
    void applyMtf(ImageBuffer& img, const MTFParams& p);

    // Invert the MTF stretch to restore the original tonal range.
    void invertMtf(ImageBuffer& img, const MTFParams& p);

    std::atomic<bool> m_stop{false};

    // Returns the path to the StarNet++ executable from persistent settings.
    QString getExecutablePath();
};

// ----------------------------------------------------------------------------
// StarNetRunner
// Public interface for the StarNet++ integration. Manages the worker thread
// lifecycle and provides a synchronous run() method that blocks via a local
// QEventLoop until the worker signals completion or a 15-minute timeout elapses.
// ----------------------------------------------------------------------------
class StarNetRunner : public QObject {
    Q_OBJECT

public:
    explicit StarNetRunner(QObject* parent = nullptr);
    ~StarNetRunner();

    // Execute the star removal pipeline synchronously.
    // Returns false and populates errorMsg on failure or cancellation.
    bool run(const ImageBuffer& input,
             ImageBuffer&       output,
             const StarNetParams& params,
             QString* errorMsg);

signals:
    void processOutput(const QString& text);
    void finished();

public slots:
    void cancel() {
        if (m_worker) m_worker->cancel();
    }

private slots:
    void onWorkerFinished(const ImageBuffer& output, const QString& errorMsg);

private:
    QPointer<QThread>       m_thread;    // Pointer to the worker thread (safe to check validity)
    QPointer<StarNetWorker> m_worker;    // Pointer to the worker object
    ImageBuffer             m_output;    // Temporary buffer for the completed result
    QString                 m_errorMsg;  // Error message populated by onWorkerFinished
    bool                    m_finished = false;  // Synchronisation flag for the event loop
};

#endif // STARNETRUNNER_H