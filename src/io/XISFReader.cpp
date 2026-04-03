#include "XISFReader.h"
#include "CompressionUtils.h"
#include "FitsHeaderUtils.h"
#include "IccProfileExtractor.h"
#include "core/ColorProfileManager.h"

#include <QFile>
#include <QByteArray>
#include <QXmlStreamReader>
#include <QRegularExpression>
#include <QDateTime>
#include <QDebug>
#include <QtEndian>
#include <QCoreApplication>

#include <cmath>
#include <vector>

// ============================================================================
// Public API
// ============================================================================

bool XISFReader::read(const QString& filePath, ImageBuffer& buffer, QString* errorMsg)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMsg)
            *errorMsg = QCoreApplication::translate(
                "XISFReader", "Could not open file: %1").arg(filePath);
        return false;
    }

    // ------------------------------------------------------------------
    // Step 1: Verify the 8-byte XISF 1.0 signature.
    // ------------------------------------------------------------------
    QByteArray sig = file.read(8);
    if (sig != "XISF0100") {
        if (errorMsg)
            *errorMsg = QCoreApplication::translate(
                "XISFReader", "Invalid XISF signature. File is not XISF 1.0.");
        return false;
    }

    // ------------------------------------------------------------------
    // Step 2: Read the 4-byte little-endian XML header length.
    // ------------------------------------------------------------------
    uchar lenBytes[4];
    if (file.read(reinterpret_cast<char*>(lenBytes), 4) != 4) {
        if (errorMsg)
            *errorMsg = QCoreApplication::translate(
                "XISFReader", "Unexpected EOF while reading header length.");
        return false;
    }
    const quint32 headerLen = qFromLittleEndian<quint32>(lenBytes);

    // ------------------------------------------------------------------
    // Step 3: Skip the 4 reserved bytes following the header length.
    // ------------------------------------------------------------------
    file.read(4);

    // ------------------------------------------------------------------
    // Step 4: Read and validate the XML header block.
    // ------------------------------------------------------------------
    if (headerLen == 0 || headerLen > 100u * 1024u * 1024u) {
        if (errorMsg)
            *errorMsg = QCoreApplication::translate(
                "XISFReader", "Invalid header length: %1").arg(headerLen);
        return false;
    }

    const QByteArray xmlBytes = file.read(headerLen);
    if (xmlBytes.size() != static_cast<int>(headerLen)) {
        if (errorMsg)
            *errorMsg = QCoreApplication::translate(
                "XISFReader", "Incomplete XML header read.");
        return false;
    }

    // ------------------------------------------------------------------
    // Step 5: Parse the XML header to extract image geometry, location,
    //         compression, and metadata.
    // ------------------------------------------------------------------
    XISFHeaderInfo info;
    if (!parseHeader(xmlBytes, info, errorMsg))
        return false;

    if (info.width <= 0 || info.height <= 0) {
        if (errorMsg)
            *errorMsg = QCoreApplication::translate(
                "XISFReader", "No valid image found in XISF header.");
        return false;
    }

    info.meta.filePath = filePath;

    // ------------------------------------------------------------------
    // Step 6: Load pixel data and populate the output buffer.
    // ------------------------------------------------------------------
    return loadImage(file, info, buffer, errorMsg);
}

// ----------------------------------------------------------------------------

QList<XISFImageInfo> XISFReader::listImages(const QString& filePath, QString* errorMsg)
{
    QList<XISFImageInfo> result;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMsg)
            *errorMsg = QCoreApplication::translate(
                "XISFReader", "Could not open file: %1").arg(filePath);
        return result;
    }

    // Verify signature.
    if (file.read(8) != "XISF0100") {
        if (errorMsg)
            *errorMsg = QCoreApplication::translate(
                "XISFReader", "Invalid XISF signature.");
        return result;
    }

    // Read header length.
    uchar lenBytes[4];
    if (file.read(reinterpret_cast<char*>(lenBytes), 4) != 4) {
        if (errorMsg)
            *errorMsg = QCoreApplication::translate(
                "XISFReader", "Unexpected EOF.");
        return result;
    }
    const quint32 headerLen = qFromLittleEndian<quint32>(lenBytes);
    file.read(4); // reserved

    if (headerLen == 0 || headerLen > 100u * 1024u * 1024u) {
        if (errorMsg)
            *errorMsg = QCoreApplication::translate(
                "XISFReader", "Invalid header length: %1").arg(headerLen);
        return result;
    }

    const QByteArray xmlBytes = file.read(headerLen);

    // Parse all Image elements from the header.
    QList<XISFHeaderInfo> images;
    if (!parseAllImages(xmlBytes, images, errorMsg))
        return result;

    // Convert internal descriptors to the public XISFImageInfo structure.
    for (int i = 0; i < images.size(); ++i) {
        const XISFHeaderInfo& info = images[i];
        XISFImageInfo imgInfo;
        imgInfo.index        = i;
        imgInfo.name         = info.imageId.isEmpty() ? QString::number(i) : info.imageId;
        imgInfo.width        = info.width;
        imgInfo.height       = info.height;
        imgInfo.channels     = info.channels;
        imgInfo.sampleFormat = info.sampleFormat;
        imgInfo.colorSpace   = info.colorSpace;
        result.append(imgInfo);
    }

    return result;
}

// ----------------------------------------------------------------------------

bool XISFReader::readImage(const QString& filePath,
                           int            imageIndex,
                           ImageBuffer&   buffer,
                           QString*       errorMsg)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMsg)
            *errorMsg = QCoreApplication::translate(
                "XISFReader", "Could not open file: %1").arg(filePath);
        return false;
    }

    // Verify signature.
    if (file.read(8) != "XISF0100") {
        if (errorMsg)
            *errorMsg = QCoreApplication::translate(
                "XISFReader", "Invalid XISF signature.");
        return false;
    }

    // Read header length.
    uchar lenBytes[4];
    if (file.read(reinterpret_cast<char*>(lenBytes), 4) != 4) {
        if (errorMsg)
            *errorMsg = QCoreApplication::translate(
                "XISFReader", "Unexpected EOF.");
        return false;
    }
    const quint32 headerLen = qFromLittleEndian<quint32>(lenBytes);
    file.read(4); // reserved

    const QByteArray xmlBytes = file.read(headerLen);

    // Parse all images and select the requested index.
    QList<XISFHeaderInfo> images;
    if (!parseAllImages(xmlBytes, images, errorMsg))
        return false;

    if (imageIndex < 0 || imageIndex >= images.size()) {
        if (errorMsg)
            *errorMsg = QCoreApplication::translate(
                "XISFReader",
                "Image index %1 out of range (file has %2 images).")
                .arg(imageIndex).arg(images.size());
        return false;
    }

    XISFHeaderInfo& info = images[imageIndex];
    info.meta.filePath   = filePath;

    return loadImage(file, info, buffer, errorMsg);
}

// ============================================================================
// Header parsing
// ============================================================================

bool XISFReader::parseHeader(const QByteArray& headerXml,
                             XISFHeaderInfo&   info,
                             QString*          errorMsg)
{
    QXmlStreamReader xml(headerXml);
    bool foundImage = false;

    while (!xml.atEnd()) {
        xml.readNext();

        if (!xml.isStartElement())
            continue;

        const QString elemName = xml.name().toString();

        // ------------------------------------------------------------------
        // Image element: contains geometry, location, format, and all child
        // metadata elements (FITSKeyword, Property, ICCProfile, etc.).
        // ------------------------------------------------------------------
        if (elemName == "Image" && !foundImage) {
            foundImage = true;

            // Parse geometry attribute: "width:height:channels"
            const QString geom = xml.attributes().value("geometry").toString();
            const QStringList parts = geom.split(':');
            if (parts.size() >= 2) {
                info.width    = parts[0].toInt();
                info.height   = parts[1].toInt();
                info.channels = (parts.size() > 2) ? parts[2].toInt() : 1;
            }

            // Sample format, colour space, and pixel layout.
            info.sampleFormat = xml.attributes().value("sampleFormat").toString();
            if (info.sampleFormat.isEmpty())
                info.sampleFormat = "Float32";

            info.colorSpace   = xml.attributes().value("colorSpace").toString();
            info.pixelStorage = xml.attributes().value("pixelStorage").toString();
            info.byteOrder    = xml.attributes().value("byteOrder").toString();

            // Reject big-endian files immediately.
            if (!info.byteOrder.isEmpty()
                && info.byteOrder.toLower() == "big")
            {
                if (errorMsg)
                    *errorMsg = QCoreApplication::translate(
                        "XISFReader", "Big-endian XISF files are not supported.");
                return false;
            }

            // Parse the location attribute: "type:param1:param2"
            const QString      loc      = xml.attributes().value("location").toString();
            const QStringList  locParts = loc.split(':');
            if (!locParts.isEmpty()) {
                const QString locType = locParts[0].toLower();
                if (locType == "attachment" && locParts.size() >= 3) {
                    info.locationType  = XISFHeaderInfo::Attachment;
                    info.dataLocation  = locParts[1].toLongLong();
                    info.dataSize      = locParts[2].toLongLong();
                } else if (locType == "inline" && locParts.size() >= 2) {
                    info.locationType  = XISFHeaderInfo::Inline;
                    info.inlineEncoding = locParts[1];
                } else if (locType == "embedded") {
                    info.locationType  = XISFHeaderInfo::Embedded;
                }
            }

            // Parse optional compression attribute.
            const QString compression = xml.attributes().value("compression").toString();
            if (!compression.isEmpty()) {
                if (!CompressionUtils::parseCompressionAttr(
                        compression,
                        info.compressionCodec,
                        info.uncompressedSize,
                        info.shuffleItemSize))
                {
                    qWarning() << "XISFReader: failed to parse compression attribute:"
                               << compression;
                }
            }

            // ----------------------------------------------------------
            // Consume child elements until the closing </Image> tag.
            // ----------------------------------------------------------
            while (!xml.atEnd()) {
                xml.readNext();

                // Collect text content for Embedded / Inline location types.
                if (xml.isCharacters() && !xml.isWhitespace()) {
                    if (info.locationType == XISFHeaderInfo::Embedded
                        || info.locationType == XISFHeaderInfo::Inline)
                    {
                        info.embeddedData += xml.text().toString();
                    }
                }

                if (xml.isEndElement() && xml.name() == "Image")
                    break;

                if (!xml.isStartElement())
                    continue;

                const QString childName = xml.name().toString();

                // ----------------------------------------------------------
                // FITSKeyword child: map standard keyword names to metadata
                // fields and also push every card into rawHeaders.
                // ----------------------------------------------------------
                if (childName == "FITSKeyword") {
                    const QString name    = xml.attributes().value("name").toString();
                    const QString val     = xml.attributes().value("value").toString();
                    const QString comment = xml.attributes().value("comment").toString();

                    info.meta.rawHeaders.push_back({name, val, comment});

                    // Instrument / observation metadata.
                    if (name == "FOCALLEN") {
                        info.meta.focalLength = val.toDouble();
                    } else if (name == "XPIXSZ" || name == "PIXSIZE") {
                        info.meta.pixelSize = val.toDouble();
                    } else if (name == "DATE-OBS") {
                        info.meta.dateObs = val;
                    } else if (name == "OBJECT") {
                        info.meta.objectName = val;
                    }

                    // Positional coordinates (decimal degrees or HMS/DMS).
                    else if (name == "RA" || name == "OBJCTRA" || name == "CRVAL1") {
                        bool ok = false;
                        const double ra = FitsHeaderUtils::parseRA(val, &ok);
                        if (ok) info.meta.ra = ra;
                    } else if (name == "DEC" || name == "OBJCTDEC" || name == "CRVAL2") {
                        bool ok = false;
                        const double dec = FitsHeaderUtils::parseDec(val, &ok);
                        if (ok) info.meta.dec = dec;
                    }

                    // WCS reference pixel.
                    else if (name == "CRPIX1") { info.meta.crpix1 = val.toDouble(); }
                    else if (name == "CRPIX2") { info.meta.crpix2 = val.toDouble(); }

                    // WCS CD matrix.
                    else if (name == "CD1_1")  { info.meta.cd1_1  = val.toDouble(); }
                    else if (name == "CD1_2")  { info.meta.cd1_2  = val.toDouble(); }
                    else if (name == "CD2_1")  { info.meta.cd2_1  = val.toDouble(); }
                    else if (name == "CD2_2")  { info.meta.cd2_2  = val.toDouble(); }

                    // Coordinate type strings.
                    else if (name == "CTYPE1") { info.meta.ctype1  = val; }
                    else if (name == "CTYPE2") { info.meta.ctype2  = val; }
                    else if (name == "EQUINOX") { info.meta.equinox = val.toDouble(); }

                    // SIP distortion polynomial orders.
                    else if (name == "A_ORDER")  { info.meta.sipOrderA  = val.toInt(); }
                    else if (name == "B_ORDER")  { info.meta.sipOrderB  = val.toInt(); }
                    else if (name == "AP_ORDER") { info.meta.sipOrderAP = val.toInt(); }
                    else if (name == "BP_ORDER") { info.meta.sipOrderBP = val.toInt(); }

                    // SIP distortion coefficients (A_i_j, B_i_j, AP_i_j, BP_i_j).
                    else if (name.startsWith("A_")  || name.startsWith("B_")  ||
                             name.startsWith("AP_") || name.startsWith("BP_"))
                    {
                        info.meta.sipCoeffs[name] = val.toDouble();
                    }
                }

                // ----------------------------------------------------------
                // Property child: typed XISF metadata properties.
                // ----------------------------------------------------------
                else if (childName == "Property") {
                    XISFProperty prop = parseProperty(xml);
                    if (prop.id.isEmpty())
                        continue;

                    info.properties[prop.id] = prop;

                    // Map well-known PixInsight astrometric solution properties
                    // to the ImageBuffer::Metadata WCS fields.
                    if (prop.id == "PCL:AstrometricSolution:ReferenceImageCoordinates") {
                        const QVariantList coords = prop.value.toList();
                        if (coords.size() >= 2) {
                            info.meta.crpix1 = coords[0].toDouble();
                            info.meta.crpix2 = coords[1].toDouble();
                        }
                    } else if (prop.id == "PCL:AstrometricSolution:ReferenceCelestialCoordinates") {
                        const QVariantList coords = prop.value.toList();
                        if (coords.size() >= 2) {
                            info.meta.ra  = coords[0].toDouble();
                            info.meta.dec = coords[1].toDouble();
                        }
                    } else if (prop.id == "PCL:AstrometricSolution:LinearTransformationMatrix") {
                        const QVariantList matrix = prop.value.toList();
                        if (matrix.size() >= 2) {
                            const QVariantList row0 = matrix[0].toList();
                            const QVariantList row1 = matrix[1].toList();
                            if (row0.size() >= 2 && row1.size() >= 2) {
                                info.meta.cd1_1 = row0[0].toDouble();
                                info.meta.cd1_2 = row0[1].toDouble();
                                info.meta.cd2_1 = row1[0].toDouble();
                                info.meta.cd2_2 = row1[1].toDouble();
                            }
                        }
                    } else if (prop.id == "Observation:Center:RA") {
                        if (info.meta.ra == 0.0)
                            info.meta.ra = prop.value.toDouble();
                    } else if (prop.id == "Observation:Center:Dec") {
                        if (info.meta.dec == 0.0)
                            info.meta.dec = prop.value.toDouble();
                    }
                }

                // ----------------------------------------------------------
                // Resolution child: horizontal/vertical resolution with unit.
                // ----------------------------------------------------------
                else if (childName == "Resolution") {
                    info.resolutionH = xml.attributes().value("horizontal").toDouble();
                    info.resolutionV = xml.attributes().value("vertical").toDouble();
                    const QString unit = xml.attributes().value("unit").toString();
                    if (!unit.isEmpty())
                        info.resolutionUnit = unit;
                }

                // ----------------------------------------------------------
                // ICCProfile child: location and optional compression for the
                // embedded colour profile data.
                // ----------------------------------------------------------
                else if (childName == "ICCProfile") {
                    info.hasICCProfile = true;

                    const QString     iccLoc      = xml.attributes().value("location").toString();
                    const QStringList iccLocParts = iccLoc.split(':');

                    if (!iccLocParts.isEmpty()) {
                        const QString iccLocType = iccLocParts[0].toLower();
                        if (iccLocType == "attachment" && iccLocParts.size() >= 3) {
                            info.iccLocationType = XISFHeaderInfo::Attachment;
                            info.iccLocation     = iccLocParts[1].toLongLong();
                            info.iccSize         = iccLocParts[2].toLongLong();
                        } else if (iccLocType == "inline" && iccLocParts.size() >= 2) {
                            info.iccLocationType  = XISFHeaderInfo::Inline;
                            info.iccInlineEncoding = iccLocParts[1];
                        } else if (iccLocType == "embedded") {
                            info.iccLocationType = XISFHeaderInfo::Embedded;
                        }
                    }

                    const QString iccCompStr = xml.attributes().value("compression").toString();
                    if (!iccCompStr.isEmpty()) {
                        CompressionUtils::parseCompressionAttr(
                            iccCompStr,
                            info.iccCompression,
                            info.iccUncompressedSize,
                            info.iccShuffleItemSize);
                    }

                    if (info.iccLocationType == XISFHeaderInfo::Embedded
                        || info.iccLocationType == XISFHeaderInfo::Inline)
                    {
                        info.iccEmbeddedData = xml.readElementText();
                    }
                }

                // ----------------------------------------------------------
                // Thumbnail child: note presence only; pixel data not loaded.
                // ----------------------------------------------------------
                else if (childName == "Thumbnail") {
                    info.hasThumbnail = true;
                }
            }
        }

        // ------------------------------------------------------------------
        // Metadata element: file-level properties (not image-specific).
        // ------------------------------------------------------------------
        else if (elemName == "Metadata") {
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isEndElement() && xml.name() == "Metadata")
                    break;
                if (xml.isStartElement() && xml.name() == "Property") {
                    XISFProperty prop = parseProperty(xml);
                    if (!prop.id.isEmpty())
                        info.properties[prop.id] = prop;
                }
            }
        }
    }

    if (xml.hasError()) {
        if (errorMsg)
            *errorMsg = QCoreApplication::translate(
                "XISFReader", "XML parse error: %1").arg(xml.errorString());
        return false;
    }

    return foundImage;
}

// ----------------------------------------------------------------------------

bool XISFReader::parseAllImages(const QByteArray&      headerXml,
                                QList<XISFHeaderInfo>& images,
                                QString*               errorMsg)
{
    QXmlStreamReader xml(headerXml);
    int imageIndex = 0;

    while (!xml.atEnd()) {
        xml.readNext();

        if (!xml.isStartElement() || xml.name() != "Image")
            continue;

        XISFHeaderInfo info;

        // Geometry: "width:height:channels"
        const QString     geom  = xml.attributes().value("geometry").toString();
        const QStringList parts = geom.split(':');
        if (parts.size() >= 2) {
            info.width    = parts[0].toInt();
            info.height   = parts[1].toInt();
            info.channels = (parts.size() > 2) ? parts[2].toInt() : 1;
        }

        if (info.width <= 0 || info.height <= 0)
            continue;

        info.imageId = xml.attributes().value("id").toString();
        if (info.imageId.isEmpty())
            info.imageId = QString("Image_%1").arg(imageIndex);

        info.sampleFormat = xml.attributes().value("sampleFormat").toString();
        if (info.sampleFormat.isEmpty())
            info.sampleFormat = "Float32";

        info.colorSpace   = xml.attributes().value("colorSpace").toString();
        info.pixelStorage = xml.attributes().value("pixelStorage").toString();
        info.byteOrder    = xml.attributes().value("byteOrder").toString();

        // Location attribute.
        const QString     loc      = xml.attributes().value("location").toString();
        const QStringList locParts = loc.split(':');
        if (!locParts.isEmpty()) {
            const QString locType = locParts[0].toLower();
            if (locType == "attachment" && locParts.size() >= 3) {
                info.locationType = XISFHeaderInfo::Attachment;
                info.dataLocation = locParts[1].toLongLong();
                info.dataSize     = locParts[2].toLongLong();
            } else if (locType == "inline" && locParts.size() >= 2) {
                info.locationType   = XISFHeaderInfo::Inline;
                info.inlineEncoding = locParts[1];
            } else if (locType == "embedded") {
                info.locationType = XISFHeaderInfo::Embedded;
            }
        }

        // Compression attribute.
        const QString compression = xml.attributes().value("compression").toString();
        if (!compression.isEmpty()) {
            CompressionUtils::parseCompressionAttr(
                compression,
                info.compressionCodec,
                info.uncompressedSize,
                info.shuffleItemSize);
        }

        // Consume child elements.
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isEndElement() && xml.name() == "Image")
                break;
            if (!xml.isStartElement())
                continue;

            const QString childName = xml.name().toString();

            if (childName == "FITSKeyword") {
                const QString name    = xml.attributes().value("name").toString();
                const QString val     = xml.attributes().value("value").toString();
                const QString comment = xml.attributes().value("comment").toString();

                info.meta.rawHeaders.push_back({name, val, comment});

                if (name == "FOCALLEN") {
                    info.meta.focalLength = val.toDouble();
                } else if (name == "XPIXSZ" || name == "PIXSIZE") {
                    info.meta.pixelSize = val.toDouble();
                } else if (name == "DATE-OBS") {
                    info.meta.dateObs = val;
                } else if (name == "OBJECT") {
                    info.meta.objectName = val;
                } else if (name == "RA" || name == "OBJCTRA" || name == "CRVAL1") {
                    bool ok = false;
                    const double ra = FitsHeaderUtils::parseRA(val, &ok);
                    if (ok) info.meta.ra = ra;
                } else if (name == "DEC" || name == "OBJCTDEC" || name == "CRVAL2") {
                    bool ok = false;
                    const double dec = FitsHeaderUtils::parseDec(val, &ok);
                    if (ok) info.meta.dec = dec;
                } else if (name == "CRPIX1") { info.meta.crpix1  = val.toDouble(); }
                else if (name == "CRPIX2")   { info.meta.crpix2  = val.toDouble(); }
                else if (name == "CD1_1")    { info.meta.cd1_1   = val.toDouble(); }
                else if (name == "CD1_2")    { info.meta.cd1_2   = val.toDouble(); }
                else if (name == "CD2_1")    { info.meta.cd2_1   = val.toDouble(); }
                else if (name == "CD2_2")    { info.meta.cd2_2   = val.toDouble(); }
            } else if (childName == "ICCProfile") {
                info.hasICCProfile = true;

                const QString     iccLoc      = xml.attributes().value("location").toString();
                const QStringList iccLocParts = iccLoc.split(':');
                if (!iccLocParts.isEmpty()) {
                    const QString iccLocType = iccLocParts[0].toLower();
                    if (iccLocType == "attachment" && iccLocParts.size() >= 3) {
                        info.iccLocationType = XISFHeaderInfo::Attachment;
                        info.iccLocation     = iccLocParts[1].toLongLong();
                        info.iccSize         = iccLocParts[2].toLongLong();
                    } else if (iccLocType == "inline" && iccLocParts.size() >= 2) {
                        info.iccLocationType   = XISFHeaderInfo::Inline;
                        info.iccInlineEncoding = iccLocParts[1];
                    } else if (iccLocType == "embedded") {
                        info.iccLocationType = XISFHeaderInfo::Embedded;
                    }
                }

                const QString iccCompStr = xml.attributes().value("compression").toString();
                if (!iccCompStr.isEmpty()) {
                    CompressionUtils::parseCompressionAttr(
                        iccCompStr,
                        info.iccCompression,
                        info.iccUncompressedSize,
                        info.iccShuffleItemSize);
                }

                if (info.iccLocationType == XISFHeaderInfo::Embedded
                    || info.iccLocationType == XISFHeaderInfo::Inline)
                {
                    info.iccEmbeddedData = xml.readElementText();
                }
            }
        }

        images.append(info);
        ++imageIndex;
    }

    if (xml.hasError()) {
        if (errorMsg)
            *errorMsg = QCoreApplication::translate(
                "XISFReader", "XML parse error: %1").arg(xml.errorString());
        return false;
    }

    return true;
}

// ============================================================================
// Data block I/O
// ============================================================================

QByteArray XISFReader::readDataBlock(QFile&                file,
                                     const XISFHeaderInfo& info,
                                     QString*              errorMsg)
{
    switch (info.locationType) {
    case XISFHeaderInfo::Attachment:
        return readAttachedDataBlock(file, info.dataLocation, info.dataSize, errorMsg);

    case XISFHeaderInfo::Inline:
        return decodeInlineData(info.embeddedData, info.inlineEncoding, errorMsg);

    case XISFHeaderInfo::Embedded:
        // Per XISF 1.0 spec, embedded data is implicitly base64-encoded.
        return decodeInlineData(info.embeddedData, "base64", errorMsg);

    default:
        if (errorMsg)
            *errorMsg = QCoreApplication::translate(
                "XISFReader", "Unknown data location type.");
        return QByteArray();
    }
}

// ----------------------------------------------------------------------------

QByteArray XISFReader::readAttachedDataBlock(QFile&    file,
                                              long long position,
                                              long long size,
                                              QString*  errorMsg)
{
    if (!file.seek(position)) {
        if (errorMsg)
            *errorMsg = QCoreApplication::translate(
                "XISFReader", "Seek error to data block at offset %1.").arg(position);
        return QByteArray();
    }

    const QByteArray data = file.read(size);
    if (data.size() != static_cast<int>(size)) {
        if (errorMsg)
            *errorMsg = QCoreApplication::translate(
                "XISFReader",
                "Incomplete data read: expected %1 bytes, got %2.")
                .arg(size).arg(data.size());
        return QByteArray();
    }

    return data;
}

// ----------------------------------------------------------------------------

QByteArray XISFReader::decodeInlineData(const QString& data,
                                         const QString& encoding,
                                         QString*       errorMsg)
{
    const QString enc = encoding.toLower();

    if (enc == "base64")
        return QByteArray::fromBase64(data.toLatin1());

    if (enc == "hex")
        return QByteArray::fromHex(data.toLatin1());

    if (errorMsg)
        *errorMsg = QCoreApplication::translate(
            "XISFReader", "Unknown inline data encoding: %1").arg(encoding);
    return QByteArray();
}

// ----------------------------------------------------------------------------

QByteArray XISFReader::decodeEmbeddedData(const QString& embeddedXml,
                                           QString*       errorMsg)
{
    // Parse the Data child element that carries encoding and text content.
    QXmlStreamReader xml(embeddedXml);
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == "Data") {
            const QString encoding = xml.attributes().value("encoding").toString();
            const QString text     = xml.readElementText();
            return decodeInlineData(text, encoding, errorMsg);
        }
    }

    if (errorMsg)
        *errorMsg = QCoreApplication::translate(
            "XISFReader", "Could not locate Data element in embedded block.");
    return QByteArray();
}

// ============================================================================
// Pixel data conversion
// ============================================================================

bool XISFReader::convertToFloat(const QByteArray&     rawData,
                                const XISFHeaderInfo& info,
                                std::vector<float>&   outData,
                                QString*              errorMsg)
{
    const long long totalSamples = static_cast<long long>(info.width)
                                   * info.height
                                   * info.channels;
    const int sampleSize = getSampleSize(info.sampleFormat);

    if (rawData.size() < static_cast<int>(totalSamples * sampleSize)) {
        if (errorMsg)
            *errorMsg = QCoreApplication::translate(
                "XISFReader",
                "Data block too small: expected %1 bytes, got %2.")
                .arg(totalSamples * sampleSize).arg(rawData.size());
        return false;
    }

    outData.resize(static_cast<size_t>(totalSamples));

    if (info.sampleFormat == "Float32" || info.sampleFormat == "Float") {
        const float* src = reinterpret_cast<const float*>(rawData.constData());
        for (long long i = 0; i < totalSamples; ++i)
            outData[static_cast<size_t>(i)] = src[i];
    }
    else if (info.sampleFormat == "Float64" || info.sampleFormat == "Double") {
        const double* src = reinterpret_cast<const double*>(rawData.constData());
        for (long long i = 0; i < totalSamples; ++i)
            outData[static_cast<size_t>(i)] = static_cast<float>(src[i]);
    }
    else if (info.sampleFormat == "UInt16") {
        const quint16* src = reinterpret_cast<const quint16*>(rawData.constData());
        for (long long i = 0; i < totalSamples; ++i)
            outData[static_cast<size_t>(i)] = static_cast<float>(src[i]) / 65535.0f;
    }
    else if (info.sampleFormat == "UInt32") {
        const quint32* src = reinterpret_cast<const quint32*>(rawData.constData());
        for (long long i = 0; i < totalSamples; ++i)
            outData[static_cast<size_t>(i)] = static_cast<float>(src[i]) / 4294967295.0f;
    }
    else if (info.sampleFormat == "UInt8") {
        const quint8* src = reinterpret_cast<const quint8*>(rawData.constData());
        for (long long i = 0; i < totalSamples; ++i)
            outData[static_cast<size_t>(i)] = static_cast<float>(src[i]) / 255.0f;
    }
    else {
        if (errorMsg)
            *errorMsg = QCoreApplication::translate(
                "XISFReader", "Unsupported sample format: %1").arg(info.sampleFormat);
        return false;
    }

    return true;
}

// ----------------------------------------------------------------------------

void XISFReader::planarToInterleaved(const std::vector<float>& planar,
                                     std::vector<float>&       interleaved,
                                     int width, int height, int channels)
{
    const long planeSize = static_cast<long>(width) * height;

    for (int c = 0; c < channels; ++c) {
        for (long i = 0; i < planeSize; ++i) {
            interleaved[static_cast<size_t>(i * channels + c)] =
                planar[static_cast<size_t>(c * planeSize + i)];
        }
    }
}

// ============================================================================
// Property element parsing
// ============================================================================

XISFReader::XISFProperty XISFReader::parseProperty(QXmlStreamReader& xml)
{
    XISFProperty prop;
    prop.id      = xml.attributes().value("id").toString();
    prop.type    = xml.attributes().value("type").toString();
    prop.format  = xml.attributes().value("format").toString();
    prop.comment = xml.attributes().value("comment").toString();

    const QString valueStr = xml.attributes().value("value").toString();
    const int     length   = xml.attributes().value("length").toInt();
    const int     rows     = xml.attributes().value("rows").toInt();
    const int     columns  = xml.attributes().value("columns").toInt();

    // readElementText() advances past the closing tag and returns any
    // text content, which is used for vector/matrix and inline data types.
    const QString textContent = xml.readElementText();

    prop.value = parsePropertyValue(prop.type, valueStr, textContent,
                                    length, rows, columns);
    return prop;
}

// ----------------------------------------------------------------------------

QVariant XISFReader::parsePropertyValue(const QString& type,
                                         const QString& valueStr,
                                         const QString& textContent,
                                         int            length,
                                         int            rows,
                                         int            columns)
{
    Q_UNUSED(length)
    Q_UNUSED(rows)
    Q_UNUSED(columns)

    // --- Scalar types ---

    if (type == "Boolean")
        return valueStr.toLower() == "true";

    if (type == "Int8"  || type == "Byte"  ||
        type == "Int16" || type == "Short" ||
        type == "Int32" || type == "Int")
        return valueStr.toInt();

    if (type == "Int64")
        return valueStr.toLongLong();

    if (type == "UInt8" || type == "UInt16")
        return valueStr.toUInt();

    if (type == "UInt32" || type == "UInt")
        return valueStr.toUInt();

    if (type == "UInt64")
        return valueStr.toULongLong();

    if (type == "Float32" || type == "Float")
        return valueStr.toFloat();

    if (type == "Float64" || type == "Double")
        return valueStr.toDouble();

    if (type == "String")
        return valueStr.isEmpty() ? textContent : valueStr;

    if (type == "TimePoint") {
        const QDateTime dt = QDateTime::fromString(valueStr, Qt::ISODate);
        return dt.isValid() ? QVariant(dt) : QVariant(valueStr);
    }

    // --- Aggregate types: Vector<T> and Matrix<T> ---

    const bool isVector = type.startsWith("Vector");
    const bool isMatrix = type.startsWith("Matrix");

    if (isVector || isMatrix) {
        QString data = textContent.isEmpty() ? valueStr : textContent;

        // Remove brace delimiters used in XISF vector/matrix literals.
        data.replace('{', ' ').replace('}', ' ');

        const QStringList tokens =
            data.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);

        QVariantList list;
        list.reserve(tokens.size());

        for (const QString& token : tokens) {
            if (type.contains("Int"))
                list << token.toInt();
            else if (type.contains("Float") || type.contains("Double"))
                list << token.toDouble();
            else
                list << token;
        }
        return list;
    }

    // Fallback: return as plain string.
    return valueStr;
}

// ============================================================================
// Utility helpers
// ============================================================================

int XISFReader::getSampleSize(const QString& format)
{
    if (format == "Float32" || format == "Float")                                  return 4;
    if (format == "Float64" || format == "Double")                                 return 8;
    if (format == "UInt16"  || format == "Int16" ||
        format == "Short"   || format == "UShort")                                 return 2;
    if (format == "UInt32"  || format == "Int32" ||
        format == "Int"     || format == "UInt")                                   return 4;
    if (format == "UInt8"   || format == "Int8" || format == "Byte")               return 1;

    return 4; // Default: Float32
}

// ----------------------------------------------------------------------------

bool XISFReader::loadImage(QFile&          file,
                           XISFHeaderInfo& info,
                           ImageBuffer&    buffer,
                           QString*        errorMsg)
{
    // ------------------------------------------------------------------
    // Read the raw data block (attached, inline, or embedded).
    // ------------------------------------------------------------------
    QByteArray rawData = readDataBlock(file, info, errorMsg);
    if (rawData.isEmpty() && info.dataSize > 0)
        return false;

    // ------------------------------------------------------------------
    // Decompress if a compression codec was specified.
    // ------------------------------------------------------------------
    if (info.compressionCodec != CompressionUtils::Codec_None) {
        QByteArray decompressed = CompressionUtils::decompress(
            rawData, info.compressionCodec, info.uncompressedSize, errorMsg);

        if (decompressed.isEmpty())
            return false;

        // Reverse byte shuffling applied before compression (if any).
        if (info.shuffleItemSize > 0)
            rawData = CompressionUtils::unshuffle(decompressed, info.shuffleItemSize);
        else
            rawData = decompressed;
    }

    // ------------------------------------------------------------------
    // Convert raw bytes to normalised float32 samples.
    // ------------------------------------------------------------------
    std::vector<float> pixelData;
    if (!convertToFloat(rawData, info, pixelData, errorMsg))
        return false;

    // ------------------------------------------------------------------
    // Convert from XISF planar layout to ImageBuffer interleaved layout.
    // XISF default is planar; "normal" means interleaved.
    // ------------------------------------------------------------------
    const long long totalSamples = static_cast<long long>(info.width)
                                   * info.height
                                   * info.channels;

    std::vector<float> finalData;

    if (info.channels > 1
        && (info.pixelStorage.isEmpty()
            || info.pixelStorage.toLower() == "planar"))
    {
        finalData.resize(static_cast<size_t>(totalSamples));
        planarToInterleaved(pixelData, finalData, info.width, info.height, info.channels);
    } else {
        finalData = std::move(pixelData);
    }

    // ------------------------------------------------------------------
    // Resolve and load the embedded ICC colour profile, if present.
    // ------------------------------------------------------------------
    if (info.hasICCProfile) {
        QByteArray iccData;

        if (info.iccLocationType == XISFHeaderInfo::Attachment) {
            iccData = readAttachedDataBlock(
                file, info.iccLocation, info.iccSize, errorMsg);
        } else if (info.iccLocationType == XISFHeaderInfo::Inline) {
            iccData = decodeInlineData(
                info.iccEmbeddedData, info.iccInlineEncoding, errorMsg);
        } else if (info.iccLocationType == XISFHeaderInfo::Embedded) {
            iccData = decodeInlineData(info.iccEmbeddedData, "base64", errorMsg);
        }

        if (!iccData.isEmpty()) {
            // Decompress the ICC profile if it was stored compressed.
            if (info.iccCompression != CompressionUtils::Codec_None) {
                QByteArray decompressed = CompressionUtils::decompress(
                    iccData, info.iccCompression, info.iccUncompressedSize, errorMsg);

                if (!decompressed.isEmpty()) {
                    iccData = (info.iccShuffleItemSize > 0)
                        ? CompressionUtils::unshuffle(decompressed, info.iccShuffleItemSize)
                        : decompressed;
                } else {
                    iccData.clear();
                }
            }

            if (!iccData.isEmpty())
                info.meta.iccData = iccData;
        }
    } else if (!info.meta.filePath.isEmpty()) {
        // Fallback: attempt to extract an ICC profile via the generic extractor.
        IccProfileExtractor::extractFromFile(info.meta.filePath, info.meta.iccData);
    }

    // ------------------------------------------------------------------
    // Populate the output buffer with pixel data and metadata.
    // ------------------------------------------------------------------
    buffer.setMetadata(info.meta);
    buffer.setData(info.width, info.height, info.channels, finalData);

    return true;
}