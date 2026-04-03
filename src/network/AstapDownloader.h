/**
 * @file AstapDownloader.h
 * @brief ASTAP star database downloader interface
 *
 * Provides functionality to download and install the ASTAP D50 star database
 * from SourceForge. The download runs in a background thread with progress
 * reporting and cancellation support.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#ifndef ASTAPDOWNLOADER_H
#define ASTAPDOWNLOADER_H

#include <QObject>
#include <QString>
#include <QThread>

#include <atomic>

/* ============================================================================
 * AstapDownloaderWorker
 * ============================================================================ */

/**
 * @class AstapDownloaderWorker
 * @brief Background worker for ASTAP database download operations
 *
 * This worker class performs the actual download and installation in a
 * separate thread. It handles HTTP downloads with redirect following,
 * progress reporting, and platform-specific installer launching.
 */
class AstapDownloaderWorker : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Construct a new worker instance
     * @param parent Parent QObject (optional)
     */
    explicit AstapDownloaderWorker(QObject* parent = nullptr);

public slots:
    /**
     * @brief Execute the download operation
     *
     * Downloads the appropriate ASTAP D50 database installer for the
     * current platform and launches it. This slot is called when the
     * worker thread starts.
     */
    void run();

    /**
     * @brief Request cancellation of the download
     *
     * Sets a cancellation flag that is checked during download.
     * The download will stop at the next opportunity.
     */
    void cancel();

signals:
    /**
     * @brief Emitted to report progress status
     * @param message Human-readable progress message
     */
    void progress(const QString& message);

    /**
     * @brief Emitted to report numeric progress
     * @param value Progress percentage (0-100), or -1 for indeterminate
     */
    void progressValue(int value);

    /**
     * @brief Emitted when the operation completes
     * @param ok      true if successful, false on error or cancellation
     * @param message Result description or error message
     */
    void finished(bool ok, const QString& message);

private:
    /**
     * @brief Download a file via HTTP with progress reporting
     *
     * @param url      URL to download from
     * @param destPath Local path to save the downloaded file
     * @return true on success, false on error or cancellation
     */
    bool downloadHttp(const QString& url, const QString& destPath);

    /**
     * @brief Launch the downloaded installer
     *
     * Uses platform-appropriate method to execute the installer:
     * - Windows: QDesktopServices::openUrl
     * - macOS: QProcess with 'open' command
     * - Linux: QDesktopServices::openUrl
     *
     * @param installerPath Path to the installer file
     * @return true if installer was launched successfully
     */
    bool launchInstaller(const QString& installerPath);

    /** Atomic cancellation flag for thread-safe access */
    std::atomic<bool> m_cancel{false};
};

/* ============================================================================
 * AstapDownloader
 * ============================================================================ */

/**
 * @class AstapDownloader
 * @brief Main-thread interface for ASTAP database downloading
 *
 * This class provides a simple interface for initiating ASTAP database
 * downloads from the main thread. It manages the background worker thread
 * and forwards progress signals.
 *
 * Usage:
 * @code
 *   AstapDownloader* downloader = new AstapDownloader(this);
 *   connect(downloader, &AstapDownloader::progress, this, &MyClass::onProgress);
 *   connect(downloader, &AstapDownloader::finished, this, &MyClass::onFinished);
 *   downloader->startDownload();
 * @endcode
 */
class AstapDownloader : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Construct a new AstapDownloader
     * @param parent Parent QObject for memory management
     */
    explicit AstapDownloader(QObject* parent = nullptr);

    /**
     * @brief Destructor - ensures worker thread is properly terminated
     */
    ~AstapDownloader();

    /**
     * @brief Start the download operation
     *
     * Creates a background worker thread and begins downloading.
     * Does nothing if a download is already in progress.
     */
    void startDownload();

signals:
    /**
     * @brief Emitted to report progress status
     * @param message Human-readable progress message
     */
    void progress(const QString& message);

    /**
     * @brief Emitted to report numeric progress
     * @param value Progress percentage (0-100), or -1 for indeterminate
     */
    void progressValue(int value);

    /**
     * @brief Emitted when the download operation completes
     * @param ok      true if successful, false on error or cancellation
     * @param message Result description or error message
     */
    void finished(bool ok, const QString& message);

public slots:
    /**
     * @brief Request cancellation of the current download
     */
    void cancel();

private:
    /** Background worker thread */
    QThread* m_thread = nullptr;

    /** Worker instance running in background thread */
    AstapDownloaderWorker* m_worker = nullptr;
};

#endif  // ASTAPDOWNLOADER_H