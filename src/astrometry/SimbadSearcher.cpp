#include "SimbadSearcher.h"

#include <QNetworkRequest>
#include <QUrl>
#include <QNetworkReply>
#include <QXmlStreamReader>
#include <QDebug>

// ============================================================================
// Construction
// ============================================================================

SimbadSearcher::SimbadSearcher(QObject* parent) : QObject(parent)
{
    m_manager = new QNetworkAccessManager(this);
}

// ============================================================================
// search --- Initiate a Sesame name-resolution query
// ============================================================================

void SimbadSearcher::search(const QString& objectName)
{
    // Build the CDS Sesame Name Resolver URL (XML output format)
    QString urlStr = QString(
        "http://cdsweb.u-strasbg.fr/cgi-bin/nph-sesame/-ox?%1")
        .arg(objectName);

    QNetworkRequest request{QUrl(urlStr)};

    QNetworkReply* reply = m_manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onReplyFinished(reply);
    });
}

// ============================================================================
// onReplyFinished --- Parse the Sesame XML response
// ============================================================================

void SimbadSearcher::onReplyFinished(QNetworkReply* reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit errorOccurred(
            tr("Network Error: %1").arg(reply->errorString()));
        return;
    }

    QByteArray data = reply->readAll();
    QXmlStreamReader xml(data);

    QString resolvedName;
    double  ra    = 0;
    double  dec   = 0;
    bool    found = false;

    // Extract J2000 coordinates from the Sesame XML response
    while (!xml.atEnd() && !xml.hasError()) {
        QXmlStreamReader::TokenType token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            if (xml.name() == QLatin1String("jradeg")) {
                ra = xml.readElementText().toDouble();
            } else if (xml.name() == QLatin1String("jdedeg")) {
                dec   = xml.readElementText().toDouble();
                found = true;  // Both RA and Dec are assumed present
            }
        }
    }

    if (xml.hasError()) {
        emit errorOccurred(tr("XML Parse Error"));
        return;
    }

    if (found) {
        emit objectFound("", ra, dec);
    } else {
        emit errorOccurred(tr("Object not found in Simbad/Sesame."));
    }
}