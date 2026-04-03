// ============================================================================
// CosmicClarityRunner.cpp
// Implementation of the Cosmic Clarity AI processing runner.
// Manages Python bridge invocation, raw I/O, cancel/timeout handling,
// and platform-specific environment configuration (macOS framework paths).
// ============================================================================

#include "CosmicClarityRunner.h"
#include "ImageBuffer.h"
#include "network/ModelDownloader.h"

#include <QSettings>
#include <QProcess>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QThread>
#include <QDebug>
#include <QRegularExpression>
#include <QEventLoop>
#include <QTimer>
#include <QStandardPaths>
#include <QWaitCondition>
#include <QMutex>
#include <QJsonObject>
#include <QJsonDocument>
#include <QOverload>

#include "PythonFinder.h"

// ============================================================================
// CosmicClarityWorker Implementation
// ============================================================================

CosmicClarityWorker::CosmicClarityWorker(QObject* parent)
    : QObject(parent)
{
}

void CosmicClarityWorker::process(const ImageBuffer& input,
                                  const CosmicClarityParams& params)
{
    ImageBuffer output;

    // ---- Validate input ----
    if (input.data().empty()) {
        emit finished(output, "Input buffer is empty");
        return;
    }

    // ---- Locate downloaded models directory ----
    QString modelsRoot = ModelDownloader::cosmicClarityRoot();
    if (modelsRoot.isEmpty() || !QDir(modelsRoot).exists()) {
        emit finished(output,
            "Cosmic Clarity models not found. Please download them in Settings.");
        return;
    }

    m_stop = false;

    // ---- Prepare temp I/O directory ----
    QDir modelsDir(modelsRoot);
    QDir tempDir(modelsDir.filePath("temp_io"));
    if (!tempDir.exists()) {
        tempDir.mkpath(".");
    }

    auto purge = [](QDir& d) {
        if (!d.exists()) return;
        for (const auto& fi : d.entryInfoList(QDir::Files | QDir::NoDotAndDotDot)) {
            QFile::remove(fi.absoluteFilePath());
        }
    };
    purge(tempDir);

    // Locate bundled Python interpreter.
    QString pythonExe = findPythonExecutable();
    if (!QFile::exists(pythonExe)) {
        emit finished(output,
            "Bundled Python interpreter not found.\nExpected path: " + pythonExe);
        return;
    }

    // ---- Locate bridge script ----
    QString bridge = QCoreApplication::applicationDirPath()
                     + "/scripts/cosmic_bridge.py";
    if (!QFile::exists(bridge)) {
        bridge = QCoreApplication::applicationDirPath()
                 + "/../Resources/scripts/cosmic_bridge.py";
    }
    if (!QFile::exists(bridge)) {
        bridge = QCoreApplication::applicationDirPath()
                 + "/../src/scripts/cosmic_bridge.py";
    }
    if (!QFile::exists(bridge)) {
        emit finished(output, "Bridge script (cosmic_bridge.py) not found.");
        return;
    }

    // ---- Build parameters JSON ----
    QJsonObject json;

    if (params.mode == CosmicClarityParams::Mode_Sharpen)   json["mode"] = "sharpen";
    else if (params.mode == CosmicClarityParams::Mode_Denoise) json["mode"] = "denoise";
    else if (params.mode == CosmicClarityParams::Mode_Both)    json["mode"] = "both";
    else if (params.mode == CosmicClarityParams::Mode_SuperRes) json["mode"] = "superres";

    json["use_gpu"]                    = params.useGpu;
    json["sharpen_mode"]               = params.sharpenMode;
    json["stellar_amount"]             = params.stellarAmount;
    json["nonstellar_amount"]          = params.nonStellarAmount;
    json["nonstellar_psf"]             = params.nonStellarPSF;
    json["auto_psf"]                   = params.autoPSF;
    json["separate_channels_sharpen"]  = params.separateChannelsSharpen;
    json["denoise_luma"]               = params.denoiseLum;
    json["denoise_color"]              = params.denoiseColor;
    json["denoise_mode"]               = params.denoiseMode;
    json["separate_channels_denoise"]  = params.separateChannelsDenoise;

    int scale = 2;
    if (params.scaleFactor.contains("3")) scale = 3;
    if (params.scaleFactor.contains("4")) scale = 4;

    json["scale"]      = scale;
    json["width"]      = input.width();
    json["height"]     = input.height();
    json["channels"]   = input.channels();
    json["models_dir"] = modelsRoot;

    // Write parameters file
    QString paramsFile = tempDir.filePath("params.json");
    {
        QFile f(paramsFile);
        if (!f.open(QIODevice::WriteOnly)) {
            emit finished(output, "Failed to write params.json");
            return;
        }
        f.write(QJsonDocument(json).toJson());
        f.close();
    }

    // ---- Write raw input data ----
    QString rawIn = tempDir.filePath("input.raw");
    {
        QFile f(rawIn);
        if (!f.open(QIODevice::WriteOnly)) {
            emit finished(output, "Failed to write input.raw");
            return;
        }
        f.write(reinterpret_cast<const char*>(input.data().data()),
                input.data().size() * sizeof(float));
        f.close();
    }

    QString rawOut = tempDir.filePath("output.raw");

    // ---- Launch Python bridge process ----
    emit processOutput("Launching Cosmic Clarity (ONNX)...");

    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONUNBUFFERED", "1");

#if defined(Q_OS_MAC)
    // Configure macOS framework paths so the bundled Python.framework is found
    {
        // Fix pyvenv.cfg to point to the actual bundled framework location
        QString pyvenvCfg = QCoreApplication::applicationDirPath()
                            + "/../Resources/python_venv/pyvenv.cfg";
        if (!QFile::exists(pyvenvCfg)) {
            pyvenvCfg = QCoreApplication::applicationDirPath()
                        + "/../../deps/python_venv/pyvenv.cfg";
        }

        if (QFile::exists(pyvenvCfg)) {
            QString pythonFwBin = QDir::cleanPath(
                QCoreApplication::applicationDirPath()
                + "/../Frameworks/Python.framework/Versions/Current/bin");

            QFile f(pyvenvCfg);
            if (f.open(QIODevice::ReadWrite | QIODevice::Text)) {
                QString content = QString::fromUtf8(f.readAll());
                bool changed = false;
                QStringList lines = content.split('\n');

                for (int i = 0; i < lines.size(); ++i) {
                    if (lines[i].trimmed().startsWith("home =")) {
                        QString newLine = "home = " + pythonFwBin;
                        if (lines[i] != newLine) {
                            lines[i] = newLine;
                            changed = true;
                        }
                    } else if (lines[i].trimmed().startsWith("executable =")) {
                        QString newLine = "executable = " + pythonFwBin + "/python3";
                        if (lines[i] != newLine) {
                            lines[i] = newLine;
                            changed = true;
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

        // Set DYLD paths so the dynamic linker finds the bundled framework
        const QString frameworksDir =
            QCoreApplication::applicationDirPath() + "/../Frameworks";
        if (QDir(frameworksDir).exists()) {
            const QString curFw = env.value("DYLD_FRAMEWORK_PATH");
            env.insert("DYLD_FRAMEWORK_PATH",
                       curFw.isEmpty() ? frameworksDir
                                       : frameworksDir + ":" + curFw);

            const QString curLib = env.value("DYLD_LIBRARY_PATH");
            env.insert("DYLD_LIBRARY_PATH",
                       curLib.isEmpty() ? frameworksDir
                                        : frameworksDir + ":" + curLib);
        }
    }
#endif

    proc.setProcessEnvironment(env);
    proc.setProgram(pythonExe);
    proc.setArguments(QStringList()
        << bridge << "process" << paramsFile << rawIn << rawOut);
    proc.setProcessChannelMode(QProcess::MergedChannels);

    // Real-time stdout relay
    QString aiLog;
    connect(&proc, &QProcess::readyReadStandardOutput,
            [&proc, &aiLog, this]() {
        QString txt = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        if (!txt.isEmpty()) {
            emit processOutput(txt);
            aiLog.append(txt + "\n");
        }
    });

    proc.start();
    if (!proc.waitForStarted(10000)) {
        emit finished(output, "Failed to start Python process.");
        return;
    }

    // ---- Poll for completion with cancel and timeout support ----
    int       elapsed   = 0;
    const int interval  = 200;         // ms
    const int timeoutMs = 20 * 60 * 1000;  // 20 minutes

    while (!proc.waitForFinished(interval)) {
        if (m_stop) {
            proc.kill();
            proc.waitForFinished(3000);
            emit finished(output, "Cancelled.");
            return;
        }
        elapsed += interval;
        if (elapsed > timeoutMs) {
            proc.kill();
            proc.waitForFinished(3000);
            emit finished(output, "Timed out.");
            return;
        }
    }

    // Capture any remaining output
    {
        QString tail = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        if (!tail.isEmpty()) {
            emit processOutput(tail);
            aiLog.append(tail + "\n");
        }
    }

    // Check exit status
    if (proc.exitCode() != 0) {
        QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        if (err.isEmpty()) {
            err = "bridge exited with code " + QString::number(proc.exitCode());
        }
        emit finished(output,
            "Python error: " + err + "\n\nLog:\n" + aiLog.trimmed().right(1000));
        return;
    }

    // ---- Read result raw file ----
    if (!QFile::exists(rawOut)) {
        emit finished(output, "Result file not found.");
        return;
    }

    int outW = input.width();
    int outH = input.height();
    int outC = input.channels();

    if (params.mode == CosmicClarityParams::Mode_SuperRes) {
        outW *= scale;
        outH *= scale;
    }

    QFile rawRes(rawOut);
    if (!rawRes.open(QIODevice::ReadOnly)) {
        emit finished(output, "Failed to read output.raw");
        return;
    }

    qint64 expected = static_cast<qint64>(outW) * outH * outC * sizeof(float);
    if (rawRes.size() != expected) {
        emit finished(output,
            QString("Size mismatch: expected %1 got %2")
                .arg(expected).arg(rawRes.size()));
        return;
    }

    QByteArray blob = rawRes.readAll();
    rawRes.close();

    std::vector<float> data(blob.size() / sizeof(float));
    memcpy(data.data(), blob.constData(), blob.size());

    output.setData(outW, outH, outC, data);
    output.setMetadata(input.metadata());

    // Clean up temporary files
    purge(tempDir);

    emit processOutput(QString("Done: %1x%2 %3ch").arg(outW).arg(outH).arg(outC));
    emit finished(output, "");
}

// ============================================================================
// CosmicClarityRunner Implementation
// ============================================================================

CosmicClarityRunner::CosmicClarityRunner(QObject* parent)
    : QObject(parent)
{
}

CosmicClarityRunner::~CosmicClarityRunner()
{
    if (m_thread) {
        m_thread->quit();
        if (!m_thread->wait(5000)) {
            m_thread->terminate();
            m_thread->wait();
        }
    }
}

bool CosmicClarityRunner::run(const ImageBuffer& input, ImageBuffer& output,
                              const CosmicClarityParams& params,
                              QString* errorMsg)
{
    // Create thread and worker on first use
    if (!m_thread) {
        m_thread = new QThread(this);
        m_worker = new CosmicClarityWorker();
        m_worker->moveToThread(m_thread);

        connect(m_thread, &QThread::finished,
                m_worker, &QObject::deleteLater);
        connect(m_worker, &CosmicClarityWorker::finished,
                this,     &CosmicClarityRunner::onWorkerFinished);
        connect(m_worker, &CosmicClarityWorker::processOutput,
                this,     &CosmicClarityRunner::processOutput);

        m_thread->start();
    }

    // Reset synchronization state
    m_finished = false;

    // Dispatch processing to worker thread
    QMetaObject::invokeMethod(m_worker, "process", Qt::QueuedConnection,
                              Q_ARG(const ImageBuffer&, input),
                              Q_ARG(const CosmicClarityParams&, params));

    // Block until completion or timeout
    QEventLoop loop;

    QObject::connect(
        m_worker,
        QOverload<const ImageBuffer&, const QString&>::of(
            &CosmicClarityWorker::finished),
        this,
        &CosmicClarityRunner::onWorkerFinished);

    QObject::connect(this, &CosmicClarityRunner::workerDone,
                     &loop, &QEventLoop::quit);

    QTimer timer;
    timer.setSingleShot(true);
    timer.start(15 * 60 * 1000);  // 15-minute timeout
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    loop.exec();
    timer.stop();

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

void CosmicClarityRunner::onWorkerFinished(const ImageBuffer& output,
                                           const QString& errorMsg)
{
    m_output   = output;
    m_errorMsg = errorMsg;
    m_finished = true;
    emit workerDone();
}