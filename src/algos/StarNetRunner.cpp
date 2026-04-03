#include "StarNetRunner.h"
#include "io/SimpleTiffWriter.h"
#include "io/SimpleTiffReader.h"

#include <QProcess>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QThread>
#include <QSettings>
#include <QDebug>
#include <QStandardPaths>
#include <QEventLoop>
#include <QTimer>

#include <cmath>
#include <algorithm>
#include <iostream>
#include <vector>

// ============================================================================
// Internal math helpers
// ============================================================================

// Midtone Transfer Function: maps input x to output y given midtone parameter m.
// Returns 0 for x <= 0, 1 for x >= 1, and 0.5 for a degenerate denominator.
static float mtf_calc(float x, float m) {
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    float num = (m - 1.0f) * x;
    float den = (2.0f * m - 1.0f) * x - m;
    if (std::abs(den) < 1e-9f) return 0.5f;
    return std::max(0.0f, std::min(1.0f, num / den));
}

// Inverse Midtone Transfer Function: recovers the original value from a
// stretched value y given midtone parameter m.
static float mtf_calc_inv(float y, float m) {
    if (y <= 0.0f) return 0.0f;
    if (y >= 1.0f) return 1.0f;
    float num = m * y;
    float den = (2.0f * m - 1.0f) * y - m + 1.0f;
    if (std::abs(den) < 1e-9f) return 0.0f;
    return std::max(0.0f, std::min(1.0f, num / den));
}

// Compute the median and Median Absolute Deviation (MAD) from a strided
// channel in a flat interleaved float buffer.
// The stride and offset parameters select a specific channel from an
// interleaved multi-channel array. A subsampled view is used for performance
// on large images, keeping the sample count below 200 000.
static void get_median_mad(const std::vector<float>& data,
                            int step, size_t offset,
                            float& median, float& mad)
{
    if (data.empty()) { median = 0.0f; mad = 0.0f; return; }

    size_t total  = data.size() / step;
    size_t stride = 1;
    if (total > 200000) stride = total / 100000;

    std::vector<float> samples;
    samples.reserve(total / stride + 1000);

    for (size_t i = 0; i < total; i += stride) {
        samples.push_back(data[i * step + offset]);
    }

    if (samples.empty()) { median = 0.0f; mad = 0.0f; return; }

    size_t n   = samples.size();
    auto   mid = samples.begin() + n / 2;
    std::nth_element(samples.begin(), mid, samples.end());
    median = *mid;

    // MAD: median of absolute deviations from the median.
    std::vector<float> absdevs;
    absdevs.reserve(n);
    for (float v : samples) absdevs.push_back(std::abs(v - median));

    mid = absdevs.begin() + n / 2;
    std::nth_element(absdevs.begin(), mid, absdevs.end());

    // Scale by 1.4826 to make MAD a consistent estimator of sigma for a normal distribution.
    mad = (*mid) * 1.4826f;
}

// ============================================================================
// StarNetWorker Implementation
// ============================================================================

StarNetWorker::StarNetWorker(QObject* parent)
    : QObject(parent)
{}

// Retrieve the StarNet++ executable path from persistent application settings.
QString StarNetWorker::getExecutablePath() {
    QSettings s;
    return s.value("paths/starnet", "").toString();
}

// Compute per-channel MTF stretch parameters from the image statistics.
// Uses the standard PixInsight auto-stretch formula:
//   shadows clipping at median - 2.8 * MAD, target background at 0.25.
StarNetWorker::MTFParams StarNetWorker::computeMtfParams(const ImageBuffer& img) {
    MTFParams p;
    constexpr float shadows_clipping = -2.8f;
    constexpr float targetbg         =  0.25f;

    const int          c_in = img.channels();
    const auto&        data = img.data();

    for (int c = 0; c < 3; ++c) {
        int ch_idx = (c_in == 1) ? 0 : c;

        if (ch_idx >= c_in) {
            // Replicate channel 0 parameters for missing channels.
            p.s[c] = p.s[0]; p.m[c] = p.m[0]; p.h[c] = p.h[0];
            continue;
        }

        float med, mad;
        get_median_mad(data, c_in, ch_idx, med, mad);
        if (mad == 0.0f) mad = 0.001f;

        bool  is_inv = (med > 0.5f);
        float s_val, m_val, h_val;

        if (!is_inv) {
            float c0 = std::max(0.0f, med + shadows_clipping * mad);
            float m2 = med - c0;
            s_val = c0;
            h_val = 1.0f;
            m_val = mtf_calc(m2, targetbg);
        } else {
            // Inverted (negative) image: mirror the clipping.
            float c1 = std::min(1.0f, med - shadows_clipping * mad);
            float m2 = c1 - med;
            s_val = 0.0f;
            h_val = c1;
            m_val = 1.0f - mtf_calc(m2, targetbg);
        }

        p.s[c] = s_val;
        p.m[c] = m_val;
        p.h[c] = h_val;
    }

    return p;
}

// Apply the forward MTF stretch to all pixels and channels in-place.
void StarNetWorker::applyMtf(ImageBuffer& img, const MTFParams& p) {
    int    channels = img.channels();
    auto&  data     = img.data();
    size_t total    = data.size() / channels;

    for (size_t i = 0; i < total; ++i) {
        for (int c = 0; c < channels; ++c) {
            float v = data[i * channels + c];
            float s = p.s[c >= 3 ? 0 : c];
            float m = p.m[c >= 3 ? 0 : c];
            float h = p.h[c >= 3 ? 0 : c];

            float range = h - s;
            if (range < 1e-9f) range = 1e-9f;

            float vn = std::max(0.0f, std::min(1.0f, (v - s) / range));
            data[i * channels + c] = mtf_calc(vn, m);
        }
    }
}

// Invert the MTF stretch to recover the pre-stretch tonal range.
void StarNetWorker::invertMtf(ImageBuffer& img, const MTFParams& p) {
    int    channels = img.channels();
    auto&  data     = img.data();
    size_t total    = data.size() / channels;

    for (size_t i = 0; i < total; ++i) {
        for (int c = 0; c < channels; ++c) {
            float y = data[i * channels + c];
            float s = p.s[c >= 3 ? 0 : c];
            float m = p.m[c >= 3 ? 0 : c];
            float h = p.h[c >= 3 ? 0 : c];

            float vn    = mtf_calc_inv(y, m);
            float range = h - s;
            float v     = vn * range + s;

            data[i * channels + c] = std::max(0.0f, std::min(1.0f, v));
        }
    }
}

// -------------------------------------------------------------------------
// Helper: resolve and patch pyvenv.cfg at runtime
// -------------------------------------------------------------------------
// Updates the "home" and "executable" keys in the virtual environment
// configuration file so that the bundled Python.framework is found correctly
// regardless of the path baked into the venv at creation time.
// Returns the path to the resolved pyvenv.cfg (may be empty if not found).
#if defined(Q_OS_MAC)
static void patchPyvenvCfg(const QString& appDir) {
    QString pyvenvCfg = appDir + "/../Resources/python_venv/pyvenv.cfg";
    if (!QFile::exists(pyvenvCfg))
        pyvenvCfg = appDir + "/../../deps/python_venv/pyvenv.cfg";
    if (!QFile::exists(pyvenvCfg)) return;

    const QString pythonFwBin = QDir::cleanPath(
        appDir + "/../Frameworks/Python.framework/Versions/Current/bin");

    QFile f(pyvenvCfg);
    if (!f.open(QIODevice::ReadWrite | QIODevice::Text)) return;

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
        f.resize(0); f.seek(0);
        f.write(lines.join('\n').toUtf8());
    }
    f.close();
}
#endif

// -------------------------------------------------------------------------
// Helper: configure process environment
// -------------------------------------------------------------------------
// Sets PYTHONUNBUFFERED and, on macOS, prepends the bundle Frameworks
// directory to DYLD_FRAMEWORK_PATH and DYLD_LIBRARY_PATH so that
// Python.framework and native extension modules resolve inside the bundle.
static QProcessEnvironment buildProcessEnvironment(const QString& appDir) {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONUNBUFFERED", "1");

#if defined(Q_OS_MAC)
    patchPyvenvCfg(appDir);

    const QString frameworksDir = appDir + "/../Frameworks";
    if (QDir(frameworksDir).exists()) {
        const QString curFw  = env.value("DYLD_FRAMEWORK_PATH");
        const QString curLib = env.value("DYLD_LIBRARY_PATH");
        env.insert("DYLD_FRAMEWORK_PATH",
                   curFw.isEmpty()  ? frameworksDir : frameworksDir + ":" + curFw);
        env.insert("DYLD_LIBRARY_PATH",
                   curLib.isEmpty() ? frameworksDir : frameworksDir + ":" + curLib);
    }
#else
    Q_UNUSED(appDir)
#endif

    return env;
}

// Main processing slot. Runs in the worker thread.
void StarNetWorker::process(const ImageBuffer& input, const StarNetParams& params) {
    QString     errorMsg;
    ImageBuffer output;

    if (input.data().empty()) {
        emit finished(output, "Input buffer is empty.");
        return;
    }

    const QString exe = getExecutablePath();
    if (exe.isEmpty() || !QFileInfo::exists(exe)) {
        emit finished(output, "StarNet executable not configured. Please set it in Settings.");
        return;
    }

    m_stop = false;

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        emit finished(output, "Failed to create temporary directory.");
        return;
    }

    const QString appDir = QCoreApplication::applicationDirPath();

    // --- Work on a local copy so the original buffer is not modified ---
    ImageBuffer working = input;

    // --- Pad to multiples of 32 for StarNet network stability ---
    const int origW = working.width();
    const int origH = working.height();

    int padW = std::max(origW, 512);
    int padH = std::max(origH, 512);
    if (padW % 32 != 0) padW += (32 - (padW % 32));
    if (padH % 32 != 0) padH += (32 - (padH % 32));

    const bool didPad = (padW != origW || padH != origH);

    if (didPad) {
        emit processOutput(QString("Padding image to %1x%2 for StarNet stability...")
                           .arg(padW).arg(padH));

        const auto& sData = working.data();
        const int   ch    = working.channels();
        std::vector<float> pData(padW * padH * ch, 0.0f);

        for (int y = 0; y < padH; ++y) {
            for (int x = 0; x < padW; ++x) {
                int sx = std::min(x, origW - 1);
                int sy = std::min(y, origH - 1);
                for (int c = 0; c < ch; ++c) {
                    pData[(y * padW + x) * ch + c] = sData[(sy * origW + sx) * ch + c];
                }
            }
        }
        working.setData(padW, padH, ch, pData);
    }

    // --- Optional auto-stretch for linear (unstretched) input ---
    MTFParams mtfP;
    bool      didStretch  = false;
    float     globalScale = 1.0f;

    if (params.isLinear) {
        // Guard against values above 1.0 that would be clipped after stretch.
        float maxVal = 0.0f;
        for (float v : working.data()) if (v > maxVal) maxVal = v;

        if (maxVal > 1.0f) {
            globalScale = maxVal;
            emit processOutput(QString("Image peak %1 > 1.0. Scaling down to preserve highlights...")
                               .arg(globalScale));
            for (float& v : working.data()) v /= globalScale;
        }

        emit processOutput("Linear data detected. Calculating auto-stretch parameters...");
        mtfP = computeMtfParams(working);
        applyMtf(working, mtfP);
        didStretch = true;
    }

    const QString inputFile  = tempDir.filePath("starnet_input.tif");
    const QString outputFile = tempDir.filePath("starnet_output.tif");

    const int w = working.width();
    const int h = working.height();
    int       c = working.channels();

    // StarNet++ requires a three-channel TIFF; expand mono images.
    std::vector<float> exportData;
    int exportChannels = 3;

    if (c == 1) {
        exportData.resize(w * h * 3);
        const auto& d = working.data();
        for (size_t i = 0; i < d.size(); ++i) {
            exportData[i * 3 + 0] = d[i];
            exportData[i * 3 + 1] = d[i];
            exportData[i * 3 + 2] = d[i];
        }
    } else if (c == 3) {
        exportData = working.data();
    } else {
        emit finished(output, "Unsupported channel count for StarNet++.");
        return;
    }

    emit processOutput("Saving temporary input TIFF (16-bit)...");
    if (!SimpleTiffWriter::write(inputFile, w, h, exportChannels,
                                 SimpleTiffWriter::Format_uint16,
                                 exportData, QByteArray(), &errorMsg)) {
        emit finished(output, errorMsg);
        return;
    }

    // --- Launch StarNet++ ---
    emit processOutput("Running StarNet++...");

    QStringList args;
#if defined(Q_OS_MAC)
    args << "-i" << inputFile << "-o" << outputFile << "-s" << QString::number(params.stride);
#else
    args << inputFile << outputFile << QString::number(params.stride);
#endif

    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.setWorkingDirectory(QFileInfo(exe).absolutePath());

    QString starNetLog;
    connect(&p, &QProcess::readyReadStandardOutput, [&p, &starNetLog, this]() {
        QString txt = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
        if (!txt.isEmpty()) {
            emit processOutput(txt);
            starNetLog.append(txt + "\n");
        }
    });

    p.start(exe, args);

    // Wait loop with cancellation and a 10-minute hard timeout.
    int elapsed  = 0;
    int interval = 100;

    while (p.state() != QProcess::NotRunning) {
        if (m_stop) {
            p.kill(); p.waitForFinished();
            emit finished(output, "StarNet process cancelled by user.");
            return;
        }
        QCoreApplication::processEvents();
        QThread::msleep(interval);
        elapsed += interval;
        if (elapsed > 600000) {
            p.kill();
            emit finished(output, "StarNet process timed out after 10 minutes.");
            return;
        }
    }

    if (p.exitCode() != 0) {
        const QString tail = starNetLog.trimmed().right(1000);
        errorMsg = QString("StarNet process failed (exit code %1): %2\n\nLog:\n%3")
                   .arg(p.exitCode())
                   .arg(p.errorString().isEmpty() ? "Unknown error" : p.errorString())
                   .arg(tail);
        emit finished(output, errorMsg);
        return;
    }

    if (!QFile::exists(outputFile)) {
        emit finished(output, "StarNet did not produce an output file.");
        return;
    }

    // --- Convert StarNet TIFF output to raw float32 via Python bridge ---
    emit processOutput("Converting StarNet output via Python bridge...");

    QString converterScript = appDir + "/scripts/starnet_converter.py";
    if (!QFile::exists(converterScript))
        converterScript = appDir + "/../Resources/scripts/starnet_converter.py";
    if (!QFile::exists(converterScript))
        converterScript = appDir + "/../src/scripts/starnet_converter.py";

    if (!QFile::exists(converterScript)) {
        emit finished(output, "Converter script not found.");
        return;
    }

    const QString rawOutput = tempDir.filePath("starnet_output.raw");
    QStringList   convArgs;
    convArgs << converterScript << outputFile << rawOutput;

    // Locate bundled Python interpreter.
    QString pythonExe;
#if defined(Q_OS_MAC)
    pythonExe = appDir + "/../Resources/python_venv/bin/python3";
    if (!QFile::exists(pythonExe))
        pythonExe = appDir + "/../../deps/python_venv/bin/python3";
#else
    pythonExe = appDir + "/python/python.exe";
    if (!QFile::exists(pythonExe))
        pythonExe = appDir + "/../deps/python/python.exe";
#endif

    if (!QFile::exists(pythonExe)) {
        emit finished(output, "Bundled Python interpreter not found.\nExpected: " + pythonExe);
        return;
    }

    QProcess conv;
    conv.setProcessEnvironment(buildProcessEnvironment(appDir));
    conv.setProcessChannelMode(QProcess::MergedChannels);

    QString convLog;
    connect(&conv, &QProcess::readyReadStandardOutput, [&conv, &convLog, this]() {
        QString txt = QString::fromUtf8(conv.readAllStandardOutput()).trimmed();
        if (!txt.isEmpty()) {
            emit processOutput(txt);
            convLog.append(txt + "\n");
        }
    });

    conv.start(pythonExe, convArgs);

    elapsed = 0;
    while (!conv.waitForFinished(interval)) {
        if (m_stop) {
            conv.kill(); conv.waitForFinished(3000);
            emit finished(output, "Conversion cancelled by user.");
            return;
        }
        elapsed += interval;
        if (elapsed > 120000) {
            conv.kill();
            emit finished(output, "Conversion timed out after 2 minutes.");
            return;
        }
    }

    if (conv.exitCode() != 0) {
        QString err = QString::fromUtf8(conv.readAllStandardError()).trimmed();
        if (err.isEmpty() && !convLog.isEmpty()) err = convLog.trimmed().right(1000);
        emit finished(output, "Output conversion failed:\n\n" + err);
        return;
    }

    // --- Load raw float32 result ---
    emit processOutput("Loading raw float32 result...");

    QFile rawFile(rawOutput);
    if (!rawFile.open(QIODevice::ReadOnly)) {
        emit finished(output, "Failed to open converted raw file.");
        return;
    }
    const QByteArray blob = rawFile.readAll();
    rawFile.close();

    // Validate the expected byte count for both RGB and mono cases.
    size_t expectedRGB  = static_cast<size_t>(w) * h * 3 * sizeof(float);
    size_t expectedMono = static_cast<size_t>(w) * h * 1 * sizeof(float);

    if (static_cast<size_t>(blob.size()) == expectedRGB) {
        c = 3;
    } else if (static_cast<size_t>(blob.size()) == expectedMono) {
        c = 1;
    } else {
        emit finished(output,
                      QString("Output size mismatch. Expected %1 or %2 bytes, got %3.")
                      .arg(expectedRGB).arg(expectedMono).arg(blob.size()));
        return;
    }

    std::vector<float> outData(blob.size() / sizeof(float));
    emit processOutput(QString("Copying %1 float values from raw result...").arg(outData.size()));
    std::memcpy(outData.data(), blob.constData(), blob.size());

    // Normalise 16-bit range if the worker returned values above 1.0.
    float dMax = -1e9f;
    for (float v : outData) if (v > dMax) dMax = v;
    emit processOutput(QString("Output peak value: %1").arg(dMax));

    if (dMax > 1.0f) {
        emit processOutput("Normalising output from 16-bit range...");
        const float normScale = 1.0f / 65535.0f;
        for (float& v : outData) v *= normScale;
    }

    ImageBuffer starlessLocal;
    emit processOutput(QString("Creating starless buffer: %1x%2 ch=%3").arg(w).arg(h).arg(c));
    starlessLocal.setData(w, h, c, outData);

    // Revert RGB expansion back to mono if the source was single-channel.
    if (input.channels() == 1 && c == 3) {
        emit processOutput("Reverting to mono channel...");
        std::vector<float> monoData(w * h);
        for (int i = 0; i < w * h; ++i) {
            monoData[i] = (outData[i * 3] + outData[i * 3 + 1] + outData[i * 3 + 2]) / 3.0f;
        }
        ImageBuffer mono;
        mono.setData(w, h, 1, monoData);
        starlessLocal = mono;
    }

    // --- Invert auto-stretch if it was applied ---
    if (didStretch) {
        emit processOutput("Inverting auto-stretch...");
        invertMtf(starlessLocal, mtfP);

        if (globalScale > 1.0f) {
            emit processOutput(QString("Restoring global scale factor (%1)...").arg(globalScale));
            for (float& v : starlessLocal.data()) v *= globalScale;
        }
    }

    // --- Crop padding ---
    if (didPad) {
        emit processOutput("Cropping padding to original dimensions...");
        starlessLocal.crop(0, 0, origW, origH);
    }

    output = starlessLocal;
    output.setMetadata(input.metadata());

    emit finished(output, "");  // Empty error string signals success.
}

// ============================================================================
// StarNetRunner Implementation
// ============================================================================

StarNetRunner::StarNetRunner(QObject* parent)
    : QObject(parent)
{}

StarNetRunner::~StarNetRunner() {
    if (m_thread) {
        m_thread->quit();
        if (!m_thread->wait(5000)) {
            m_thread->terminate();
            m_thread->wait();
        }
    }
}

// Run the star removal pipeline synchronously by dispatching to the worker
// thread and blocking on a local event loop until the worker signals completion
// or a 15-minute timeout fires.
bool StarNetRunner::run(const ImageBuffer& input,
                        ImageBuffer&       output,
                        const StarNetParams& params,
                        QString* errorMsg)
{
    // Lazily create the worker thread on first use.
    if (!m_thread) {
        m_thread = new QThread(this);
        m_worker = new StarNetWorker();
        m_worker->moveToThread(m_thread);

        connect(m_thread, &QThread::finished,             m_worker, &QObject::deleteLater);
        connect(m_worker, &StarNetWorker::finished,       this,     &StarNetRunner::onWorkerFinished);
        connect(m_worker, &StarNetWorker::processOutput,  this,     &StarNetRunner::processOutput);

        m_thread->start();
    }

    m_finished = false;

    // Invoke the worker's process() slot in the worker thread via queued connection.
    QMetaObject::invokeMethod(m_worker, "process",
                              Qt::QueuedConnection,
                              Q_ARG(const ImageBuffer&,   input),
                              Q_ARG(const StarNetParams&, params));

    // Block the calling thread using a local event loop so Qt events continue
    // to be processed (required for the queued signal to reach the worker).
    QEventLoop loop;
    connect(this, &StarNetRunner::finished, &loop, &QEventLoop::quit);

    QTimer timer;
    timer.setSingleShot(true);
    timer.start(15 * 60 * 1000);  // 15-minute timeout
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    loop.exec();
    timer.stop();

    if (!m_finished) {
        if (errorMsg) *errorMsg = "Operation timed out.";
        m_errorMsg = "Operation timed out.";
        return false;
    }

    if (!m_errorMsg.isEmpty()) {
        if (errorMsg) *errorMsg = m_errorMsg;
        return false;
    }

    output = m_output;
    return true;
}

// Slot called by the worker when it finishes (success or failure).
void StarNetRunner::onWorkerFinished(const ImageBuffer& output, const QString& errorMsg) {
    m_output   = output;
    m_errorMsg = errorMsg;
    m_finished = true;
    emit finished();
}