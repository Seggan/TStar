#ifndef COSMICCLARITYRUNNER_H
#define COSMICCLARITYRUNNER_H

// ============================================================================
// CosmicClarityRunner.h
// Interface for running the Cosmic Clarity AI processing pipeline
// (sharpening, denoising, super-resolution) via a bundled Python/ONNX bridge.
// Provides a thread-safe runner with cancel and timeout support.
// ============================================================================

#include <QString>
#include <QObject>
#include <QPointer>
#include <QThread>
#include <atomic>

#include "ImageBuffer.h"

// ============================================================================
// Processing Parameters
// ============================================================================

/**
 * @brief Parameters controlling the Cosmic Clarity AI processing mode
 *        and per-mode settings.
 */
struct CosmicClarityParams {
    enum Mode {
        Mode_Sharpen,
        Mode_Denoise,
        Mode_Both,
        Mode_SuperRes
    };

    Mode mode = Mode_Sharpen;

    // Sharpening parameters
    QString sharpenMode            = "Both";  // "Both", "Stellar Only", "Non-Stellar Only"
    float   stellarAmount          = 0.5f;
    float   nonStellarAmount       = 0.5f;
    float   nonStellarPSF          = 3.0f;
    bool    separateChannelsSharpen = false;
    bool    autoPSF                = true;

    // Denoising parameters
    float   denoiseLum             = 0.5f;
    float   denoiseColor           = 0.5f;
    QString denoiseMode            = "full";  // "full", "luminance"
    bool    separateChannelsDenoise = false;

    // Super-resolution parameters
    QString scaleFactor            = "2x";

    // Execution settings
    bool    useGpu                 = true;
};

// ============================================================================
// Worker (runs in a dedicated QThread)
// ============================================================================

/**
 * @brief Background worker that executes the Cosmic Clarity bridge process.
 *        Designed to be moved to a QThread via moveToThread().
 */
class CosmicClarityWorker : public QObject {
    Q_OBJECT

public:
    explicit CosmicClarityWorker(QObject* parent = nullptr);

public slots:
    /**
     * @brief Execute the AI processing pipeline for the given input image.
     * @param input   Source image buffer.
     * @param params  Processing parameters.
     */
    void process(const ImageBuffer& input, const CosmicClarityParams& params);

    /** @brief Request cancellation of the current processing operation. */
    void cancel() { m_stop = true; }

signals:
    /** @brief Emitted when processing completes (success or failure). */
    void finished(const ImageBuffer& output, const QString& errorMsg);

    /** @brief Emitted with real-time log output from the Python bridge. */
    void processOutput(const QString& text);

private:
    std::atomic<bool> m_stop{false};
};

// ============================================================================
// Runner (main-thread interface)
// ============================================================================

/**
 * @brief Thread-safe interface for launching and managing Cosmic Clarity
 *        AI processing. Manages the worker thread lifecycle, provides
 *        synchronous run() with timeout, and supports cancellation.
 */
class CosmicClarityRunner : public QObject {
    Q_OBJECT

public:
    explicit CosmicClarityRunner(QObject* parent = nullptr);
    ~CosmicClarityRunner();

    /**
     * @brief Run the AI processing pipeline synchronously.
     *        Blocks until completion, cancellation, or timeout (15 min).
     * @param input     Source image buffer.
     * @param output    Result image buffer (populated on success).
     * @param params    Processing parameters.
     * @param errorMsg  Optional pointer to receive error description.
     * @return true on success, false on error or timeout.
     */
    bool run(const ImageBuffer& input, ImageBuffer& output,
             const CosmicClarityParams& params, QString* errorMsg);

signals:
    /** @brief Forwarded real-time log output from the worker. */
    void processOutput(const QString& text);

    /** @brief Internal signal: worker has completed. */
    void workerDone();

public slots:
    /** @brief Cancel the current processing operation. */
    void cancel() {
        if (m_worker) {
            m_worker->cancel();
        }
    }

private slots:
    /** @brief Handle worker completion and store results. */
    void onWorkerFinished(const ImageBuffer& output, const QString& errorMsg);

private:
    QPointer<QThread>              m_thread;
    QPointer<CosmicClarityWorker>  m_worker;
    ImageBuffer                    m_output;
    QString                        m_errorMsg;
    bool                           m_finished = false;
};

#endif // COSMICCLARITYRUNNER_H