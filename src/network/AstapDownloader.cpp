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

// ============================================================================
// AstapDownloaderWorker
// ============================================================================

AstapDownloaderWorker::AstapDownloaderWorker(QObject* parent) : QObject(parent) {}

void AstapDownloaderWorker::cancel() {
    m_cancel = true;
}

bool AstapDownloaderWorker::downloadHttp(const QString& url, const QString& destPath) {
    QNetworkAccessManager nam;

    emit progress(tr("Connecting to SourceForge…"));

    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "TStar-AstapDownloader");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = nam.get(req);
    
    QString partPath = destPath + ".part";
    QFile file(partPath);
    bool fileOpened = false;
    
    connect(reply, &QNetworkReply::downloadProgress, [&](qint64 bytesReceived, qint64 bytesTotal) {
        if (m_cancel) return;
        
        if (!fileOpened && file.open(QIODevice::WriteOnly)) {
            fileOpened = true;
        }

        if (bytesTotal > 0) {
            int pct = qBound(0, static_cast<int>((double(bytesReceived) / double(bytesTotal)) * 100.0), 100);
            emit progress(tr("Downloading… %1%").arg(pct));
            emit progressValue(pct);
        } else if (bytesReceived > 0) {
            emit progress(tr("Downloading… %1 MB").arg(bytesReceived / (1024.0 * 1024.0), 0, 'f', 1));
            emit progressValue(-1);
        }
    });
    
    connect(reply, &QNetworkReply::readyRead, [&]() {
        if (m_cancel) return;
        if (!fileOpened) {
            if (file.open(QIODevice::WriteOnly)) {
                fileOpened = true;
            } else {
                return;
            }
        }
        QByteArray chunk = reply->readAll();
        file.write(chunk);
    });

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    bool timedOut = false;
    connect(&timeout, &QTimer::timeout, [&](){ timedOut = true; loop.quit(); });
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeout.start(600000); // 10 min timeout
    loop.exec();
    timeout.stop();
    
    if (fileOpened) file.close();

    if (m_cancel) {
        QFile::remove(partPath);
        reply->deleteLater();
        return false;
    }

    if (timedOut || reply->error() != QNetworkReply::NoError) {
        emit progress(tr("Download failed or timed out."));
        qWarning() << "Download failed:" << reply->errorString();
        QFile::remove(partPath);
        reply->deleteLater();
        return false;
    }

    reply->deleteLater();

    QFile::remove(destPath);
    if (!QFile::rename(partPath, destPath)) {
        QFile::remove(partPath);
        return false;
    }

    emit progress(tr("Download complete."));
    return true;
}

bool AstapDownloaderWorker::launchInstaller(const QString& installerPath) {
    emit progress(tr("Launching installer..."));
    emit progressValue(-1);

    bool opened = false;
#ifdef Q_OS_WIN
    // Standard execution on Windows
    opened = QDesktopServices::openUrl(QUrl::fromLocalFile(installerPath));
#elif defined(Q_OS_MAC)
    // On Mac, we need to run 'open' for .pkg
    opened = QProcess::startDetached("open", QStringList() << installerPath);
#else
    // Linux uses .deb
    opened = QDesktopServices::openUrl(QUrl::fromLocalFile(installerPath));
#endif

    if (!opened) {
        emit progress(tr("Failed to launch the installer automatically."));
        return false;
    }

    emit progressValue(100);
    return true;
}

void AstapDownloaderWorker::run() {
    m_cancel = false;
    emit progressValue(-1);

    QString downloadUrl;
    QString fileName;

#ifdef Q_OS_WIN
    downloadUrl = "https://downloads.sourceforge.net/project/astap-program/star_databases/d50_star_database.exe";
    fileName = "d50_star_database.exe";
#elif defined(Q_OS_MAC)
    downloadUrl = "https://downloads.sourceforge.net/project/astap-program/star_databases/d50_star_database.pkg";
    fileName = "d50_star_database.pkg";
#else
    downloadUrl = "https://downloads.sourceforge.net/project/astap-program/star_databases/d50_star_database.deb";
    fileName = "d50_star_database.deb";
#endif

    QString tempFile = QDir::tempPath() + QDir::separator() + fileName;

    bool downloadOk = downloadHttp(downloadUrl, tempFile);

    if (m_cancel) {
        emit finished(false, tr("Download cancelled."));
        return;
    }

    if (!downloadOk) {
        emit finished(false, tr("Failed to download ASTAP D50 Database."));
        return;
    }

    if (!launchInstaller(tempFile)) {
        emit finished(false, tr("Download succeeded but automatically launching the installer failed."));
        return;
    }

    emit finished(true, tr("Installer successfully launched. Please follow its instructions."));
}

// ============================================================================
// AstapDownloader
// ============================================================================

AstapDownloader::AstapDownloader(QObject* parent) : QObject(parent) {}

AstapDownloader::~AstapDownloader() {
    if (m_thread) {
        m_thread->quit();
        if (!m_thread->wait(5000)) {
            m_thread->terminate();
            m_thread->wait();
        }
    }
}

void AstapDownloader::startDownload() {
    if (m_thread) return;

    m_thread = new QThread(this);
    m_worker = new AstapDownloaderWorker();
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started, m_worker, &AstapDownloaderWorker::run);
    connect(m_worker, &AstapDownloaderWorker::progress, this, &AstapDownloader::progress);
    connect(m_worker, &AstapDownloaderWorker::finished, this, [this](bool ok, const QString& msg) {
        emit finished(ok, msg);
        m_thread->quit();
    });
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    connect(m_worker, &AstapDownloaderWorker::progressValue, this, &AstapDownloader::progressValue);
    connect(m_thread, &QThread::finished, this, [this]() {
        m_thread = nullptr;
        m_worker = nullptr;
    });

    m_thread->start();
}

void AstapDownloader::cancel() {
    if (m_worker) {
        m_worker->cancel();
    }
}
