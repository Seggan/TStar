#ifndef UPDATEDIALOG_H
#define UPDATEDIALOG_H

// =============================================================================
// UpdateDialog.h
// Dialog that presents release notes for a new application version and
// manages the download-and-install workflow.
// =============================================================================

#include "DialogBase.h"

#include <QDialog>
#include <QtNetwork>
#include <QFile>

class QTextBrowser;
class QProgressBar;
class QLabel;
class QPushButton;

class UpdateDialog : public DialogBase {
    Q_OBJECT

public:
    UpdateDialog(QWidget* parent,
                 const QString& version,
                 const QString& changelog,
                 const QString& downloadUrl);
    ~UpdateDialog();

private slots:
    void startDownload();
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onDownloadFinished();
    void onReadyRead();

private:
    /// Launches the downloaded installer and quits the application.
    void launchInstaller();

    // Download state
    QString m_downloadUrl;
    QString m_destinationPath;

    // UI widgets
    QTextBrowser* m_changelogView;
    QProgressBar* m_progressBar;
    QLabel*       m_statusLabel;
    QPushButton*  m_updateBtn;
    QPushButton*  m_cancelBtn;

    // Network objects
    QNetworkAccessManager* m_nam;
    QNetworkReply*         m_reply;
    QFile*                 m_file;
};

#endif // UPDATEDIALOG_H