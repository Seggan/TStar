#ifndef TSTAR_CORE_VERSION_H
#define TSTAR_CORE_VERSION_H

// ============================================================================
// Version.h
// Application version string accessor.
// The actual version is injected at build time via CMake configure_file().
// ============================================================================

namespace TStar {

    /**
     * @brief Get the application version string.
     * @return Semantic version string (e.g. "1.2.3"), set at build time.
     */
    const char* getVersion();

} // namespace TStar

#endif // TSTAR_CORE_VERSION_H