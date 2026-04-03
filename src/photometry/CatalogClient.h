#ifndef CATALOGCLIENT_H
#define CATALOGCLIENT_H

// =============================================================================
// CatalogClient.h
//
// Asynchronous network client for querying astronomical catalogs via VizieR
// mirror servers. Supports APASS DR9, Gaia DR3, and HyperLeda (PGC).
//
// Features:
//   - Automatic mirror failover with configurable timeout per mirror
//   - Adaptive query radius reduction for Gaia DR3 on repeated failures
//   - VOTable XML response parsing with robust UCD-based field mapping
//   - Gaia BP-RP to Johnson B-V color index conversion
// =============================================================================

#include <QObject>
#include <QNetworkAccessManager>

#include <vector>
#include <limits>

// =============================================================================
// CatalogStar - Photometric data for a single catalog star
// =============================================================================

struct CatalogStar {
    double ra    = 0.0;                                         // Right ascension (J2000, degrees)
    double dec   = 0.0;                                         // Declination (J2000, degrees)
    double magB  = std::numeric_limits<double>::quiet_NaN();    // Johnson B magnitude
    double magV  = std::numeric_limits<double>::quiet_NaN();    // Johnson V magnitude
    double B_V   = std::numeric_limits<double>::quiet_NaN();    // Johnson B-V color index
    double teff  = 0.0;                                         // Effective temperature (K), from Gaia GSP-Phot
    double bp_rp = std::numeric_limits<double>::quiet_NaN();    // Gaia BP-RP color index
};

// =============================================================================
// PGCGalaxy - Morphological data for a HyperLeda (PGC) galaxy
// =============================================================================

struct PGCGalaxy {
    QString pgcName;                // PGC catalog designation (e.g., "PGC 12345")
    double  ra              = 0.0;  // Right ascension (J2000, degrees)
    double  dec             = 0.0;  // Declination (J2000, degrees)
    double  majorAxisArcmin = 0.0;  // Major axis diameter (arcminutes)
    double  minorAxisArcmin = 0.0;  // Minor axis diameter (arcminutes)
    double  posAngle        = 0.0;  // Position angle (degrees, north through east)
};

// =============================================================================
// CatalogClient - Asynchronous VizieR catalog query client
// =============================================================================

class CatalogClient : public QObject {
    Q_OBJECT

public:
    explicit CatalogClient(QObject* parent = nullptr);

    // Initiate an asynchronous query against APASS DR9 (VizieR II/336/apass9).
    void queryAPASS(double ra, double dec, double radiusDeg);

    // Initiate an asynchronous query against Gaia DR3 (VizieR I/355/gaiadr3).
    void queryGaiaDR3(double ra, double dec, double radiusDeg);

    // Initiate an asynchronous query against HyperLeda PGC (VizieR VII/237/pgc).
    void queryHyperLeda(double ra, double dec, double radiusDeg);

signals:
    // Emitted when star catalog data has been successfully retrieved and parsed.
    void catalogReady(const std::vector<CatalogStar>& stars);

    // Emitted when galaxy catalog data has been successfully retrieved and parsed.
    void galaxiesReady(const std::vector<PGCGalaxy>& galaxies);

    // Emitted when all mirrors have been exhausted without a successful query.
    void errorOccurred(const QString& msg);

    // Emitted to report the status of mirror selection and failover attempts.
    void mirrorStatus(const QString& msg);

private slots:
    // Handler for completed network replies; parses VOTable XML or triggers failover.
    void onReply(QNetworkReply* reply);

    // Advances to the next VizieR mirror and retries the current query.
    void retryWithNextMirror();

private:
    QNetworkAccessManager* m_manager;

    // Shared mirror index across all instances for session-persistent failover.
    static int s_currentMirrorIndex;

    // Parameters of the most recent query, retained for mirror retry logic.
    double  m_lastQueryRa     = 0.0;
    double  m_lastQueryDec    = 0.0;
    double  m_lastQueryRadius = 0.0;
    QString m_lastQueryType;  // "GAIA", "APASS", or "HYPERLEDA"

    // Internal send methods that do not reset the mirror index (used for retries).
    void sendGaia();
    void sendAPASS();
    void sendHyperLeda();
};

#endif // CATALOGCLIENT_H