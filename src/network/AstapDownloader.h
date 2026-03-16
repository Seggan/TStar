#ifndef ASTAPDOWNLOADER_H
#define ASTAPDOWNLOADER_H

#include <QObject>
#include <QString>
#include <QThread>

class AstapDownloaderWorker : public QObject {
    Q_OBJECT
public:
    explicit AstapDownloaderWorker(QObject* parent = nullptr);

public slots:
    void run();
    void cancel();

signals:
    void progress(const QString& message);
    void progressValue(int value);
    void finished(bool ok, const QString& message);

private:
    bool downloadHttp(const QString& url, const QString& destPath);
    bool launchInstaller(const QString& installerPath);

    std::atomic<bool> m_cancel{false};
};

class AstapDownloader : public QObject {
    Q_OBJECT
public:
    explicit AstapDownloader(QObject* parent = nullptr);
    ~AstapDownloader();

    void startDownload();

signals:
    void progress(const QString& message);
    void progressValue(int value);
    void finished(bool ok, const QString& message);

public slots:
    void cancel();

private:
    QThread* m_thread = nullptr;
    AstapDownloaderWorker* m_worker = nullptr;
};

#endif // ASTAPDOWNLOADER_H
