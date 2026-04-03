#ifndef FITSHEADERUTILS_H
#define FITSHEADERUTILS_H

#include <QString>
#include <QStringList>
#include <QVariant>
#include <QMap>
#include <vector>

#include "../ImageBuffer.h"

/**
 * @brief Static utility class for FITS header manipulation and WCS coordinate handling.
 *
 * Provides helpers for:
 *   - Validating and filtering FITS keyword cards.
 *   - Building and validating WCS (World Coordinate System) header blocks.
 *   - Parsing FITS keyword values from XISF property maps.
 *   - Converting between sexagesimal (HMS/DMS) and decimal degree representations.
 *   - Ensuring a minimal set of required header cards is present.
 */
class FitsHeaderUtils
{
public:

    using HeaderCard = ImageBuffer::Metadata::HeaderCard;
    using Metadata   = ImageBuffer::Metadata;

    // -------------------------------------------------------------------------
    // Structural keyword list
    // -------------------------------------------------------------------------

    /**
     * @brief FITS keywords that describe image structure and are handled separately
     *        from user-defined metadata (SIMPLE, BITPIX, NAXIS*, BZERO, BSCALE, END).
     */
    static const QStringList structuralKeywords;

    // -------------------------------------------------------------------------
    // Header card validation and filtering
    // -------------------------------------------------------------------------

    /**
     * @brief Removes cards with empty, structural, or syntactically invalid keywords.
     *
     * Cards whose keyword exceeds 8 characters (unless prefixed with "HIERARCH ")
     * or contains characters outside [A-Z0-9_-] are silently dropped with a warning.
     * Cards whose value length exceeds 68 characters are retained but warned about.
     *
     * @param cards Input card vector.
     * @return Filtered card vector containing only valid, non-structural cards.
     */
    static std::vector<HeaderCard> dropInvalidCards(const std::vector<HeaderCard>& cards);

    // -------------------------------------------------------------------------
    // WCS header construction
    // -------------------------------------------------------------------------

    /**
     * @brief Returns true if the supplied metadata contains a usable WCS solution.
     *
     * Validity requires a non-degenerate CD matrix (|det| > 1e-20) and at least
     * one non-zero CRPIX coordinate. RA == 0 and Dec == 0 are accepted as valid.
     *
     * @param meta Metadata structure to inspect.
     * @return true if the WCS data is sufficient for a valid projection.
     */
    static bool hasValidWCS(const Metadata& meta);

    /**
     * @brief Builds a complete set of WCS FITS header cards from the metadata.
     *
     * Includes CTYPE, EQUINOX, CRVAL, CRPIX, CD matrix, optional LONPOLE/LATPOLE,
     * and all SIP polynomial coefficients when present.
     *
     * @param meta Source metadata.
     * @return Vector of header cards. Empty if the WCS data is invalid.
     */
    static std::vector<HeaderCard> buildWCSHeader(const Metadata& meta);

    // -------------------------------------------------------------------------
    // XISF keyword and property ingestion
    // -------------------------------------------------------------------------

    /**
     * @brief Converts an XISF FITSKeyword property map into a vector of header cards.
     *
     * Handles all three storage forms used by PixInsight XISF files:
     *   - key -> QVariantList of {value, comment} maps
     *   - key -> QVariantMap with "value" and "comment" keys
     *   - key -> plain scalar QVariant
     *
     * @param fitsKeywords Source keyword map as stored in an XISF metadata block.
     * @return Vector of header cards.
     */
    static std::vector<HeaderCard> parseXISFFitsKeywords(
        const QMap<QString, QVariant>& fitsKeywords);

    /**
     * @brief Extracts known astrometric and observational properties from an XISF
     *        property map and writes them into the supplied metadata structure.
     *
     * Recognised property IDs include PCL:AstrometricSolution:* and
     * Instrument:Telescope:* namespaces, as well as Observation:* properties.
     * All properties are also stored verbatim in meta.xisfProperties.
     *
     * @param props Source XISF property map.
     * @param meta  Metadata structure to populate.
     */
    static void applyXISFProperties(const QVariantMap& props, Metadata& meta);

    // -------------------------------------------------------------------------
    // FITS value coercion
    // -------------------------------------------------------------------------

    /**
     * @brief Attempts to coerce a raw FITS value string to the most appropriate
     *        QVariant type (bool, qlonglong, double, or QString).
     *
     * Quoted string values have their surrounding single-quotes stripped.
     * Integer detection requires the absence of decimal points or exponent notation.
     *
     * @param value Raw FITS value field string.
     * @return Typed QVariant, or an invalid QVariant for an empty input.
     */
    static QVariant coerceFitsValue(const QString& value);

    // -------------------------------------------------------------------------
    // Coordinate parsing
    // -------------------------------------------------------------------------

    /**
     * @brief Parses a right-ascension string to decimal degrees.
     *
     * Accepts decimal degrees directly, or sexagesimal HMS notation in any of
     * these forms: "HH MM SS.ss", "HH:MM:SS.ss", "HHhMMmSS.sss".
     *
     * @param str Source string.
     * @param ok  Optional output flag; set to false when parsing fails.
     * @return RA in decimal degrees, or 0.0 on failure.
     */
    static double parseRA(const QString& str, bool* ok = nullptr);

    /**
     * @brief Parses a declination string to decimal degrees.
     *
     * Accepts decimal degrees directly, or sexagesimal DMS notation in any of
     * these forms: "DD MM SS.ss", "DD:MM:SS.ss", "DDdMMmSS.sss".
     * A leading minus sign is honoured for negative declinations.
     *
     * @param str Source string.
     * @param ok  Optional output flag; set to false when parsing fails.
     * @return Declination in decimal degrees, or 0.0 on failure.
     */
    static double parseDec(const QString& str, bool* ok = nullptr);

    // -------------------------------------------------------------------------
    // Coordinate formatting
    // -------------------------------------------------------------------------

    /**
     * @brief Formats a right-ascension value (decimal degrees) as HH:MM:SS.ss.
     *
     * The input is normalised to [0, 360) before conversion to hours.
     *
     * @param ra        RA in decimal degrees.
     * @param precision Number of decimal places for the seconds field.
     * @return Formatted HMS string.
     */
    static QString formatRAToHMS(double ra, int precision = 2);

    /**
     * @brief Formats a declination value (decimal degrees) as +/-DD:MM:SS.ss.
     *
     * @param dec       Declination in decimal degrees.
     * @param precision Number of decimal places for the seconds field.
     * @return Formatted DMS string with sign prefix.
     */
    static QString formatDecToDMS(double dec, int precision = 2);

    // -------------------------------------------------------------------------
    // Minimal header enforcement
    // -------------------------------------------------------------------------

    /**
     * @brief Ensures a minimal set of required header cards is present.
     *
     * Currently checks for DATE-OBS: if absent and a file path is supplied,
     * the file's last-modified timestamp is appended as a fallback value.
     *
     * @param cards    Input card vector.
     * @param filePath Optional path to the source file, used to derive DATE-OBS.
     * @return Augmented card vector.
     */
    static std::vector<HeaderCard> ensureMinimalHeader(
        const std::vector<HeaderCard>& cards,
        const QString&                 filePath = QString());

private:

    /**
     * @brief Returns true if the keyword string is syntactically valid for FITS.
     *
     * Standard keywords: 1-8 characters from [A-Z0-9_-].
     * HIERARCH keywords are accepted unconditionally.
     *
     * @param key Keyword string (case-insensitive).
     * @return true if the keyword is valid.
     */
    static bool isValidKeyword(const QString& key);

    /**
     * @brief Converts individual hours/degrees, minutes, and seconds components
     *        to a signed decimal angle.
     *
     * @param h    Hours or degrees (absolute value is used).
     * @param m    Minutes component.
     * @param s    Seconds component.
     * @param sign +1.0 for positive, -1.0 for negative.
     * @return Computed decimal angle.
     */
    static double parseHMSComponents(double h, double m, double s, double sign);
};

#endif // FITSHEADERUTILS_H