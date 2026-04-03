#ifndef LOGGER_H
#define LOGGER_H

// ============================================================================
// Logger.h
// Centralized thread-safe logging system with file output, log rotation,
// Qt message handler integration, and crash signal capture.
// ============================================================================

#include <QString>
#include <QFile>
#include <QRecursiveMutex>
#include <QDateTime>
#include <QDir>
#include <QCoreApplication>
#include <QTextStream>
#include <QDebug>

/**
 * @brief Application-wide logging facility.
 *
 * Features:
 *   - Automatic file logging with timestamped session files
 *   - Log rotation (configurable maximum file count)
 *   - Thread-safe via QRecursiveMutex
 *   - Intercepts qDebug/qWarning/qCritical/qFatal output
 *   - Crash signal handlers for post-mortem diagnostics
 *
 * Lifecycle:
 *   Logger::init();       // Call early, before QApplication event loop
 *   Logger::shutdown();   // Call on application exit
 */
class Logger
{
public:
    enum Level {
        Debug,
        Info,
        Warning,
        Error,
        Critical,
        Fatal
    };

    /**
     * @brief Initialize the logging subsystem.
     * @param logDirPath  Custom log directory (defaults to platform-specific location).
     * @param maxLogFiles Maximum number of retained log files (oldest are pruned).
     */
    static void init(const QString& logDirPath = QString(), int maxLogFiles = 5);

    /** Flush and close the current log file, restoring the previous Qt handler. */
    static void shutdown();

    /** Write a log entry at the specified severity level. */
    static void log(Level level, const QString& message,
                    const QString& category = QString());

    /** Get the absolute path of the current session log file. */
    static QString currentLogFile();

    /**
     * @brief Retrieve the last N lines from the current log file.
     * @param lines Number of lines to retrieve.
     */
    static QString getRecentLogs(int lines = 50);

    // -- Convenience wrappers -------------------------------------------------
    static void debug(const QString& msg, const QString& cat = QString())    { log(Debug, msg, cat); }
    static void info(const QString& msg, const QString& cat = QString())     { log(Info, msg, cat); }
    static void warning(const QString& msg, const QString& cat = QString())  { log(Warning, msg, cat); }
    static void error(const QString& msg, const QString& cat = QString())    { log(Error, msg, cat); }
    static void critical(const QString& msg, const QString& cat = QString()) { log(Critical, msg, cat); }

private:
    Logger()  = default;
    ~Logger() = default;

    /** Qt message handler callback installed via qInstallMessageHandler(). */
    static void qtMessageHandler(QtMsgType type,
                                 const QMessageLogContext& context,
                                 const QString& msg);

    /** Remove oldest log files to stay within the configured limit. */
    static void rotateLogFiles();

    /** Convert a Level enum value to its string representation. */
    static QString levelToString(Level level);

    /** Write crash-specific information to the log. */
    static void writeCrashLog(const QString& reason);

    // -- Static state ---------------------------------------------------------
    static QFile*            s_logFile;
    static QTextStream*      s_logStream;
    static QRecursiveMutex   s_mutex;
    static QString           s_logDirPath;
    static QString           s_currentLogPath;
    static int               s_maxLogFiles;
    static bool              s_initialized;
    static QtMessageHandler  s_previousHandler;
};

#endif // LOGGER_H