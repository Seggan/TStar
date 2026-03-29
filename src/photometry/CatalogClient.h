#ifndef CATALOGCLIENT_H
#define CATALOGCLIENT_H

#include <QObject>
#include <QNetworkAccessManager>
#include <vector>

struct CatalogStar {
    double ra = 0;
    double dec = 0;
    double magB = std::numeric_limits<double>::quiet_NaN();
    double magV = std::numeric_limits<double>::quiet_NaN();
    double B_V = std::numeric_limits<double>::quiet_NaN(); // Color index
    double teff = 0; // Effective Temperature (Gaia DR3)
    double bp_rp = std::numeric_limits<double>::quiet_NaN(); // Gaia BP-RP color index
};

struct PGCGalaxy {
    QString pgcName;
    double ra = 0;
    double dec = 0;
    double majorAxisArcmin = 0;
    double minorAxisArcmin = 0;
    double posAngle = 0;
};

class CatalogClient : public QObject {
    Q_OBJECT
public:
    explicit CatalogClient(QObject* parent = nullptr);
    void queryAPASS(double ra, double dec, double radiusDeg);
    void queryGaiaDR3(double ra, double dec, double radiusDeg);
    void queryHyperLeda(double ra, double dec, double radiusDeg);

signals:
    void catalogReady(const std::vector<CatalogStar>& stars);
    void galaxiesReady(const std::vector<PGCGalaxy>& galaxies);
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
    void sendHyperLeda();

};

#endif // CATALOGCLIENT_H
