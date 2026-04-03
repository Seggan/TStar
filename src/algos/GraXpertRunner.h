#ifndef GRAXPERTRUNNER_H
#define GRAXPERTRUNNER_H

/**
 * @file GraXpertRunner.h
 * @brief Threaded runner for the GraXpert background-extraction / denoising tool.
 *
 * Provides a worker-thread model that:
 *  1. Serializes the input ImageBuffer to a temporary raw file.
 *  2. Converts raw data to TIFF via a Python bridge script.
 *  3. Invokes the GraXpert CLI executable.
 *  4. Reads the result TIFF back through the bridge.
 *  5. Returns the processed ImageBuffer to the caller.
 *
 * The synchronous @ref GraXpertRunner::run() method blocks with an event
 * loop and supports cancellation and timeout.
 */

#include <atomic>

#include <QString>
#include <QObject>
#include <QPointer>
#include <QThread>

#include "ImageBuffer.h"

/**
 * @brief Parameters for a GraXpert processing operation.
 */
struct GraXpertParams {
    bool    isDenoise  = false;     ///< false = background extraction, true = denoising.
    float   smoothing  = 0.1f;     ///< Background extraction smoothing (0.0 - 1.0).
    float   strength   = 0.5f;     ///< Denoising strength (0.0 - 1.0).
    QString aiVersion  = "3.0.1";  ///< AI model version.
    bool    useGpu     = true;     ///< Enable GPU acceleration.
};

/* =========================================================================
 * GraXpertWorker -- runs on a dedicated QThread
 * ========================================================================= */

/**
 * @brief Worker object that performs the GraXpert processing on a background thread.
 */
class GraXpertWorker : public QObject {
    Q_OBJECT
public:
    explicit GraXpertWorker(QObject* parent = nullptr);

public slots:
    /** @brief Execute a GraXpert operation. Emits @ref finished when done. */
    void process(const ImageBuffer& input, const GraXpertParams& params);

    /** @brief Request cancellation of the current operation. */
    void cancel() { m_stop = true; }

signals:
    /** @brief Emitted when processing completes (errorMsg is empty on success). */
    void finished(const ImageBuffer& output, const QString& errorMsg);

    /** @brief Emitted for real-time log/status output. */
    void processOutput(const QString& text);

private:
    /** @brief Retrieve the configured GraXpert executable path from QSettings. */
    QString getExecutablePath();

    std::atomic<bool> m_stop{false};
};

/* =========================================================================
 * GraXpertRunner -- main-thread controller
 * ========================================================================= */

/**
 * @brief Synchronous wrapper that manages the worker thread and provides
 *        a blocking @ref run() method with timeout and cancellation support.
 */
class GraXpertRunner : public QObject {
    Q_OBJECT
public:
    explicit GraXpertRunner(QObject* parent = nullptr);
    ~GraXpertRunner();

    /**
     * @brief Execute a GraXpert operation synchronously.
     *
     * Blocks the calling thread (while pumping the event loop) until the
     * worker finishes, the operation is cancelled, or the timeout expires.
     *
     * @param input    Source image data.
     * @param output   Destination for the processed image data.
     * @param params   Processing parameters.
     * @param errorMsg Optional pointer to receive an error description.
     * @return @c true on success.
     */
    bool run(const ImageBuffer& input, ImageBuffer& output,
             const GraXpertParams& params, QString* errorMsg);

signals:
    /** @brief Relayed real-time output from the worker. */
    void processOutput(const QString& text);

    /** @brief Emitted when the operation completes (used internally). */
    void finished();

public slots:
    /** @brief Forward a cancellation request to the worker. */
    void cancel() {
        if (m_worker) m_worker->cancel();
    }

private slots:
    /** @brief Handle the worker's finished signal. */
    void onWorkerFinished(const ImageBuffer& output, const QString& errorMsg);

private:
    QPointer<QThread>          m_thread;
    QPointer<GraXpertWorker>   m_worker;
    ImageBuffer                m_output;
    QString                    m_errorMsg;
    bool                       m_finished = false;
};

#endif // GRAXPERTRUNNER_H