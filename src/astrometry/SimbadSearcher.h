#ifndef SIMBADSEARCHER_H
#define SIMBADSEARCHER_H

// ============================================================================
// SimbadSearcher
//
// Resolves astronomical object names to (RA, Dec) coordinates using the
// CDS Sesame name resolver service (which queries SIMBAD, NED, and VizieR).
// ============================================================================

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class SimbadSearcher : public QObject {
    Q_OBJECT

public:
    explicit SimbadSearcher(QObject* parent = nullptr);

    /**
     * Initiates an asynchronous name resolution query.
     * @param objectName  The astronomical object name (e.g. "M31", "NGC 7000").
     */
    void search(const QString& objectName);

signals:
    /** Emitted when the object is successfully resolved. */
    void objectFound(const QString& name, double ra, double dec);

    /** Emitted when the query fails or the object cannot be resolved. */
    void errorOccurred(const QString& errorMsg);

private slots:
    void onReplyFinished(QNetworkReply* reply);

private:
    QNetworkAccessManager* m_manager;
};

#endif // SIMBADSEARCHER_H