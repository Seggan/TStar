#ifndef HIPSCLIENT_H
#define HIPSCLIENT_H

// ============================================================================
// HiPSClient
//
// Fetches sky-survey image cutouts from the CDS Aladin hips2fits service.
// Supports multiple survey catalogs, local FITS caching with LRU eviction,
// and asynchronous download with progress reporting.
// ============================================================================

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTemporaryFile>

#include "../ImageBuffer.h"

class HiPSClient : public QObject {
    Q_OBJECT

public:
    explicit HiPSClient(QObject* parent = nullptr);

    // -- Predefined survey identifiers ------------------------------------
    static const QString SUR_PANSTARRS_DR1_COLOR;
    static const QString SUR_DSS2_RED;
    static const QString SUR_UNWISE_COLOR;

    /**
     * Fetches a FITS image cutout from CDS Aladin hips2fits.
     *
     * @param hips           The HiPS survey ID (e.g. "CDS/P/DSS2/red").
     * @param ra             Right Ascension of the center (degrees).
     * @param dec            Declination of the center (degrees).
     * @param fov            Field of View of the X axis (width) in degrees.
     * @param width          Requested image width in pixels.
     * @param height         Requested image height in pixels.
     * @param rotationAngle  Position angle of image Y-axis from North,
     *                       East of North (degrees). Pass 0 for standard
     *                       North-up orientation. Computed from the target
     *                       image WCS CD matrix via WCSUtils::positionAngle().
     */
    void fetchFITS(const QString& hips, double ra, double dec, double fov,
                   int width, int height, double rotationAngle = 0.0);

    /** Aborts any in-flight download request. */
    void cancel();

    /** Removes all cached FITS files. */
    void clearCache();

    /** Returns the total size of all cached files in bytes. */
    static qint64 getCacheSize();

    /** Sets the maximum allowed cache size in bytes (LRU eviction). */
    static void setMaxCacheSize(qint64 bytes);

signals:
    /** Emitted when the FITS image has been downloaded and parsed. */
    void imageReady(const ImageBuffer& buffer);

    /** Emitted on network or parsing errors. */
    void errorOccurred(const QString& errorMsg);

    /** Reports download progress (received bytes / total bytes). */
    void downloadProgress(qint64 received, qint64 total);

private slots:
    void onReplyFinished();

private:
    // -- Cache management -------------------------------------------------
    QString getCacheFilePath(const QString& hips, double ra, double dec,
                             double fov, int width, int height,
                             double rotationAngle);
    void cleanupCache();

    // -- Members ----------------------------------------------------------
    QNetworkAccessManager*  m_manager;
    QNetworkReply*          m_reply = nullptr;
    QString                 m_cacheDir;

    static qint64           s_maxCacheSize;
};

#endif // HIPSCLIENT_H