#include "hardwareload.h"
#include <QFile>
#include <QTextStream>
#include <QHash>
#include <QRegularExpression>
#include <algorithm>
#include <vector>
#include <cstddef>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <pdh.h>
#elif defined(Q_OS_MACOS)
#include <mach/mach.h>
#include <mach/host_info.h>
#include <sys/sysctl.h>
#endif

HardwareLoad HardwareMonitor::sample(const QSet<qint64> &gpuProcesses, bool nvenc)
{
    HardwareLoad load;
    quint64 total = 0, idle = 0;
#ifdef Q_OS_WIN
    FILETIME idleTime, kernelTime, userTime;
    auto ticks = [](const FILETIME &time) {
        return (quint64(time.dwHighDateTime) << 32) | time.dwLowDateTime;
    };
    if (GetSystemTimes(&idleTime, &kernelTime, &userTime))
    {
        total = ticks(kernelTime) + ticks(userTime);
        idle = ticks(idleTime);
    }
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory))
    {
        load.availableMemory = memory.ullAvailPhys;
        load.totalMemory = memory.ullTotalPhys;
    }
#elif defined(Q_OS_MACOS)
    const auto host = mach_host_self();
    host_cpu_load_info_data_t cpu{};
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(host, HOST_CPU_LOAD_INFO, reinterpret_cast<host_info_t>(&cpu), &count) == KERN_SUCCESS)
    {
        for (auto tick : cpu.cpu_ticks)
            total += tick;
        idle = cpu.cpu_ticks[CPU_STATE_IDLE];
    }
    vm_statistics64_data_t memory{};
    count = HOST_VM_INFO64_COUNT;
    vm_size_t pageSize = 0;
    if (host_page_size(host, &pageSize) == KERN_SUCCESS &&
        host_statistics64(host, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&memory), &count) == KERN_SUCCESS)
        load.availableMemory = (quint64(memory.free_count) + memory.inactive_count) * pageSize;
    size_t length = sizeof(load.totalMemory);
    if (sysctlbyname("hw.memsize", &load.totalMemory, &length, nullptr, 0) != 0)
        load.totalMemory = 0;
    mach_port_deallocate(mach_task_self(), host);
#else
    QFile stat("/proc/stat");
    if (stat.open(QIODevice::ReadOnly))
    {
        const auto fields = stat.readLine().simplified().split(' ');
        if (fields.size() >= 5 && fields[0] == "cpu")
        {
            // guest/guest_nice are already included in user/nice.
            for (int i = 1; i < std::min(int(fields.size()), 9); ++i)
                total += fields[i].toULongLong();
            idle = fields[4].toULongLong();
            if (fields.size() > 5)
                idle += fields[5].toULongLong();
        }
    }
    QFile meminfo("/proc/meminfo");
    if (meminfo.open(QIODevice::ReadOnly))
    {
        // procfs reports size zero, so use readLine rather than QFile::atEnd.
        for (QByteArray line; !(line = meminfo.readLine()).isEmpty();)
        {
            const auto fields = line.simplified().split(' ');
            if (fields.size() < 2)
                continue;
            if (fields[0] == "MemTotal:")
                load.totalMemory = fields[1].toULongLong() * 1024;
            else if (fields[0] == "MemAvailable:")
                load.availableMemory = fields[1].toULongLong() * 1024;
        }
    }
#endif
    if (previousTotal && total > previousTotal && idle >= previousIdle)
    {
        const auto elapsed = total - previousTotal;
        load.cpu = std::clamp(1.0 - double(idle - previousIdle) / elapsed, 0.0, 1.0);
    }
    load.gpuEncoding = !gpuProcesses.isEmpty();
    load.gpu = load.gpuEncoding ? gpuLoad(gpuProcesses, nvenc) : -1;
    if (!load.gpuEncoding)
        gpuWasActive = false;
    previousTotal = total;
    previousIdle = idle;
    return load;
}
static bool memoryPressure(const HardwareLoad &load)
{
    return load.totalMemory && load.availableMemory < std::max<quint64>(512ULL * 1024 * 1024, load.totalMemory / 10);
}
int AdaptiveWorkers::ceiling(int cores, int threads)
{
    return std::clamp(std::max(1, cores) / (threads > 0 ? threads : 2), 1, 8);
}
int AdaptiveWorkers::initial(int maximum, const HardwareLoad &load)
{
    return memoryPressure(load) ? 1 : std::clamp(maximum, 1, 2);
}
int AdaptiveWorkers::adjust(int current, int maximum, const HardwareLoad &load, int &headroomSamples)
{
    current = std::clamp(current, 1, std::max(1, maximum));
    if (memoryPressure(load) || load.cpu >= .93 || (load.gpuEncoding && load.gpu >= .90))
    {
        headroomSamples = 0;
        return std::max(1, current - 1);
    }
    const bool memoryHeadroom = load.totalMemory &&
        load.availableMemory >= std::max<quint64>(1024ULL * 1024 * 1024, load.totalMemory / 5);
    if (load.cpu >= 0 && load.cpu < .75 && memoryHeadroom &&
        (!load.gpuEncoding || (load.gpu >= 0 && load.gpu < .75)))
    {
        if (++headroomSamples >= 3)
        {
            headroomSamples = 0;
            return std::min(maximum, current + 1);
        }
    }
    else
        headroomSamples = 0;
    return current;
}

HardwareMonitor::~HardwareMonitor()
{
#ifdef Q_OS_WIN
    if (gpuQuery)
        PdhCloseQuery(gpuQuery);
#endif
    if (nvmlInitialized && nvml)
        if (auto shutdown = reinterpret_cast<int (*)()>(nvml->resolve("nvmlShutdown")))
            shutdown();
}
void HardwareMonitor::reset()
{
    previousTotal = previousIdle = 0;
    gpuWasActive = false;
}
double HardwareMonitor::gpuLoad(const QSet<qint64> &processes, bool nvenc)
{
#ifdef Q_OS_WIN
    if (!gpuQuery)
    {
        PDH_HQUERY query = nullptr;
        PDH_HCOUNTER counter = nullptr;
        if (PdhOpenQueryW(nullptr, 0, &query) == ERROR_SUCCESS)
        {
            if (PdhAddEnglishCounterW(query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &counter) == ERROR_SUCCESS)
            {
                gpuQuery = query;
                gpuCounter = counter;
            }
            else
                PdhCloseQuery(query);
        }
    }
    if (gpuQuery && PdhCollectQueryData(gpuQuery) == ERROR_SUCCESS)
    {
        const bool primed = gpuWasActive;
        gpuWasActive = true;
        DWORD bytes = 0, count = 0;
        if (primed && PdhGetFormattedCounterArrayW(gpuCounter, PDH_FMT_DOUBLE, &bytes, &count, nullptr) == PDH_MORE_DATA)
        {
            // max_align_t storage keeps the native counter structures aligned.
            std::vector<std::max_align_t> buffer((bytes + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t));
            auto *items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(buffer.data());
            if (PdhGetFormattedCounterArrayW(gpuCounter, PDH_FMT_DOUBLE, &bytes, &count, items) == ERROR_SUCCESS)
            {
                const QRegularExpression pattern("^pid_(\\d+)_(luid_.*)_eng_(\\d+)_engtype_.*");
                QSet<QString> adapters;
                QHash<QString, double> engines;
                for (DWORD i = 0; i < count; ++i)
                {
                    if (items[i].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA &&
                        items[i].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA)
                        continue;
                    const auto match = pattern.match(QString::fromWCharArray(items[i].szName));
                    if (!match.hasMatch())
                        continue;
                    const QString adapter = match.captured(2), engine = adapter + "_eng_" + match.captured(3);
                    if (processes.contains(match.captured(1).toLongLong()))
                        adapters.insert(adapter);
                    engines[engine] += qMax(0.0, items[i].FmtValue.doubleValue);
                }
                double busiest = -1;
                for (auto it = engines.cbegin(); it != engines.cend(); ++it)
                    for (const auto &adapter : adapters)
                        if (it.key().startsWith(adapter + "_eng_"))
                            busiest = qMax(busiest, qMin(1.0, it.value() / 100.0));
                if (busiest >= 0)
                    return busiest;
            }
        }
    }
#endif
    // Match encoder session PIDs to the actual device, including multi-GPU systems.
    // NVML is optional and is loaded only after actual NVENC output confirms use.
    if (nvenc)
    {
        if (!nvml)
        {
#ifdef Q_OS_WIN
            nvml = std::make_unique<QLibrary>("nvml");
#else
            nvml = std::make_unique<QLibrary>("libnvidia-ml.so.1");
#endif
        }
        if (nvml->load())
        {
            const auto init = reinterpret_cast<int (*)()>(nvml->resolve("nvmlInit_v2"));
            const auto device = reinterpret_cast<int (*)(unsigned int, void **)>(nvml->resolve("nvmlDeviceGetHandleByIndex_v2"));
            struct Utilization { unsigned int gpu, memory; };
            const auto utilization = reinterpret_cast<int (*)(void *, Utilization *)>(nvml->resolve("nvmlDeviceGetUtilizationRates"));
            const auto encoder = reinterpret_cast<int (*)(void *, unsigned int *, unsigned int *)>(nvml->resolve("nvmlDeviceGetEncoderUtilization"));
            if (!nvmlInitialized && init && init() == 0)
                nvmlInitialized = true;
            // Stable NVML encoder-session ABI (eight 32-bit fields).
            struct EncoderSession {
                unsigned int sessionId, pid, vgpuInstance, codecType, width, height, averageFps, averageLatency;
            };
            const auto deviceCount = reinterpret_cast<int (*)(unsigned int *)>(nvml->resolve("nvmlDeviceGetCount_v2"));
            const auto sessions = reinterpret_cast<int (*)(void *, unsigned int *, EncoderSession *)>(nvml->resolve("nvmlDeviceGetEncoderSessions"));
            unsigned int count = 0;
            double busy = -1;
            if (nvmlInitialized && device && deviceCount && sessions && deviceCount(&count) == 0)
                for (unsigned int i = 0; i < count; ++i)
                {
                    void *handle = nullptr;
                    if (device(i, &handle) != 0)
                        continue;
                    unsigned int size = 0;
                    if (sessions(handle, &size, nullptr) != 0 || size == 0 || size > 4096)
                        continue;
                    std::vector<EncoderSession> active(size);
                    if (sessions(handle, &size, active.data()) != 0)
                        continue;
                    bool ours = false;
                    for (unsigned int j = 0; j < size; ++j)
                        ours |= processes.contains(active[j].pid);
                    if (!ours)
                        continue;
                    Utilization rates{};
                    unsigned int encode = 0, period = 0;
                    if (utilization && utilization(handle, &rates) == 0)
                        busy = qMax(busy, rates.gpu / 100.0);
                    if (encoder && encoder(handle, &encode, &period) == 0)
                        busy = qMax(busy, encode / 100.0);
                }
            return qMin(1.0, busy);
        }
    }
    return -1;
}
