#pragma once
#include <QtGlobal>
#include <QSet>
#include <QLibrary>
#include <memory>

struct HardwareLoad
{
    double cpu = -1; // Fraction busy since the previous sample; -1 means unavailable.
    double gpu = -1;
    bool gpuEncoding = false;
    quint64 availableMemory = 0, totalMemory = 0;
};
class HardwareMonitor
{
  public:
    ~HardwareMonitor();
    HardwareLoad sample(const QSet<qint64> &gpuProcesses = {}, bool nvenc = false);
    void reset();
  private:
    double gpuLoad(const QSet<qint64> &, bool nvenc);
    void *gpuQuery = nullptr, *gpuCounter = nullptr;
    bool gpuWasActive = false, nvmlInitialized = false;
    std::unique_ptr<QLibrary> nvml;
    quint64 previousTotal = 0, previousIdle = 0;
};
class AdaptiveWorkers
{
  public:
    static int ceiling(int cores, int threads);
    static int initial(int maximum, const HardwareLoad &);
    static int adjust(int current, int maximum, const HardwareLoad &, int &headroomSamples);
};
