#ifndef RESOURCEMONITORWIDGET_H
#define RESOURCEMONITORWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <d3d11.h>
#include <dxgi.h>
#elif defined(Q_OS_MACOS)
#include <mach/mach.h>
#include <mach/processor_info.h>
#include <mach/mach_host.h>
#endif

// ---------------------------------------------------------------------------
// ResourceMonitorWidget
// Compact status-bar widget that periodically samples CPU usage, RAM usage,
// and GPU utilisation and displays them as a formatted text label.
//
// Platform coverage:
//   Windows : CPU via GetSystemTimes, RAM via GlobalMemoryStatusEx,
//             GPU via NVIDIA NVML (dynamically loaded) with PDH fallback
//   macOS   : CPU via host_statistics, RAM via Mach VM statistics,
//             GPU name via IOKit (utilisation not available without SPI)
//   Other   : Metrics are reported as unavailable ("--")
// ---------------------------------------------------------------------------
class ResourceMonitorWidget : public QWidget {
    Q_OBJECT

public:
    explicit ResourceMonitorWidget(QWidget* parent = nullptr);
    ~ResourceMonitorWidget() override;

private slots:
    void updateStats();

private:
    QLabel* m_label;
    QTimer* m_timer;

    // -- CPU tracking (delta-based idle/kernel/user time counters) --------
#ifdef Q_OS_WIN
    ULARGE_INTEGER m_prevIdleTime{};
    ULARGE_INTEGER m_prevKernelTime{};
    ULARGE_INTEGER m_prevUserTime{};

    // PDH: generic GPU utilisation counter (Intel, AMD, NVIDIA via Windows)
    PDH_HQUERY    m_pdhQuery      = nullptr;
    PDH_HCOUNTER  m_pdhGpuCounter = nullptr;
    bool          m_pdhAvailable  = false;

    QString getGpuNameDxgi();

#elif defined(Q_OS_MACOS)
    uint64_t m_prevIdleTicks  = 0;
    uint64_t m_prevTotalTicks = 0;
#endif

    // -- NVIDIA NVML (dynamically loaded on Windows) ----------------------
    void*   m_nvmlLib       = nullptr;
    void*   m_nvmlDevice    = nullptr;
    bool    m_nvmlAvailable = false;
    QString m_gpuName;

    // -- Platform query methods ------------------------------------------
    float queryCpuUsage();
    void  queryRamUsage(float& usedGB, float& totalGB, float& percent);
    float queryGpuUsage();

#ifdef Q_OS_WIN
    float queryPdhGpuUsage();
#endif

    // -- Initialisation / cleanup helpers --------------------------------
    void initCpuBaseline();
    void initNvml();
    void cleanupNvml();

#ifdef Q_OS_WIN
    void initPdh();
    void cleanupPdh();
#endif
};

#endif // RESOURCEMONITORWIDGET_H