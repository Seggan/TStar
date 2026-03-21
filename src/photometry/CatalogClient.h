#ifndef CATALOGCLIENT_H
#define CATALOGCLIENT_H

#include <QObject>
#include <QNetworkAccessManager>
#include <vector>

struct CatalogStar {
    double ra;
    double dec;
    double magB;
    double magV;
    double B_V; // Color index
    double teff; // Effective Temperature (Gaia DR3)
    double bp_rp; // Gaia BP-RP color index
};

class CatalogClient : public QObject {
    Q_OBJECT
public:
    explicit CatalogClient(QObject* parent = nullptr);
    void queryAPASS(double ra, double dec, double radiusDeg);
    void queryGaiaDR3(double ra, double dec, double radiusDeg);

signals:
    void catalogReady(const std::vector<CatalogStar>& stars);
    void errorOccurred(const QString& msg);
    void mirrorStatus(const QString& msg); // For retry transparency

private slots:
    void onReply(QNetworkReply* reply);
    void retryWithNextMirror();

private:
    QNetworkAccessManager* m_manager;
    static int s_currentMirrorIndex; // SHARED across all instances for consistency
    double m_lastQueryRa, m_lastQueryDec, m_lastQueryRadius;
    QString m_lastQueryType; // "GAIA" or "APASS"
    
    // Internal send methods (don't reset mirror index — used for retries)
    void sendGaia();
    void sendAPASS();

};

#endif // CATALOGCLIENT_H
