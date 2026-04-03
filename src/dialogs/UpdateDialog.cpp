// =============================================================================
// UpdateDialog.cpp
// Implements the application update dialog. Displays a changelog, downloads
// the platform-appropriate installer, and optionally launches it.
// =============================================================================

#include "UpdateDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextBrowser>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QNetworkRequest>
#include <QUrl>
#include <QDir>
#include <QStandardPaths>
#include <QProcess>
#include <QDesktopServices>
#include <QMessageBox>
#include <QCoreApplication>
#include <QApplication>
#include <QScreen>
#include <QDebug>

// -----------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------
UpdateDialog::UpdateDialog(QWidget* parent,
                           const QString& version,
                           const QString& changelog,
                           const QString& downloadUrl)
    : DialogBase(parent, tr("Update Available: v%1").arg(version), 500, 400)
    , m_downloadUrl(downloadUrl)
    , m_nam(nullptr)
    , m_reply(nullptr)
    , m_file(nullptr)
{
    setWindowIcon(QIcon(":/images/Logo.png"));

    // Centre the dialog on the primary screen, overriding the default
    // DialogBase positioning
    QScreen* screen = QApplication::primaryScreen();
    if (screen) {
        QRect geo = screen->geometry();
        move((geo.width() - 500) / 2, (geo.height() - 400) / 2);
    }

    QVBoxLayout* layout = new QVBoxLayout(this);

    // --- Header ---
    QLabel* header = new QLabel(
        tr("<h3>A new version of TStar is available!</h3>"), this);
    layout->addWidget(header);

    // --- Changelog ---
    layout->addWidget(new QLabel(tr("What's New:"), this));

    m_changelogView = new QTextBrowser(this);
    m_changelogView->setMarkdown(changelog);
    m_changelogView->setOpenExternalLinks(true);
    layout->addWidget(m_changelogView);

    // --- Progress status ---
    m_statusLabel = new QLabel(tr("Would you like to update now?"), this);
    layout->addWidget(m_statusLabel);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setVisible(false);
    layout->addWidget(m_progressBar);

    // --- Action buttons ---
    QHBoxLayout* btnLayout = new QHBoxLayout();

    m_updateBtn = new QPushButton(tr("Update Now"), this);
    m_updateBtn->setStyleSheet(
        "font-weight: bold; background-color: #007acc; color: white; padding: 6px;");

    m_cancelBtn = new QPushButton(tr("Not Now"), this);

    btnLayout->addStretch();
    btnLayout->addWidget(m_cancelBtn);
    btnLayout->addWidget(m_updateBtn);
    layout->addLayout(btnLayout);

    connect(m_updateBtn, &QPushButton::clicked, this, &UpdateDialog::startDownload);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

// -----------------------------------------------------------------------------
// Destructor  --  Cleans up any active network reply and open file handle.
// -----------------------------------------------------------------------------
UpdateDialog::~UpdateDialog()
{
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
    }
    if (m_file) {
        m_file->close();
        delete m_file;
    }
}

// -----------------------------------------------------------------------------
// startDownload  --  Initiates the installer download. If the URL does not
// point to a platform-specific binary, the system browser is opened instead.
// -----------------------------------------------------------------------------
void UpdateDialog::startDownload()
{
    // Determine whether a direct download is available for this platform
#if defined(Q_OS_WIN)
    if (!m_downloadUrl.endsWith(".exe", Qt::CaseInsensitive)) {
#elif defined(Q_OS_MAC)
    if (!m_downloadUrl.endsWith(".dmg", Qt::CaseInsensitive)) {
#else
    if (true) {  // Linux: always open in browser
#endif
        QDesktopServices::openUrl(QUrl(m_downloadUrl));
        accept();
        return;
    }

    // Prepare the destination file in the system temp directory
    QString tempDir  = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QString fileName = m_downloadUrl.section('/', -1);
    m_destinationPath = QDir(tempDir).filePath(fileName);

    m_file = new QFile(m_destinationPath);
    if (!m_file->open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, tr("Error"),
            tr("Could not save update file: %1").arg(m_file->errorString()));
        return;
    }

    // Update button states for download mode
    m_updateBtn->setEnabled(false);
    m_cancelBtn->setEnabled(false);
    m_progressBar->setVisible(true);
    m_progressBar->setValue(0);
    m_statusLabel->setText(tr("Downloading update..."));

    // Start the HTTP request with redirect following
    m_nam = new QNetworkAccessManager(this);
    QNetworkRequest request{QUrl(m_downloadUrl)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    m_reply = m_nam->get(request);

    connect(m_reply, &QNetworkReply::downloadProgress,
            this,    &UpdateDialog::onDownloadProgress);
    connect(m_reply, &QNetworkReply::readyRead,
            this,    &UpdateDialog::onReadyRead);
    connect(m_reply, &QNetworkReply::finished,
            this,    &UpdateDialog::onDownloadFinished);
}

// -----------------------------------------------------------------------------
// onDownloadProgress  --  Updates the progress bar during the download.
// -----------------------------------------------------------------------------
void UpdateDialog::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    if (bytesTotal > 0) {
        m_progressBar->setMaximum(100);
        int pct = static_cast<int>((bytesReceived * 100) / bytesTotal);
        m_progressBar->setValue(pct);
        m_statusLabel->setText(tr("Downloading... %1%").arg(pct));
    } else {
        // Indeterminate progress
        m_progressBar->setMaximum(0);
        m_statusLabel->setText(tr("Downloading... (%1 bytes)").arg(bytesReceived));
    }
}

// -----------------------------------------------------------------------------
// onReadyRead  --  Writes incoming data to the destination file.
// -----------------------------------------------------------------------------
void UpdateDialog::onReadyRead()
{
    if (m_file) {
        m_file->write(m_reply->readAll());
    }
}

// -----------------------------------------------------------------------------
// onDownloadFinished  --  Handles download completion, verifies success,
// and prompts the user to install.
// -----------------------------------------------------------------------------
void UpdateDialog::onDownloadFinished()
{
    if (m_file) {
        m_file->close();
    }

    if (m_reply->error() != QNetworkReply::NoError) {
        QMessageBox::critical(this, tr("Update Failed"),
            tr("Download failed: %1").arg(m_reply->errorString()));
        m_progressBar->setVisible(false);
        m_updateBtn->setEnabled(true);
        m_cancelBtn->setEnabled(true);
        m_file->remove();  // Remove the partial download
        return;
    }

    // Download completed successfully
    m_statusLabel->setText(tr("Download complete. Verifying..."));
    m_progressBar->setValue(100);

    // Prompt the user to install
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, tr("Ready to Install"),
        tr("Download complete. Close TStar and start the installer?"),
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        launchInstaller();
    } else {
        m_statusLabel->setText(
            tr("Update downloaded. Run installer manually from temp if desired."));
        m_updateBtn->setEnabled(true);
        m_cancelBtn->setEnabled(true);
    }
}

// -----------------------------------------------------------------------------
// launchInstaller  --  Launches the downloaded installer and terminates the
// application. On macOS, user scripts are backed up before the update.
// -----------------------------------------------------------------------------
void UpdateDialog::launchInstaller()
{
#if defined(Q_OS_MAC)
    // Back up user scripts before updating on macOS
    QString appDir     = QCoreApplication::applicationDirPath();
    QString scriptsSrc = QDir::cleanPath(appDir + "/../Resources/scripts");
    QString scriptsBak = QDir::homePath() +
                         "/Library/Application Support/TStar/scripts_backup";

    QDir bakDir(scriptsBak);
    if (!bakDir.exists()) bakDir.mkpath(".");

    QDir srcDir(scriptsSrc);
    if (srcDir.exists()) {
        for (const QFileInfo& fi : srcDir.entryInfoList({"*.tss"}, QDir::Files)) {
            QString dest = bakDir.absoluteFilePath(fi.fileName());
            if (!QFile::exists(dest)) {
                QFile::copy(fi.absoluteFilePath(), dest);
            }
        }
    }

    bool success = QProcess::startDetached("open", QStringList() << m_destinationPath);
#else
    bool success = QProcess::startDetached(m_destinationPath, QStringList());
#endif

    if (success) {
        QCoreApplication::quit();
    } else {
        QMessageBox::critical(this, tr("Error"),
            tr("Failed to launch installer. Please run it manually:\n%1")
                .arg(m_destinationPath));
    }
}