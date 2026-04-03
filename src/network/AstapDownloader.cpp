/**
 * @file AstapDownloader.cpp
 * @brief Implementation of ASTAP star database downloader
 *
 * Downloads the ASTAP D50 star database from SourceForge and launches
 * the platform-appropriate installer. Supports Windows (.exe), macOS (.pkg),
 * and Linux (.deb) installers.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#include "AstapDownloader.h"

#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QEventLoop>
#include <QTimer>
#include <QUrl>
#include <QDesktopServices>
#include <QProcess>
#include <QDebug>

/* ============================================================================
 * Constants
 * ============================================================================ */

namespace {

/** Download timeout in milliseconds (10 minutes for large database files) */
constexpr int DOWNLOAD_TIMEOUT_MS = 600000;

/** Base URL for ASTAP database downloads on SourceForge */
const QString SOURCEFORGE_BASE_URL =
    "https://downloads.sourceforge.net/project/astap-program/star_databases/";

}  // anonymous namespace

/* ============================================================================
 * AstapDownloaderWorker Implementation
 * ============================================================================ */

AstapDownloaderWorker::AstapDownloaderWorker(QObject* parent)
    : QObject(parent)
{
}

void AstapDownloaderWorker::cancel()
{
    m_cancel = true;
}

bool AstapDownloaderWorker::downloadHttp(const QString& url, const QString& destPath)
{
    QNetworkAccessManager networkManager;

    emit progress(tr("Connecting to SourceForge..."));

    /* Configure request with user agent and redirect handling */
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, "TStar-AstapDownloader");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = networkManager.get(request);

    /* Set up temporary file for streaming download */
    QString partialPath = destPath + ".part";
    QFile outputFile(partialPath);
    bool fileOpened = false;

    /* Handle download progress updates */
    connect(reply, &QNetworkReply::downloadProgress,
            [this, &outputFile, &fileOpened](qint64 bytesReceived, qint64 bytesTotal) {

        if (m_cancel) {
            return;
        }

        /* Open file on first data received */
        if (!fileOpened && outputFile.open(QIODevice::WriteOnly)) {
            fileOpened = true;
        }

        /* Report progress */
        if (bytesTotal > 0) {
            int percentage = qBound(0,
                static_cast<int>((double(bytesReceived) / double(bytesTotal)) * 100.0),
                100);
            emit progress(tr("Downloading... %1%").arg(percentage));
            emit progressValue(percentage);
        } else if (bytesReceived > 0) {
            double megabytes = bytesReceived / (1024.0 * 1024.0);
            emit progress(tr("Downloading... %1 MB").arg(megabytes, 0, 'f', 1));
            emit progressValue(-1);
        }
    });

    /* Stream data to file as it arrives */
    connect(reply, &QNetworkReply::readyRead, [this, &reply, &outputFile, &fileOpened]() {
        if (m_cancel) {
            return;
        }

        if (!fileOpened) {
            if (outputFile.open(QIODevice::WriteOnly)) {
                fileOpened = true;
            } else {
                return;
            }
        }

        QByteArray chunk = reply->readAll();
        outputFile.write(chunk);
    });

    /* Wait for download completion with timeout */
    QEventLoop eventLoop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    bool timedOut = false;

    connect(&timeoutTimer, &QTimer::timeout, [&]() {
        timedOut = true;
        eventLoop.quit();
    });

    connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);

    timeoutTimer.start(DOWNLOAD_TIMEOUT_MS);
    eventLoop.exec();
    timeoutTimer.stop();

    /* Clean up file handle */
    if (fileOpened) {
        outputFile.close();
    }

    /* Handle cancellation */
    if (m_cancel) {
        QFile::remove(partialPath);
        reply->deleteLater();
        return false;
    }

    /* Handle timeout or network error */
    if (timedOut || reply->error() != QNetworkReply::NoError) {
        emit progress(tr("Download failed or timed out."));
        qWarning() << "Download failed:" << reply->errorString();
        QFile::remove(partialPath);
        reply->deleteLater();
        return false;
    }

    reply->deleteLater();

    /* Atomic rename from partial to final path */
    QFile::remove(destPath);
    if (!QFile::rename(partialPath, destPath)) {
        QFile::remove(partialPath);
        return false;
    }

    emit progress(tr("Download complete."));
    return true;
}

bool AstapDownloaderWorker::launchInstaller(const QString& installerPath)
{
    emit progress(tr("Launching installer..."));
    emit progressValue(-1);

    bool launched = false;

#ifdef Q_OS_WIN
    /* Windows: Use shell association to run installer */
    launched = QDesktopServices::openUrl(QUrl::fromLocalFile(installerPath));

#elif defined(Q_OS_MAC)
    /* macOS: Use 'open' command for .pkg files */
    launched = QProcess::startDetached("open", QStringList() << installerPath);

#else
    /* Linux: Use desktop services for .deb files */
    launched = QDesktopServices::openUrl(QUrl::fromLocalFile(installerPath));
#endif

    if (!launched) {
        emit progress(tr("Failed to launch the installer automatically."));
        return false;
    }

    emit progressValue(100);
    return true;
}

void AstapDownloaderWorker::run()
{
    m_cancel = false;
    emit progressValue(-1);

    /* Determine platform-specific download URL and filename */
    QString downloadUrl;
    QString fileName;

#ifdef Q_OS_WIN
    downloadUrl = SOURCEFORGE_BASE_URL + "d50_star_database.exe";
    fileName = "d50_star_database.exe";

#elif defined(Q_OS_MAC)
    downloadUrl = SOURCEFORGE_BASE_URL + "d50_star_database.pkg";
    fileName = "d50_star_database.pkg";

#else
    downloadUrl = SOURCEFORGE_BASE_URL + "d50_star_database.deb";
    fileName = "d50_star_database.deb";
#endif

    /* Download to system temp directory */
    QString tempFilePath = QDir::tempPath() + QDir::separator() + fileName;

    bool downloadSuccess = downloadHttp(downloadUrl, tempFilePath);

    /* Check for cancellation */
    if (m_cancel) {
        emit finished(false, tr("Download cancelled."));
        return;
    }

    /* Check for download failure */
    if (!downloadSuccess) {
        emit finished(false, tr("Failed to download ASTAP D50 Database."));
        return;
    }

    /* Launch the installer */
    if (!launchInstaller(tempFilePath)) {
        emit finished(false,
            tr("Download succeeded but automatically launching the installer failed."));
        return;
    }

    emit finished(true,
        tr("Installer successfully launched. Please follow its instructions."));
}

/* ============================================================================
 * AstapDownloader Implementation
 * ============================================================================ */

AstapDownloader::AstapDownloader(QObject* parent)
    : QObject(parent)
{
}

AstapDownloader::~AstapDownloader()
{
    /* Ensure worker thread is properly terminated */
    if (m_thread) {
        m_thread->quit();

        if (!m_thread->wait(5000)) {
            /* Force termination if graceful shutdown fails */
            m_thread->terminate();
            m_thread->wait();
        }
    }
}

void AstapDownloader::startDownload()
{
    /* Prevent multiple concurrent downloads */
    if (m_thread) {
        return;
    }

    /* Create worker and thread */
    m_thread = new QThread(this);
    m_worker = new AstapDownloaderWorker();
    m_worker->moveToThread(m_thread);

    /* Connect thread lifecycle signals */
    connect(m_thread, &QThread::started,
            m_worker, &AstapDownloaderWorker::run);

    /* Forward worker signals to this object */
    connect(m_worker, &AstapDownloaderWorker::progress,
            this, &AstapDownloader::progress);

    connect(m_worker, &AstapDownloaderWorker::progressValue,
            this, &AstapDownloader::progressValue);

    connect(m_worker, &AstapDownloaderWorker::finished,
            this, [this](bool ok, const QString& msg) {
                emit finished(ok, msg);
                m_thread->quit();
            });

    /* Clean up when thread finishes */
    connect(m_thread, &QThread::finished,
            m_worker, &QObject::deleteLater);

    connect(m_thread, &QThread::finished,
            m_thread, &QObject::deleteLater);

    connect(m_thread, &QThread::finished, this, [this]() {
        m_thread = nullptr;
        m_worker = nullptr;
    });

    /* Start the download */
    m_thread->start();
}

void AstapDownloader::cancel()
{
    if (m_worker) {
        m_worker->cancel();
    }
}