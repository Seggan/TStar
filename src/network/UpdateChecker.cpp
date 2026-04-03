/**
 * @file UpdateChecker.cpp
 * @brief Implementation of application update checker
 *
 * Queries GitHub Releases API for the latest release, performs version
 * comparison, and selects the appropriate platform-specific installer.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#include "UpdateChecker.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QDebug>

#include <algorithm>

/* ============================================================================
 * Constants
 * ============================================================================ */

namespace {

/** GitHub API URL for latest release */
const QString GITHUB_RELEASES_API =
    "https://api.github.com/repos/Ft2801/TStar/releases/latest";

/** URL for changelog file in repository */
const QString CHANGELOG_URL =
    "https://raw.githubusercontent.com/Ft2801/TStar/master/changelog.txt";

}  // anonymous namespace

/* ============================================================================
 * UpdateChecker Implementation
 * ============================================================================ */

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

void UpdateChecker::checkForUpdates(const QString& currentVersion)
{
    m_currentVersion = currentVersion;

    QNetworkRequest request{QUrl(GITHUB_RELEASES_API)};
    request.setHeader(QNetworkRequest::UserAgentHeader, "TStar-Updater");

    m_nam->get(request);

    /* Use UniqueConnection to prevent duplicate slot connections */
    connect(m_nam, &QNetworkAccessManager::finished,
            this, &UpdateChecker::onReplyFinished,
            Qt::UniqueConnection);
}

void UpdateChecker::onReplyFinished(QNetworkReply* reply)
{
    reply->deleteLater();

    QUrl requestUrl = reply->request().url();

    /* Handle network errors */
    if (reply->error() != QNetworkReply::NoError) {
        emit errorOccurred(tr("Network error: %1").arg(reply->errorString()));
        return;
    }

    /* --------------------------------------------------------------------
     * Handle GitHub Release API Response
     * -------------------------------------------------------------------- */
    if (requestUrl.toString().contains("api.github.com")) {

        QByteArray responseData = reply->readAll();
        QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData);

        if (!jsonDoc.isObject()) {
            emit errorOccurred(tr("Invalid JSON response from GitHub."));
            return;
        }

        QJsonObject releaseObj = jsonDoc.object();
        QString tagName = releaseObj["tag_name"].toString();

        /* Extract version number from tag (strip leading 'v' or 'V') */
        QString remoteVersion = tagName;
        if (remoteVersion.startsWith('v') || remoteVersion.startsWith('V')) {
            remoteVersion = remoteVersion.mid(1);
        }

        /* Check if update is available */
        if (!isNewer(m_currentVersion, remoteVersion)) {
            emit noUpdateAvailable();
            return;
        }

        m_pendingVersion = remoteVersion;
        m_pendingDownloadUrl.clear();

        /* Find platform-appropriate download asset */
        QJsonArray assets = releaseObj["assets"].toArray();

        for (const QJsonValue& assetVal : assets) {
            QJsonObject asset = assetVal.toObject();
            QString assetName = asset["name"].toString();

#if defined(Q_OS_WIN)
            /* Windows: Look for .exe installer */
            if (assetName.endsWith(".exe", Qt::CaseInsensitive)) {
                m_pendingDownloadUrl = asset["browser_download_url"].toString();
                break;
            }

#elif defined(Q_OS_MAC)
            /* macOS: Select architecture-specific installer */
            QString arch = detectMacOSArchitecture();
            bool isArm64 = (arch == "arm64");
            bool isIntel = (arch == "x86_64");

            /* Check for Apple Silicon build */
            if (isArm64 &&
                (assetName.contains("Apple-Silicon", Qt::CaseInsensitive) ||
                 assetName.contains("ARM64", Qt::CaseInsensitive) ||
                 assetName.contains("aarch64", Qt::CaseInsensitive))) {

                m_pendingDownloadUrl = asset["browser_download_url"].toString();
                break;
            }

            /* Check for Intel build */
            if (isIntel &&
                (assetName.contains("Intel", Qt::CaseInsensitive) ||
                 assetName.contains("x86_64", Qt::CaseInsensitive))) {

                m_pendingDownloadUrl = asset["browser_download_url"].toString();
                break;
            }

            /* Fallback: Accept generic .dmg if no architecture-specific match */
            if (m_pendingDownloadUrl.isEmpty() &&
                assetName.endsWith(".dmg", Qt::CaseInsensitive)) {

                m_pendingDownloadUrl = asset["browser_download_url"].toString();
            }

#else
            /* Linux: No specific asset matching (use release page) */
            Q_UNUSED(assetName);
#endif
        }

        /* Fallback to release page if no direct download found */
        if (m_pendingDownloadUrl.isEmpty()) {
            m_pendingDownloadUrl = releaseObj["html_url"].toString();
        }

        /* Fetch detailed changelog from repository */
        fetchChangelog();
    }
    /* --------------------------------------------------------------------
     * Handle Changelog Response
     * -------------------------------------------------------------------- */
    else if (requestUrl.toString().contains("changelog.txt")) {

        QString changelog = QString::fromUtf8(reply->readAll());

        if (changelog.isEmpty()) {
            changelog = tr("No detailed changelog available.");
        }

        emit updateAvailable(m_pendingVersion, changelog, m_pendingDownloadUrl);
    }
}

void UpdateChecker::fetchChangelog()
{
    QNetworkRequest request{QUrl(CHANGELOG_URL)};
    request.setHeader(QNetworkRequest::UserAgentHeader, "TStar-Updater");

    m_nam->get(request);
}

QString UpdateChecker::detectMacOSArchitecture()
{
#if defined(Q_OS_MAC)
    /*
     * Compile-time architecture detection.
     * This reports the architecture the binary was compiled for,
     * which is the relevant architecture for update selection.
     */
    #if defined(__arm64__) || defined(__aarch64__)
        return "arm64";
    #elif defined(__x86_64__) || defined(__i386__)
        return "x86_64";
    #else
        return "unknown";
    #endif
#else
    return "unknown";
#endif
}

bool UpdateChecker::isNewer(const QString& current, const QString& remote)
{
    QStringList currentParts = current.split('.');
    QStringList remoteParts = remote.split('.');

    int compareCount = std::min(currentParts.size(), remoteParts.size());

    /* Compare version components numerically */
    for (int i = 0; i < compareCount; ++i) {
        int currentNum = currentParts[i].toInt();
        int remoteNum = remoteParts[i].toInt();

        if (remoteNum > currentNum) {
            return true;
        }

        if (remoteNum < currentNum) {
            return false;
        }
    }

    /*
     * If all compared parts are equal, the version with more parts
     * is considered newer (e.g., 1.0.1 > 1.0)
     */
    return remoteParts.size() > currentParts.size();
}