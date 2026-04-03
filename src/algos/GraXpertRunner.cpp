#include "GraXpertRunner.h"
#include "PythonFinder.h"


#include <QSettings>
#include <QTemporaryDir>
#include <QProcess>
#include <QFileInfo>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>

// =============================================================================
// GraXpertWorker -- background-thread worker that drives the GraXpert CLI
// =============================================================================

GraXpertWorker::GraXpertWorker(QObject* parent)
    : QObject(parent)
{}

// -----------------------------------------------------------------------------
// Retrieve the user-configured GraXpert executable path from QSettings.
// -----------------------------------------------------------------------------
QString GraXpertWorker::getExecutablePath()
{
    QSettings settings;
    return settings.value("paths/graxpert").toString();
}

// -----------------------------------------------------------------------------
// Main processing entry point (invoked on the worker thread).
//
// Pipeline:
//   1. Validate inputs and locate the bridge script + bundled Python.
//   2. Write the input ImageBuffer to a raw float file on disk.
//   3. Convert the raw file to TIFF via the Python bridge script.
//   4. Run the GraXpert CLI (background extraction or denoising).
//   5. Load the GraXpert output TIFF back through the bridge script.
//   6. Emit the result as an ImageBuffer.
// -----------------------------------------------------------------------------
void GraXpertWorker::process(const ImageBuffer& input,
                             const GraXpertParams& params)
{
    QString    errorMsg;
    ImageBuffer output;

    // -- Input validation -----------------------------------------------------
    if (input.data().empty()) {
        emit finished(output, "Input buffer is empty");
        return;
    }

    QString exe = getExecutablePath();
    if (exe.isEmpty()) {
        emit finished(output, "GraXpert path not set.");
        return;
    }

    m_stop = false;

    // -- Temporary working directory ------------------------------------------
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        emit finished(output, "Failed to create temp dir.");
        return;
    }

    // -- Locate the Python bridge script --------------------------------------
    // Search in several candidate locations relative to the application binary.
    QString scriptPath = QCoreApplication::applicationDirPath()
                         + "/scripts/graxpert_bridge.py";

    if (!QFile::exists(scriptPath)) {
        scriptPath = QCoreApplication::applicationDirPath()
                     + "/../Resources/scripts/graxpert_bridge.py";
    }
    if (!QFile::exists(scriptPath)) {
        scriptPath = QCoreApplication::applicationDirPath()
                     + "/../src/scripts/graxpert_bridge.py";
    }
    if (!QFile::exists(scriptPath)) {
        emit finished(output,
                      "Bridge script not found at: " + scriptPath);
        return;
    }

    // Locate the bundled Python interpreter
    QString pythonExe = findPythonExecutable();
    if (!QFile::exists(pythonExe)) {
        emit finished(output,
                      "Bundled Python interpreter not found.\n"
                      "Expected path: " + pythonExe);
        return;
    }

    // -- Helper: build a QProcessEnvironment for every bridge subprocess -------
    // Sets PYTHONUNBUFFERED and, on macOS, patches pyvenv.cfg so that the
    // bundled Python.framework is found even when the baked-in Cellar path
    // is stale.  Also injects DYLD_FRAMEWORK_PATH / DYLD_LIBRARY_PATH.
    auto makePythonEnv = []() -> QProcessEnvironment
    {
        QProcessEnvironment env =
            QProcessEnvironment::systemEnvironment();
        env.insert("PYTHONUNBUFFERED", "1");

#if defined(Q_OS_MAC)
        // Patch pyvenv.cfg with the actual framework location at runtime.
        QString pyvenvCfg =
            QCoreApplication::applicationDirPath()
            + "/../Resources/python_venv/pyvenv.cfg";
        if (!QFile::exists(pyvenvCfg)) {
            pyvenvCfg = QCoreApplication::applicationDirPath()
                        + "/../../deps/python_venv/pyvenv.cfg";
        }

        if (QFile::exists(pyvenvCfg)) {
            const QString pythonFwBin =
                QDir::cleanPath(
                    QCoreApplication::applicationDirPath()
                    + "/../Frameworks/Python.framework"
                      "/Versions/Current/bin");

            QFile f(pyvenvCfg);
            if (f.open(QIODevice::ReadWrite | QIODevice::Text)) {
                QString     content = QString::fromUtf8(f.readAll());
                bool        changed = false;
                QStringList lines   = content.split('\n');

                for (int i = 0; i < lines.size(); ++i) {
                    if (lines[i].trimmed().startsWith("home =")) {
                        QString newLine = "home = " + pythonFwBin;
                        if (lines[i] != newLine) {
                            lines[i] = newLine;
                            changed   = true;
                        }
                    } else if (lines[i].trimmed().startsWith("executable =")) {
                        QString newLine =
                            "executable = " + pythonFwBin + "/python3";
                        if (lines[i] != newLine) {
                            lines[i] = newLine;
                            changed   = true;
                        }
                    }
                }

                if (changed) {
                    f.resize(0);
                    f.seek(0);
                    f.write(lines.join('\n').toUtf8());
                }
                f.close();
            }
        }

        // Ensure dyld can resolve Python.framework from inside the bundle.
        const QString frameworksDir =
            QCoreApplication::applicationDirPath() + "/../Frameworks";

        if (QDir(frameworksDir).exists()) {
            const QString curFw  = env.value("DYLD_FRAMEWORK_PATH");
            env.insert("DYLD_FRAMEWORK_PATH",
                       curFw.isEmpty() ? frameworksDir
                                       : frameworksDir + ":" + curFw);

            const QString curLib = env.value("DYLD_LIBRARY_PATH");
            env.insert("DYLD_LIBRARY_PATH",
                       curLib.isEmpty() ? frameworksDir
                                        : frameworksDir + ":" + curLib);
        }
#endif
        return env;
    };

    // =========================================================================
    // Step 1 -- Write the input ImageBuffer to a raw float file
    // =========================================================================
    const QString rawInputFile = tempDir.filePath("input.raw");
    {
        QFile raw(rawInputFile);
        if (!raw.open(QIODevice::WriteOnly)) {
            emit finished(output, "Failed to write raw input.");
            return;
        }
        raw.write(reinterpret_cast<const char*>(input.data().data()),
                  static_cast<qint64>(input.data().size() * sizeof(float)));
        raw.close();
    }

    // =========================================================================
    // Step 2 -- Convert the raw float file to TIFF via the bridge script
    // =========================================================================
    const QString inputFile = tempDir.filePath("input.tiff");
    QString       convLog;
    {
        QStringList args;
        args << scriptPath
             << "save"
             << inputFile
             << QString::number(input.width())
             << QString::number(input.height())
             << QString::number(input.channels())
             << rawInputFile;

        QProcess p;
        p.setProcessEnvironment(makePythonEnv());
        p.setProcessChannelMode(QProcess::MergedChannels);

        connect(&p, &QProcess::readyReadStandardOutput,
                [&p, &convLog, this]() {
            QString txt =
                QString::fromUtf8(p.readAllStandardOutput()).trimmed();
            if (!txt.isEmpty()) {
                emit processOutput(txt);
                convLog.append(txt + "\n");
            }
        });

        p.start(pythonExe, args);

        if (!p.waitForFinished(60000)) {
            emit finished(output,
                          "Bridge timeout saving TIFF.\n\nLog:\n"
                          + convLog.trimmed().right(1000));
            return;
        }
        if (p.exitCode() != 0) {
            QString err =
                QString::fromUtf8(p.readAllStandardError()).trimmed();
            if (err.isEmpty() && !convLog.isEmpty())
                err = convLog.trimmed().right(1000);
            emit finished(output,
                          "Bridge failed to save TIFF:\n\n" + err);
            return;
        }
    }

    emit processOutput("Input staged via Python Bridge: " + inputFile);

    // =========================================================================
    // Step 3 -- Run the GraXpert CLI
    // =========================================================================
    const QString op = params.isDenoise ? "denoising"
                                        : "background-extraction";

    QStringList graxpertArgs;
    graxpertArgs << "-cmd" << op << inputFile << "-cli";
    graxpertArgs << "-gpu" << (params.useGpu ? "true" : "false");

    if (params.isDenoise) {
        graxpertArgs << "-strength"
                     << QString::number(params.strength, 'f', 2);
        graxpertArgs << "-batch_size"
                     << (params.useGpu ? "4" : "1");

        if (!params.aiVersion.isEmpty()
            && params.aiVersion != "Latest (auto)") {
            graxpertArgs << "-ai_version" << params.aiVersion;
        }
    } else {
        graxpertArgs << "-smoothing"
                     << QString::number(params.smoothing, 'f', 2);
    }

    emit processOutput("Running GraXpert...");

    QProcess process;
    process.setProgram(exe);
    process.setArguments(graxpertArgs);

    QString graxpertLog;

    // Forward stdout from the GraXpert process in real time.
    connect(&process, &QProcess::readyReadStandardOutput,
            [&process, &graxpertLog, this]() {
        QString out = process.readAllStandardOutput();
        if (!out.isEmpty()) {
            emit processOutput(out.trimmed());
            graxpertLog.append(out + "\n");
        }
    });

    // Forward stderr, tagging informational lines differently from errors.
    connect(&process, &QProcess::readyReadStandardError,
            [&process, &graxpertLog, this]() {
        QString err = process.readAllStandardError();
        if (!err.isEmpty()) {
            graxpertLog.append(err + "\n");
            QString trimmed = err.trimmed();
            if (trimmed.contains("INFO") || trimmed.contains("Progress")) {
                emit processOutput(trimmed);
            } else {
                emit processOutput("LOG: " + trimmed);
            }
        }
    });

    process.start();

    // Poll until the process finishes, checking for user cancellation.
    // Hard timeout: 10 minutes.
    static constexpr int kPollIntervalMs = 100;
    static constexpr int kTimeoutMs      = 600000;
    int elapsed = 0;

    while (process.state() != QProcess::NotRunning) {
        if (m_stop) {
            process.kill();
            process.waitForFinished();
            emit finished(output, "Process cancelled by user.");
            return;
        }
        QCoreApplication::processEvents();
        QThread::msleep(kPollIntervalMs);
        elapsed += kPollIntervalMs;

        if (elapsed > kTimeoutMs) {
            process.kill();
            emit finished(output, "Process timed out.");
            return;
        }
    }

    if (process.exitCode() != 0) {
        const QString tail = graxpertLog.trimmed().right(1000);
        errorMsg = QString(
            "GraXpert failed (Exit Code %1): %2\n\nLog:\n%3")
            .arg(process.exitCode())
            .arg(process.errorString().isEmpty()
                     ? "Unknown error"
                     : process.errorString())
            .arg(tail);
        emit finished(output, errorMsg);
        return;
    }

    // =========================================================================
    // Step 4 -- Locate the GraXpert output file
    // =========================================================================
    // GraXpert naming convention: <basename>_GraXpert.<ext>
    QString outputFile = tempDir.filePath("input_GraXpert.tiff");

    if (!QFileInfo::exists(outputFile)) {
        // Fallback: scan the temp directory for any matching file.
        QStringList    filters;
        filters << "input_GraXpert.*";
        QFileInfoList found =
            QDir(tempDir.path()).entryInfoList(filters, QDir::Files);

        if (!found.isEmpty()) {
            outputFile = found.first().absoluteFilePath();
        } else {
            emit finished(output, "GraXpert output file not found.");
            return;
        }
    }

    // =========================================================================
    // Step 5 -- Load the result TIFF back via the bridge script
    // =========================================================================
    const QString rawResult = tempDir.filePath("result.raw");
    {
        QStringList args;
        args << scriptPath << "load" << outputFile << rawResult;

        QProcess p;
        p.setProcessEnvironment(makePythonEnv());
        p.setProcessChannelMode(QProcess::MergedChannels);

        convLog.clear();
        connect(&p, &QProcess::readyReadStandardOutput,
                [&p, &convLog, this]() {
            QString txt =
                QString::fromUtf8(p.readAllStandardOutput()).trimmed();
            if (!txt.isEmpty()) {
                emit processOutput(txt);
                convLog.append(txt + "\n");
            }
        });

        p.start(pythonExe, args);

        if (!p.waitForFinished(60000)) {
            emit finished(output,
                          "Bridge timeout loading result.\n\nLog:\n"
                          + convLog.trimmed().right(1000));
            return;
        }

        const QString outData = convLog.trimmed();

        if (p.exitCode() != 0 || outData.contains("Error")) {
            QString err =
                QString::fromUtf8(p.readAllStandardError()).trimmed();
            if (err.isEmpty() && !convLog.isEmpty())
                err = convLog.trimmed().right(1000);
            emit finished(output,
                          "Bridge error loading result:\n\n" + err);
            return;
        }

        // Locate the "RESULT: W H C" marker emitted by the bridge script.
        QString     resultLine;
        QStringList lines = outData.split('\n');
        for (const QString& line : lines) {
            if (line.trimmed().startsWith("RESULT:")) {
                resultLine = line.trimmed();
                break;
            }
        }

        if (resultLine.isEmpty()) {
            emit finished(output,
                          "Bridge failed to provide result marker: "
                          + outData);
            return;
        }

        // Parse dimensions from the marker line.
        QStringList parts =
            resultLine.mid(7).trimmed().split(
                QRegularExpression("\\s+"));

        if (parts.size() < 3) {
            emit finished(output,
                          "Bridge failed to parse dimensions: "
                          + resultLine);
            return;
        }

        const int w = parts[0].toInt();
        const int h = parts[1].toInt();
        const int c = parts[2].toInt();

        if (w <= 0 || h <= 0) {
            emit finished(output,
                          "Bridge returned invalid dimensions: "
                          + resultLine);
            return;
        }

        // Read the raw float result into an ImageBuffer.
        QFile rawRes(rawResult);
        if (rawRes.open(QIODevice::ReadOnly)) {
            QByteArray blob = rawRes.readAll();
            std::vector<float> data(blob.size() / sizeof(float));
            std::memcpy(data.data(), blob.constData(),
                        static_cast<size_t>(blob.size()));

            output.setData(w, h, c, data);
            output.setMetadata(input.metadata());

            emit processOutput(
                QString("Loaded via Bridge: %1x%2 %3ch")
                    .arg(w).arg(h).arg(c));
        } else {
            emit finished(output, "Failed to read raw result.");
            return;
        }
    }

    // -- Success --------------------------------------------------------------
    emit finished(output, "");
}

// =============================================================================
// GraXpertRunner -- synchronous facade that manages the worker thread
// =============================================================================

GraXpertRunner::GraXpertRunner(QObject* parent)
    : QObject(parent)
{}

// -----------------------------------------------------------------------------
// Destructor: gracefully shut down the worker thread.
// -----------------------------------------------------------------------------
GraXpertRunner::~GraXpertRunner()
{
    if (m_thread) {
        m_thread->quit();
        if (!m_thread->wait(5000)) {
            m_thread->terminate();
            m_thread->wait();
        }
    }
}

// -----------------------------------------------------------------------------
// Synchronous convenience method.
// Launches the worker on a dedicated thread and blocks (with event
// processing) until completion or a 15-minute timeout.
// Returns true on success; on failure, fills *errorMsg if non-null.
// -----------------------------------------------------------------------------
bool GraXpertRunner::run(const ImageBuffer&      input,
                         ImageBuffer&            output,
                         const GraXpertParams&   params,
                         QString*                errorMsg)
{
    // Lazily create the worker thread on first use.
    if (!m_thread) {
        m_thread = new QThread(this);
        m_worker = new GraXpertWorker();
        m_worker->moveToThread(m_thread);

        connect(m_thread, &QThread::finished,
                m_worker, &QObject::deleteLater);
        connect(m_worker, &GraXpertWorker::finished,
                this,     &GraXpertRunner::onWorkerFinished);
        connect(m_worker, &GraXpertWorker::processOutput,
                this,     &GraXpertRunner::processOutput);

        m_thread->start();
    }

    // Reset synchronization state.
    m_finished = false;

    // Dispatch the work to the worker thread via a queued invocation.
    QMetaObject::invokeMethod(
        m_worker, "process", Qt::QueuedConnection,
        Q_ARG(const ImageBuffer&,    input),
        Q_ARG(const GraXpertParams&, params));

    // Block until the worker signals completion or the timeout fires.
    QEventLoop loop;
    connect(this, &GraXpertRunner::finished, &loop, &QEventLoop::quit);

    static constexpr int kTimeoutMs = 15 * 60 * 1000;   // 15 minutes
    QTimer timer;
    timer.setSingleShot(true);
    timer.start(kTimeoutMs);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    loop.exec();
    timer.stop();

    // Evaluate outcome.
    if (!m_finished) {
        if (errorMsg) *errorMsg = "Operation timed out";
        m_errorMsg = "Operation timed out";
        return false;
    }

    if (!m_errorMsg.isEmpty()) {
        if (errorMsg) *errorMsg = m_errorMsg;
        return false;
    }

    output = m_output;
    return true;
}

// -----------------------------------------------------------------------------
// Slot connected to GraXpertWorker::finished.
// Stores the result and signals the blocking event loop to exit.
// -----------------------------------------------------------------------------
void GraXpertRunner::onWorkerFinished(const ImageBuffer& output,
                                      const QString&     errorMsg)
{
    m_output   = output;
    m_errorMsg = errorMsg;
    m_finished = true;
    emit finished();
}