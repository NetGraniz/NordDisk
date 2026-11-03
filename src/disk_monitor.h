#pragma once

#include <atomic>
#include <map>
#include <string>
#include <thread>
#include <vector>
#include <pdh.h>

constexpr UINT WM_NORDDISK_UPDATE = WM_APP + 17;

struct DiskSnapshot
{
    int physicalDriveId = -1;
    std::wstring model;
    std::wstring connection;
    int temperature = 0;
    bool hasTemperature = false;
    double activity = 0.0;
    bool hasActivity = false;
};

struct MonitorSnapshot
{
    std::vector<DiskSnapshot> disks;
    std::wstring error;
};

class DiskMonitor
{
public:
    DiskMonitor() = default;
    ~DiskMonitor();

    DiskMonitor(const DiskMonitor&) = delete;
    DiskMonitor& operator=(const DiskMonitor&) = delete;

    void Start(HWND target);
    void Stop();
    static MonitorSnapshot CollectForProbe();
    struct PerformanceSample
    {
        long long queryTime = 0;
        long long idleTime = 0;
        long long readTime = 0;
        long long writeTime = 0;
    };

    static std::wstring ConnectionName(int commandType);

private:

    void Run(HWND target);
    static void ConfigureHardwareAccess(class CAtaSmart& engine);
    std::map<int, double> SampleActivity(const std::vector<int>& diskIds);
    std::map<int, double> SamplePdhActivity();
    void ClosePdh();

    std::atomic_bool stop_{false};
    std::thread worker_;
    std::map<int, PerformanceSample> previousPerformance_;
    PDH_HQUERY pdhQuery_ = nullptr;
    PDH_HCOUNTER pdhCounter_ = nullptr;
};

