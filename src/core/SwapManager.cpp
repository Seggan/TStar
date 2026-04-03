// ============================================================================
// SwapManager.cpp
// LRU-based memory pressure manager that evicts inactive ImageBuffers to disk.
// ============================================================================

#include "SwapManager.h"
#include "../ImageBuffer.h"

#include <QDebug>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/types.h>
#include <sys/sysctl.h>
#include <mach/mach.h>
#include <unistd.h>
#else
#include <unistd.h>
#endif

// ============================================================================
// Singleton Access
// ============================================================================

SwapManager& SwapManager::instance()
{
    static SwapManager s_instance;
    return s_instance;
}

// ============================================================================
// Construction / Destruction
// ============================================================================

SwapManager::SwapManager()
{
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout,
            this,    &SwapManager::checkMemoryPressure);
    m_timer->start(DEFAULT_CHECK_INTERVAL_MS);
}

SwapManager::~SwapManager()
{
    m_timer->stop();
    // Buffers are not owned by SwapManager; no deletion required
}

// ============================================================================
// Buffer Registration
// ============================================================================

void SwapManager::registerBuffer(ImageBuffer* buffer)
{
    QMutexLocker lock(&m_listMutex);
    if (!m_buffers.contains(buffer)) {
        m_buffers.append(buffer);
    }
}

void SwapManager::unregisterBuffer(ImageBuffer* buffer)
{
    QMutexLocker lock(&m_listMutex);
    m_buffers.removeAll(buffer);
}

// ============================================================================
// Memory Usage Query
// ============================================================================

/**
 * @brief Query the OS for current physical memory utilization.
 * @return Usage percentage [0, 100], or 0.0 if unavailable.
 */
double SwapManager::getMemoryUsagePercent()
{
#ifdef _WIN32
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);

    DWORDLONG totalPhysMem = memInfo.ullTotalPhys;
    DWORDLONG physMemUsed  = memInfo.ullTotalPhys - memInfo.ullAvailPhys;

    return (static_cast<double>(physMemUsed) / totalPhysMem) * 100.0;

#elif defined(__APPLE__)
    int64_t totalPhysMem = 0;
    size_t  len = sizeof(totalPhysMem);
    if (sysctlbyname("hw.memsize", &totalPhysMem, &len, NULL, 0) != 0) {
        return 0.0;
    }

    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    vm_statistics64_data_t vmstat;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          (host_info64_t)&vmstat, &count) != KERN_SUCCESS) {
        return 0.0;
    }

    long   page_size    = sysconf(_SC_PAGESIZE);
    double availPhysMem = static_cast<double>(
        vmstat.free_count + vmstat.inactive_count + vmstat.speculative_count)
        * page_size;
    double physMemUsed  = static_cast<double>(totalPhysMem) - availPhysMem;

    return (physMemUsed / static_cast<double>(totalPhysMem)) * 100.0;

#else
    // Linux implementation
    long pages       = sysconf(_SC_PHYS_PAGES);
    long avail_pages = sysconf(_SC_AV_PHYS_PAGES);
    long page_size   = sysconf(_SC_PAGESIZE);

    if (pages <= 0 || page_size <= 0) return 0.0;

    double totalPhysMem = static_cast<double>(pages) * page_size;
    double physMemUsed  = static_cast<double>(pages - avail_pages) * page_size;

    return (physMemUsed / totalPhysMem) * 100.0;
#endif
}

// ============================================================================
// Memory Pressure Check
// ============================================================================

/**
 * @brief Periodic callback that checks RAM usage and evicts buffers if needed.
 *
 * Strategy:
 *   - If usage is below the threshold, do nothing.
 *   - Collect all swappable (non-swapped, swap-eligible) buffers.
 *   - Sort by last access time (LRU order: oldest first).
 *   - Swap out the least-recently-used buffer.
 *   - Only one buffer is evicted per cycle to maintain UI responsiveness.
 */
void SwapManager::checkMemoryPressure()
{
    double usage = getMemoryUsagePercent();

    if (usage < m_maxRamUsagePercent) return;

    qDebug() << "[SwapManager] RAM pressure detected:" << usage
             << "% (threshold:" << m_maxRamUsagePercent << "%)";

    QMutexLocker lock(&m_listMutex);

    qDebug() << "[SwapManager] Registered buffers:" << m_buffers.size();

    // Collect eligible swap candidates
    std::vector<ImageBuffer*> candidates;
    for (int i = 0; i < m_buffers.size(); ++i) {
        ImageBuffer* buf = m_buffers[i];

        qDebug() << "[SwapManager] Buffer" << i << ":"
                 << static_cast<void*>(buf)
                 << "name:" << buf->name()
                 << "swapped:" << buf->isSwapped()
                 << "canSwap:" << buf->canSwap();

        if (!buf->isSwapped() && buf->canSwap()) {
            candidates.push_back(buf);
        }
    }

    if (candidates.empty()) {
        qDebug() << "[SwapManager] No swap candidates available.";
        return;
    }

    // Sort by last access time (oldest first = highest eviction priority)
    std::sort(candidates.begin(), candidates.end(),
              [](ImageBuffer* a, ImageBuffer* b) {
                  return a->getLastAccessTime() < b->getLastAccessTime();
              });

    // Evict the oldest buffer (one per cycle to avoid UI lag)
    for (ImageBuffer* buf : candidates) {
        qDebug() << "[SwapManager] Attempting swap out:"
                 << buf->name() << static_cast<void*>(buf);

        if (buf->trySwapOut()) {
            qInfo() << "[SwapManager] Swapped out:" << buf->name()
                    << "due to RAM pressure:" << usage << "%";
            break;
        }
    }
}