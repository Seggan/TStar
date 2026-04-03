#include "SimpleTiffReader.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QDebug>
#include <QFile>

// =============================================================================
// Internal types and TIFF tag constants
// =============================================================================

namespace {

// Baseline TIFF tag identifiers (TIFF 6.0 specification).
const quint16 TAG_ImageWidth             = 256;
const quint16 TAG_ImageLength            = 257;
const quint16 TAG_BitsPerSample          = 258;
const quint16 TAG_Compression            = 259;
const quint16 TAG_StripOffsets           = 273;
const quint16 TAG_SamplesPerPixel        = 277;
const quint16 TAG_StripByteCounts        = 279;
const quint16 TAG_PlanarConfiguration    = 284;  // 1 = chunky, 2 = planar
const quint16 TAG_SampleFormat          = 339;  // 1 = uint, 3 = float
const quint16 TAG_TileWidth              = 322;
const quint16 TAG_TileLength             = 323;
const quint16 TAG_TileOffsets            = 324;
const quint16 TAG_TileByteCounts         = 325;

// TIFF data type sizes in bytes.
const int TYPE_SIZES[] = { 0, 1, 1, 2, 4, 8, 1, 1, 2, 4, 8, 4, 8 };

} // anonymous namespace

// =============================================================================
// readInfo: header-only inspection
// =============================================================================

bool SimpleTiffReader::readInfo(const QString& path,
                                int&           width,
                                int&           height,
                                int&           channels,
                                int&           bitsPerSample,
                                QString*       errorMsg)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (errorMsg)
            *errorMsg = QCoreApplication::translate(
                "SimpleTiffReader", "File open failed: %1").arg(file.errorString());
        return false;
    }

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);

    // Read byte-order indicator ("II" = little-endian, "MM" = big-endian).
    quint16 byteOrder;
    stream >> byteOrder;
    if (byteOrder == 0x4949)
        stream.setByteOrder(QDataStream::LittleEndian);
    else if (byteOrder == 0x4D4D)
        stream.setByteOrder(QDataStream::BigEndian);
    else
        return false;

    // Validate the TIFF version magic number (must be 42).
    quint16 version;
    stream >> version;
    if (version != 42)
        return false;

    // Seek to the first Image File Directory (IFD).
    quint32 ifdOffset;
    stream >> ifdOffset;
    if (!file.seek(ifdOffset))
        return false;

    quint16 numEntries;
    stream >> numEntries;

    quint32 t_width    = 0;
    quint32 t_height   = 0;
    quint16 t_bits     = 0;
    quint16 t_channels = 1;

    for (int i = 0; i < numEntries; ++i)
    {
        quint16 tag, type;
        quint32 count, val;
        stream >> tag >> type >> count >> val;

        switch (tag)
        {
        case TAG_ImageWidth:      t_width    = val;            break;
        case TAG_ImageLength:     t_height   = val;            break;
        case TAG_SamplesPerPixel: t_channels = static_cast<quint16>(val); break;

        case TAG_BitsPerSample:
            if (count == 1)
            {
                t_bits = static_cast<quint16>(val);
            }
            else if (count == 2)
            {
                t_bits = static_cast<quint16>(val & 0xFFFF);
            }
            else
            {
                // Value field contains an offset to the actual data.
                const qint64 savePos = file.pos();
                if (file.seek(val))
                {
                    quint16 v;
                    stream >> v;
                    t_bits = v;
                }
                file.seek(savePos);
            }
            break;

        default:
            break;
        }
    }

    width         = static_cast<int>(t_width);
    height        = static_cast<int>(t_height);
    channels      = static_cast<int>(t_channels);
    bitsPerSample = static_cast<int>(t_bits);

    return (width > 0 && height > 0);
}

// =============================================================================
// readFloat32: full pixel data decode
// =============================================================================

bool SimpleTiffReader::readFloat32(const QString&      path,
                                   int&                width,
                                   int&                height,
                                   int&                channels,
                                   std::vector<float>& data,
                                   QString*            errorMsg,
                                   QString*            debugInfo)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (errorMsg)
            *errorMsg = QCoreApplication::translate(
                "SimpleTiffReader", "File open failed: %1").arg(file.errorString());
        return false;
    }

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);

    // -------------------------------------------------------------------------
    // 1. Parse TIFF header
    // -------------------------------------------------------------------------
    quint16 byteOrder;
    stream >> byteOrder;

    if (byteOrder == 0x4949)
        stream.setByteOrder(QDataStream::LittleEndian);
    else if (byteOrder == 0x4D4D)
        stream.setByteOrder(QDataStream::BigEndian);
    else
    {
        if (errorMsg)
            *errorMsg = QCoreApplication::translate(
                "SimpleTiffReader", "Invalid TIFF byte order marker.");
        return false;
    }

    quint16 version;
    stream >> version;
    if (version != 42)
    {
        if (errorMsg)
            *errorMsg = QCoreApplication::translate(
                "SimpleTiffReader", "Invalid TIFF version (expected 42).");
        return false;
    }

    quint32 ifdOffset;
    stream >> ifdOffset;

    // -------------------------------------------------------------------------
    // 2. Read and parse the first IFD
    // -------------------------------------------------------------------------
    if (!file.seek(ifdOffset))
    {
        if (errorMsg)
            *errorMsg = QCoreApplication::translate(
                "SimpleTiffReader", "Could not seek to IFD.");
        return false;
    }

    quint16 numEntries;
    stream >> numEntries;

    // Parsed IFD fields
    quint32 t_width          = 0;
    quint32 t_height         = 0;
    quint16 t_bitsPerSample  = 0;
    quint16 t_samplesPerPixel = 1;
    quint16 t_compression    = 1;   // 1 = no compression
    quint16 t_sampleFormat   = 1;   // 1 = unsigned int, 3 = float
    quint16 t_planarConfig   = 1;   // 1 = chunky, 2 = planar

    quint32 t_tileWidth  = 0;
    quint32 t_tileLength = 0;

    std::vector<quint32> stripOffsets;
    std::vector<quint32> stripByteCounts;
    std::vector<quint32> tileOffsets;
    std::vector<quint32> tileByteCounts;

    for (int i = 0; i < numEntries; ++i)
    {
        quint16 tag, type;
        quint32 count, val;
        stream >> tag >> type >> count >> val;

        switch (tag)
        {
        case TAG_ImageWidth:
            t_width = val;
            break;

        case TAG_ImageLength:
            t_height = val;
            break;

        case TAG_SamplesPerPixel:
            t_samplesPerPixel = static_cast<quint16>(val);
            break;

        case TAG_Compression:
            t_compression = static_cast<quint16>(val);
            break;

        case TAG_SampleFormat:
            t_sampleFormat = static_cast<quint16>(val);
            break;

        case TAG_PlanarConfiguration:
            t_planarConfig = static_cast<quint16>(val);
            break;

        case TAG_BitsPerSample:
            if (count == 1)
            {
                t_bitsPerSample = static_cast<quint16>(val);
            }
            else if (count == 2)
            {
                t_bitsPerSample = static_cast<quint16>(val & 0xFFFF);
            }
            else
            {
                const qint64 savePos = file.pos();
                if (file.seek(val))
                {
                    quint16 v;
                    stream >> v;
                    t_bitsPerSample = v;
                }
                file.seek(savePos);
            }
            break;

        case TAG_StripOffsets:
        {
            if (count == 1)
            {
                stripOffsets.push_back(val);
            }
            else
            {
                const qint64 savePos = file.pos();
                file.seek(val);
                for (quint32 k = 0; k < count; ++k)
                {
                    quint32 off;
                    stream >> off;
                    stripOffsets.push_back(off);
                }
                file.seek(savePos);
            }
            break;
        }

        case TAG_StripByteCounts:
        {
            if (count == 1)
            {
                stripByteCounts.push_back(val);
            }
            else
            {
                const qint64 savePos = file.pos();
                file.seek(val);
                for (quint32 k = 0; k < count; ++k)
                {
                    quint32 cnt;
                    stream >> cnt;
                    stripByteCounts.push_back(cnt);
                }
                file.seek(savePos);
            }
            break;
        }

        case TAG_TileWidth:
            t_tileWidth = val;
            break;

        case TAG_TileLength:
            t_tileLength = val;
            break;

        case TAG_TileOffsets:
        {
            if (count == 1)
            {
                tileOffsets.push_back(val);
            }
            else
            {
                const qint64 savePos = file.pos();
                file.seek(val);
                for (quint32 k = 0; k < count; ++k)
                {
                    quint32 off;
                    stream >> off;
                    tileOffsets.push_back(off);
                }
                file.seek(savePos);
            }
            break;
        }

        case TAG_TileByteCounts:
        {
            if (count == 1)
            {
                tileByteCounts.push_back(val);
            }
            else
            {
                const qint64 savePos = file.pos();
                file.seek(val);
                for (quint32 k = 0; k < count; ++k)
                {
                    quint32 cnt;
                    stream >> cnt;
                    tileByteCounts.push_back(cnt);
                }
                file.seek(savePos);
            }
            break;
        }

        default:
            break;
        }
    }

    // -------------------------------------------------------------------------
    // 3. Validate parsed parameters
    // -------------------------------------------------------------------------
    if (t_compression != 1)
    {
        if (errorMsg)
            *errorMsg = QCoreApplication::translate(
                "SimpleTiffReader",
                "Compressed TIFFs are not supported (only uncompressed).");
        return false;
    }

    width    = static_cast<int>(t_width);
    height   = static_cast<int>(t_height);
    channels = static_cast<int>(t_samplesPerPixel);

    // -------------------------------------------------------------------------
    // 4. Decode pixel data into a flat float buffer
    // -------------------------------------------------------------------------
    std::vector<float> rawData(static_cast<size_t>(width) * height * channels, 0.0f);

    float dMin     =  1e9f;
    float dMax     = -1e9f;
    int   nanCount = 0;

    const bool isTiled = !tileOffsets.empty();

    // Helper: read a single sample value from a QDataStream and convert to float.
    auto readSample = [&](QDataStream& ds, int bps, int fmt) -> float
    {
        if (fmt == 3) // IEEE float
        {
            if (bps == 4) { float f;  ds >> f; return f; }
            if (bps == 8) { double d; ds >> d; return static_cast<float>(d); }
        }
        else if (fmt == 1) // unsigned integer
        {
            if (bps == 1) { quint8  v; ds >> v; return v / 255.0f; }
            if (bps == 2) { quint16 v; ds >> v; return v / 65535.0f; }
            if (bps == 4) { quint32 v; ds >> v; return v / 4294967295.0f; }
        }
        return 0.0f;
    };

    const int bytesPerSample = (t_bitsPerSample > 0)
        ? (t_bitsPerSample / 8)
        : 4; // Default to float32

    if (isTiled)
    {
        // -----------------------------------------------------------------
        // Tiled image layout
        // -----------------------------------------------------------------
        if (tileOffsets.size() != tileByteCounts.size())
        {
            if (errorMsg)
                *errorMsg = QCoreApplication::translate(
                    "SimpleTiffReader", "Tile offset/count mismatch.");
            return false;
        }

        const int tilesAcross   = (width  + t_tileWidth  - 1) / t_tileWidth;
        const int tilesDown     = (height + t_tileLength - 1) / t_tileLength;
        const int tilesPerPlane = tilesAcross * tilesDown;

        // For chunky images each tile holds all channels; for planar images
        // each tile holds exactly one channel (plane).
        const int samplesInTile = (t_planarConfig == 1) ? channels : 1;

        for (size_t t = 0; t < tileOffsets.size(); ++t)
        {
            if (!file.seek(tileOffsets[t]))
                continue;

            QByteArray bytes = file.read(tileByteCounts[t]);
            QDataStream ds(bytes);
            ds.setByteOrder(stream.byteOrder());
            ds.setFloatingPointPrecision(QDataStream::SinglePrecision);

            // Determine which plane and tile position this tile index corresponds to.
            int planeIdx   = 0;
            int tileInPlane = static_cast<int>(t);

            if (t_planarConfig == 2)
            {
                planeIdx    = static_cast<int>(t) / tilesPerPlane;
                tileInPlane = static_cast<int>(t) % tilesPerPlane;
            }

            const int ty = (tileInPlane / tilesAcross) * static_cast<int>(t_tileLength);
            const int tx = (tileInPlane % tilesAcross) * static_cast<int>(t_tileWidth);

            const int totalTileSamples = static_cast<int>(bytes.size()) / bytesPerSample;
            int       readIdx          = 0;

            for (int r = 0; r < static_cast<int>(t_tileLength); ++r)
            {
                const int imgY = ty + r;
                if (imgY >= height)
                    break;

                for (int c = 0; c < static_cast<int>(t_tileWidth); ++c)
                {
                    const int imgX = tx + c;

                    // Pixels beyond the right image edge are padding; skip them.
                    if (imgX >= width)
                    {
                        readIdx += samplesInTile;
                        continue;
                    }

                    for (int s = 0; s < samplesInTile; ++s)
                    {
                        if (readIdx >= totalTileSamples)
                            break;

                        float f = readSample(ds, bytesPerSample, t_sampleFormat);
                        ++readIdx;

                        size_t destIdx = 0;
                        if (t_planarConfig == 2)
                        {
                            // Planar: plane N occupies a separate tile set.
                            destIdx = static_cast<size_t>(planeIdx) * (width * height)
                                    + static_cast<size_t>(imgY) * width + imgX;
                        }
                        else
                        {
                            // Chunky: samples interleaved per pixel.
                            destIdx = (static_cast<size_t>(imgY) * width + imgX)
                                    * channels + s;
                        }

                        if (std::isnan(f))
                        {
                            ++nanCount;
                            f = 0.0f;
                        }
                        else
                        {
                            if (f < dMin) dMin = f;
                            if (f > dMax) dMax = f;
                        }

                        if (destIdx < rawData.size())
                            rawData[destIdx] = f;
                    }
                }
            }
        }
    }
    else
    {
        // -----------------------------------------------------------------
        // Strip-based image layout
        //
        // For planar images the strips for plane 0 precede those for plane 1,
        // etc. Sequential reading into rawData is correct for both chunky and
        // planar layouts because the planar-to-interleaved reorder happens
        // as a post-processing step below.
        // -----------------------------------------------------------------
        if (stripOffsets.size() != stripByteCounts.size())
        {
            if (errorMsg)
                *errorMsg = QCoreApplication::translate(
                    "SimpleTiffReader", "Strip offset/count mismatch.");
            return false;
        }

        size_t currentOutIdx = 0;

        for (size_t s = 0; s < stripOffsets.size(); ++s)
        {
            if (!file.seek(stripOffsets[s]))
                continue;

            QByteArray  bytes = file.read(stripByteCounts[s]);
            QDataStream ds(bytes);
            ds.setByteOrder(stream.byteOrder());
            ds.setFloatingPointPrecision(QDataStream::SinglePrecision);

            const int numSamples = static_cast<int>(bytes.size()) / bytesPerSample;

            for (int k = 0; k < numSamples; ++k)
            {
                if (currentOutIdx >= rawData.size())
                    break;

                float f = readSample(ds, bytesPerSample, t_sampleFormat);

                if (std::isnan(f))
                {
                    ++nanCount;
                    f = 0.0f;
                }
                else
                {
                    if (f < dMin) dMin = f;
                    if (f > dMax) dMax = f;
                }

                rawData[currentOutIdx++] = f;
            }
        }
    }

    // -------------------------------------------------------------------------
    // 5. Diagnostic output and optional auto-normalization
    // -------------------------------------------------------------------------
    qDebug() << "SimpleTiffReader stats:" << path
             << "min:" << dMin << "max:" << dMax << "NaNs:" << nanCount;

    if (debugInfo)
    {
        *debugInfo = QString("TIFF Stats: Min=%1 Max=%2 NaNs=%3")
                        .arg(dMin).arg(dMax).arg(nanCount);
    }

    // Normalize values that appear to be in an integer ADU range rather than [0, 1].
    if (dMax > 1.0f)
    {
        const float scale = 1.0f / dMax;

        qDebug() << "SimpleTiffReader: auto-normalizing with scale" << scale;

        if (debugInfo)
            *debugInfo += QString(" [Normalized by 1/%1]").arg(dMax);

        for (float& v : rawData)
            v *= scale;
    }

    // -------------------------------------------------------------------------
    // 6. Convert planar layout to interleaved (chunky) if required
    // -------------------------------------------------------------------------
    if (t_planarConfig == 2 && channels > 1)
    {
        const int planeSize = width * height;
        data.resize(static_cast<size_t>(planeSize) * channels);

        for (int i = 0; i < planeSize; ++i)
        {
            for (int ch = 0; ch < channels; ++ch)
            {
                const int srcIdx = ch * planeSize + i;
                const int dstIdx = i * channels + ch;

                if (srcIdx < static_cast<int>(rawData.size()))
                    data[dstIdx] = rawData[srcIdx];
            }
        }
    }
    else
    {
        data = std::move(rawData);
    }

    return true;
}