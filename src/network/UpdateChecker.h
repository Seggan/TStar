/**
 * @file UpdateChecker.h
 * @brief Application update checker interface
 *
 * Provides functionality to check for application updates via the GitHub
 * Releases API. Supports version comparison, platform-specific installer
 * selection, and changelog fetching.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#ifndef UPDATECHECKER_H
#define UPDATECHECKER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>

/**
 * @class UpdateChecker
 * @brief Checks for application updates via GitHub Releases API
 *
 * This class queries the GitHub API for the latest release, compares
 * version numbers, and provides download URLs for platform-specific
 * installers.
 *
 * Usage:
 * @code
 *   UpdateChecker* checker = new UpdateChecker(this);
 *   connect(checker, &UpdateChecker::updateAvailable, this, &MyClass::onUpdateFound);
 *   connect(checker, &UpdateChecker::noUpdateAvailable, this, &MyClass::onUpToDate);
 *   checker->checkForUpdates("1.0.0");
 * @endcode
 */
class UpdateChecker : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Construct a new UpdateChecker
     * @param parent Parent QObject for memory management
     */
    explicit UpdateChecker(QObject* parent = nullptr);

    /**
     * @brief Initiate an update check
     *
     * Queries the GitHub Releases API for the latest release and compares
     * it against the provided current version.
     *
     * @param currentVersion Current application version (e.g., "1.2.3")
     */
    void checkForUpdates(const QString& currentVersion);

signals:
    /**
     * @brief Emitted when a newer version is available
     *
     * @param newVersion  Version string of the new release
     * @param changelog   Release notes or changelog text
     * @param downloadUrl URL to download the update (platform-specific)
     */
    void updateAvailable(const QString& newVersion,
                         const QString& changelog,
                         const QString& downloadUrl);

    /**
     * @brief Emitted when no update is available
     *
     * The current version is up-to-date or newer than the latest release.
     */
    void noUpdateAvailable();

    /**
     * @brief Emitted when an error occurs during the update check
     * @param error Human-readable error description
     */
    void errorOccurred(const QString& error);

private slots:
    /**
     * @brief Handle network reply completion
     * @param reply Completed network reply
     */
    void onReplyFinished(QNetworkReply* reply);

private:
    /**
     * @brief Compare two version strings
     *
     * Performs semantic version comparison (major.minor.patch).
     *
     * @param current Current version string
     * @param remote  Remote version string
     * @return true if remote is newer than current
     */
    bool isNewer(const QString& current, const QString& remote);

    /**
     * @brief Fetch changelog from repository
     *
     * Downloads changelog.txt from the master branch.
     */
    void fetchChangelog();

    /**
     * @brief Detect macOS CPU architecture
     *
     * @return "arm64" for Apple Silicon, "x86_64" for Intel, "unknown" otherwise
     */
    QString detectMacOSArchitecture();

    /** Network access manager for API requests */
    QNetworkAccessManager* m_nam;

    /** Current application version being checked against */
    QString m_currentVersion;

    /** Pending update version (set after API response) */
    QString m_pendingVersion;

    /** Pending download URL (set after API response) */
    QString m_pendingDownloadUrl;
};

#endif  // UPDATECHECKER_H