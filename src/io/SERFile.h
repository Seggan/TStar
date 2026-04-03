#ifndef SERFILE_H
#define SERFILE_H

#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include <memory>

#include "../ImageBuffer.h"

/**
 * @brief Reader and writer for the SER video file format.
 *
 * The SER format is widely used in planetary astronomy for recording high-speed
 * image sequences. Each file consists of a fixed 178-byte header, followed by
 * sequential frame data, and an optional block of per-frame UTC timestamps.
 *
 * Pixel data is normalised to the range [0.0, 1.0] as float32 on read.
 * On write, float32 data is quantised to the requested bit depth.
 *
 * Reference: http://www.grischa-hahn.homepage.t-online.de/astro/ser/
 */
class SERFile
{
public:

    // -------------------------------------------------------------------------
    // Color ID constants (as defined by the SER specification)
    // -------------------------------------------------------------------------

    enum ColorID
    {
        MONO        = 0,    ///< Monochrome (no Bayer filter).
        BAYER_RGGB  = 8,    ///< Bayer pattern RGGB.
        BAYER_GRBG  = 9,    ///< Bayer pattern GRBG.
        BAYER_GBRG  = 10,   ///< Bayer pattern GBRG.
        BAYER_BGGR  = 11,   ///< Bayer pattern BGGR.
        BAYER_CYYM  = 16,   ///< CMYG pattern variant.
        BAYER_YCMY  = 17,   ///< CMYG pattern variant.
        BAYER_YMCY  = 18,   ///< CMYG pattern variant.
        BAYER_MYYC  = 19,   ///< CMYG pattern variant.
        RGB         = 100,  ///< 3-channel RGB (interleaved).
        BGR         = 101   ///< 3-channel BGR (interleaved, requires swap on read).
    };

    // -------------------------------------------------------------------------
    // Construction / destruction
    // -------------------------------------------------------------------------

    SERFile();
    ~SERFile();

    // -------------------------------------------------------------------------
    // File I/O
    // -------------------------------------------------------------------------

    /**
     * @brief Opens a SER file for reading and validates the header.
     * @param filename Path to the SER file.
     * @return true on success.
     */
    bool open(const std::string& filename);

    /**
     * @brief Closes the file and releases all resources.
     */
    void close();

    // -------------------------------------------------------------------------
    // Header accessors
    // -------------------------------------------------------------------------

    int     width()      const; ///< Image width in pixels.
    int     height()     const; ///< Image height in pixels.
    int     frameCount() const; ///< Total number of frames in the sequence.
    int     bitDepth()   const; ///< Bits per channel (8 or 16).
    ColorID colorID()    const; ///< Color / Bayer type identifier.

    /**
     * @brief Returns true when the color ID indicates a Bayer-mosaic sensor.
     */
    bool isBayer() const;

    // -------------------------------------------------------------------------
    // Frame reading
    // -------------------------------------------------------------------------

    /**
     * @brief Reads a single frame from the SER file into an ImageBuffer.
     *
     * Pixel data is normalised to [0.0, 1.0]. BGR frames are converted to RGB
     * by swapping the red and blue channels.
     *
     * @param index  0-based frame index.
     * @param buffer Output ImageBuffer.
     * @return true on success.
     */
    bool readFrame(int index, ImageBuffer& buffer);

    // -------------------------------------------------------------------------
    // Frame writing (static utility)
    // -------------------------------------------------------------------------

    /**
     * @brief Writes a sequence of ImageBuffers to a new SER file.
     *
     * All frames must have the same dimensions and channel count as frames[0].
     *
     * @param filename  Destination file path. Overwritten if it already exists.
     * @param frames    Source frames in sequential order.
     * @param colorId   Color ID to write in the SER header.
     * @param bitDepth  Output bit depth (8 or 16).
     * @return true on success.
     */
    static bool write(const std::string&            filename,
                      const std::vector<ImageBuffer>& frames,
                      ColorID                         colorId,
                      int                             bitDepth);

private:

    // -------------------------------------------------------------------------
    // SER header layout (178 bytes, little-endian)
    // -------------------------------------------------------------------------

    struct Header
    {
        char     fileID[14];     ///< Magic string "LUCAM-RECORDER".
        int32_t  luID;           ///< Camera LuID (camera-specific, may be 0).
        int32_t  colorID;        ///< ColorID enumerator value.
        int32_t  littleEndian;   ///< 1 = little-endian pixel data.
        int32_t  imageWidth;     ///< Frame width in pixels.
        int32_t  imageHeight;    ///< Frame height in pixels.
        int32_t  pixelDepth;     ///< Bits per pixel per channel.
        int32_t  frameCount;     ///< Number of frames in the sequence.
        int32_t  observer[2];    ///< Observer name (unused, zero-padded).
        int32_t  instrument[2];  ///< Instrument name (unused, zero-padded).
        int32_t  telescope[2];   ///< Telescope name (unused, zero-padded).
        int64_t  dateTime;       ///< Sequence start time as Win32 FILETIME.
        int64_t  dateTimeUTC;    ///< UTC sequence start time as Win32 FILETIME.
    };

    // -------------------------------------------------------------------------
    // Private members
    // -------------------------------------------------------------------------

    Header              m_header;
    std::ifstream       m_file;
    std::string         m_filename;
    std::vector<int64_t> m_timestamps;  ///< Per-frame UTC timestamps (lazy-loaded).

    /**
     * @brief Reads and validates the SER file header from the current stream position.
     * @return true when the "LUCAM-RECORDER" magic string is present.
     */
    bool readHeader();

    /**
     * @brief Reads per-frame timestamps from the block following the image data.
     * @return true on success.
     */
    bool readTimestamps();
};

#endif // SERFILE_H