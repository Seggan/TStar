#ifndef RESOURCEMANAGER_H
#define RESOURCEMANAGER_H

// ============================================================================
// ResourceManager.h
// System resource monitoring and thread/memory limit enforcement.
// Enforces the application policy of maximum 90% CPU and RAM utilization.
// ============================================================================

#include <QObject>
#include <QtGlobal>

/**
 * @brief Manages system resource constraints (CPU thread count, RAM).
 *
 * Singleton. Call init() once during application startup to configure
 * thread limits based on available hardware.
 */
class ResourceManager : public QObject {
    Q_OBJECT

public:
    static ResourceManager& instance();

    /**
     * @brief Initialize resource limits based on detected hardware.
     * Sets the maximum thread count to 90% of available logical cores.
     * Configures OpenMP and Qt's global thread pool accordingly.
     */
    void init();

    /** @return Maximum number of worker threads (90% of logical cores, min 1). */
    int maxThreads() const;

    /**
     * @brief Check whether allocating the given number of bytes is safe.
     * @param estimatedBytes Anticipated allocation size (0 = check current usage only).
     * @return true if current + projected usage stays below 90%.
     */
    bool isMemorySafe(size_t estimatedBytes = 0) const;

    /** @return Current system-wide physical memory utilization as a percentage. */
    double getMemoryUsagePercent() const;

private:
    ResourceManager();
    ~ResourceManager() = default;

    int m_maxThreads = 1;
};

#endif // RESOURCEMANAGER_H