#ifndef SWAPMANAGER_H
#define SWAPMANAGER_H

// ============================================================================
// SwapManager.h
// Automatic memory pressure management via LRU-based buffer eviction.
// Periodically monitors RAM usage and swaps inactive ImageBuffers to disk.
// ============================================================================

#include <QObject>
#include <QList>
#include <QMutex>
#include <QTimer>
#include <vector>

class ImageBuffer;

/**
 * @brief Monitors system memory and evicts inactive buffers to disk.
 *
 * Singleton. Runs a periodic timer that checks physical RAM usage.
 * When usage exceeds the configured threshold, the least-recently-accessed
 * eligible ImageBuffer is swapped out to temporary storage.
 */
class SwapManager : public QObject {
    Q_OBJECT

public:
    static SwapManager& instance();

    /** Register an ImageBuffer for consideration during memory pressure events. */
    void registerBuffer(ImageBuffer* buffer);

    /** Remove an ImageBuffer from the managed set. */
    void unregisterBuffer(ImageBuffer* buffer);

    /** Manually trigger a memory pressure check and potential swap-out. */
    void checkMemoryPressure();

    /** Set the RAM usage threshold (percent) that triggers swap-out. */
    void setMaxRamUsagePercent(int percent) { m_maxRamUsagePercent = percent; }

    /** Default interval between memory pressure checks (milliseconds). */
    static const int DEFAULT_CHECK_INTERVAL_MS = 2000;

private:
    SwapManager();
    ~SwapManager();

    QList<ImageBuffer*> m_buffers;
    QMutex              m_listMutex;
    QTimer*             m_timer;
    int                 m_maxRamUsagePercent = 80;

    /** Query the operating system for current physical memory utilization. */
    double getMemoryUsagePercent();
};

#endif // SWAPMANAGER_H