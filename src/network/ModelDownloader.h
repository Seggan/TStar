/**
 * @file ModelDownloader.h
 * @brief AI model downloader interface for Cosmic Clarity models
 *
 * Provides functionality to download AI models from multiple sources
 * (Google Drive primary, Google Drive backup, GitHub mirror) with
 * automatic fallback, ZIP extraction, and installation verification.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#ifndef MODELDOWNLOADER_H
#define MODELDOWNLOADER_H

#include <QObject>
#include <QString>
#include <QThread>
#include <QMap>

#include <atomic>

/* ============================================================================
 * ModelDownloaderWorker
 * ============================================================================ */

/**
 * @class ModelDownloaderWorker
 * @brief Background worker for AI model download and installation
 *
 * Handles the complete download workflow including:
 * - Google Drive downloads with virus scan interstitial handling
 * - HTTP fallback to GitHub mirror
 * - ZIP extraction using platform tools
 * - Installation verification
 */
class ModelDownloaderWorker : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Construct a new worker instance
     * @param parent Parent QObject (optional)
     */
    explicit ModelDownloaderWorker(QObject* parent = nullptr);

public slots:
    /**
     * @brief Execute the download and installation operation
     *
     * Attempts to download from multiple sources in order:
     * 1. Primary Google Drive
     * 2. Backup Google Drive
     * 3. GitHub HTTP mirror
     *
     * After successful download, extracts and verifies the models.
     */
    void run();

    /**
     * @brief Request cancellation of the current operation
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
    /* --------------------------------------------------------------------
     * Download Methods
     * -------------------------------------------------------------------- */

    /**
     * @brief Download a file from Google Drive
     *
     * Handles Google Drive's virus scan interstitial page by parsing
     * the confirmation form and resubmitting. Falls back to GitHub
     * mirror on failure.
     *
     * @param fileId   Google Drive file ID
     * @param destPath Local path to save the downloaded file
     * @return true on success, false on error
     */
    bool downloadGoogleDrive(const QString& fileId, const QString& destPath);

    /**
     * @brief Download a file via direct HTTP
     *
     * @param url      URL to download from
     * @param destPath Local path to save the downloaded file
     * @return true on success, false on error
     */
    bool downloadHttp(const QString& url, const QString& destPath);

    /* --------------------------------------------------------------------
     * Extraction Methods
     * -------------------------------------------------------------------- */

    /**
     * @brief Extract a ZIP archive to a destination directory
     *
     * Uses platform-appropriate extraction tools:
     * - Windows: tar (Windows 10+) or PowerShell Expand-Archive
     * - macOS: bsdtar or unzip
     * - Linux: unzip
     *
     * @param zipPath Path to the ZIP file
     * @param destDir Destination directory for extracted files
     * @return true on success, false on error
     */
    bool extractZip(const QString& zipPath, const QString& destDir);

    /* --------------------------------------------------------------------
     * Google Drive Helpers
     * -------------------------------------------------------------------- */

    /**
     * @brief Parse Google Drive virus scan interstitial page
     *
     * Extracts the confirmation form action URL and hidden parameters
     * needed to bypass the virus scan warning for large files.
     *
     * @param html   HTML content of the interstitial page
     * @param action Output: Form action URL
     * @param params Output: Hidden form parameters
     * @return true if parsing succeeded, false otherwise
     */
    bool parseGDriveInterstitial(const QByteArray& html,
                                 QString& action,
                                 QMap<QString, QString>& params);

    /**
     * @brief Extract Google Drive file ID from URL or raw ID
     *
     * Supports multiple URL formats:
     * - https://drive.google.com/file/d/FILE_ID/view
     * - https://drive.google.com/open?id=FILE_ID
     * - Raw file ID string
     *
     * @param urlOrId URL or file ID string
     * @return Extracted file ID, or empty string on failure
     */
    QString extractDriveFileId(const QString& urlOrId);

    /** Atomic cancellation flag */
    std::atomic<bool> m_cancel{false};
};

/* ============================================================================
 * ModelDownloader
 * ============================================================================ */

/**
 * @class ModelDownloader
 * @brief Main-thread interface for AI model downloading
 *
 * Provides a simple interface for downloading Cosmic Clarity AI models
 * from the main thread. Manages the background worker thread and provides
 * static utility methods for model path management.
 *
 * Usage:
 * @code
 *   if (!ModelDownloader::modelsInstalled()) {
 *       ModelDownloader* downloader = new ModelDownloader(this);
 *       connect(downloader, &ModelDownloader::finished, this, &MyClass::onModelsReady);
 *       downloader->startDownload();
 *   }
 * @endcode
 */
class ModelDownloader : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Construct a new ModelDownloader
     * @param parent Parent QObject for memory management
     */
    explicit ModelDownloader(QObject* parent = nullptr);

    /**
     * @brief Destructor - ensures worker thread is properly terminated
     */
    ~ModelDownloader();

    /**
     * @brief Start the download operation
     *
     * Creates a background worker thread and begins downloading.
     * Does nothing if a download is already in progress.
     */
    void startDownload();

    /* --------------------------------------------------------------------
     * Static Utility Methods
     * -------------------------------------------------------------------- */

    /**
     * @brief Get the root directory for all TStar models
     *
     * Creates the directory if it doesn't exist.
     *
     * @return Path to the models root directory
     */
    static QString modelsRoot();

    /**
     * @brief Get the directory for Cosmic Clarity models
     *
     * Creates the directory if it doesn't exist.
     *
     * @return Path to the Cosmic Clarity models directory
     */
    static QString cosmicClarityRoot();

    /**
     * @brief Check if required models are installed
     *
     * Verifies that the sentinel model file exists.
     *
     * @return true if models are installed, false otherwise
     */
    static bool modelsInstalled();

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
    ModelDownloaderWorker* m_worker = nullptr;
};

#endif  // MODELDOWNLOADER_H