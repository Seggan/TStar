#pragma once

// ============================================================================
// ColorProfileManager.h
// ICC color profile representation and application-wide workspace management.
// Uses LittleCMS (lcms2) for profile creation and pixel-level transforms.
// ============================================================================

#include <QString>
#include <QByteArray>
#include <QObject>
#include <QMutex>
#include <memory>

class ImageBuffer;

namespace core {

// ----------------------------------------------------------------------------
// Enumerations
// ----------------------------------------------------------------------------

/** Standard ICC color profiles supported natively. */
enum class StandardProfile {
    sRGB,
    AdobeRGB,
    ProPhotoRGB,
    LinearRGB,
    Custom          ///< User-supplied ICC profile from file or embedded data
};

/** Policy for automatic color profile conversion on image load. */
enum class AutoConversionMode {
    Never,          ///< Never convert; leave data as-is
    Ask,            ///< Prompt the user on each mismatch
    Always          ///< Automatically convert to workspace profile
};

// ----------------------------------------------------------------------------
// ColorProfile
// ----------------------------------------------------------------------------

/**
 * @brief Encapsulates an ICC color profile (standard or custom).
 *
 * Instances can be constructed from a StandardProfile enum, an ICC file path,
 * or raw ICC data bytes (e.g. embedded in a TIFF or PNG).
 */
class ColorProfile {
public:
    ColorProfile();
    explicit ColorProfile(StandardProfile type);
    explicit ColorProfile(const QString& iccFilePath);
    explicit ColorProfile(const QByteArray& iccData);

    bool            isValid() const;
    StandardProfile type()    const;
    QString         name()    const;
    QByteArray      iccData() const;

    bool operator==(const ColorProfile& other) const;
    bool operator!=(const ColorProfile& other) const;

private:
    StandardProfile m_type;
    QString         m_name;
    QByteArray      m_iccData;
    bool            m_valid;

    void loadStandardProfile(StandardProfile type);
};

// ----------------------------------------------------------------------------
// ColorProfileManager (Singleton)
// ----------------------------------------------------------------------------

/**
 * @brief Manages the application workspace color profile and performs
 *        ICC-based color space conversions.
 *
 * Thread-safe singleton. Persists workspace preference and auto-conversion
 * mode via QSettings.
 */
class ColorProfileManager : public QObject {
    Q_OBJECT

public:
    static ColorProfileManager& instance();

    // -- Workspace profile management -----------------------------------------
    void         setWorkspaceProfile(const ColorProfile& profile);
    ColorProfile workspaceProfile() const;

    // -- Auto-conversion preference -------------------------------------------
    void               setAutoConversionMode(AutoConversionMode mode);
    AutoConversionMode autoConversionMode() const;

    /** Reload workspace profile and auto-conversion mode from QSettings. */
    void syncSettings();

    // -- Image operations -----------------------------------------------------

    /** Check whether the image profile differs from the workspace profile. */
    bool isMismatch(const ColorProfile& imageProfile) const;

    /** Convert the buffer from sourceProfile to the current workspace profile. */
    bool convertToWorkspace(ImageBuffer& buffer, const ColorProfile& sourceProfile);

    /** Convert the buffer between two arbitrary profiles. */
    bool convertProfile(ImageBuffer& buffer,
                        const ColorProfile& sourceProfile,
                        const ColorProfile& targetProfile);

    /** Asynchronous version of convertProfile (submitted to TaskManager). */
    void convertProfileAsync(ImageBuffer& buffer,
                             const ColorProfile& sourceProfile,
                             const ColorProfile& targetProfile);

signals:
    void workspaceProfileChanged(const ColorProfile& newProfile);
    void conversionStarted(quint64 taskId);
    void conversionFinished(quint64 taskId);
    void conversionFailed(quint64 taskId, const QString& error);

private:
    ColorProfileManager();
    ~ColorProfileManager();
    Q_DISABLE_COPY(ColorProfileManager)

    ColorProfile       m_workspaceProfile;
    AutoConversionMode m_autoConversionMode;
    mutable QMutex     m_mutex;
};

} // namespace core