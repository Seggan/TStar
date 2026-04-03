// ============================================================================
// ResourceManager.cpp
// System resource monitoring and thread/memory limit enforcement.
// ============================================================================

#include "ResourceManager.h"

#include <QThread>
#include <QDebug>
#include <QThreadPool>

#include <cmath>
#include <algorithm>

#ifdef _OPENMP
#include <omp.h>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#ifdef Q_OS_MAC
#include <mach/mach.h>
#include <sys/sysctl.h>
#endif

#ifdef Q_OS_LINUX
#include <sys/sysinfo.h>
#endif

// ============================================================================
// Singleton Access
// ============================================================================

ResourceManager& ResourceManager::instance()
{
    static ResourceManager _instance;
    return _instance;
}

// ============================================================================
// Construction
// ============================================================================

ResourceManager::ResourceManager()
{
    // Conservative default until init() is called
    m_maxThreads = std::max(1, QThread::idealThreadCount() - 1);
}

// ============================================================================
// Initialization
// ============================================================================

/**
 * @brief Configure thread limits based on the 90% CPU utilization policy.
 *
 * Calculates the maximum number of worker threads as floor(cores * 0.9),
 * ensuring at least one thread is always available. Applies the limit
 * to both OpenMP and Qt's global QThreadPool.
 */
void ResourceManager::init()
{
    int totalCores = QThread::idealThreadCount();

    // 90% rule: floor(cores * 0.9), minimum 1
    //   16 cores -> 14 threads
    //    8 cores ->  7 threads
    //    4 cores ->  3 threads
    m_maxThreads = std::max(1,
        static_cast<int>(std::floor(totalCores * 0.9)));

    qInfo() << "ResourceManager: CPU limit set to" << m_maxThreads
            << "threads (Total:" << totalCores << ")";

#ifdef _OPENMP
    omp_set_num_threads(m_maxThreads);
    qInfo() << "ResourceManager: OpenMP thread limit configured.";
#endif

    // Apply the same limit to Qt's global thread pool (used by QtConcurrent)
    QThreadPool::globalInstance()->setMaxThreadCount(m_maxThreads);
    qInfo() << "ResourceManager: Global QThreadPool set to"
            << m_maxThreads << "threads";
}

// ============================================================================
// Thread Limit Query
// ============================================================================

int ResourceManager::maxThreads() const
{
    return m_maxThreads;
}

// ============================================================================
// Memory Usage Monitoring
// ============================================================================

/**
 * @brief Query the operating system for current physical memory utilization.
 * @return Memory usage as a percentage [0, 100], or 0.0 if unavailable.
 */
double ResourceManager::getMemoryUsagePercent() const
{
#ifdef Q_OS_WIN
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        return static_cast<double>(memInfo.dwMemoryLoad);
    }

#elif defined(Q_OS_MAC)
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    vm_statistics64_data_t vmstat;

    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          (host_info64_t)&vmstat, &count) == KERN_SUCCESS) {
        // Determine page size
        vm_size_t vmsize;
        mach_port_t host_port = mach_host_self();
        host_page_size(host_port, &vmsize);
        long long page_size = vmsize;

        // Query total physical memory via sysctl
        int mib[2] = { CTL_HW, HW_MEMSIZE };
        int64_t total_memory = 0;
        size_t len = sizeof(total_memory);
        if (sysctl(mib, 2, &total_memory, &len, NULL, 0) != 0) {
            return 0.0;
        }

        // Used = active + wired + compressor pages
        long long used_pages = vmstat.active_count
                             + vmstat.wire_count
                             + vmstat.compressor_page_count;
        double used_bytes = static_cast<double>(used_pages) * page_size;

        return (used_bytes / static_cast<double>(total_memory)) * 100.0;
    }

#elif defined(Q_OS_LINUX)
    struct sysinfo memInfo;
    if (sysinfo(&memInfo) == 0) {
        long long total = memInfo.totalram;
        long long free  = memInfo.freeram;
        total *= memInfo.mem_unit;
        free  *= memInfo.mem_unit;

        return (1.0 - static_cast<double>(free) / total) * 100.0;
    }
#endif

    return 0.0;
}

// ============================================================================
// Memory Safety Check
// ============================================================================

/**
 * @brief Determine whether an allocation of the given size is safe.
 *
 * Returns false if current memory usage is at or above 90%, or if the
 * projected usage after the allocation would exceed 90%.
 *
 * @param estimatedBytes Number of bytes the caller intends to allocate.
 * @return true if the allocation is expected to be safe.
 */
bool ResourceManager::isMemorySafe(size_t estimatedBytes) const
{
    Q_UNUSED(estimatedBytes);
    double usage = getMemoryUsagePercent();

    // If measurement is unavailable, assume safe to avoid blocking the user
    if (usage <= 0.1) return true;

    // Hard limit: reject if already at 90% or above
    if (usage >= 90.0) {
        qWarning() << "ResourceManager: High memory usage detected:"
                   << usage << "%";
        return false;
    }

    // Projected usage check (Windows only -- requires precise available RAM query)
#ifdef Q_OS_WIN
    if (estimatedBytes > 0) {
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        GlobalMemoryStatusEx(&memInfo);

        unsigned long long avail = memInfo.ullAvailPhys;

        if (estimatedBytes > avail) {
            qWarning() << "ResourceManager: Insufficient physical RAM."
                       << "Needed:" << estimatedBytes
                       << "Available:" << avail;
            return false;
        }

        unsigned long long total = memInfo.ullTotalPhys;
        unsigned long long used  = total - avail;
        double projected = static_cast<double>(used + estimatedBytes)
                         / total * 100.0;

        if (projected >= 90.0) {
            qWarning() << "ResourceManager: Operation would exceed 90% RAM."
                       << "Projected:" << projected << "%";
            return false;
        }
    }
#endif

    return true;
}