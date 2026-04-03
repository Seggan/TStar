#include "SERFile.h"

#include <algorithm>
#include <cstring>

#include <QDateTime>
#include <QDebug>

// =============================================================================
// Constructor / Destructor
// =============================================================================

SERFile::SERFile()
{
    memset(&m_header, 0, sizeof(Header));
}

SERFile::~SERFile()
{
    close();
}

// =============================================================================
// File I/O
// =============================================================================

bool SERFile::open(const std::string& filename)
{
    close();

    m_file.open(filename, std::ios::binary);
    if (!m_file.is_open())
        return false;

    m_filename = filename;

    if (!readHeader())
    {
        close();
        return false;
    }

    // The optional timestamp block is located after all frame data.
    // It is read on demand rather than eagerly to avoid unnecessary I/O.

    return true;
}

void SERFile::close()
{
    if (m_file.is_open())
        m_file.close();

    m_timestamps.clear();
}

// =============================================================================
// Header accessors
// =============================================================================

int     SERFile::width()      const { return m_header.imageWidth;  }
int     SERFile::height()     const { return m_header.imageHeight; }
int     SERFile::frameCount() const { return m_header.frameCount;  }
int     SERFile::bitDepth()   const { return m_header.pixelDepth;  }

SERFile::ColorID SERFile::colorID() const
{
    return static_cast<ColorID>(m_header.colorID);
}

bool SERFile::isBayer() const
{
    int id = m_header.colorID;
    return (id >= 8 && id <= 19);
}

// =============================================================================
// Internal: header parsing
// =============================================================================

bool SERFile::readHeader()
{
    m_file.seekg(0);
    m_file.read(reinterpret_cast<char*>(&m_header), sizeof(Header));

    // Validate the SER magic string (first 14 bytes of the header).
    // Note: sizeof(Header) may differ from 178 due to compiler padding;
    // the structure fields are predominantly 4-byte aligned so this is
    // typically safe in practice.
    std::string id(m_header.fileID, 14);
    if (id != "LUCAM-RECORDER")
        return false;

    return true;
}

bool SERFile::readTimestamps()
{
    // Timestamps are appended after all frame data.
    // Each timestamp is a 64-bit Win32 FILETIME value.
    if (m_header.frameCount <= 0)
        return false;

    int     channels  = (m_header.colorID == RGB || m_header.colorID == BGR) ? 3 : 1;
    bool    is16bit   = (m_header.pixelDepth > 8);
    size_t  frameSize = static_cast<size_t>(m_header.imageWidth)
                      * m_header.imageHeight
                      * channels
                      * (is16bit ? 2 : 1);

    uint64_t tsOffset = 178 + static_cast<uint64_t>(m_header.frameCount) * frameSize;

    m_file.seekg(static_cast<std::streamoff>(tsOffset));
    if (m_file.fail())
        return false;

    m_timestamps.resize(m_header.frameCount);
    m_file.read(reinterpret_cast<char*>(m_timestamps.data()),
                m_header.frameCount * sizeof(int64_t));

    return !m_file.fail();
}

// =============================================================================
// Frame reading
// =============================================================================

bool SERFile::readFrame(int index, ImageBuffer& buffer)
{
    if (index < 0 || index >= m_header.frameCount)
        return false;

    const int    w        = m_header.imageWidth;
    const int    h        = m_header.imageHeight;
    const int    bpp      = m_header.pixelDepth;
    const bool   is16bit  = (bpp > 8);
    const int    channels = (m_header.colorID == RGB || m_header.colorID == BGR) ? 3 : 1;

    // Compute byte offset to the requested frame (header is always 178 bytes).
    const size_t   frameSize = static_cast<size_t>(w) * h * channels * (is16bit ? 2 : 1);
    const uint64_t offset    = 178ULL + static_cast<uint64_t>(index) * frameSize;

    m_file.seekg(static_cast<std::streamoff>(offset));
    if (m_file.fail())
        return false;

    std::vector<uint8_t> rawData(frameSize);
    m_file.read(reinterpret_cast<char*>(rawData.data()),
                static_cast<std::streamsize>(frameSize));

    if (m_file.gcount() != static_cast<std::streamsize>(frameSize))
        return false;

    // Normalize raw integer data to [0, 1] float range.
    const float norm = is16bit ? 65535.0f : 255.0f;
    const size_t totalSamples = static_cast<size_t>(w) * h * channels;
    std::vector<float> floatData(totalSamples);

    if (is16bit)
    {
        const uint16_t* src = reinterpret_cast<const uint16_t*>(rawData.data());

        #pragma omp parallel for
        for (int i = 0; i < static_cast<int>(totalSamples); ++i)
            floatData[i] = src[i] / norm;
    }
    else
    {
        const uint8_t* src = rawData.data();

        #pragma omp parallel for
        for (int i = 0; i < static_cast<int>(totalSamples); ++i)
            floatData[i] = src[i] / norm;
    }

    // Handle channel ordering based on the SER color ID.
    if (m_header.colorID == RGB)
    {
        // Data is already in RGB order; pass through directly.
        buffer.setData(w, h, 3, floatData);
    }
    else if (m_header.colorID == BGR)
    {
        // Swap the B and R channels so the output is always RGB.
        #pragma omp parallel for
        for (int i = 0; i < w * h; ++i)
        {
            float b = floatData[i * 3 + 0];
            float g = floatData[i * 3 + 1];
            float r = floatData[i * 3 + 2];
            floatData[i * 3 + 0] = r;
            floatData[i * 3 + 1] = g;
            floatData[i * 3 + 2] = b;
        }
        buffer.setData(w, h, 3, floatData);
    }
    else
    {
        // Mono or Bayer CFA: treated as a single-channel image.
        // Debayering is handled downstream by the preprocessing pipeline.
        buffer.setData(w, h, 1, floatData);
    }

    // Store the original bit depth in metadata for downstream reference.
    buffer.metadata().bitDepth = QString::number(bpp);

    return true;
}

// =============================================================================
// File writing
// =============================================================================

bool SERFile::write(const std::string&              filename,
                    const std::vector<ImageBuffer>& frames,
                    ColorID                         colorId,
                    int                             bitDepth)
{
    if (frames.empty())
        return false;

    std::ofstream out(filename, std::ios::binary);
    if (!out.is_open())
        return false;

    const int w     = frames[0].width();
    const int h     = frames[0].height();
    const int count = static_cast<int>(frames.size());

    // Build and write the SER header.
    Header head;
    memset(&head, 0, sizeof(Header));
    memcpy(head.fileID, "LUCAM-RECORDER", 14);
    head.colorID     = colorId;
    head.littleEndian = 1;      // SER files are always little-endian
    head.imageWidth  = w;
    head.imageHeight = h;
    head.pixelDepth  = bitDepth;
    head.frameCount  = count;

    // Encode the current time as a Win32 FILETIME (100-nanosecond intervals
    // since 1601-01-01). The offset from Unix epoch is 11644473600 seconds.
    const QDateTime now = QDateTime::currentDateTime();
    const uint64_t  ft  = (static_cast<uint64_t>(now.toMSecsSinceEpoch())
                           + 11644473600000ULL) * 10000ULL;
    head.dateTime    = static_cast<int64_t>(ft);
    head.dateTimeUTC = static_cast<int64_t>(ft);

    out.write(reinterpret_cast<const char*>(&head), sizeof(Header));

    // Write frame pixel data.
    const bool  is16bit = (bitDepth > 8);
    const float maxVal  = is16bit ? 65535.0f : 255.0f;

    for (const auto& img : frames)
    {
        const int    channels = img.channels();
        const size_t pixels   = static_cast<size_t>(w) * h * channels;
        const float* src      = img.data().data();

        if (is16bit)
        {
            std::vector<uint16_t> buf(pixels);
            for (size_t i = 0; i < pixels; ++i)
                buf[i] = static_cast<uint16_t>(
                    std::clamp(src[i] * maxVal, 0.0f, maxVal));

            out.write(reinterpret_cast<const char*>(buf.data()),
                      static_cast<std::streamsize>(pixels * sizeof(uint16_t)));
        }
        else
        {
            std::vector<uint8_t> buf(pixels);
            for (size_t i = 0; i < pixels; ++i)
                buf[i] = static_cast<uint8_t>(
                    std::clamp(src[i] * maxVal, 0.0f, maxVal));

            out.write(reinterpret_cast<const char*>(buf.data()),
                      static_cast<std::streamsize>(pixels));
        }
    }

    return true;
}