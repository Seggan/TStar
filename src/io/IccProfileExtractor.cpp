#include "IccProfileExtractor.h"

#include <QFile>
#include <QDataStream>
#include <QFileInfo>
#include <QDebug>
#include <QtEndian>
#include <QXmlStreamReader>


// =============================================================================
// Format dispatch
// =============================================================================

bool IccProfileExtractor::extractFromFile(const QString& filePath, QByteArray& iccData)
{
    const QString ext = QFileInfo(filePath).suffix().toLower();

    if (ext == "tif"  || ext == "tiff") return extractFromTiff(filePath, iccData);
    if (ext == "png")                   return extractFromPng(filePath,  iccData);
    if (ext == "jpg"  || ext == "jpeg") return extractFromJpeg(filePath, iccData);
    if (ext == "fits" || ext == "fit")  return extractFromFits(filePath, iccData);
    if (ext == "xisf")                  return extractFromXisf(filePath, iccData);

    // Camera RAW extensions supported by LibRaw.
    static const QStringList rawExts = {
        "cr2", "crw", "nef", "nrw", "arw", "dng", "raf",
        "orf", "rw2", "raw"
    };
    if (rawExts.contains(ext))
        return extractFromRaw(filePath, iccData);

    return false;
}


// =============================================================================
// TIFF extraction (IFD tag 34675)
// =============================================================================

bool IccProfileExtractor::extractFromTiff(const QString& filePath, QByteArray& iccData)
{
    static const quint16 TIFF_ICC_PROFILE_TAG = 34675;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QDataStream stream(&file);

    // Determine byte order from the TIFF header.
    quint16 byteOrder = 0;
    stream >> byteOrder;

    if      (byteOrder == 0x4949) stream.setByteOrder(QDataStream::LittleEndian);
    else if (byteOrder == 0x4D4D) stream.setByteOrder(QDataStream::BigEndian);
    else                          return false;

    // Verify TIFF magic number.
    quint16 version = 0;
    stream >> version;
    if (version != 42)
        return false;

    // Navigate to the first Image File Directory.
    quint32 ifdOffset = 0;
    stream >> ifdOffset;
    if (!file.seek(ifdOffset))
        return false;

    // Scan all IFD entries for the ICC profile tag.
    quint16 numEntries = 0;
    stream >> numEntries;

    for (int i = 0; i < numEntries; ++i)
    {
        quint16 tag = 0, type = 0;
        quint32 count = 0, value = 0;
        stream >> tag >> type >> count >> value;

        if (tag != TIFF_ICC_PROFILE_TAG)
            continue;

        const qint64 savePos = file.pos();

        if (count <= 4)
        {
            // Data fits in the value field directly.
            iccData = QByteArray::fromRawData(reinterpret_cast<const char*>(&value),
                                              static_cast<int>(count));
        }
        else
        {
            // Value field contains the offset to the profile data.
            if (file.seek(value))
                iccData = file.read(static_cast<qint64>(count));
        }

        file.seek(savePos);
        return !iccData.isEmpty();
    }

    return false;
}


// =============================================================================
// PNG extraction (iCCP chunk)
// =============================================================================

#include <zlib.h>

bool IccProfileExtractor::extractFromPng(const QString& filePath, QByteArray& iccData)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    // Verify the 8-byte PNG signature.
    const QByteArray pngSig = file.read(8);
    if (pngSig.size() != 8 || pngSig != QByteArray("\x89PNG\r\n\x1a\n"))
        return false;

    // Iterate over PNG chunks until iCCP is found or IEND is reached.
    while (!file.atEnd())
    {
        // Chunk length (big-endian, 4 bytes).
        QByteArray lenData = file.read(4);
        if (lenData.size() != 4)
            break;

        const quint32 length =
            (static_cast<quint8>(lenData[0]) << 24) |
            (static_cast<quint8>(lenData[1]) << 16) |
            (static_cast<quint8>(lenData[2]) <<  8) |
             static_cast<quint8>(lenData[3]);

        const QByteArray chunkType = file.read(4);
        if (chunkType.size() != 4)
            break;

        if (chunkType == "IEND")
            break;

        if (chunkType == "iCCP")
        {
            // iCCP layout:
            //   profile name (null-terminated string)
            //   compression method (1 byte, always 0 = zlib)
            //   zlib-compressed ICC profile data
            const QByteArray chunkData = file.read(static_cast<qint64>(length));

            const int nameEnd = chunkData.indexOf('\0');
            if (nameEnd < 0 || nameEnd + 2 >= chunkData.size())
            {
                file.read(4); // Skip CRC.
                continue;
            }

            // Skip name + null terminator + compression method byte.
            const int dataStart = nameEnd + 2;
            const QByteArray compressed = chunkData.mid(dataStart);

            // Decompress; try a 2 MB buffer first, expanding to 10 MB on overflow.
            unsigned long uncompressedSize = 2 * 1024 * 1024;
            iccData.resize(static_cast<int>(uncompressedSize));

            int res = uncompress(
                reinterpret_cast<Bytef*>(iccData.data()), &uncompressedSize,
                reinterpret_cast<const Bytef*>(compressed.constData()),
                static_cast<uLong>(compressed.size()));

            if (res == Z_OK)
            {
                iccData.resize(static_cast<int>(uncompressedSize));
                return true;
            }
            if (res == Z_BUF_ERROR)
            {
                uncompressedSize = 10 * 1024 * 1024;
                iccData.resize(static_cast<int>(uncompressedSize));
                if (uncompress(
                        reinterpret_cast<Bytef*>(iccData.data()), &uncompressedSize,
                        reinterpret_cast<const Bytef*>(compressed.constData()),
                        static_cast<uLong>(compressed.size())) == Z_OK)
                {
                    iccData.resize(static_cast<int>(uncompressedSize));
                    return true;
                }
            }
        }
        else
        {
            file.read(static_cast<qint64>(length));
        }

        file.read(4); // Skip the 4-byte CRC.
    }

    return false;
}


// =============================================================================
// JPEG extraction (APP2 markers)
// =============================================================================

bool IccProfileExtractor::extractFromJpeg(const QString& filePath, QByteArray& iccData)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    // Verify JPEG SOI marker (0xFF 0xD8).
    quint8 b1 = 0, b2 = 0;
    if (file.read(reinterpret_cast<char*>(&b1), 1) != 1 ||
        file.read(reinterpret_cast<char*>(&b2), 1) != 1)
        return false;

    if (b1 != 0xFF || b2 != 0xD8)
        return false;

    // Collect ICC segments (JPEG may split a profile across multiple APP2 markers).
    QMap<int, QByteArray> segments;

    while (!file.atEnd())
    {
        quint8 m1 = 0, m2 = 0;
        if (file.read(reinterpret_cast<char*>(&m1), 1) != 1 ||
            file.read(reinterpret_cast<char*>(&m2), 1) != 1)
            break;

        if (m1 != 0xFF)
            continue;

        if (m2 == 0xDA)  // SOS: Start of Scan; no more metadata segments follow.
            break;

        if (m2 >= 0xD0 && m2 <= 0xD9)  // Markers without a length field.
            continue;

        // Read the 2-byte big-endian segment length (includes the length bytes).
        quint16 length = 0;
        file.read(reinterpret_cast<char*>(&length), 2);
        length = (static_cast<quint8>(reinterpret_cast<const uchar*>(&length)[0]) << 8) |
                  static_cast<quint8>(reinterpret_cast<const uchar*>(&length)[1]);

        if (length < 2)
            continue;

        const QByteArray appData = file.read(length - 2);

        if (m2 == 0xE2 && appData.startsWith("ICC_PROFILE\0"))
        {
            // Format: "ICC_PROFILE\0" (12 bytes) + segment index + total count + data.
            if (appData.size() > 14)
            {
                const int index = static_cast<quint8>(appData[12]);
                segments[index] = appData.mid(14);
            }
        }
    }

    if (segments.isEmpty())
        return false;

    // Reassemble segments in ascending index order.
    iccData.clear();
    for (int i = 1; i <= segments.size(); ++i)
    {
        if (!segments.contains(i))
            return false;  // A segment is missing; the profile is incomplete.
        iccData.append(segments[i]);
    }

    return !iccData.isEmpty();
}


// =============================================================================
// FITS extraction (dedicated ICC_PROFILE HDU)
// =============================================================================

// Include CFITSIO here to avoid polluting earlier translation units.
#include <fitsio.h>

// On Windows, <fitsio.h> may define TBYTE as a macro that conflicts with the
// Windows SDK typedef. Undefine it immediately after the include.
#ifdef TBYTE
#  undef TBYTE
#endif

bool IccProfileExtractor::extractFromFits(const QString& filePath, QByteArray& iccData)
{
    fitsfile* fptr   = nullptr;
    int       status = 0;

    if (fits_open_file(&fptr, filePath.toUtf8().constData(), READONLY, &status))
        return false;

    int num_hdus = 0;
    fits_get_num_hdus(fptr, &num_hdus, &status);

    for (int hdu = 1; hdu <= num_hdus; hdu++)
    {
        int hdutype = 0;
        fits_movabs_hdu(fptr, hdu, &hdutype, &status);

        char extname[FLEN_VALUE] = "";
        if (fits_read_key(fptr, TSTRING, "EXTNAME", extname, nullptr, &status) == 0)
        {
            const QString name = QString::fromUtf8(extname).trimmed().toUpper();

            if (name == "ICC_PROFILE" || name == "ICCPROFILE")
            {
                int  naxis = 0;
                long naxes[2] = {0, 0};
                fits_get_img_dim(fptr, &naxis, &status);

                if (naxis > 0)
                {
                    fits_get_img_size(fptr, 2, naxes, &status);
                    long nelements = naxes[0];
                    if (naxis > 1) nelements *= naxes[1];

                    iccData.resize(static_cast<int>(nelements));

                    // Use the literal value 11 (TBYTE) to avoid the Windows macro conflict.
                    fits_read_img(fptr, 11, 1, nelements, nullptr,
                                  iccData.data(), nullptr, &status);

                    if (status == 0)
                    {
                        fits_close_file(fptr, &status);
                        return true;
                    }
                }
            }
        }

        // Reset status so subsequent HDU operations are not aborted.
        status = 0;
    }

    fits_close_file(fptr, &status);
    return false;
}


// =============================================================================
// Camera RAW extraction (LibRaw)
// =============================================================================

#ifdef HAVE_LIBRAW
#  include <libraw/libraw.h>
#endif

bool IccProfileExtractor::extractFromRaw(const QString& filePath, QByteArray& iccData)
{
#ifdef HAVE_LIBRAW
    LibRaw processor;

    // Open the file without unpacking the image data.
    if (processor.open_file(filePath.toUtf8().constData()) != LIBRAW_SUCCESS)
        return false;

    if (processor.imgdata.color.profile_length > 0 &&
        processor.imgdata.color.profile != nullptr)
    {
        iccData = QByteArray(
            static_cast<const char*>(processor.imgdata.color.profile),
            static_cast<int>(processor.imgdata.color.profile_length));
        return true;
    }

    return false;
#else
    Q_UNUSED(filePath)
    Q_UNUSED(iccData)
    return false;
#endif
}


// =============================================================================
// XISF extraction (inline ICCProfile element)
// =============================================================================

bool IccProfileExtractor::extractFromXisf(const QString& filePath, QByteArray& iccData)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    // Verify the XISF 1.0 signature.
    const QByteArray sig = file.read(8);
    if (sig != "XISF0100")
        return false;

    // Read the XML header length (little-endian uint32).
    uchar lenBytes[4];
    if (file.read(reinterpret_cast<char*>(lenBytes), 4) != 4)
        return false;

    const quint32 headerLen = qFromLittleEndian<quint32>(lenBytes);

    file.read(4); // Skip 4 reserved bytes.

    // Apply a safety limit to prevent allocating unreasonable buffers.
    if (headerLen == 0 || headerLen > 20 * 1024 * 1024)
        return false;

    const QByteArray xmlBytes = file.read(static_cast<qint64>(headerLen));

    // Scan the XML header for an ICCProfile element with inline base64 data.
    QXmlStreamReader xml(xmlBytes);
    while (!xml.atEnd())
    {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == "ICCProfile")
        {
            const QString profileData = xml.readElementText();
            if (!profileData.isEmpty())
            {
                iccData = QByteArray::fromBase64(profileData.toUtf8());
                return !iccData.isEmpty();
            }
        }
    }

    return false;
}