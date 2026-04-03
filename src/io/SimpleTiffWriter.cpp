#include "SimpleTiffWriter.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QDebug>
#include <QFile>

#include <algorithm>
#include <cstdint>

// =============================================================================
// TIFF tag constants (TIFF 6.0 specification)
// =============================================================================

#define TIFF_TAG_ImageWidth                256
#define TIFF_TAG_ImageLength               257
#define TIFF_TAG_BitsPerSample             258
#define TIFF_TAG_Compression               259
#define TIFF_TAG_PhotometricInterpretation 262
#define TIFF_TAG_StripOffsets              273
#define TIFF_TAG_SamplesPerPixel           277
#define TIFF_TAG_RowsPerStrip              278
#define TIFF_TAG_StripByteCounts           279
#define TIFF_TAG_XResolution               282
#define TIFF_TAG_YResolution               283
#define TIFF_TAG_PlanarConfiguration       284
#define TIFF_TAG_ResolutionUnit            296
#define TIFF_TAG_SampleFormat              339
#define TIFF_TAG_ICCProfile                34675

// =============================================================================
// Internal helpers
// =============================================================================

namespace {

/**
 * @brief Write a single 12-byte IFD entry to the stream.
 *
 * @param out   Output data stream (must be in little-endian mode).
 * @param tag   TIFF tag identifier.
 * @param type  TIFF data type (1=BYTE, 3=SHORT, 4=LONG, 5=RATIONAL, 7=UNDEFINED).
 * @param count Number of values.
 * @param value Inline value or offset to external data.
 */
void writeIFDEntry(QDataStream& out,
                   uint16_t     tag,
                   uint16_t     type,
                   uint32_t     count,
                   uint32_t     value)
{
    out << tag << type << count << value;
}

} // anonymous namespace

// =============================================================================
// Public write implementation
// =============================================================================

bool SimpleTiffWriter::write(const QString&            filename,
                             int                       width,
                             int                       height,
                             int                       channels,
                             Format                    fmt,
                             const std::vector<float>& data,
                             const QByteArray&         iccData,
                             QString*                  errorMsg)
{
    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly))
    {
        if (errorMsg)
            *errorMsg = QCoreApplication::translate(
                "SimpleTiffWriter", "Could not open file for writing.");
        return false;
    }

    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian); // "II" byte order

    // -------------------------------------------------------------------------
    // Determine output sample properties from the requested format.
    // -------------------------------------------------------------------------
    int bitsPerSample = 8;
    int sampleFormat  = 1; // 1 = unsigned integer

    switch (fmt)
    {
    case Format_uint8:   bitsPerSample =  8; sampleFormat = 1; break;
    case Format_uint16:  bitsPerSample = 16; sampleFormat = 1; break;
    case Format_uint32:  bitsPerSample = 32; sampleFormat = 1; break;
    case Format_float32: bitsPerSample = 32; sampleFormat = 3; break;
    }

    // Photometric interpretation: 1 = BlackIsZero (mono), 2 = RGB.
    const int photometric = (channels == 1) ? 1 : 2;

    // -------------------------------------------------------------------------
    // Encode pixel data into a byte buffer before computing file offsets.
    // -------------------------------------------------------------------------
    QByteArray  dataBuf;
    QDataStream dOut(&dataBuf, QIODevice::WriteOnly);
    dOut.setByteOrder(QDataStream::LittleEndian);
    dOut.setFloatingPointPrecision(QDataStream::SinglePrecision);

    const size_t totalPixels = static_cast<size_t>(width) * height;

    for (size_t i = 0; i < totalPixels; ++i)
    {
        for (int c = 0; c < channels; ++c)
        {
            const float val = data[i * channels + c];

            switch (fmt)
            {
            case Format_uint8:
                dOut << static_cast<uint8_t>(
                    std::max(0.0f, std::min(255.0f, val * 255.0f)));
                break;

            case Format_uint16:
                dOut << static_cast<uint16_t>(
                    std::max(0.0f, std::min(65535.0f, val * 65535.0f)));
                break;

            case Format_uint32:
                dOut << static_cast<uint32_t>(
                    std::max(0.0f, std::min(4294967295.0f, val * 4294967295.0f)));
                break;

            case Format_float32:
                dOut << val;
                break;
            }
        }
    }

    // -------------------------------------------------------------------------
    // Compute file layout offsets.
    //
    // File structure:
    //   [0]  TIFF header (8 bytes)
    //   [8]  IFD: 2 + numEntries*12 + 4 bytes
    //   [+]  BitsPerSample array (if channels > 1, channels * 2 bytes)
    //   [+]  XResolution rational (8 bytes)
    //   [+]  YResolution rational (8 bytes)
    //   [+]  ICC profile data (iccData.size() bytes, if present)
    //   [+]  Strip pixel data
    // -------------------------------------------------------------------------
    const uint32_t ifdOffset = 8;  // IFD immediately follows the 8-byte header.

    const int numEntries = 12 + (iccData.isEmpty() ? 0 : 1);

    const uint32_t ifdSize     = 2 + static_cast<uint32_t>(numEntries) * 12 + 4;
    uint32_t       extraOffset = ifdOffset + ifdSize;

    // BitsPerSample array (only needed when channels > 1).
    uint32_t bitsOffset = 0;
    if (channels > 1)
    {
        bitsOffset   = extraOffset;
        extraOffset += static_cast<uint32_t>(channels) * 2;
    }

    // XResolution and YResolution rationals (each 8 bytes: numerator + denominator).
    const uint32_t xResOffset = extraOffset;
    extraOffset += 8;
    const uint32_t yResOffset = extraOffset;
    extraOffset += 8;

    // ICC profile data block.
    uint32_t iccOffset = 0;
    if (!iccData.isEmpty())
    {
        iccOffset    = extraOffset;
        extraOffset += static_cast<uint32_t>(iccData.size());
    }

    // Strip pixel data immediately follows all auxiliary data.
    const uint32_t stripOffset    = extraOffset;
    const uint32_t stripByteCount = static_cast<uint32_t>(dataBuf.size());

    // -------------------------------------------------------------------------
    // Write TIFF header (8 bytes).
    // -------------------------------------------------------------------------
    out.writeRawData("II", 2);          // Little-endian byte order mark
    out << static_cast<uint16_t>(42);   // TIFF magic number
    out << ifdOffset;                   // Offset to first IFD

    // -------------------------------------------------------------------------
    // Write IFD.
    // -------------------------------------------------------------------------
    out << static_cast<uint16_t>(numEntries);

    writeIFDEntry(out, TIFF_TAG_ImageWidth,                4, 1,
                  static_cast<uint32_t>(width));

    writeIFDEntry(out, TIFF_TAG_ImageLength,               4, 1,
                  static_cast<uint32_t>(height));

    if (channels == 1)
        writeIFDEntry(out, TIFF_TAG_BitsPerSample,         3, 1,
                      static_cast<uint32_t>(bitsPerSample));
    else
        writeIFDEntry(out, TIFF_TAG_BitsPerSample,         3,
                      static_cast<uint32_t>(channels), bitsOffset);

    writeIFDEntry(out, TIFF_TAG_Compression,               3, 1, 1); // No compression

    writeIFDEntry(out, TIFF_TAG_PhotometricInterpretation, 3, 1,
                  static_cast<uint32_t>(photometric));

    writeIFDEntry(out, TIFF_TAG_StripOffsets,              4, 1, stripOffset);

    writeIFDEntry(out, TIFF_TAG_SamplesPerPixel,           3, 1,
                  static_cast<uint32_t>(channels));

    writeIFDEntry(out, TIFF_TAG_RowsPerStrip,              4, 1,
                  static_cast<uint32_t>(height));

    writeIFDEntry(out, TIFF_TAG_StripByteCounts,           4, 1, stripByteCount);

    writeIFDEntry(out, TIFF_TAG_XResolution,               5, 1, xResOffset);

    writeIFDEntry(out, TIFF_TAG_YResolution,               5, 1, yResOffset);

    writeIFDEntry(out, TIFF_TAG_SampleFormat,              3, 1,
                  static_cast<uint32_t>(sampleFormat));

    if (!iccData.isEmpty())
    {
        writeIFDEntry(out, TIFF_TAG_ICCProfile,            7,
                      static_cast<uint32_t>(iccData.size()), iccOffset);
    }

    out << static_cast<uint32_t>(0); // Offset to next IFD (0 = none)

    // -------------------------------------------------------------------------
    // Write auxiliary data blocks.
    // -------------------------------------------------------------------------

    // BitsPerSample array for multi-channel images.
    if (channels > 1)
    {
        for (int i = 0; i < channels; ++i)
            out << static_cast<uint16_t>(bitsPerSample);
    }

    // XResolution: 72/1 dpi
    out << static_cast<uint32_t>(72) << static_cast<uint32_t>(1);
    // YResolution: 72/1 dpi
    out << static_cast<uint32_t>(72) << static_cast<uint32_t>(1);

    // ICC profile.
    if (!iccData.isEmpty())
        out.writeRawData(iccData.constData(), iccData.size());

    // -------------------------------------------------------------------------
    // Write pixel data strip.
    // -------------------------------------------------------------------------
    out.writeRawData(dataBuf.constData(), dataBuf.size());

    file.close();
    return true;
}