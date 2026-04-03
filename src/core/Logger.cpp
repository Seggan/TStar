// ============================================================================
// Logger.cpp
// Centralized logging system with file output, log rotation, Qt message
// handler integration, and crash signal capture.
// ============================================================================

#include "Logger.h"
#include "Version.h"

#include <QStandardPaths>
#include <QFileInfo>
#include <QStringConverter>

#include <iostream>
#include <csignal>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dbghelp.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#ifdef _MSC_VER
#pragma comment(lib, "dbghelp.lib")
#endif
#pragma GCC diagnostic pop
#endif

// ============================================================================
// Static Member Initialization
// ============================================================================

QFile*            Logger::s_logFile         = nullptr;
QTextStream*      Logger::s_logStream       = nullptr;
QRecursiveMutex   Logger::s_mutex;
QString           Logger::s_logDirPath;
QString           Logger::s_currentLogPath;
int               Logger::s_maxLogFiles     = 5;
bool              Logger::s_initialized     = false;
QtMessageHandler  Logger::s_previousHandler = nullptr;

// ============================================================================
// Crash Signal Handler
// ============================================================================

/**
 * @brief Signal handler invoked on fatal signals (SIGSEGV, SIGABRT, etc.).
 *
 * Writes crash diagnostics to the log file, shuts down the logger cleanly,
 * then re-raises the signal to produce a core dump or default OS behavior.
 */
static void crashSignalHandler(int signal)
{
    QString reason;
    switch (signal) {
        case SIGSEGV: reason = "Segmentation Fault (SIGSEGV)";       break;
        case SIGABRT: reason = "Abort Signal (SIGABRT)";             break;
        case SIGFPE:  reason = "Floating Point Exception (SIGFPE)";  break;
        case SIGILL:  reason = "Illegal Instruction (SIGILL)";       break;
        default:      reason = QString("Signal %1").arg(signal);     break;
    }

    Logger::critical("=== APPLICATION CRASH ===");
    Logger::critical("Reason: " + reason);
    Logger::critical("The application has encountered a fatal error and will now exit.");
    Logger::shutdown();

    // Re-raise with default handler to allow core dump generation
    std::signal(signal, SIG_DFL);
    std::raise(signal);
}

// ============================================================================
// Initialization
// ============================================================================

void Logger::init(const QString& logDirPath, int maxLogFiles)
{
    QMutexLocker locker(&s_mutex);

    if (s_initialized) return;

    s_maxLogFiles = maxLogFiles;

    // -- Determine log directory ----------------------------------------------
    if (logDirPath.isEmpty()) {
        // Platform default: GenericDataLocation/TStar/logs
        // Windows: C:/Users/<User>/AppData/Local/TStar/logs
        QString dataDir = QStandardPaths::writableLocation(
            QStandardPaths::GenericDataLocation);
        s_logDirPath = dataDir + "/TStar/logs";
    } else {
        s_logDirPath = logDirPath;
    }

    // Ensure the log directory exists
    QDir dir(s_logDirPath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    // -- Create session log file with unique timestamp ------------------------
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    s_currentLogPath  = s_logDirPath + QString("/TStar_%1.log").arg(timestamp);

    // Prune old log files before creating the new one
    rotateLogFiles();

    s_logFile = new QFile(s_currentLogPath);
    if (!s_logFile->open(QIODevice::WriteOnly | QIODevice::Text)) {
        std::cerr << "Failed to open log file: "
                  << s_currentLogPath.toStdString() << std::endl;
        delete s_logFile;
        s_logFile = nullptr;
        return;
    }

    s_logStream = new QTextStream(s_logFile);
    s_logStream->setEncoding(QStringConverter::Utf8);

    // -- Write session header -------------------------------------------------
    *s_logStream << "========================================="
                    "========================================\n";
    *s_logStream << "TStar Log - Started "
                 << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
    *s_logStream << "Version: " << TStar::getVersion() << "\n";

#ifdef Q_OS_WIN
    *s_logStream << "Platform: Windows\n";
#elif defined(Q_OS_MAC)
    *s_logStream << "Platform: macOS\n";
#else
    *s_logStream << "Platform: Linux/Other\n";
#endif

    *s_logStream << "========================================="
                    "========================================\n\n";
    s_logStream->flush();

    // -- Install Qt message handler -------------------------------------------
    s_previousHandler = qInstallMessageHandler(qtMessageHandler);

    // -- Install crash signal handlers ----------------------------------------
    std::signal(SIGSEGV, crashSignalHandler);
    std::signal(SIGABRT, crashSignalHandler);
    std::signal(SIGFPE,  crashSignalHandler);
    std::signal(SIGILL,  crashSignalHandler);

#ifdef Q_OS_WIN
    // Windows-specific: catch unhandled SEH exceptions
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* exInfo) -> LONG {
        Logger::critical("=== UNHANDLED EXCEPTION ===");
        Logger::critical(QString("Exception Code: 0x%1")
            .arg(exInfo->ExceptionRecord->ExceptionCode, 8, 16, QChar('0')));
        Logger::shutdown();
        return EXCEPTION_CONTINUE_SEARCH;
    });
#endif

    s_initialized = true;

    log(Info, "Logging system initialized", "Logger");
    log(Info, QString("Log file: %1").arg(s_currentLogPath), "Logger");
}

// ============================================================================
// Shutdown
// ============================================================================

void Logger::shutdown()
{
    QMutexLocker locker(&s_mutex);

    if (!s_initialized) return;

    // Restore the previous Qt message handler
    if (s_previousHandler) {
        qInstallMessageHandler(s_previousHandler);
    }

    // Write session footer and flush
    if (s_logStream) {
        *s_logStream << "\n========================================="
                        "========================================\n";
        *s_logStream << "TStar Log - Ended "
                     << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
        *s_logStream << "========================================="
                        "========================================\n";
        s_logStream->flush();
        delete s_logStream;
        s_logStream = nullptr;
    }

    if (s_logFile) {
        s_logFile->close();
        delete s_logFile;
        s_logFile = nullptr;
    }

    s_initialized = false;
}

// ============================================================================
// Core Logging
// ============================================================================

void Logger::log(Level level, const QString& message, const QString& category)
{
    QMutexLocker locker(&s_mutex);

    QString timestamp   = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    QString levelStr    = levelToString(level);
    QString categoryStr = category.isEmpty()
                        ? QString()
                        : QString("[%1] ").arg(category);

    QString formattedMsg = QString("[%1] [%2] %3%4")
        .arg(timestamp)
        .arg(levelStr, -8)
        .arg(categoryStr)
        .arg(message);

    // Write to file (always flush immediately for crash resilience)
    if (s_logStream) {
        *s_logStream << formattedMsg << "\n";
        s_logStream->flush();
    }

#ifdef QT_DEBUG
    // Optionally echo to stderr in debug builds
    // std::cerr << formattedMsg.toStdString() << std::endl;
#endif
}

// ============================================================================
// Qt Message Handler Integration
// ============================================================================

/**
 * @brief Qt message handler callback that routes all qDebug/qWarning/etc.
 *        output through the Logger system.
 */
void Logger::qtMessageHandler(QtMsgType type,
                              const QMessageLogContext& context,
                              const QString& msg)
{
    Level level;
    switch (type) {
        case QtDebugMsg:    level = Debug;    break;
        case QtInfoMsg:     level = Info;     break;
        case QtWarningMsg:  level = Warning;  break;
        case QtCriticalMsg: level = Critical; break;
        case QtFatalMsg:    level = Fatal;    break;
        default:            level = Info;     break;
    }

    QString category;
    if (context.category && strcmp(context.category, "default") != 0) {
        category = QString::fromUtf8(context.category);
    }

    log(level, msg, category);

    // Fatal messages require immediate shutdown
    if (type == QtFatalMsg) {
        shutdown();
        std::abort();
    }
}

// ============================================================================
// Log Rotation
// ============================================================================

/**
 * @brief Remove the oldest log files to keep the total count within the
 *        configured maximum. Files are sorted by modification time.
 */
void Logger::rotateLogFiles()
{
    QDir dir(s_logDirPath);
    QStringList filters;
    filters << "TStar_*.log";

    QFileInfoList logFiles = dir.entryInfoList(filters, QDir::Files, QDir::Time);

    // Remove the oldest files if the count meets or exceeds the limit
    while (logFiles.size() >= s_maxLogFiles) {
        QFile::remove(logFiles.last().absoluteFilePath());
        logFiles.removeLast();
    }
}

// ============================================================================
// Utility
// ============================================================================

QString Logger::levelToString(Level level)
{
    switch (level) {
        case Debug:    return "DEBUG";
        case Info:     return "INFO";
        case Warning:  return "WARNING";
        case Error:    return "ERROR";
        case Critical: return "CRITICAL";
        case Fatal:    return "FATAL";
        default:       return "UNKNOWN";
    }
}

QString Logger::currentLogFile()
{
    return s_currentLogPath;
}

/**
 * @brief Read the last N lines from the current log file.
 *
 * Uses a reverse-read strategy: reads backward from the end of the file
 * in fixed-size chunks until enough lines have been collected.
 *
 * @param lines Number of trailing lines to retrieve.
 * @return Concatenated string of the requested log lines.
 */
QString Logger::getRecentLogs(int lines)
{
    QMutexLocker locker(&s_mutex);

    if (!s_logFile || !s_logFile->isOpen()) {
        return "Log file not accessible.";
    }

    s_logStream->flush();

    QFile readFile(s_currentLogPath);
    if (!readFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return "Failed to open log file for reading.";
    }

    qint64 fileSize = readFile.size();
    if (fileSize == 0) return "";

    // Read backward in chunks to efficiently locate the last N lines
    const int   bufferSize = 4096;
    QByteArray  buffer;
    QStringList resultLines;
    qint64      pos = fileSize;

    while (pos > 0 && resultLines.size() < lines + 1) {
        qint64 readSize = std::min(pos, static_cast<qint64>(bufferSize));
        pos -= readSize;

        readFile.seek(pos);
        QByteArray chunk = readFile.read(readSize);
        buffer.prepend(chunk);
    }

    // Convert the accumulated buffer to text and split into lines
    QString fullText = QString::fromUtf8(buffer);
    resultLines = fullText.split('\n');

    // Remove trailing empty line if present
    if (!resultLines.isEmpty() && resultLines.last().isEmpty()) {
        resultLines.removeLast();
    }

    // Extract only the last N lines
    int start = std::max(0, static_cast<int>(resultLines.size()) - lines);
    QString result;
    for (int i = start; i < resultLines.size(); ++i) {
        result += resultLines[i] + "\n";
    }

    return result;
}