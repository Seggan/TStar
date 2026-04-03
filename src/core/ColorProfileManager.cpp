// ============================================================================
// ColorProfileManager.cpp
// ICC color profile management and LittleCMS-based color space conversion.
// ============================================================================

#include "ColorProfileManager.h"
#include "ImageBuffer.h"
#include "core/Logger.h"
#include "core/Task.h"
#include "core/TaskManager.h"

#include <lcms2.h>

#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QCoreApplication>
#include <QDebug>
#include <QSettings>

namespace core {

// ============================================================================
// ColorProfile Implementation
// ============================================================================

ColorProfile::ColorProfile()
    : m_type(StandardProfile::sRGB)
    , m_valid(false)
{
}

ColorProfile::ColorProfile(StandardProfile type)
{
    loadStandardProfile(type);
}

ColorProfile::ColorProfile(const QString& iccFilePath)
    : m_type(StandardProfile::Custom)
    , m_valid(false)
{
    QFile file(iccFilePath);
    if (!file.open(QIODevice::ReadOnly)) return;

    m_iccData = file.readAll();
    m_valid   = !m_iccData.isEmpty();

    if (!m_valid) return;

    // Attempt to read the internal profile description name
    cmsHPROFILE hProfile = cmsOpenProfileFromMem(
        m_iccData.constData(), m_iccData.size());

    if (hProfile) {
        char name[256];
        if (cmsGetProfileInfoASCII(hProfile, cmsInfoDescription,
                                   "en", "US", name, 256) > 0) {
            m_name = QString::fromLatin1(name);
        } else if (cmsGetProfileInfoASCII(hProfile, cmsInfoDescription,
                                          cmsNoLanguage, cmsNoCountry,
                                          name, 256) > 0) {
            m_name = QString::fromLatin1(name);
        } else {
            m_name = QFileInfo(iccFilePath).baseName();
        }
        cmsCloseProfile(hProfile);
    } else {
        m_name = QFileInfo(iccFilePath).baseName();
    }
}

ColorProfile::ColorProfile(const QByteArray& iccData)
    : m_type(StandardProfile::Custom)
    , m_iccData(iccData)
    , m_valid(!iccData.isEmpty())
{
    if (m_valid) {
        cmsHPROFILE hProfile = cmsOpenProfileFromMem(
            m_iccData.constData(), m_iccData.size());

        if (hProfile) {
            char name[256];
            if (cmsGetProfileInfoASCII(hProfile, cmsInfoDescription,
                                       "en", "US", name, 256) > 0) {
                m_name = QString::fromLatin1(name);
            } else if (cmsGetProfileInfoASCII(hProfile, cmsInfoDescription,
                                              cmsNoLanguage, cmsNoCountry,
                                              name, 256) > 0) {
                m_name = QString::fromLatin1(name);
            } else {
                m_name = QObject::tr("Unknown");
            }
            cmsCloseProfile(hProfile);
        } else {
            m_name  = QObject::tr("Invalid ICC Profile");
            m_valid = false;
        }
    } else {
        m_name = QObject::tr("No Embedded Profile");
    }
}

bool            ColorProfile::isValid()  const { return m_valid; }
StandardProfile ColorProfile::type()     const { return m_type; }
QString         ColorProfile::name()     const { return m_name; }
QByteArray      ColorProfile::iccData()  const { return m_iccData; }

bool ColorProfile::operator==(const ColorProfile& other) const
{
    if (m_type != other.m_type) return false;
    if (m_type == StandardProfile::Custom) {
        return m_iccData == other.m_iccData;
    }
    return true;
}

bool ColorProfile::operator!=(const ColorProfile& other) const
{
    return !(*this == other);
}

void ColorProfile::loadStandardProfile(StandardProfile type)
{
    m_type  = type;
    m_valid = true;

    switch (type) {
        case StandardProfile::sRGB:       m_name = "sRGB IEC61966-2.1"; break;
        case StandardProfile::AdobeRGB:   m_name = "Adobe RGB (1998)";  break;
        case StandardProfile::ProPhotoRGB: m_name = "ProPhoto RGB";     break;
        case StandardProfile::LinearRGB:  m_name = "Linear RGB";        break;
        default:
            m_name  = QObject::tr("Unknown Profile");
            m_valid = false;
            break;
    }
}

// ============================================================================
// LittleCMS Profile Creation Helpers
// ============================================================================

/** Create an Adobe RGB (1998) profile using its standard primaries and gamma. */
static cmsHPROFILE createAdobeRGBProfile()
{
    cmsCIExyY whitePoint = {0.3127, 0.3290, 1.0};
    cmsCIExyYTRIPLE primaries = {
        {0.6400, 0.3300, 1.0},   // Red
        {0.2100, 0.7100, 1.0},   // Green
        {0.1500, 0.0600, 1.0}    // Blue
    };
    cmsToneCurve* gamma    = cmsBuildGamma(NULL, 2.2);
    cmsToneCurve* curves[] = {gamma, gamma, gamma};
    cmsHPROFILE h = cmsCreateRGBProfile(&whitePoint, &primaries, curves);
    cmsFreeToneCurve(gamma);
    return h;
}

/** Create a ProPhoto RGB profile using its standard primaries and gamma. */
static cmsHPROFILE createProPhotoRGBProfile()
{
    cmsCIExyY whitePoint = {0.3457, 0.3585, 1.0};
    cmsCIExyYTRIPLE primaries = {
        {0.7347, 0.2653, 1.0},
        {0.1596, 0.8404, 1.0},
        {0.0366, 0.0001, 1.0}
    };
    cmsToneCurve* gamma    = cmsBuildGamma(NULL, 1.8);
    cmsToneCurve* curves[] = {gamma, gamma, gamma};
    cmsHPROFILE h = cmsCreateRGBProfile(&whitePoint, &primaries, curves);
    cmsFreeToneCurve(gamma);
    return h;
}

/** Create a linear sRGB-primaries profile (gamma = 1.0). */
static cmsHPROFILE createLinearRGBProfile()
{
    cmsCIExyY whitePoint = {0.3127, 0.3290, 1.0};
    cmsCIExyYTRIPLE primaries = {
        {0.6400, 0.3300, 1.0},
        {0.3000, 0.6000, 1.0},
        {0.1500, 0.0600, 1.0}
    };
    cmsToneCurve* gamma    = cmsBuildGamma(NULL, 1.0);
    cmsToneCurve* curves[] = {gamma, gamma, gamma};
    cmsHPROFILE h = cmsCreateRGBProfile(&whitePoint, &primaries, curves);
    cmsFreeToneCurve(gamma);
    return h;
}

/**
 * @brief Obtain a LittleCMS profile handle for the given ColorProfile.
 *
 * For custom profiles, the handle is opened from in-memory ICC data.
 * For standard profiles, the handle is created programmatically.
 * Caller is responsible for calling cmsCloseProfile() on the result.
 */
static cmsHPROFILE getLcmsProfileHandle(const ColorProfile& profile)
{
    if (profile.type() == StandardProfile::Custom && !profile.iccData().isEmpty()) {
        return cmsOpenProfileFromMem(profile.iccData().constData(),
                                     profile.iccData().size());
    }

    switch (profile.type()) {
        case StandardProfile::sRGB:        return cmsCreate_sRGBProfile();
        case StandardProfile::AdobeRGB:    return createAdobeRGBProfile();
        case StandardProfile::ProPhotoRGB: return createProPhotoRGBProfile();
        case StandardProfile::LinearRGB:   return createLinearRGBProfile();
        default:                           return cmsCreate_sRGBProfile();
    }
}

// ============================================================================
// Asynchronous Conversion Task
// ============================================================================

/**
 * @brief Task subclass for asynchronous color profile conversion.
 *
 * Performs row-by-row LittleCMS transform with progress reporting
 * and cancellation support.
 */
class ColorProfileTask : public Threading::Task {
public:
    ColorProfileTask(ImageBuffer& buffer,
                     const ColorProfile& source,
                     const ColorProfile& target)
        : m_buffer(buffer)
        , m_source(source)
        , m_target(target)
    {
    }

    void execute() override
    {
        cmsHPROFILE hInProfile  = getLcmsProfileHandle(m_source);
        cmsHPROFILE hOutProfile = getLcmsProfileHandle(m_target);

        if (!hInProfile || !hOutProfile) {
            if (hInProfile)  cmsCloseProfile(hInProfile);
            if (hOutProfile) cmsCloseProfile(hOutProfile);
            throw std::runtime_error(
                "Failed to create color profiles for conversion.");
        }

        cmsHTRANSFORM hTransform = cmsCreateTransform(
            hInProfile,  TYPE_RGB_FLT,
            hOutProfile, TYPE_RGB_FLT,
            INTENT_PERCEPTUAL, 0);

        if (!hTransform) {
            cmsCloseProfile(hInProfile);
            cmsCloseProfile(hOutProfile);
            throw std::runtime_error("Failed to create color transform.");
        }

        const int height   = m_buffer.height();
        const int width    = m_buffer.width();
        const int channels = m_buffer.channels();

        for (int y = 0; y < height; ++y) {
            if (!shouldContinue()) break;

            float* rowPtr = m_buffer.data().data()
                          + static_cast<size_t>(y) * width * channels;
            cmsDoTransform(hTransform, rowPtr, rowPtr, width);

            if (y % 20 == 0) {
                reportProgress((y * 100) / height,
                               QObject::tr("Converting color profile..."));
            }
        }

        cmsDeleteTransform(hTransform);
        cmsCloseProfile(hInProfile);
        cmsCloseProfile(hOutProfile);

        // Update buffer metadata to reflect the new profile
        ImageBuffer::Metadata meta = m_buffer.metadata();
        meta.iccProfileName    = m_target.name();
        meta.iccProfileType    = static_cast<int>(m_target.type());
        meta.colorProfileHandled = true;

        if (m_target.type() == StandardProfile::Custom
            && !m_target.iccData().isEmpty()) {
            meta.iccData = m_target.iccData();
        } else {
            meta.iccData.clear();
        }

        m_buffer.setMetadata(meta);
        m_buffer.setModified(true);

        Logger::info(
            QObject::tr("Successfully converted buffer from %1 to %2")
                .arg(m_source.name()).arg(m_target.name()),
            "ColorManagement");
    }

private:
    ImageBuffer& m_buffer;
    ColorProfile m_source;
    ColorProfile m_target;
};

// ============================================================================
// ColorProfileManager Implementation
// ============================================================================

ColorProfileManager& ColorProfileManager::instance()
{
    static ColorProfileManager instance;
    return instance;
}

ColorProfileManager::ColorProfileManager()
{
    syncSettings();
}

ColorProfileManager::~ColorProfileManager()
{
}

// -- Workspace profile --------------------------------------------------------

void ColorProfileManager::setWorkspaceProfile(const ColorProfile& profile)
{
    QMutexLocker lock(&m_mutex);

    if (m_workspaceProfile != profile && profile.isValid()) {
        m_workspaceProfile = profile;
        lock.unlock();  // Unlock before emitting to avoid deadlocks

        emit workspaceProfileChanged(profile);
        qDebug() << "[ColorProfileManager]"
                 << tr("Workspace profile updated to:") << profile.name();
    }
}

ColorProfile ColorProfileManager::workspaceProfile() const
{
    QMutexLocker lock(&m_mutex);
    return m_workspaceProfile;
}

// -- Auto-conversion mode -----------------------------------------------------

void ColorProfileManager::setAutoConversionMode(AutoConversionMode mode)
{
    QMutexLocker lock(&m_mutex);
    m_autoConversionMode = mode;
    lock.unlock();

    qDebug() << "[ColorProfileManager] Auto-conversion mode set to:"
             << static_cast<int>(mode);
}

AutoConversionMode ColorProfileManager::autoConversionMode() const
{
    QMutexLocker lock(&m_mutex);
    return m_autoConversionMode;
}

// -- Settings synchronization -------------------------------------------------

void ColorProfileManager::syncSettings()
{
    QSettings settings;

    // Workspace profile
    QString profilePref = settings.value("color/workspace_profile", "sRGB").toString();
    StandardProfile type = StandardProfile::sRGB;

    if      (profilePref == "AdobeRGB")    type = StandardProfile::AdobeRGB;
    else if (profilePref == "ProPhotoRGB") type = StandardProfile::ProPhotoRGB;
    else if (profilePref == "LinearRGB")   type = StandardProfile::LinearRGB;

    setWorkspaceProfile(ColorProfile(type));

    // Auto-conversion mode
    QString modePref = settings.value("color/auto_conversion_mode", "Always").toString();
    AutoConversionMode mode = AutoConversionMode::Always;

    if      (modePref == "Never")  mode = AutoConversionMode::Never;
    else if (modePref == "Always") mode = AutoConversionMode::Always;
    else                           mode = AutoConversionMode::Ask;

    setAutoConversionMode(mode);

    qDebug() << "[ColorProfileManager] Settings synchronized: Profile="
             << m_workspaceProfile.name()
             << "Mode=" << static_cast<int>(m_autoConversionMode);
}

// -- Profile mismatch detection -----------------------------------------------

bool ColorProfileManager::isMismatch(const ColorProfile& imageProfile) const
{
    if (!imageProfile.isValid()) return false;

    ColorProfile workspace;
    {
        QMutexLocker lock(&m_mutex);
        workspace = m_workspaceProfile;
    }

    return (imageProfile != workspace);
}

// -- Synchronous conversion ---------------------------------------------------

bool ColorProfileManager::convertToWorkspace(ImageBuffer& buffer,
                                             const ColorProfile& sourceProfile)
{
    ColorProfile workspace;
    {
        QMutexLocker lock(&m_mutex);
        workspace = m_workspaceProfile;
    }
    return convertProfile(buffer, sourceProfile, workspace);
}

bool ColorProfileManager::convertProfile(ImageBuffer& buffer,
                                         const ColorProfile& sourceProfile,
                                         const ColorProfile& targetProfile)
{
    if (!sourceProfile.isValid() || !targetProfile.isValid()) {
        qWarning() << "[ColorProfileManager]"
                   << tr("Invalid profiles provided for conversion.");
        return false;
    }

    if (sourceProfile == targetProfile) {
        return true;  // No conversion needed
    }

    QMutexLocker lock(&m_mutex);

    cmsHPROFILE hInProfile  = getLcmsProfileHandle(sourceProfile);
    cmsHPROFILE hOutProfile = getLcmsProfileHandle(targetProfile);

    if (!hInProfile || !hOutProfile) {
        if (hInProfile)  cmsCloseProfile(hInProfile);
        if (hOutProfile) cmsCloseProfile(hOutProfile);
        return false;
    }

    // LittleCMS expects interleaved float RGB data (TYPE_RGB_FLT)
    cmsHTRANSFORM hTransform = cmsCreateTransform(
        hInProfile,  TYPE_RGB_FLT,
        hOutProfile, TYPE_RGB_FLT,
        INTENT_PERCEPTUAL, 0);

    if (hTransform) {
        #pragma omp parallel for
        for (int y = 0; y < buffer.height(); ++y) {
            float* rowPtr = buffer.data().data()
                          + static_cast<size_t>(y) * buffer.width() * buffer.channels();
            cmsDoTransform(hTransform, rowPtr, rowPtr, buffer.width());
        }
        cmsDeleteTransform(hTransform);
    }

    if (hInProfile)  cmsCloseProfile(hInProfile);
    if (hOutProfile) cmsCloseProfile(hOutProfile);

    qDebug() << "[ColorProfileManager]"
             << tr("Successfully converted buffer from")
             << sourceProfile.name() << tr("to") << targetProfile.name();

    // Update buffer metadata
    ImageBuffer::Metadata meta = buffer.metadata();
    meta.iccProfileName    = targetProfile.name();
    meta.iccProfileType    = static_cast<int>(targetProfile.type());
    meta.colorProfileHandled = true;

    if (targetProfile.type() == StandardProfile::Custom
        && !targetProfile.iccData().isEmpty()) {
        meta.iccData = targetProfile.iccData();
    } else {
        meta.iccData.clear();
    }

    buffer.setMetadata(meta);
    buffer.setModified(true);

    return true;
}

// -- Asynchronous conversion --------------------------------------------------

void ColorProfileManager::convertProfileAsync(ImageBuffer& buffer,
                                              const ColorProfile& sourceProfile,
                                              const ColorProfile& targetProfile)
{
    auto task = std::make_shared<ColorProfileTask>(
        buffer, sourceProfile, targetProfile);

    connect(task.get(), &Threading::Task::started,
            this,       &ColorProfileManager::conversionStarted);
    connect(task.get(), &Threading::Task::finished,
            this,       &ColorProfileManager::conversionFinished);
    connect(task.get(), &Threading::Task::failed,
            this,       &ColorProfileManager::conversionFailed);

    Threading::TaskManager::instance().submit(task);
}

} // namespace core