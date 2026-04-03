#include "RARRunner.h"
#include "SimpleTiffWriter.h"
#include "SimpleTiffReader.h"
#include "PythonFinder.h"

#include <QProcess>
#include <QTemporaryDir>
#include <QCoreApplication>
#include <QThread>
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QStandardPaths>
#include <iostream>

// ----------------------------------------------------------------------------
// Constructor
// ----------------------------------------------------------------------------
RARRunner::RARRunner(QObject* parent)
    : QObject(parent)
{}

// ----------------------------------------------------------------------------
// run
//
// Full pipeline:
//   1. Validate the model file path.
//   2. Write the input buffer to a temporary float32 TIFF.
//   3. Locate the bundled Python interpreter and worker script.
//   4. Patch pyvenv.cfg so the virtual environment resolves correctly inside
//      the application bundle (macOS only).
//   5. Launch the Python worker and relay its stdout as processOutput signals.
//   6. Poll for completion with a 50 ms interval, honouring cancellation.
//   7. Read the raw float32 binary produced by the worker and populate output.
// ----------------------------------------------------------------------------
bool RARRunner::run(const ImageBuffer& input,
                    ImageBuffer&       output,
                    const RARParams&   params,
                    QString*           errorMsg)
{
    // --- Validate model ---
    if (!QFileInfo::exists(params.modelPath)) {
        if (errorMsg) *errorMsg = "Model file not found: " + params.modelPath;
        return false;
    }

    m_stop = false;

    // --- Create temporary working directory ---
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        if (errorMsg) *errorMsg = "Failed to create temporary directory.";
        return false;
    }

    const QString inputFile  = tempDir.filePath("rar_input.tif");
    // Raw float32 binary output avoids TIFF parsing overhead and precision issues.
    const QString outputFile = tempDir.filePath("rar_output.raw");

    // --- Locate the Python worker script ---
    // Search in order: installed location, macOS bundle Resources, source tree.
    QString scriptPath = QCoreApplication::applicationDirPath() + "/scripts/rar_worker.py";

    if (!QFile::exists(scriptPath))
        scriptPath = QCoreApplication::applicationDirPath() + "/../Resources/scripts/rar_worker.py";

    if (!QFile::exists(scriptPath))
        scriptPath = QCoreApplication::applicationDirPath() + "/../src/scripts/rar_worker.py";

    // --- Write float32 input TIFF ---
    emit processOutput("Saving temporary input file...");
    if (!SimpleTiffWriter::write(inputFile,
                                 input.width(), input.height(), input.channels(),
                                 SimpleTiffWriter::Format_float32,
                                 input.data(), QByteArray(), errorMsg)) {
        return false;
    }

    // --- Build argument list for the worker script ---
    QStringList args;
    args << scriptPath
         << "--input"    << inputFile
         << "--output"   << outputFile
         << "--model"    << params.modelPath
         << "--patch"    << QString::number(params.patchSize)
         << "--overlap"  << QString::number(params.overlap)
         << "--provider" << params.provider;

    emit processOutput("Starting Aberration Removal AI worker...");

    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);

    QString fullLog;

    // Relay worker stdout to the UI and console, categorising by message type.
    connect(&p, &QProcess::readyReadStandardOutput, [this, &p, &fullLog]() {
        QByteArray raw = p.readAllStandardOutput();
        QString    txt = QString::fromUtf8(raw).trimmed();
        if (txt.isEmpty()) return;

        if (txt.contains("Progress:")) {
            // Overwrite the current console line for progress updates.
            std::cout << "\r" << txt.toStdString() << "  " << std::flush;
        } else if (txt.startsWith("RESULT")) {
            std::cout << "\n[AI] " << txt.toStdString() << std::endl;
        } else if (txt.contains("INFO") || txt.contains("DEBUG")) {
            std::cout << "\n[AI] " << txt.toStdString() << std::endl;
        }

        emit processOutput(txt);
        fullLog.append(txt + "\n");
    });

    // Locate bundled Python interpreter.
    QString pythonExe = findPythonExecutable();
    if (!QFile::exists(pythonExe)) {
        if (errorMsg)
            *errorMsg = "Bundled Python interpreter not found.\nExpected path: " + pythonExe;
        return false;
    }

    // --- Configure process environment ---
    {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("PYTHONUNBUFFERED", "1");

#if defined(Q_OS_MAC)
        // Patch pyvenv.cfg so the virtual environment resolves to the bundled
        // Python.framework regardless of the path baked into the binary at build time.
        QString pyvenvCfg = QCoreApplication::applicationDirPath()
                            + "/../Resources/python_venv/pyvenv.cfg";
        if (!QFile::exists(pyvenvCfg))
            pyvenvCfg = QCoreApplication::applicationDirPath()
                        + "/../../deps/python_venv/pyvenv.cfg";

        if (QFile::exists(pyvenvCfg)) {
            const QString pythonFwBin = QDir::cleanPath(
                QCoreApplication::applicationDirPath()
                + "/../Frameworks/Python.framework/Versions/Current/bin");

            QFile f(pyvenvCfg);
            if (f.open(QIODevice::ReadWrite | QIODevice::Text)) {
                QString     content = QString::fromUtf8(f.readAll());
                bool        changed = false;
                QStringList lines   = content.split('\n');

                for (int i = 0; i < lines.size(); ++i) {
                    if (lines[i].trimmed().startsWith("home =")) {
                        const QString newLine = "home = " + pythonFwBin;
                        if (lines[i] != newLine) { lines[i] = newLine; changed = true; }
                    } else if (lines[i].trimmed().startsWith("executable =")) {
                        const QString newLine = "executable = " + pythonFwBin + "/python3";
                        if (lines[i] != newLine) { lines[i] = newLine; changed = true; }
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

        // Prepend the bundle Frameworks directory to the dynamic library search paths
        // so Python.framework and native extension modules (.so) resolve correctly.
        const QString frameworksDir = QCoreApplication::applicationDirPath() + "/../Frameworks";
        if (QDir(frameworksDir).exists()) {
            const QString curFw  = env.value("DYLD_FRAMEWORK_PATH");
            const QString curLib = env.value("DYLD_LIBRARY_PATH");
            env.insert("DYLD_FRAMEWORK_PATH",
                       curFw.isEmpty()  ? frameworksDir : frameworksDir + ":" + curFw);
            env.insert("DYLD_LIBRARY_PATH",
                       curLib.isEmpty() ? frameworksDir : frameworksDir + ":" + curLib);
        }
#endif

        p.setProcessEnvironment(env);
    }

    // --- Launch worker process ---
    p.start(pythonExe, args);

    if (!p.waitForStarted(3000)) {
        if (errorMsg) *errorMsg = "Failed to start AI worker: " + p.errorString();
        return false;
    }

    // --- Wait loop with cancellation support (50 ms polling interval) ---
    while (p.state() != QProcess::NotRunning) {
        if (m_stop) {
            p.kill();
            p.waitForFinished();
            if (errorMsg) *errorMsg = "Operation aborted by user.";
            return false;
        }
        QCoreApplication::processEvents();
        QThread::msleep(50);
    }

    // --- Check exit status ---
    if (p.exitCode() != 0) {
        const QString tail = fullLog.trimmed().right(1000);
        if (errorMsg) {
            *errorMsg = QString("Worker process failed (exit code %1): %2\n\nLog:\n%3")
                        .arg(p.exitCode())
                        .arg(p.errorString().isEmpty() ? "Unknown error" : p.errorString())
                        .arg(tail);
        }
        return false;
    }

    if (!QFile::exists(outputFile)) {
        if (errorMsg) *errorMsg = "Worker did not produce an output file.";
        return false;
    }

    // --- Read raw float32 result ---
    emit processOutput("Loading result...");

    QFile resFile(outputFile);
    if (!resFile.open(QIODevice::ReadOnly)) {
        if (errorMsg) *errorMsg = "Failed to open result file.";
        return false;
    }

    const QByteArray blob = resFile.readAll();
    resFile.close();

    // Validate byte count: expected w * h * channels * sizeof(float).
    const size_t expectedBytes = static_cast<size_t>(input.width())
                                 * input.height()
                                 * input.channels()
                                 * sizeof(float);

    if (static_cast<size_t>(blob.size()) != expectedBytes) {
        if (errorMsg) {
            *errorMsg = QString("Result size mismatch. Expected %1 bytes, got %2.")
                        .arg(expectedBytes)
                        .arg(blob.size());
        }
        return false;
    }

    std::vector<float> data(blob.size() / sizeof(float));
    std::memcpy(data.data(), blob.constData(), blob.size());

    emit processOutput(QString("Loaded RAW result: %1 bytes.").arg(blob.size()));

    output.setData(input.width(), input.height(), input.channels(), data);
    output.setMetadata(input.metadata());  // Preserve WCS and all auxiliary metadata.

    return true;
}