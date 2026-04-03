#include "FitsLoader.h"
#include "IccProfileExtractor.h"
#include "core/ColorProfileManager.h"

#include <fitsio.h>

#include <QDebug>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QFileInfo>
#include <QDir>

#include <vector>
#include <set>
#include <algorithm>

#ifdef _OPENMP
#  include <omp.h>
#endif


// =============================================================================
// Internal normalisation helpers
// =============================================================================

namespace {

/**
 * @brief Determines the normalisation divisor for FITS pixel data.
 *
 * Integer BITPIX values map to their type maximum. Float BITPIX values are
 * normalised heuristically only when the observed maximum falls within a
 * recognisable ADU range (8-bit or 16-bit). Values already in [0, 1] or
 * large-scale HDR data are left unchanged.
 *
 * Min-max normalisation is deliberately avoided to preserve relative offsets
 * required for calibration frame arithmetic.
 *
 * @param bitpix    FITS BITPIX value.
 * @param globalMax Observed maximum pixel value across all channels.
 * @param doScale   Output flag: true when the divisor should be applied.
 * @return Divisor to use when doScale is true.
 */
float selectNormalisationDivisor(int bitpix, float globalMax, bool& doScale)
{
    doScale = false;

    if (bitpix == 8)
    {
        doScale = true;
        return 255.0f;
    }
    if (bitpix == 16)
    {
        doScale = true;
        return 65535.0f;
    }
    if (bitpix == 32)
    {
        doScale = true;
        return 4294967295.0f;
    }

    // Floating-point FITS.
    if (bitpix < 0 && globalMax > 1.0f)
    {
        if (globalMax <= 255.5f)
        {
            // Float32 FITS with 8-bit range values (e.g. colour HiPS tiles from
            // hips2fits for PanSTARRS or DSS). Dividing by 65535 would make the
            // image nearly black; use 255 instead.
            doScale = true;
            return 255.0f;
        }
        if (globalMax <= 66000.0f)
        {
            // Float32 FITS with 16-bit ADU range (0-65535); typical uncalibrated
            // monochrome astronomical sensor data stored as float.
            doScale = true;
            return 65535.0f;
        }
        // Large-scale HDR or unknown units: leave as-is.
    }

    return 1.0f;
}

} // anonymous namespace


// =============================================================================
// Primary load entry point
// =============================================================================

bool FitsLoader::load(const QString& filePath, ImageBuffer& buffer, QString* errorMsg)
{
    // Verify that the file exists before passing the path to CFITSIO.
    const QFileInfo fi(filePath);
    if (!fi.exists())
    {
        if (errorMsg)
            *errorMsg = QCoreApplication::translate(
                "FitsLoader", "File does not exist: %1").arg(filePath);
        return false;
    }

    fitsfile* fptr  = nullptr;
    int       status = 0;

    const QString nativePath = QDir::toNativeSeparators(fi.absoluteFilePath());

    if (fits_open_file(&fptr, nativePath.toUtf8().constData(), READONLY, &status))
    {
        if (errorMsg)
        {
            char statusStr[FLEN_STATUS];
            fits_get_errstatus(status, statusStr);
            *errorMsg = QCoreApplication::translate(
                "FitsLoader", "CFITSIO Open Error %1: %2\nPath: %3")
                .arg(status).arg(statusStr).arg(filePath);
        }
        return false;
    }

    // Retrieve image geometry.
    int  bitpix = 0;
    int  naxis  = 0;
    long naxes[9] = {1, 1, 1, 1, 1, 1, 1, 1, 1};

    if (fits_get_img_param(fptr, 9, &bitpix, &naxis, naxes, &status))
    {
        if (errorMsg)
        {
            char statusStr[FLEN_STATUS];
            fits_get_errstatus(status, statusStr);
            *errorMsg = QCoreApplication::translate(
                "FitsLoader", "CFITSIO Param Error %1: %2").arg(status).arg(statusStr);
        }
        status = 0;
        fits_close_file(fptr, &status);
        return false;
    }

    if (naxis < 2)
    {
        if (errorMsg)
            *errorMsg = QCoreApplication::translate(
                "FitsLoader", "Image has < 2 dimensions (NAXIS=%1)").arg(naxis);
        fits_close_file(fptr, &status);
        return false;
    }

    // For 3-D FITS (e.g. RGB), read all planes; 2-D FITS is treated as mono.
    const int  nChannels       = (naxis >= 3) ? static_cast<int>(naxes[2]) : 1;
    const int  width           = static_cast<int>(naxes[0]);
    const int  height          = static_cast<int>(naxes[1]);
    const long npixelsPerPlane = static_cast<long>(width) * height;

    buffer.resize(width, height, nChannels);
    std::vector<float>& allPixels = buffer.data();

    float nulval = 0.0f;
    int   anynul = 0;

    // FITS image planes are stored sequentially; read into a temporary planar
    // buffer and interleave into the ImageBuffer layout.
    std::vector<float> planePixels(static_cast<size_t>(npixelsPerPlane));

    for (int c = 0; c < nChannels; ++c)
    {
        // CFITSIO pixel coordinates are 1-based.
        long firstpix[3] = {1, 1, static_cast<long>(c + 1)};

        if (fits_read_pix(fptr, TFLOAT, firstpix, npixelsPerPlane,
                          &nulval, planePixels.data(), &anynul, &status))
        {
            if (errorMsg)
            {
                char statusStr[FLEN_STATUS];
                fits_get_errstatus(status, statusStr);
                *errorMsg = QCoreApplication::translate(
                    "FitsLoader", "CFITSIO Read Error (Plane %1) %2: %3")
                    .arg(c).arg(status).arg(statusStr);
            }
            fits_close_file(fptr, &status);
            return false;
        }

        // Interleave the planar data into the output buffer.
#ifdef _OPENMP
#        pragma omp parallel for
#endif
        for (long i = 0; i < npixelsPerPlane; ++i)
            allPixels[i * nChannels + c] = planePixels[static_cast<size_t>(i)];
    }

    // Compute the global pixel range for normalisation heuristics.
    float globalMax = -1e30f;
    float globalMin =  1e30f;

#ifdef _OPENMP
#    pragma omp parallel for reduction(max : globalMax) reduction(min : globalMin)
#endif
    for (size_t i = 0; i < allPixels.size(); ++i)
    {
        if (allPixels[i] > globalMax) globalMax = allPixels[i];
        if (allPixels[i] < globalMin) globalMin = allPixels[i];
    }

    bool  doScale = false;
    const float divisor = selectNormalisationDivisor(bitpix, globalMax, doScale);

    if (doScale && divisor > 0.0f)
    {
        const float invDiv = 1.0f / divisor;
#ifdef _OPENMP
#        pragma omp parallel for
#endif
        for (size_t i = 0; i < allPixels.size(); ++i)
            allPixels[i] *= invDiv;
    }

    // Populate metadata from the FITS header.
    ImageBuffer::Metadata meta;
    readCommonMetadata(fptr, meta);
    meta.filePath = filePath;

    // Attempt to extract an embedded ICC colour profile.
    if (!filePath.isEmpty())
        IccProfileExtractor::extractFromFile(filePath, meta.iccData);

    buffer.setMetadata(meta);

    status = 0;
    fits_close_file(fptr, &status);
    return true;
}


// =============================================================================
// Metadata-only load
// =============================================================================

bool FitsLoader::loadMetadata(const QString& filePath, ImageBuffer& buffer, QString* errorMsg)
{
    fitsfile* fptr  = nullptr;
    int       status = 0;

    if (fits_open_file(&fptr, filePath.toUtf8().constData(), READONLY, &status))
    {
        if (errorMsg)
        {
            char statusStr[FLEN_STATUS];
            fits_get_errstatus(status, statusStr);
            *errorMsg = QCoreApplication::translate(
                "FitsLoader", "CFITSIO Open Error %1: %2").arg(status).arg(statusStr);
        }
        return false;
    }

    int  bitpix = 0;
    int  naxis  = 0;
    long naxes[9] = {1, 1, 1, 1, 1, 1, 1, 1, 1};

    if (fits_get_img_param(fptr, 9, &bitpix, &naxis, naxes, &status))
    {
        fits_close_file(fptr, &status);
        return false;
    }

    ImageBuffer::Metadata meta;
    readCommonMetadata(fptr, meta);
    meta.filePath = filePath;

    fits_close_file(fptr, &status);

    const int nChannels = (naxis >= 3) ? static_cast<int>(naxes[2]) : 1;

    // Allocate dimensions without filling pixel data.
    if (buffer.width() == 0 || buffer.height() == 0)
        buffer.resize(static_cast<int>(naxes[0]),
                       static_cast<int>(naxes[1]),
                       nChannels);

    // Attempt ICC profile extraction.
    if (IccProfileExtractor::extractFromFile(filePath, meta.iccData))
    {
        core::ColorProfile profile(meta.iccData);
        if (profile.isValid())
        {
            meta.iccProfileName = profile.name();
            meta.iccProfileType = static_cast<int>(profile.type());
        }
    }

    buffer.setMetadata(meta);
    return true;
}


// =============================================================================
// Extension enumeration
// =============================================================================

QMap<QString, FitsExtensionInfo>
FitsLoader::listExtensions(const QString& filePath, QString* errorMsg)
{
    QMap<QString, FitsExtensionInfo> result;

    fitsfile* fptr  = nullptr;
    int       status = 0;

    if (fits_open_file(&fptr, filePath.toUtf8().constData(), READONLY, &status))
    {
        if (errorMsg)
        {
            char statusStr[FLEN_STATUS];
            fits_get_errstatus(status, statusStr);
            *errorMsg = QCoreApplication::translate(
                "FitsLoader", "CFITSIO Open Error: %1").arg(statusStr);
        }
        return result;
    }

    int numHdus = 0;
    fits_get_num_hdus(fptr, &numHdus, &status);

    for (int i = 1; i <= numHdus; ++i)  // CFITSIO HDU indices are 1-based.
    {
        if (fits_movabs_hdu(fptr, i, nullptr, &status))
        {
            status = 0;
            continue;
        }

        int hduType = 0;
        fits_get_hdu_type(fptr, &hduType, &status);

        if (hduType != IMAGE_HDU)
        {
            status = 0;
            continue;
        }

        int  bitpix = 0;
        int  naxis  = 0;
        long naxes[9] = {1, 1, 1, 1, 1, 1, 1, 1, 1};

        if (fits_get_img_param(fptr, 9, &bitpix, &naxis, naxes, &status))
        {
            status = 0;
            continue;
        }

        if (naxis < 2)
        {
            status = 0;
            continue;
        }

        // Read the optional extension name.
        char extname[FLEN_VALUE] = "";
        status = 0;
        fits_read_key(fptr, TSTRING, "EXTNAME", extname, nullptr, &status);
        status = 0;

        FitsExtensionInfo info;
        info.index    = i - 1;  // Expose as 0-based to callers.
        info.name     = (strlen(extname) > 0)
                            ? QString::fromUtf8(extname)
                            : QString::number(i - 1);
        info.width    = static_cast<int>(naxes[0]);
        info.height   = static_cast<int>(naxes[1]);
        info.channels = (naxis >= 3) ? static_cast<int>(naxes[2]) : 1;
        info.bitpix   = bitpix;

        switch (bitpix)
        {
            case   8: info.dtype = "uint8";   break;
            case  16: info.dtype = "int16";   break;
            case  32: info.dtype = "int32";   break;
            case -32: info.dtype = "float32"; break;
            case -64: info.dtype = "float64"; break;
            default:  info.dtype = QString("bitpix%1").arg(bitpix); break;
        }

        result[info.name.toUpper()] = info;
    }

    fits_close_file(fptr, &status);
    return result;
}


// =============================================================================
// Extension load by key
// =============================================================================

bool FitsLoader::loadExtension(const QString& filePath,
                                const QString& extensionKey,
                                ImageBuffer&   buffer,
                                QString*       errorMsg)
{
    const QMap<QString, FitsExtensionInfo> exts = listExtensions(filePath, errorMsg);

    if (exts.isEmpty())
    {
        if (errorMsg && errorMsg->isEmpty())
            *errorMsg = QCoreApplication::translate(
                "FitsLoader", "No image extensions found in file");
        return false;
    }

    // Try case-insensitive name lookup first.
    const QString upperKey = extensionKey.toUpper();
    if (exts.contains(upperKey))
        return loadExtension(filePath, exts[upperKey].index, buffer, errorMsg);

    // Fall back to treating the key as a numeric index.
    bool ok = false;
    const int idx = extensionKey.toInt(&ok);
    if (ok)
        return loadExtension(filePath, idx, buffer, errorMsg);

    if (errorMsg)
        *errorMsg = QCoreApplication::translate(
            "FitsLoader", "Extension '%1' not found").arg(extensionKey);
    return false;
}


// =============================================================================
// Extension load by integer index
// =============================================================================

bool FitsLoader::loadExtension(const QString& filePath,
                                int            hduIndex,
                                ImageBuffer&   buffer,
                                QString*       errorMsg)
{
    fitsfile* fptr  = nullptr;
    int       status = 0;

    if (fits_open_file(&fptr, filePath.toUtf8().constData(), READONLY, &status))
    {
        if (errorMsg)
        {
            char statusStr[FLEN_STATUS];
            fits_get_errstatus(status, statusStr);
            *errorMsg = QCoreApplication::translate(
                "FitsLoader", "CFITSIO Open Error: %1").arg(statusStr);
        }
        return false;
    }

    // Navigate to the requested HDU (convert to 1-based CFITSIO index).
    int hduType = 0;
    if (fits_movabs_hdu(fptr, hduIndex + 1, &hduType, &status))
    {
        if (errorMsg)
        {
            char statusStr[FLEN_STATUS];
            fits_get_errstatus(status, statusStr);
            *errorMsg = QCoreApplication::translate(
                "FitsLoader", "Failed to move to HDU %1: %2")
                .arg(hduIndex).arg(statusStr);
        }
        fits_close_file(fptr, &status);
        return false;
    }

    if (hduType != IMAGE_HDU)
    {
        if (errorMsg)
            *errorMsg = QCoreApplication::translate(
                "FitsLoader", "HDU %1 is not an image extension").arg(hduIndex);
        fits_close_file(fptr, &status);
        return false;
    }

    const bool result = loadHDU(fptr, hduIndex, buffer, errorMsg, filePath);

    status = 0;
    fits_close_file(fptr, &status);
    return result;
}


// =============================================================================
// Region load
// =============================================================================

bool FitsLoader::loadRegion(const QString& filePath,
                             ImageBuffer&   buffer,
                             int x, int y, int w, int h,
                             QString*       errorMsg)
{
    fitsfile* fptr  = nullptr;
    int       status = 0;

    if (fits_open_file(&fptr, filePath.toUtf8().constData(), READONLY, &status))
    {
        if (errorMsg)
        {
            char statusStr[FLEN_STATUS];
            fits_get_errstatus(status, statusStr);
            *errorMsg = QCoreApplication::translate(
                "FitsLoader", "CFITSIO Open Error %1: %2\nPath: %3")
                .arg(status).arg(statusStr).arg(filePath);
        }
        return false;
    }

    const bool res = loadHDU(fptr, 0, buffer, errorMsg, filePath, x, y, w, h);

    status = 0;
    fits_close_file(fptr, &status);
    return res;
}


// =============================================================================
// Core HDU loading routine
// =============================================================================

bool FitsLoader::loadHDU(void*          fitsptr,
                          [[maybe_unused]] int hduIndex,
                          ImageBuffer&   buffer,
                          QString*       errorMsg,
                          const QString& filePath,
                          int rx, int ry, int rw, int rh)
{
    fitsfile* fptr  = static_cast<fitsfile*>(fitsptr);
    int       status = 0;

    int  bitpix = 0;
    int  naxis  = 0;
    long naxes[9] = {1, 1, 1, 1, 1, 1, 1, 1, 1};

    if (fits_get_img_param(fptr, 9, &bitpix, &naxis, naxes, &status))
    {
        if (errorMsg)
        {
            char statusStr[FLEN_STATUS];
            fits_get_errstatus(status, statusStr);
            *errorMsg = QCoreApplication::translate(
                "FitsLoader", "CFITSIO Param Error: %1").arg(statusStr);
        }
        return false;
    }

    if (naxis < 2)
    {
        if (errorMsg)
            *errorMsg = QCoreApplication::translate(
                "FitsLoader", "Image has < 2 dimensions");
        return false;
    }

    const int nChannels = (naxis == 3 && naxes[2] >= 3) ? 3 : 1;
    const int imgWidth  = static_cast<int>(naxes[0]);
    const int imgHeight = static_cast<int>(naxes[1]);

    // Determine the effective read region, clamped to image bounds.
    int x1     = rx;
    int y1     = ry;
    int width  = (rw > 0) ? rw : imgWidth;
    int height = (rh > 0) ? rh : imgHeight;

    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x1 + width  > imgWidth)  width  = imgWidth  - x1;
    if (y1 + height > imgHeight) height = imgHeight - y1;

    if (width <= 0 || height <= 0)
    {
        if (errorMsg)
            *errorMsg = QCoreApplication::translate("FitsLoader", "Invalid read region");
        return false;
    }

    const long npixelsPerPlane = static_cast<long>(width) * height;

    std::vector<float> allPixels(static_cast<size_t>(npixelsPerPlane) * nChannels);
    float nulval = 0.0f;
    int   anynul = 0;

    for (int c = 0; c < nChannels; ++c)
    {
        // fits_read_subset uses 1-based pixel coordinates.
        long fpixel[3] = { static_cast<long>(x1 + 1),
                           static_cast<long>(y1 + 1),
                           static_cast<long>(c  + 1) };
        long lpixel[3] = { static_cast<long>(x1 + width),
                           static_cast<long>(y1 + height),
                           static_cast<long>(c  + 1) };
        long inc[3]    = { 1, 1, 1 };

        std::vector<float> planePixels(static_cast<size_t>(npixelsPerPlane));

        if (fits_read_subset(fptr, TFLOAT, fpixel, lpixel, inc,
                             &nulval, planePixels.data(), &anynul, &status))
        {
            if (errorMsg)
            {
                char statusStr[FLEN_STATUS];
                fits_get_errstatus(status, statusStr);
                *errorMsg = QCoreApplication::translate(
                    "FitsLoader", "CFITSIO Read Error: %1").arg(statusStr);
            }
            return false;
        }

        // Interleave the planar data into the multi-channel output buffer.
#ifdef _OPENMP
#        pragma omp parallel for
#endif
        for (long i = 0; i < npixelsPerPlane; ++i)
            allPixels[static_cast<size_t>(i) * nChannels + c] =
                planePixels[static_cast<size_t>(i)];
    }

    // Compute range statistics for normalisation.
    float globalMax = -1e30f;
    float globalMin =  1e30f;
    const size_t pixelCount = allPixels.size();

#ifdef _OPENMP
#    pragma omp parallel for reduction(max : globalMax) reduction(min : globalMin)
#endif
    for (size_t i = 0; i < pixelCount; ++i)
    {
        if (allPixels[i] > globalMax) globalMax = allPixels[i];
        if (allPixels[i] < globalMin) globalMin = allPixels[i];
    }

    bool  doScale = false;
    const float divisor = selectNormalisationDivisor(bitpix, globalMax, doScale);

    if (doScale && divisor > 0.0f)
    {
        const float invDiv = 1.0f / divisor;
#ifdef _OPENMP
#        pragma omp parallel for
#endif
        for (size_t i = 0; i < pixelCount; ++i)
            allPixels[i] *= invDiv;
    }

    // Populate metadata.
    ImageBuffer::Metadata meta;
    readCommonMetadata(fptr, meta);

    // Store the extension name in objectName when EXTNAME is present.
    char   extname[FLEN_VALUE] = "";
    int    status_meta = 0;
    fits_read_key(fptr, TSTRING, "EXTNAME", extname, nullptr, &status_meta);
    if (strlen(extname) > 0 && meta.objectName.isEmpty())
        meta.objectName = QString::fromUtf8(extname);

    // Attempt ICC profile extraction.
    if (IccProfileExtractor::extractFromFile(filePath, meta.iccData))
    {
        core::ColorProfile profile(meta.iccData);
        if (profile.isValid())
        {
            meta.iccProfileName = profile.name();
            meta.iccProfileType = static_cast<int>(profile.type());
        }
    }

    buffer.setMetadata(meta);
    buffer.setData(width, height, nChannels, allPixels);
    return true;
}


// =============================================================================
// Coordinate string parsers
// =============================================================================

double FitsLoader::parseRAString(const QString& str, bool* ok)
{
    if (ok) *ok = false;

    const QString trimmed = str.trimmed();

    // Attempt decimal degrees.
    bool parseOk = false;
    const double val = trimmed.toDouble(&parseOk);
    if (parseOk)
    {
        if (ok) *ok = true;
        return val;
    }

    // Attempt sexagesimal HMS: "HH MM SS.ss" or "HH:MM:SS.ss".
    const QStringList parts = trimmed.split(QRegularExpression("[:\\s]+"));
    if (parts.size() >= 3)
    {
        const double h = parts[0].toDouble();
        const double m = parts[1].toDouble();
        const double s = parts[2].toDouble();
        if (ok) *ok = true;
        return (h + m / 60.0 + s / 3600.0) * 15.0;  // Hours to degrees.
    }

    return 0.0;
}

double FitsLoader::parseDecString(const QString& str, bool* ok)
{
    if (ok) *ok = false;

    const QString trimmed = str.trimmed();

    // Attempt decimal degrees.
    bool parseOk = false;
    const double val = trimmed.toDouble(&parseOk);
    if (parseOk)
    {
        if (ok) *ok = true;
        return val;
    }

    // Attempt sexagesimal DMS: "DD MM SS.ss" or "DD:MM:SS.ss".
    const QStringList parts = trimmed.split(QRegularExpression("[:\\s]+"));
    if (parts.size() >= 3)
    {
        const QString dStr  = parts[0];
        const double  d     = std::abs(dStr.toDouble());
        const double  m     = parts[1].toDouble();
        const double  s     = parts[2].toDouble();
        const double  sign  = dStr.startsWith('-') ? -1.0 : 1.0;
        if (ok) *ok = true;
        return sign * (d + m / 60.0 + s / 3600.0);
    }

    return 0.0;
}


// =============================================================================
// SIP distortion coefficient extraction
// =============================================================================

void FitsLoader::readSIPCoefficients(void* fitsptr, ImageBuffer::Metadata& meta)
{
    fitsfile* fptr  = static_cast<fitsfile*>(fitsptr);
    int       status = 0;
    char      comment[FLEN_COMMENT];
    long      lval  = 0;
    double    dv    = 0.0;

    // Read polynomial orders.
    if (!fits_read_key(fptr, TLONG, "A_ORDER",  &lval, comment, &status))
        meta.sipOrderA  = static_cast<int>(lval);
    status = 0;
    if (!fits_read_key(fptr, TLONG, "B_ORDER",  &lval, comment, &status))
        meta.sipOrderB  = static_cast<int>(lval);
    status = 0;
    if (!fits_read_key(fptr, TLONG, "AP_ORDER", &lval, comment, &status))
        meta.sipOrderAP = static_cast<int>(lval);
    status = 0;
    if (!fits_read_key(fptr, TLONG, "BP_ORDER", &lval, comment, &status))
        meta.sipOrderBP = static_cast<int>(lval);
    status = 0;

    // Read forward SIP coefficients (A_i_j and B_i_j).
    const int maxForwardOrder = std::max(meta.sipOrderA, meta.sipOrderB);
    for (int i = 0; i <= maxForwardOrder; ++i)
    {
        for (int j = 0; j <= maxForwardOrder - i; ++j)
        {
            if (i == 0 && j == 0)
                continue;

            if (i + j <= meta.sipOrderA)
            {
                const QString key = QString("A_%1_%2").arg(i).arg(j);
                if (!fits_read_key(fptr, TDOUBLE,
                                   key.toLatin1().constData(), &dv, comment, &status))
                    meta.sipCoeffs[key] = dv;
                status = 0;
            }

            if (i + j <= meta.sipOrderB)
            {
                const QString key = QString("B_%1_%2").arg(i).arg(j);
                if (!fits_read_key(fptr, TDOUBLE,
                                   key.toLatin1().constData(), &dv, comment, &status))
                    meta.sipCoeffs[key] = dv;
                status = 0;
            }
        }
    }

    // Read inverse SIP coefficients (AP_i_j and BP_i_j).
    const int maxInverseOrder = std::max(meta.sipOrderAP, meta.sipOrderBP);
    for (int i = 0; i <= maxInverseOrder; ++i)
    {
        for (int j = 0; j <= maxInverseOrder - i; ++j)
        {
            if (i == 0 && j == 0)
                continue;

            if (i + j <= meta.sipOrderAP)
            {
                const QString key = QString("AP_%1_%2").arg(i).arg(j);
                if (!fits_read_key(fptr, TDOUBLE,
                                   key.toLatin1().constData(), &dv, comment, &status))
                    meta.sipCoeffs[key] = dv;
                status = 0;
            }

            if (i + j <= meta.sipOrderBP)
            {
                const QString key = QString("BP_%1_%2").arg(i).arg(j);
                if (!fits_read_key(fptr, TDOUBLE,
                                   key.toLatin1().constData(), &dv, comment, &status))
                    meta.sipCoeffs[key] = dv;
                status = 0;
            }
        }
    }
}


// =============================================================================
// Common metadata extraction
// =============================================================================

void FitsLoader::readCommonMetadata(void* fitsptr, ImageBuffer::Metadata& meta)
{
    fitsfile* fptr       = static_cast<fitsfile*>(fitsptr);
    char      comment[FLEN_COMMENT];
    char      strVal[FLEN_VALUE];
    double    dv         = 0.0;
    int       status_meta = 0;

    // ---- Instrument parameters ------------------------------------------------

    if (!fits_read_key(fptr, TDOUBLE, "FOCALLEN", &dv, comment, &status_meta))
        meta.focalLength = dv;
    else
    {
        status_meta = 0;
        if (!fits_read_key(fptr, TDOUBLE, "FOCAL", &dv, comment, &status_meta))
            meta.focalLength = dv;
    }
    status_meta = 0;

    if (!fits_read_key(fptr, TDOUBLE, "PIXSIZE", &dv, comment, &status_meta))
        meta.pixelSize = dv;
    else
    {
        status_meta = 0;
        if (!fits_read_key(fptr, TDOUBLE, "XPIXSZ", &dv, comment, &status_meta))
            meta.pixelSize = dv;
    }
    status_meta = 0;

    // ---- Object coordinates ---------------------------------------------------

    if (!fits_read_key(fptr, TDOUBLE, "RA", &dv, comment, &status_meta))
    {
        meta.ra = dv;
    }
    else
    {
        status_meta = 0;
        if (!fits_read_key(fptr, TSTRING, "OBJCTRA", strVal, comment, &status_meta))
            meta.ra = parseRAString(strVal);
    }
    status_meta = 0;

    if (!fits_read_key(fptr, TDOUBLE, "DEC", &dv, comment, &status_meta))
    {
        meta.dec = dv;
    }
    else
    {
        status_meta = 0;
        if (!fits_read_key(fptr, TSTRING, "OBJCTDEC", strVal, comment, &status_meta))
            meta.dec = parseDecString(strVal);
    }
    status_meta = 0;

    // ---- WCS reference values -------------------------------------------------

    if (!fits_read_key(fptr, TDOUBLE, "CRVAL1", &dv, comment, &status_meta)) meta.ra     = dv;
    status_meta = 0;
    if (!fits_read_key(fptr, TDOUBLE, "CRVAL2", &dv, comment, &status_meta)) meta.dec    = dv;
    status_meta = 0;
    if (!fits_read_key(fptr, TDOUBLE, "CRPIX1", &dv, comment, &status_meta)) meta.crpix1 = dv;
    status_meta = 0;
    if (!fits_read_key(fptr, TDOUBLE, "CRPIX2", &dv, comment, &status_meta)) meta.crpix2 = dv;
    status_meta = 0;

    // ---- CD transformation matrix --------------------------------------------

    double cd11 = 0.0, cd12 = 0.0, cd21 = 0.0, cd22 = 0.0;
    bool   hasCD = false;

    if (!fits_read_key(fptr, TDOUBLE, "CD1_1", &dv, comment, &status_meta)) { cd11 = dv; hasCD = true; }
    status_meta = 0;
    if (!fits_read_key(fptr, TDOUBLE, "CD1_2", &dv, comment, &status_meta)) { cd12 = dv; hasCD = true; }
    status_meta = 0;
    if (!fits_read_key(fptr, TDOUBLE, "CD2_1", &dv, comment, &status_meta)) { cd21 = dv; hasCD = true; }
    status_meta = 0;
    if (!fits_read_key(fptr, TDOUBLE, "CD2_2", &dv, comment, &status_meta)) { cd22 = dv; hasCD = true; }
    status_meta = 0;

    // If no CD matrix is present, try CDELT + PC matrix or CROTA2 fallback.
    if (!hasCD)
    {
        double cdelt1 = 1.0, cdelt2 = 1.0;
        if (!fits_read_key(fptr, TDOUBLE, "CDELT1", &dv, comment, &status_meta)) cdelt1 = dv;
        status_meta = 0;
        if (!fits_read_key(fptr, TDOUBLE, "CDELT2", &dv, comment, &status_meta)) cdelt2 = dv;
        status_meta = 0;

        double pc11 = 1.0, pc12 = 0.0, pc21 = 0.0, pc22 = 1.0;
        bool   hasPC = false;

        if (!fits_read_key(fptr, TDOUBLE, "PC1_1", &dv, comment, &status_meta)) { pc11 = dv; hasPC = true; }
        status_meta = 0;
        if (!fits_read_key(fptr, TDOUBLE, "PC1_2", &dv, comment, &status_meta)) { pc12 = dv; hasPC = true; }
        status_meta = 0;
        if (!fits_read_key(fptr, TDOUBLE, "PC2_1", &dv, comment, &status_meta)) { pc21 = dv; hasPC = true; }
        status_meta = 0;
        if (!fits_read_key(fptr, TDOUBLE, "PC2_2", &dv, comment, &status_meta)) { pc22 = dv; hasPC = true; }
        status_meta = 0;

        if (hasPC)
        {
            // Combine PCi_j with CDELTi to produce the CD matrix.
            cd11 = pc11 * cdelt1;
            cd12 = pc12 * cdelt1;
            cd21 = pc21 * cdelt2;
            cd22 = pc22 * cdelt2;
        }
        else
        {
            // Legacy CROTA2 rotation keyword.
            if (!fits_read_key(fptr, TDOUBLE, "CROTA2", &dv, comment, &status_meta))
            {
                const double crota2 = dv * M_PI / 180.0;
                cd11 = cdelt1 * std::cos(crota2);
                cd12 = -cdelt2 * std::sin(crota2);
                cd21 = cdelt1 * std::sin(crota2);
                cd22 = cdelt2 * std::cos(crota2);
            }
            else
            {
                // No rotation: diagonal scale matrix.
                cd11 = cdelt1;
                cd22 = cdelt2;
            }
            status_meta = 0;
        }
    }

    meta.cd1_1 = cd11;
    meta.cd1_2 = cd12;
    meta.cd2_1 = cd21;
    meta.cd2_2 = cd22;

    // ---- Projection type and equinox -----------------------------------------

    if (!fits_read_key(fptr, TSTRING, "CTYPE1", strVal, comment, &status_meta))
        meta.ctype1 = QString::fromUtf8(strVal).trimmed().remove('\'');
    status_meta = 0;

    if (!fits_read_key(fptr, TSTRING, "CTYPE2", strVal, comment, &status_meta))
        meta.ctype2 = QString::fromUtf8(strVal).trimmed().remove('\'');
    status_meta = 0;

    if (!fits_read_key(fptr, TDOUBLE, "EQUINOX", &dv, comment, &status_meta))
        meta.equinox = dv;
    status_meta = 0;

    // ---- SIP distortion coefficients -----------------------------------------

    readSIPCoefficients(fptr, meta);

    // ---- Observational keywords ----------------------------------------------

    if (!fits_read_key(fptr, TSTRING, "OBJECT",   strVal, comment, &status_meta))
        meta.objectName = QString::fromUtf8(strVal);
    status_meta = 0;

    if (!fits_read_key(fptr, TSTRING, "DATE-OBS", strVal, comment, &status_meta))
        meta.dateObs = QString::fromUtf8(strVal);
    status_meta = 0;

    // Exposure time: try EXPTIME, then EXPOSURE, then EXP.
    if (!fits_read_key(fptr, TSTRING, "EXPTIME", strVal, comment, &status_meta))
    {
        meta.exposure = QString::fromUtf8(strVal).toDouble();
    }
    else
    {
        status_meta = 0;
        if (!fits_read_key(fptr, TDOUBLE, "EXPOSURE", &dv, comment, &status_meta))
            meta.exposure = dv;
        else
        {
            status_meta = 0;
            if (!fits_read_key(fptr, TDOUBLE, "EXP", &dv, comment, &status_meta))
                meta.exposure = dv;
        }
    }
    status_meta = 0;

    // ---- Bayer pattern (critical for correct colour demosaicing) -------------

    if (!fits_read_key(fptr, TSTRING, "BAYERPAT", strVal, comment, &status_meta))
        meta.bayerPattern = QString::fromUtf8(strVal).trimmed().remove('\'');
    status_meta = 0;

    if (meta.bayerPattern.isEmpty())
    {
        if (!fits_read_key(fptr, TSTRING, "COLORTYP", strVal, comment, &status_meta))
            meta.bayerPattern = QString::fromUtf8(strVal).trimmed().remove('\'');
        status_meta = 0;
    }

    // ---- Full header dump ----------------------------------------------------

    int nkeys    = 0;
    int morekeys = 0;

    if (fits_get_hdrspace(fptr, &nkeys, &morekeys, &status_meta) == 0)
    {
        for (int i = 1; i <= nkeys; ++i)
        {
            char keyname[FLEN_KEYWORD];
            char value[FLEN_VALUE];
            char comm[FLEN_COMMENT];

            if (fits_read_keyn(fptr, i, keyname, value, comm, &status_meta) == 0)
            {
                meta.rawHeaders.push_back(
                    {QString(keyname), QString(value), QString(comm)});
            }
            else
            {
                // Fall back to reading the raw 80-character card string.
                status_meta = 0;
                char card[FLEN_CARD];
                if (fits_read_record(fptr, i, card, &status_meta) == 0)
                {
                    meta.rawHeaders.push_back(
                        {"RAW", QString::fromUtf8(card, 80).trimmed(), ""});
                }
                status_meta = 0;
            }
        }
    }

    // Deduplicate rawHeaders, keeping only the first occurrence of each keyword.
    // This prevents duplicate entries from polluting the metadata panel.
    std::set<QString>                              seenKeys;
    std::vector<ImageBuffer::Metadata::HeaderCard> deduped;

    for (const auto& card : meta.rawHeaders)
    {
        const QString key = card.key.trimmed().toUpper();
        if (seenKeys.find(key) == seenKeys.end())
        {
            seenKeys.insert(key);
            deduped.push_back(card);
        }
    }

    meta.rawHeaders = deduped;
}