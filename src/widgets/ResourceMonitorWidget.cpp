#include "ResourceMonitorWidget.h"

#include <QHBoxLayout>
#include <QFont>
#include <QApplication>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#elif defined(Q_OS_MACOS)
#include <mach/mach.h>
#include <mach/processor_info.h>
#include <mach/mach_host.h>
#include <sys/sysctl.h>
#include <IOKit/IOKitLib.h>
#endif

// ============================================================================
// NVML function pointer typedefs for dynamic loading (NVIDIA GPU monitoring).
// This avoids a hard link-time dependency on nvml.dll / libnvidia-ml.so.
// ============================================================================

typedef enum {
    NVML_SUCCESS_ = 0
} nvmlReturn_t_;

typedef struct {
    unsigned int gpu;
    unsigned int memory;
} nvmlUtilization_t_;

typedef nvmlReturn_t_ (*nvmlInit_t)();
typedef nvmlReturn_t_ (*nvmlShutdown_t)();
typedef nvmlReturn_t_ (*nvmlDeviceGetCount_t)(unsigned int*);
typedef nvmlReturn_t_ (*nvmlDeviceGetHandleByIndex_t)(unsigned int, void**);
typedef nvmlReturn_t_ (*nvmlDeviceGetUtilizationRates_t)(void*, nvmlUtilization_t_*);
typedef nvmlReturn_t_ (*nvmlDeviceGetName_t)(void*, char*, unsigned int);

// Cached NVML function pointers (Windows only; resolved at runtime via GetProcAddress)
#ifdef Q_OS_WIN
static nvmlInit_t                    s_nvmlInit                    = nullptr;
static nvmlShutdown_t                s_nvmlShutdown                = nullptr;
static nvmlDeviceGetCount_t          s_nvmlDeviceGetCount          = nullptr;
static nvmlDeviceGetHandleByIndex_t  s_nvmlDeviceGetHandleByIndex  = nullptr;
static nvmlDeviceGetUtilizationRates_t s_nvmlDeviceGetUtilizationRates = nullptr;
static nvmlDeviceGetName_t           s_nvmlDeviceGetName           = nullptr;
#endif


// ============================================================================
// Constructor / Destructor
// ============================================================================

ResourceMonitorWidget::ResourceMonitorWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 0, 8, 0);
    layout->setSpacing(0);

    m_label = new QLabel(this);
    m_label->setStyleSheet(
        "color: #999;"
        "font-size: 11px;"
        "font-family: 'Consolas', 'Monaco', 'Courier New', monospace;"
    );
    m_label->setText("CPU: -- | RAM: -- | GPU: --");
    layout->addWidget(m_label);

    // Initialize platform-specific CPU baseline counters
    initCpuBaseline();

    // Attempt to load NVML for NVIDIA GPU monitoring
    initNvml();

#ifdef Q_OS_WIN
    // Initialize PDH counters as a fallback for non-NVIDIA GPUs (Intel, AMD)
    initPdh();

    // Attempt to detect the GPU name via DXGI if NVML did not provide one
    if (m_gpuName.isEmpty()) {
        m_gpuName = getGpuNameDxgi();
    }
#endif

    // Periodic update timer (500 ms interval)
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &ResourceMonitorWidget::updateStats);
    m_timer->start(500);

    // Perform an initial update shortly after construction
    QTimer::singleShot(100, this, &ResourceMonitorWidget::updateStats);
}

ResourceMonitorWidget::~ResourceMonitorWidget()
{
    cleanupNvml();

#ifdef Q_OS_WIN
    cleanupPdh();
#endif
}


// ============================================================================
// Periodic stat update - assembles the display string from all subsystems
// ============================================================================

void ResourceMonitorWidget::updateStats()
{
    float cpu = queryCpuUsage();

    float ramUsed    = 0.0f;
    float ramTotal   = 0.0f;
    float ramPercent = 0.0f;
    queryRamUsage(ramUsed, ramTotal, ramPercent);

    float gpu = queryGpuUsage();

    QString text;

    // CPU section
    if (cpu >= 0.0f)
        text += QString("CPU: %1%").arg(QString::number(cpu, 'f', 0));
    else
        text += "CPU: --";

    text += "  |  ";

    // GPU section
    if (gpu >= 0.0f)
        text += QString("GPU: %1%").arg(QString::number(gpu, 'f', 0));
    else
        text += "GPU: N/A";

    text += "  |  ";

    // RAM section
    if (ramTotal > 0.0f) {
        text += QString("RAM: %1 / %2 GB (%3%)")
                    .arg(QString::number(ramUsed,    'f', 1))
                    .arg(QString::number(ramTotal,   'f', 1))
                    .arg(QString::number(ramPercent, 'f', 0));
    } else {
        text += "RAM: --";
    }

    m_label->setText(text);
}


// ============================================================================
// CPU Usage - delta-based measurement using OS idle/kernel/user tick counters
// ============================================================================

void ResourceMonitorWidget::initCpuBaseline()
{
#ifdef Q_OS_WIN
    FILETIME idle, kernel, user;
    GetSystemTimes(&idle, &kernel, &user);

    m_prevIdleTime.LowPart    = idle.dwLowDateTime;
    m_prevIdleTime.HighPart   = idle.dwHighDateTime;
    m_prevKernelTime.LowPart  = kernel.dwLowDateTime;
    m_prevKernelTime.HighPart = kernel.dwHighDateTime;
    m_prevUserTime.LowPart    = user.dwLowDateTime;
    m_prevUserTime.HighPart   = user.dwHighDateTime;

#elif defined(Q_OS_MACOS)
    host_cpu_load_info_data_t cpuInfo;
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;

    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                        (host_info_t)&cpuInfo, &count) == KERN_SUCCESS)
    {
        m_prevIdleTicks  = cpuInfo.cpu_ticks[CPU_STATE_IDLE];
        m_prevTotalTicks = cpuInfo.cpu_ticks[CPU_STATE_USER]
                         + cpuInfo.cpu_ticks[CPU_STATE_SYSTEM]
                         + cpuInfo.cpu_ticks[CPU_STATE_IDLE]
                         + cpuInfo.cpu_ticks[CPU_STATE_NICE];
    }
#endif
}

float ResourceMonitorWidget::queryCpuUsage()
{
#ifdef Q_OS_WIN
    FILETIME idle, kernel, user;
    if (!GetSystemTimes(&idle, &kernel, &user))
        return -1.0f;

    ULARGE_INTEGER nowIdle, nowKernel, nowUser;
    nowIdle.LowPart    = idle.dwLowDateTime;
    nowIdle.HighPart   = idle.dwHighDateTime;
    nowKernel.LowPart  = kernel.dwLowDateTime;
    nowKernel.HighPart = kernel.dwHighDateTime;
    nowUser.LowPart    = user.dwLowDateTime;
    nowUser.HighPart   = user.dwHighDateTime;

    uint64_t idleDelta   = nowIdle.QuadPart   - m_prevIdleTime.QuadPart;
    uint64_t kernelDelta = nowKernel.QuadPart  - m_prevKernelTime.QuadPart;
    uint64_t userDelta   = nowUser.QuadPart    - m_prevUserTime.QuadPart;

    m_prevIdleTime   = nowIdle;
    m_prevKernelTime = nowKernel;
    m_prevUserTime   = nowUser;

    // Kernel time includes idle time on Windows
    uint64_t totalDelta = kernelDelta + userDelta;
    if (totalDelta == 0)
        return 0.0f;

    float usage = 100.0f * (1.0f - static_cast<float>(idleDelta) / static_cast<float>(totalDelta));
    return std::clamp(usage, 0.0f, 100.0f);

#elif defined(Q_OS_MACOS)
    host_cpu_load_info_data_t cpuInfo;
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;

    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                        (host_info_t)&cpuInfo, &count) != KERN_SUCCESS)
        return -1.0f;

    uint64_t idle  = cpuInfo.cpu_ticks[CPU_STATE_IDLE];
    uint64_t total = cpuInfo.cpu_ticks[CPU_STATE_USER]
                   + cpuInfo.cpu_ticks[CPU_STATE_SYSTEM]
                   + cpuInfo.cpu_ticks[CPU_STATE_IDLE]
                   + cpuInfo.cpu_ticks[CPU_STATE_NICE];

    uint64_t idleDelta  = idle  - m_prevIdleTicks;
    uint64_t totalDelta = total - m_prevTotalTicks;

    m_prevIdleTicks  = idle;
    m_prevTotalTicks = total;

    if (totalDelta == 0)
        return 0.0f;

    float usage = 100.0f * (1.0f - static_cast<float>(idleDelta) / static_cast<float>(totalDelta));
    return std::clamp(usage, 0.0f, 100.0f);

#else
    return -1.0f;
#endif
}


// ============================================================================
// RAM Usage
// ============================================================================

void ResourceMonitorWidget::queryRamUsage(float& usedGB, float& totalGB, float& percent)
{
#ifdef Q_OS_WIN
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);

    if (GlobalMemoryStatusEx(&memInfo)) {
        totalGB      = memInfo.ullTotalPhys / (1024.0f * 1024.0f * 1024.0f);
        float availGB = memInfo.ullAvailPhys / (1024.0f * 1024.0f * 1024.0f);
        usedGB       = totalGB - availGB;
        percent      = (totalGB > 0.0f) ? (usedGB / totalGB * 100.0f) : 0.0f;
    } else {
        usedGB = totalGB = percent = 0.0f;
    }

#elif defined(Q_OS_MACOS)
    // Total physical RAM via sysctl
    int64_t totalMem = 0;
    size_t  size     = sizeof(totalMem);
    sysctlbyname("hw.memsize", &totalMem, &size, nullptr, 0);
    totalGB = totalMem / (1024.0f * 1024.0f * 1024.0f);

    // Used RAM via Mach VM statistics (matches macOS Activity Monitor methodology)
    vm_statistics64_data_t vmStats;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    vm_size_t pageSize;
    host_page_size(mach_host_self(), &pageSize);

    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          (host_info64_t)&vmStats, &count) == KERN_SUCCESS)
    {
        uint64_t active     = vmStats.active_count          * pageSize;
        uint64_t wired      = vmStats.wire_count            * pageSize;
        uint64_t compressed = vmStats.compressor_page_count * pageSize;

        // "Used" = active + wired + compressed (matches Activity Monitor)
        usedGB  = (active + wired + compressed) / (1024.0f * 1024.0f * 1024.0f);
        percent = (totalGB > 0.0f) ? (usedGB / totalGB * 100.0f) : 0.0f;
    } else {
        usedGB = percent = 0.0f;
    }

#else
    usedGB = totalGB = percent = 0.0f;
#endif
}


// ============================================================================
// GPU Usage - NVIDIA NVML (dynamically loaded at runtime)
// ============================================================================

void ResourceMonitorWidget::initNvml()
{
#ifdef Q_OS_WIN
    // Attempt to locate nvml.dll in multiple standard installation paths
    HMODULE lib = LoadLibraryA("nvml.dll");

    if (!lib) {
        const char* sysRoot = getenv("SYSTEMROOT");
        if (sysRoot) {
            std::string path = std::string(sysRoot) + "\\System32\\nvml.dll";
            lib = LoadLibraryA(path.c_str());
        }
    }

    if (!lib) {
        const char* progFiles = getenv("ProgramFiles");
        if (progFiles) {
            std::string path = std::string(progFiles)
                             + "\\NVIDIA Corporation\\NVSMI\\nvml.dll";
            lib = LoadLibraryA(path.c_str());
        }
    }

    if (!lib) {
        m_nvmlAvailable = false;
        return;
    }

    m_nvmlLib = static_cast<void*>(lib);

    // Resolve required function pointers.
    // Suppress MinGW warning: GetProcAddress returns FARPROC (incompatible signature).
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"

    s_nvmlInit = (nvmlInit_t)GetProcAddress(lib, "nvmlInit_v2");
    if (!s_nvmlInit)
        s_nvmlInit = (nvmlInit_t)GetProcAddress(lib, "nvmlInit");

    s_nvmlShutdown = (nvmlShutdown_t)GetProcAddress(lib, "nvmlShutdown");

    s_nvmlDeviceGetCount = (nvmlDeviceGetCount_t)GetProcAddress(lib, "nvmlDeviceGetCount_v2");
    if (!s_nvmlDeviceGetCount)
        s_nvmlDeviceGetCount = (nvmlDeviceGetCount_t)GetProcAddress(lib, "nvmlDeviceGetCount");

    s_nvmlDeviceGetHandleByIndex =
        (nvmlDeviceGetHandleByIndex_t)GetProcAddress(lib, "nvmlDeviceGetHandleByIndex_v2");
    if (!s_nvmlDeviceGetHandleByIndex)
        s_nvmlDeviceGetHandleByIndex =
            (nvmlDeviceGetHandleByIndex_t)GetProcAddress(lib, "nvmlDeviceGetHandleByIndex");

    s_nvmlDeviceGetUtilizationRates =
        (nvmlDeviceGetUtilizationRates_t)GetProcAddress(lib, "nvmlDeviceGetUtilizationRates");

    s_nvmlDeviceGetName =
        (nvmlDeviceGetName_t)GetProcAddress(lib, "nvmlDeviceGetName");

#pragma GCC diagnostic pop

    // Verify that all mandatory entry points were resolved
    if (!s_nvmlInit || !s_nvmlDeviceGetCount ||
        !s_nvmlDeviceGetHandleByIndex || !s_nvmlDeviceGetUtilizationRates)
    {
        m_nvmlAvailable = false;
        return;
    }

    if (s_nvmlInit() != NVML_SUCCESS_) {
        m_nvmlAvailable = false;
        return;
    }

    unsigned int deviceCount = 0;
    s_nvmlDeviceGetCount(&deviceCount);
    if (deviceCount == 0) {
        m_nvmlAvailable = false;
        return;
    }

    // Use the first available GPU device
    void* device = nullptr;
    if (s_nvmlDeviceGetHandleByIndex(0, &device) != NVML_SUCCESS_) {
        m_nvmlAvailable = false;
        return;
    }

    m_nvmlDevice    = device;
    m_nvmlAvailable = true;

    // Retrieve the GPU model name and strip common vendor prefixes for brevity
    if (s_nvmlDeviceGetName) {
        char name[128] = {};
        if (s_nvmlDeviceGetName(device, name, sizeof(name)) == NVML_SUCCESS_) {
            m_gpuName = QString::fromLatin1(name);
            m_gpuName.replace("NVIDIA ", "");
            m_gpuName.replace("GeForce ", "");
        }
    }

#elif defined(Q_OS_MACOS)
    // On macOS, enumerate PCI devices via IOKit to obtain the GPU model string.
    // Actual utilisation requires private Apple SPI and is not supported here.
    io_iterator_t iterator;
    CFMutableDictionaryRef matchDict = IOServiceMatching("IOPCIDevice");

#if defined(Q_OS_MACOS) && __MAC_OS_X_VERSION_MIN_REQUIRED < 120000
    mach_port_t mainPort = kIOMasterPortDefault;
#else
    mach_port_t mainPort = kIOMainPortDefault;
#endif

    if (matchDict &&
        IOServiceGetMatchingServices(mainPort, matchDict, &iterator) == KERN_SUCCESS)
    {
        io_service_t service;
        while ((service = IOIteratorNext(iterator)) != 0) {
            CFTypeRef model = IORegistryEntryCreateCFProperty(
                service, CFSTR("model"), kCFAllocatorDefault, 0);

            if (model) {
                if (CFGetTypeID(model) == CFDataGetTypeID()) {
                    const char* modelStr =
                        reinterpret_cast<const char*>(CFDataGetBytePtr((CFDataRef)model));
                    if (modelStr && strlen(modelStr) > 0)
                        m_gpuName = QString::fromUtf8(modelStr);
                }
                CFRelease(model);
            }

            IOObjectRelease(service);
            if (!m_gpuName.isEmpty()) break;
        }
        IOObjectRelease(iterator);
    }

    // Fallback: detect Apple Silicon GPU via CPU brand string
    if (m_gpuName.isEmpty()) {
        char brand[256] = {};
        size_t sz = sizeof(brand);
        if (sysctlbyname("machdep.cpu.brand_string", brand, &sz, nullptr, 0) == 0) {
            if (QString::fromLatin1(brand).contains("Apple"))
                m_gpuName = "Apple GPU";
        }
    }

    // NVML is not available on macOS
    m_nvmlAvailable = false;
#endif
}

void ResourceMonitorWidget::cleanupNvml()
{
#ifdef Q_OS_WIN
    if (m_nvmlAvailable && s_nvmlShutdown)
        s_nvmlShutdown();

    if (m_nvmlLib) {
        FreeLibrary(static_cast<HMODULE>(m_nvmlLib));
        m_nvmlLib = nullptr;
    }
#endif
}

float ResourceMonitorWidget::queryGpuUsage()
{
#ifdef Q_OS_WIN
    // Prefer NVML when available - provides accurate discrete GPU utilisation.
    // On Optimus laptops the dGPU may legitimately report 0% when idle.
    if (m_nvmlAvailable && m_nvmlDevice && s_nvmlDeviceGetUtilizationRates) {
        nvmlUtilization_t_ util;
        if (s_nvmlDeviceGetUtilizationRates(m_nvmlDevice, &util) == NVML_SUCCESS_)
            return static_cast<float>(util.gpu);
    }

    // Fallback: PDH covers integrated Intel/AMD graphics and cases where NVML fails
    if (m_pdhAvailable)
        return queryPdhGpuUsage();

    return -1.0f;

#elif defined(Q_OS_MACOS)
    // Apple Silicon GPU utilisation requires private IOKit APIs; not supported
    return -1.0f;

#else
    return -1.0f;
#endif
}


// ============================================================================
// Windows-only: PDH (Performance Data Helper) GPU counter
// Used as a fallback for integrated and AMD graphics cards.
// ============================================================================

#ifdef Q_OS_WIN

void ResourceMonitorWidget::initPdh()
{
    if (PdhOpenQuery(NULL, 0, &m_pdhQuery) != ERROR_SUCCESS) {
        m_pdhAvailable = false;
        return;
    }

    // Prefer the English counter name (locale-independent); fall back to
    // the localised name on systems where the English index is unavailable.
    if (PdhAddEnglishCounterW(m_pdhQuery,
                              L"\\GPU Engine(*)\\Utilization Percentage",
                              0, &m_pdhGpuCounter) != ERROR_SUCCESS)
    {
        if (PdhAddCounterW(m_pdhQuery,
                           L"\\GPU Engine(*)\\Utilization Percentage",
                           0, &m_pdhGpuCounter) != ERROR_SUCCESS)
        {
            m_pdhAvailable = false;
            PdhCloseQuery(m_pdhQuery);
            m_pdhQuery = nullptr;
            return;
        }
    }

    m_pdhAvailable = true;
}

void ResourceMonitorWidget::cleanupPdh()
{
    if (m_pdhQuery) {
        PdhCloseQuery(m_pdhQuery);
        m_pdhQuery = nullptr;
    }
    m_pdhGpuCounter = nullptr;
}

float ResourceMonitorWidget::queryPdhGpuUsage()
{
    if (!m_pdhQuery || !m_pdhGpuCounter)
        return -1.0f;

    if (PdhCollectQueryData(m_pdhQuery) != ERROR_SUCCESS)
        return -1.0f;

    // First call establishes the required buffer size
    DWORD bufferSize = 0;
    DWORD itemCount  = 0;
    PDH_STATUS status = PdhGetFormattedCounterArray(
        m_pdhGpuCounter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, nullptr);

    if (status != static_cast<PDH_STATUS>(PDH_MORE_DATA))
        return -1.0f;

    std::vector<BYTE> buffer(bufferSize);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM*>(buffer.data());

    status = PdhGetFormattedCounterArray(
        m_pdhGpuCounter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, items);

    if (status != ERROR_SUCCESS)
        return -1.0f;

    // Return the peak utilisation across all GPU engine instances
    // (covers 3D, Video Decode, Copy engines, etc.)
    double maxUsage = 0.0;
    for (DWORD i = 0; i < itemCount; ++i) {
        if (items[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA) {
            if (items[i].FmtValue.doubleValue > maxUsage)
                maxUsage = items[i].FmtValue.doubleValue;
        }
    }

    return static_cast<float>(maxUsage);
}

QString ResourceMonitorWidget::getGpuNameDxgi()
{
    IDXGIFactory1* factory = nullptr;
    if (CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                           reinterpret_cast<void**>(&factory)) != S_OK)
        return QString();

    QString bestName;
    IDXGIAdapter1* adapter = nullptr;

    for (UINT i = 0;
         factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND;
         ++i)
    {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);

        // Skip the Microsoft Basic Render Driver (software fallback)
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            adapter->Release();
            continue;
        }

        // Use the first hardware adapter found
        bestName = QString::fromWCharArray(desc.Description);
        adapter->Release();
        break;
    }

    factory->Release();
    return bestName;
}

#endif // Q_OS_WIN