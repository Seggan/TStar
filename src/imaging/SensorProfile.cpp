// ============================================================================
// SensorProfile.cpp
// Built-in sensor profile database with common CCD, CMOS, and DSLR sensors.
// ============================================================================

#include "SensorProfile.h"
#include <QDebug>

// ============================================================================
// Singleton Access
// ============================================================================

SensorProfileDatabase& SensorProfileDatabase::instance()
{
    static SensorProfileDatabase db;
    return db;
}

// ============================================================================
// Built-in Profile Initialization
// ============================================================================

void SensorProfileDatabase::initBuiltins()
{
    // -- Generic CCD (conservative defaults) ----------------------------------
    {
        SensorProfile p;
        p.name                = "Generic CCD";
        p.type                = "CCD";
        p.pixelWidth          = 4.65;
        p.pixelHeight         = 4.65;
        p.sensorWidth         = 2048;
        p.sensorHeight        = 1536;
        p.gain                = 0.7;       // e-/ADU, typical for scientific CCD
        p.readNoise           = 5.0;       // RMS electrons
        p.darkCurrent         = 0.001;     // e-/s, very low for cooled CCD
        p.fullWell            = 50000.0;   // electrons
        p.maxADU              = 65535.0;
        p.tempRefC            = 25.0;
        p.darkCurrentGradient = 0.05;      // Approximate doubling every 5-6 degrees
        m_profiles["Generic CCD"] = p;
    }

    // -- Generic CMOS ---------------------------------------------------------
    {
        SensorProfile p;
        p.name                = "Generic CMOS";
        p.type                = "CMOS";
        p.pixelWidth          = 3.45;
        p.pixelHeight         = 3.45;
        p.sensorWidth         = 3840;
        p.sensorHeight        = 2160;
        p.gain                = 3.5;       // Higher gain typical for CMOS
        p.readNoise           = 25.0;      // Higher read noise than CCD
        p.darkCurrent         = 0.1;       // Higher dark current
        p.fullWell            = 40000.0;
        p.maxADU              = 65535.0;
        p.tempRefC            = 25.0;
        p.darkCurrentGradient = 0.08;
        m_profiles["Generic CMOS"] = p;
    }

    // -- Canon EOS R7 ---------------------------------------------------------
    {
        SensorProfile p;
        p.name                = "Canon EOS R7";
        p.type                = "DSLR";
        p.pixelWidth          = 3.31;
        p.pixelHeight         = 3.31;
        p.sensorWidth         = 7360;
        p.sensorHeight        = 4912;
        p.gain                = 1.2;       // Typical for Canon DSLR sensors
        p.readNoise           = 8.0;
        p.darkCurrent         = 0.05;
        p.fullWell            = 65800.0;
        p.maxADU              = 65535.0;
        p.tempRefC            = 25.0;
        p.darkCurrentGradient = 0.06;
        m_profiles["Canon EOS R7"] = p;
    }

    // -- ZWO ASI533MM-Pro (cooled CMOS astronomy camera) ----------------------
    {
        SensorProfile p;
        p.name                = "ZWO ASI533MM-Pro";
        p.type                = "CMOS";
        p.pixelWidth          = 3.76;
        p.pixelHeight         = 3.76;
        p.sensorWidth         = 3856;
        p.sensorHeight        = 2574;
        p.gain                = 0.8;
        p.readNoise           = 6.0;
        p.darkCurrent         = 0.02;
        p.fullWell            = 100000.0;  // High full well capacity
        p.maxADU              = 65535.0;
        p.tempRefC            = 20.0;
        p.darkCurrentGradient = 0.07;
        m_profiles["ZWO ASI533MM-Pro"] = p;
    }
}

// ============================================================================
// Profile Lookup
// ============================================================================

bool SensorProfileDatabase::loadProfile(const QString& name, SensorProfile& out)
{
    // Lazy initialization of built-in profiles
    static bool initialized = false;
    if (!initialized) {
        const_cast<SensorProfileDatabase*>(this)->initBuiltins();
        initialized = true;
    }

    auto it = m_profiles.find(name);
    if (it != m_profiles.end()) {
        out = it->second;
        return true;
    }

    qWarning().noquote()
        << "[SensorProfile] Profile not found:" << name
        << "- falling back to generic CCD defaults";
    out = getDefault();
    return false;
}

void SensorProfileDatabase::registerProfile(const SensorProfile& profile)
{
    m_profiles[profile.name] = profile;
}

std::vector<QString> SensorProfileDatabase::listProfiles() const
{
    std::vector<QString> list;
    list.reserve(m_profiles.size());
    for (const auto& [name, _] : m_profiles) {
        list.push_back(name);
    }
    return list;
}

// ============================================================================
// Default Profile
// ============================================================================

SensorProfile SensorProfileDatabase::getDefault()
{
    SensorProfile def;
    def.name                = "Generic CCD (Default)";
    def.type                = "CCD";
    def.pixelWidth          = 4.65;
    def.pixelHeight         = 4.65;
    def.sensorWidth         = 2048;
    def.sensorHeight        = 1536;
    def.gain                = 0.7;
    def.readNoise           = 5.0;
    def.darkCurrent         = 0.001;
    def.fullWell            = 50000.0;
    def.maxADU              = 65535.0;
    def.tempRefC            = 25.0;
    def.darkCurrentGradient = 0.05;
    def.isCustom            = false;
    return def;
}