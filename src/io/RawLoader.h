#pragma once

#include <QString>

class ImageBuffer;

/**
 * @brief Loads camera RAW files into an ImageBuffer as single-channel CFA data.
 *
 * Supports all RAW formats handled by LibRaw, including CR2/CR3, NEF, ARW,
 * DNG, ORF, RAF, RW2, PEF, and many others.
 *
 * The raw sensor data is:
 *   1. Read from the file using LibRaw (no in-camera processing).
 *   2. Cropped to the visible image area, discarding optical black margins.
 *   3. Black-level corrected and normalised to the range [0.0, 1.0].
 *   4. Stored as a single-channel float32 ImageBuffer (CFA / Bayer layout).
 *
 * The Bayer pattern string (e.g. "RGGB") is recorded in
 * ImageBuffer::Metadata::bayerPattern so that the downstream debayering step
 * can be applied after calibration frame subtraction/division.
 *
 * Requires HAVE_LIBRAW to be defined (set automatically by CMakeLists.txt
 * when LibRaw is found at configure time).
 */
namespace RawLoader {

/**
 * @brief Loads a RAW file into an ImageBuffer.
 *
 * @param filePath Absolute path to the RAW file.
 * @param buf      Output ImageBuffer. Any existing content is overwritten.
 * @param errorMsg Optional pointer to receive a human-readable error description.
 * @return true on success.
 */
bool load(const QString& filePath, ImageBuffer& buf, QString* errorMsg = nullptr);

/**
 * @brief Returns true if the given file extension identifies a supported RAW format.
 *
 * The comparison is case-insensitive.
 *
 * @param ext File extension without the leading dot (e.g. "cr2", "NEF").
 * @return true when the extension is in the supported set.
 */
bool isSupportedExtension(const QString& ext);

/**
 * @brief Returns a Qt file-dialog filter string for all supported RAW formats.
 *
 * Example return value: "RAW Files (*.cr2 *.cr3 *.nef ...)"
 *
 * @return Formatted filter string suitable for QFileDialog.
 */
QString filterString();

} // namespace RawLoader