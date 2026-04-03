#include "XISFWriter.h"
#include "CompressionUtils.h"

#include <QFile>
#include <QDomDocument>
#include <QDomElement>
#include <QSet>
#include <QDateTime>
#include <QDebug>
#include <QtEndian>
#include <QCoreApplication>

#include <cmath>
#include <algorithm>

// ============================================================================
// Public API
// ============================================================================

bool XISFWriter::write(const QString&     filePath,
                       const ImageBuffer& buffer,
                       int                depth,
                       QString*           errorMsg)
{
    const ImageBuffer::BitDepth bitDepth =
        static_cast<ImageBuffer::BitDepth>(depth);
    WriteOptions options;
    return write(filePath, buffer, bitDepth, options, errorMsg);
}

// ----------------------------------------------------------------------------

bool XISFWriter::write(const QString&        filePath,
                       const ImageBuffer&    buffer,
                       ImageBuffer::BitDepth depth,
                       const WriteOptions&   options,
                       QString*              errorMsg)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMsg)
            *errorMsg = QCoreApplication::translate(
                "XISFWriter",
                "Could not open file for writing: %1").arg(filePath);
        return false;
    }

    // ------------------------------------------------------------------
    // Step 1: Convert pixel data to the target sample format in planar
    //         channel order.
    // ------------------------------------------------------------------
    QByteArray rawData = prepareImageData(buffer, depth);
    if (rawData.isEmpty()) {
        if (errorMsg)
            *errorMsg = QCoreApplication::translate(
                "XISFWriter", "Failed to prepare image data.");
        return false;
    }

    // ------------------------------------------------------------------
    // Step 2: Apply optional compression (with optional byte shuffling).
    //         Only use the compressed result if it is smaller than the
    //         uncompressed data.
    // ------------------------------------------------------------------
    QByteArray dataToWrite    = rawData;
    QString    compressionAttr;

    if (options.codec != CompressionUtils::Codec_None) {
        QByteArray toCompress = rawData;
        const int  itemSize   = bytesPerSample(depth);

        if (options.shuffle && itemSize > 1)
            toCompress = CompressionUtils::shuffle(rawData, itemSize);

        QByteArray compressed = CompressionUtils::compress(
            toCompress, options.codec, options.compressionLevel, errorMsg);

        if (!compressed.isEmpty() && compressed.size() < rawData.size()) {
            dataToWrite    = compressed;
            compressionAttr = CompressionUtils::buildCompressionAttr(
                options.codec,
                static_cast<qint64>(rawData.size()),
                options.shuffle ? itemSize : 0);

            qDebug() << "XISFWriter: compressed"
                     << rawData.size() << "->" << compressed.size() << "bytes ("
                     << QString::number(
                            100.0 * compressed.size() / rawData.size(), 'f', 1)
                     << "%)";
        } else {
            qDebug() << "XISFWriter: compression skipped (no size reduction).";
        }
    }

    const quint64 dataSize = static_cast<quint64>(dataToWrite.size());

    // ------------------------------------------------------------------
    // Step 3: Iteratively build the XML header until the attachment
    //         position encoded inside it stabilises.
    //
    //         The attachment position depends on the header size, but
    //         the header size depends on the attachment position (because
    //         it is written as a decimal integer inside the location
    //         attribute). Up to five iterations are sufficient in practice.
    // ------------------------------------------------------------------
    quint64    estimatedPos = 4096;
    QByteArray finalXml;

    for (int iteration = 0; iteration < 5; ++iteration) {
        const quint64 alignedPos = alignPosition(estimatedPos, options.blockAlignment);

        finalXml = buildHeader(buffer, depth, options,
                               dataSize, alignedPos, compressionAttr);

        // The file consists of: signature (8) + length (4) + reserved (4) + xml.
        const quint64 actualPos =
            alignPosition(16u + static_cast<quint64>(finalXml.size()),
                          options.blockAlignment);

        if (actualPos == alignedPos)
            break;

        estimatedPos = actualPos;
    }

    const quint64 attachmentPos =
        alignPosition(16u + static_cast<quint64>(finalXml.size()),
                      options.blockAlignment);

    // ------------------------------------------------------------------
    // Step 4: Write the physical file.
    // ------------------------------------------------------------------

    // 4a. XISF 1.0 signature (8 bytes).
    file.write("XISF0100", 8);

    // 4b. XML header length as a 32-bit little-endian unsigned integer.
    quint32 len32 = qToLittleEndian(static_cast<quint32>(finalXml.size()));
    file.write(reinterpret_cast<const char*>(&len32), 4);

    // 4c. Reserved field (4 zero bytes).
    const quint32 reserved = 0u;
    file.write(reinterpret_cast<const char*>(&reserved), 4);

    // 4d. XML header content.
    file.write(finalXml);

    // 4e. Alignment padding between header and data block.
    const qint64 currentPos = file.pos();
    if (currentPos < static_cast<qint64>(attachmentPos)) {
        const int padSize = static_cast<int>(attachmentPos - currentPos);
        file.write(QByteArray(padSize, '\0'));
    }

    // 4f. Image data block (possibly compressed).
    file.write(dataToWrite);

    return true;
}

// ============================================================================
// Pixel data preparation
// ============================================================================

QByteArray XISFWriter::prepareImageData(const ImageBuffer&    buffer,
                                         ImageBuffer::BitDepth depth)
{
    const int w = buffer.width();
    const int h = buffer.height();
    const int c = buffer.channels();

    const quint64               pixelCount = static_cast<quint64>(w) * h * c;
    const std::vector<float>&   srcData    = buffer.data();
    QByteArray                  result;

    // Convert interleaved [R G B R G B ...] to planar [RRR...][GGG...][BBB...]
    // and cast to the target integer or float type.

    if (depth == ImageBuffer::Depth_16Int) {
        result.resize(static_cast<int>(pixelCount * sizeof(quint16)));
        quint16* dst = reinterpret_cast<quint16*>(result.data());

        for (int ch = 0; ch < c; ++ch) {
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    const size_t srcIdx = static_cast<size_t>((y * w + x) * c + ch);
                    const size_t dstIdx = static_cast<size_t>(ch * (w * h) + y * w + x);
                    const float  v      = srcData[srcIdx];
                    dst[dstIdx] = static_cast<quint16>(
                        std::clamp(v * 65535.0f, 0.0f, 65535.0f));
                }
            }
        }
    }
    else if (depth == ImageBuffer::Depth_32Int) {
        result.resize(static_cast<int>(pixelCount * sizeof(quint32)));
        quint32* dst = reinterpret_cast<quint32*>(result.data());

        for (int ch = 0; ch < c; ++ch) {
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    const size_t srcIdx = static_cast<size_t>((y * w + x) * c + ch);
                    const size_t dstIdx = static_cast<size_t>(ch * (w * h) + y * w + x);
                    const double vd     = std::clamp(
                        static_cast<double>(srcData[srcIdx]) * 4294967295.0,
                        0.0, 4294967295.0);
                    dst[dstIdx] = static_cast<quint32>(vd);
                }
            }
        }
    }
    else {
        // Default: Float32
        result.resize(static_cast<int>(pixelCount * sizeof(float)));
        float* dst = reinterpret_cast<float*>(result.data());

        for (int ch = 0; ch < c; ++ch) {
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    const size_t srcIdx = static_cast<size_t>((y * w + x) * c + ch);
                    const size_t dstIdx = static_cast<size_t>(ch * (w * h) + y * w + x);
                    if (srcIdx < srcData.size())
                        dst[dstIdx] = srcData[srcIdx];
                }
            }
        }
    }

    return result;
}

// ============================================================================
// Header construction
// ============================================================================

QByteArray XISFWriter::buildHeader(const ImageBuffer&    buffer,
                                   ImageBuffer::BitDepth depth,
                                   const WriteOptions&   options,
                                   quint64               dataSize,
                                   quint64               attachmentPosition,
                                   const QString&        compressionAttr)
{
    const int w = buffer.width();
    const int h = buffer.height();
    const int c = buffer.channels();

    QDomDocument doc;

    // ------------------------------------------------------------------
    // Root element: xisf (with required namespace declarations).
    // ------------------------------------------------------------------
    QDomElement root = doc.createElement("xisf");
    root.setAttribute("version",          "1.0");
    root.setAttribute("xmlns",            "http://www.pixinsight.com/xisf");
    root.setAttribute("xmlns:xsi",        "http://www.w3.org/2001/XMLSchema-instance");
    root.setAttribute("xsi:schemaLocation",
        "http://www.pixinsight.com/xisf "
        "http://pixinsight.com/xisf/xisf-1.0.xsd");
    doc.appendChild(root);

    // ------------------------------------------------------------------
    // Image element.
    // ------------------------------------------------------------------
    QDomElement img = doc.createElement("Image");
    img.setAttribute("geometry",     QString("%1:%2:%3").arg(w).arg(h).arg(c));
    img.setAttribute("sampleFormat", sampleFormatString(depth));
    img.setAttribute("bounds",       "0:1");
    img.setAttribute("colorSpace",   (c == 1) ? "Gray" : "RGB");
    img.setAttribute("pixelStorage", "planar");
    img.setAttribute("byteOrder",    "little");
    img.setAttribute("location",
        QString("attachment:%1:%2").arg(attachmentPosition).arg(dataSize));

    if (!compressionAttr.isEmpty())
        img.setAttribute("compression", compressionAttr);

    root.appendChild(img);

    // Add FITS keyword children to the Image element.
    addFITSKeywords(doc, img, buffer.metadata());

    // Embed ICC profile as base64 inline text inside an ICCProfile element.
    if (!buffer.metadata().iccData.isEmpty()) {
        QDomElement icc = doc.createElement("ICCProfile");
        icc.appendChild(
            doc.createTextNode(
                QString::fromLatin1(buffer.metadata().iccData.toBase64())));
        img.appendChild(icc);
    }

    // ------------------------------------------------------------------
    // File-level Metadata element with standard XISF properties.
    // ------------------------------------------------------------------
    QDomElement metadata = doc.createElement("Metadata");

    // Creation timestamp.
    QDomElement creationTime = doc.createElement("Property");
    creationTime.setAttribute("id",    "XISF:CreationTime");
    creationTime.setAttribute("type",  "TimePoint");
    creationTime.setAttribute("value",
        QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    metadata.appendChild(creationTime);

    // Creator application identifier.
    QDomElement creatorApp = doc.createElement("Property");
    creatorApp.setAttribute("id",    "XISF:CreatorApplication");
    creatorApp.setAttribute("type",  "String");
    creatorApp.setAttribute("value", options.creatorApp);
    metadata.appendChild(creatorApp);

    // Block alignment size.
    QDomElement blockAlign = doc.createElement("Property");
    blockAlign.setAttribute("id",    "XISF:BlockAlignmentSize");
    blockAlign.setAttribute("type",  "UInt32");
    blockAlign.setAttribute("value", QString::number(options.blockAlignment));
    metadata.appendChild(blockAlign);

    // Active compression codec (written only when compression was used).
    if (!compressionAttr.isEmpty()) {
        QDomElement compCodecs = doc.createElement("Property");
        compCodecs.setAttribute("id",    "XISF:CompressionCodecs");
        compCodecs.setAttribute("type",  "String");
        compCodecs.setAttribute("value", CompressionUtils::codecName(options.codec));
        metadata.appendChild(compCodecs);
    }

    root.appendChild(metadata);

    return doc.toByteArray();
}

// ============================================================================
// FITS keyword output
// ============================================================================

void XISFWriter::addFITSKeywords(QDomDocument&                doc,
                                  QDomElement&                 imageElem,
                                  const ImageBuffer::Metadata& meta)
{
    // Track which keywords have been written to avoid duplicates.
    QSet<QString> writtenKeys;

    // Convenience lambda: create and append a FITSKeyword element.
    auto addKW = [&](const QString& key,
                     const QString& value,
                     const QString& comment = QString())
    {
        QDomElement kw = doc.createElement("FITSKeyword");
        kw.setAttribute("name",  key);
        kw.setAttribute("value", value);
        if (!comment.isEmpty())
            kw.setAttribute("comment", comment);
        imageElem.appendChild(kw);
        writtenKeys.insert(key.toUpper());
    };

    // ------------------------------------------------------------------
    // WCS keywords: written only when a non-degenerate CD matrix and a
    // non-zero RA or Dec are present.
    // ------------------------------------------------------------------
    const double cdDet = meta.cd1_1 * meta.cd2_2 - meta.cd1_2 * meta.cd2_1;
    const bool hasValidWCS = (meta.ra != 0.0 || meta.dec != 0.0)
                             && std::abs(cdDet) > 1e-20;

    if (hasValidWCS) {
        const QString ctype1 = meta.ctype1.isEmpty() ? "RA---TAN"  : meta.ctype1;
        const QString ctype2 = meta.ctype2.isEmpty() ? "DEC--TAN" : meta.ctype2;

        addKW("CTYPE1",  QString("'%1'").arg(ctype1),              "Coordinate type");
        addKW("CTYPE2",  QString("'%1'").arg(ctype2),              "Coordinate type");
        addKW("EQUINOX", QString::number(meta.equinox, 'f', 1),    "Equinox of coordinates");
        addKW("CRVAL1",  QString::number(meta.ra,      'f', 9));
        addKW("CRVAL2",  QString::number(meta.dec,     'f', 9));
        addKW("CRPIX1",  QString::number(meta.crpix1,  'f', 3));
        addKW("CRPIX2",  QString::number(meta.crpix2,  'f', 3));
        addKW("CD1_1",   QString::number(meta.cd1_1,   'e', 10));
        addKW("CD1_2",   QString::number(meta.cd1_2,   'e', 10));
        addKW("CD2_1",   QString::number(meta.cd2_1,   'e', 10));
        addKW("CD2_2",   QString::number(meta.cd2_2,   'e', 10));

        // SIP distortion polynomial orders.
        if (meta.sipOrderA  > 0) addKW("A_ORDER",  QString::number(meta.sipOrderA));
        if (meta.sipOrderB  > 0) addKW("B_ORDER",  QString::number(meta.sipOrderB));
        if (meta.sipOrderAP > 0) addKW("AP_ORDER", QString::number(meta.sipOrderAP));
        if (meta.sipOrderBP > 0) addKW("BP_ORDER", QString::number(meta.sipOrderBP));

        // SIP distortion coefficients.
        for (auto it = meta.sipCoeffs.constBegin();
             it != meta.sipCoeffs.constEnd(); ++it)
        {
            addKW(it.key(), QString::number(it.value(), 'e', 15));
        }
    }

    // ------------------------------------------------------------------
    // Instrument and observation keywords.
    // ------------------------------------------------------------------
    if (meta.focalLength > 0.0 && !writtenKeys.contains("FOCALLEN"))
        addKW("FOCALLEN", QString::number(meta.focalLength, 'f', 2), "Focal length [mm]");

    if (meta.pixelSize > 0.0 && !writtenKeys.contains("XPIXSZ"))
        addKW("XPIXSZ", QString::number(meta.pixelSize, 'f', 3), "Pixel size [um]");

    if (!meta.objectName.isEmpty() && !writtenKeys.contains("OBJECT"))
        addKW("OBJECT", QString("'%1'").arg(meta.objectName), "Object name");

    if (!meta.dateObs.isEmpty() && !writtenKeys.contains("DATE-OBS"))
        addKW("DATE-OBS", QString("'%1'").arg(meta.dateObs), "Observation date");

    // ------------------------------------------------------------------
    // Remaining raw header cards not yet written.
    // Structural FITS keywords that have no meaning in XISF are skipped.
    // ------------------------------------------------------------------
    static const QSet<QString> structuralKeys = {
        "SIMPLE", "BITPIX", "NAXIS", "EXTEND", "BZERO", "BSCALE"
    };

    for (const auto& card : meta.rawHeaders) {
        const QString keyUpper = card.key.toUpper();

        if (writtenKeys.contains(keyUpper))
            continue;

        // Skip structural FITS keywords and NAXIS variants.
        if (structuralKeys.contains(keyUpper) || keyUpper.startsWith("NAXIS"))
            continue;

        QDomElement kw = doc.createElement("FITSKeyword");
        kw.setAttribute("name",  card.key);
        kw.setAttribute("value", card.value);
        if (!card.comment.isEmpty())
            kw.setAttribute("comment", card.comment);
        imageElem.appendChild(kw);
        writtenKeys.insert(keyUpper);
    }
}

// ============================================================================
// Utility helpers
// ============================================================================

quint64 XISFWriter::alignPosition(quint64 pos, int alignment)
{
    if (alignment <= 0)
        return pos;
    const quint64 rem = pos % static_cast<quint64>(alignment);
    if (rem == 0)
        return pos;
    return pos + (static_cast<quint64>(alignment) - rem);
}

// ----------------------------------------------------------------------------

QString XISFWriter::sampleFormatString(ImageBuffer::BitDepth depth)
{
    switch (depth) {
    case ImageBuffer::Depth_16Int:   return "UInt16";
    case ImageBuffer::Depth_32Int:   return "UInt32";
    case ImageBuffer::Depth_32Float: // fall through
    default:                         return "Float32";
    }
}

// ----------------------------------------------------------------------------

int XISFWriter::bytesPerSample(ImageBuffer::BitDepth depth)
{
    switch (depth) {
    case ImageBuffer::Depth_16Int:   return 2;
    case ImageBuffer::Depth_32Int:   return 4;
    case ImageBuffer::Depth_32Float: // fall through
    default:                         return 4;
    }
}