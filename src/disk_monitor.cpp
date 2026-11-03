#include "stdafx.h"
#include "disk_monitor.h"

#include "storage_engine/AtaSmart.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <set>
#include <thread>
#include <winioctl.h>
#include <pdhmsg.h>

using namespace std::chrono_literals;

DiskMonitor::~DiskMonitor()
{
    Stop();
    ClosePdh();
}

void DiskMonitor::Start(HWND target)
{
    Stop();
    stop_ = false;
    worker_ = std::thread(&DiskMonitor::Run, this, target);
}

void DiskMonitor::Stop()
{
    stop_ = true;
    if (worker_.joinable())
    {
        worker_.join();
    }
}

void DiskMonitor::ConfigureHardwareAccess(CAtaSmart& engine)
{
    engine.FlagUsbSat = TRUE;
    engine.FlagUsbIodata = TRUE;
    engine.FlagUsbSunplus = TRUE;
    engine.FlagUsbLogitec = TRUE;
    engine.FlagUsbProlific = TRUE;
    engine.FlagUsbJmicron = TRUE;
    engine.FlagUsbCypress = TRUE;
    engine.FlagUsbNVMeJMicron = TRUE;
    engine.FlagUsbNVMeASMedia = TRUE;
    engine.FlagUsbNVMeRealtek = TRUE;
    engine.FlagAMD_RC2 = TRUE;
    engine.FlagMegaRAID = TRUE;
    engine.FlagIntelVROC = TRUE;
}

std::wstring DiskMonitor::ConnectionName(int commandType)
{
    switch (commandType)
    {
    case 4:
    case 10:
    case 11:
    case 12:
    case 13:
    case 19:
    case 20:
    case 21:
    case 22:
        return L"USB / UASP";
    case 17:
    case 18:
    case 23:
    case 24:
        return L"NVMe";
    case 25:
    case 26:
    case 27:
        return L"RAID";
    default:
        return L"SATA / диск";
    }
}

static bool ReadPerformanceSample(int diskId, DiskMonitor::PerformanceSample& sample)
{
    wchar_t path[64]{};
    swprintf_s(path, L"\\\\.\\PhysicalDrive%d", diskId);
    HANDLE disk = ::CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (disk == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    DISK_PERFORMANCE performance{};
    DWORD returned = 0;
    const BOOL ok = ::DeviceIoControl(disk, IOCTL_DISK_PERFORMANCE,
        nullptr, 0, &performance, sizeof(performance), &returned, nullptr);
    ::CloseHandle(disk);
    if (!ok || returned < sizeof(performance))
    {
        return false;
    }

    sample.queryTime = performance.QueryTime.QuadPart;
    sample.idleTime = performance.IdleTime.QuadPart;
    sample.readTime = performance.ReadTime.QuadPart;
    sample.writeTime = performance.WriteTime.QuadPart;
    return true;
}

std::map<int, double> DiskMonitor::SampleActivity(const std::vector<int>& diskIds)
{
    std::map<int, double> result;
    std::set<int> activeIds;
    for (const int id : diskIds)
    {
        if (id < 0)
        {
            continue;
        }
        activeIds.insert(id);
        PerformanceSample current{};
        if (!ReadPerformanceSample(id, current))
        {
            continue;
        }

        const auto previous = previousPerformance_.find(id);
        if (previous != previousPerformance_.end())
        {
            const long long queryDelta = current.queryTime - previous->second.queryTime;
            const long long idleDelta = current.idleTime - previous->second.idleTime;
            const long long ioDelta = (current.readTime - previous->second.readTime)
                + (current.writeTime - previous->second.writeTime);
            if (queryDelta > 0)
            {
                double busy = 0.0;
                if (idleDelta >= 0 && idleDelta <= queryDelta)
                {
                    busy = 100.0 * static_cast<double>(queryDelta - idleDelta)
                        / static_cast<double>(queryDelta);
                }
                else if (ioDelta >= 0)
                {
                    busy = 100.0 * static_cast<double>(ioDelta)
                        / static_cast<double>(queryDelta);
                }
                result[id] = std::clamp(busy, 0.0, 100.0);
            }
        }
        previousPerformance_[id] = current;
    }

    for (auto it = previousPerformance_.begin(); it != previousPerformance_.end();)
    {
        if (!activeIds.contains(it->first))
        {
            it = previousPerformance_.erase(it);
        }
        else
        {
            ++it;
        }
    }
    const auto pdh = SamplePdhActivity();
    for (const auto& [id, value] : pdh)
    {
        if (activeIds.contains(id) && !result.contains(id))
        {
            result[id] = value;
        }
    }
    return result;
}

std::map<int, double> DiskMonitor::SamplePdhActivity()
{
    std::map<int, double> result;
    if (!pdhQuery_)
    {
        if (::PdhOpenQueryW(nullptr, 0, &pdhQuery_) != ERROR_SUCCESS)
        {
            pdhQuery_ = nullptr;
            return result;
        }
        if (::PdhAddEnglishCounterW(pdhQuery_, L"\\PhysicalDisk(*)\\% Disk Time",
                0, &pdhCounter_) != ERROR_SUCCESS)
        {
            ClosePdh();
            return result;
        }
    }

    if (::PdhCollectQueryData(pdhQuery_) != ERROR_SUCCESS)
    {
        return result;
    }
    DWORD bytes = 0;
    DWORD count = 0;
    PDH_STATUS status = ::PdhGetFormattedCounterArrayW(pdhCounter_, PDH_FMT_DOUBLE,
        &bytes, &count, nullptr);
    if (status != PDH_MORE_DATA || bytes == 0)
    {
        return result;
    }
    std::vector<BYTE> buffer(bytes);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
    status = ::PdhGetFormattedCounterArrayW(pdhCounter_, PDH_FMT_DOUBLE,
        &bytes, &count, items);
    if (status != ERROR_SUCCESS)
    {
        return result;
    }
    for (DWORD index = 0; index < count; ++index)
    {
        const wchar_t* name = items[index].szName;
        if (!name || *name < L'0' || *name > L'9')
        {
            continue;
        }
        wchar_t* end = nullptr;
        const long id = std::wcstol(name, &end, 10);
        if (end == name || id < 0 || id > 1024)
        {
            continue;
        }
        const DWORD valueStatus = items[index].FmtValue.CStatus;
        if (valueStatus == PDH_CSTATUS_VALID_DATA || valueStatus == PDH_CSTATUS_NEW_DATA)
        {
            result[static_cast<int>(id)] = std::clamp(
                items[index].FmtValue.doubleValue, 0.0, 100.0);
        }
    }
    return result;
}

void DiskMonitor::ClosePdh()
{
    if (pdhQuery_)
    {
        ::PdhCloseQuery(pdhQuery_);
        pdhQuery_ = nullptr;
        pdhCounter_ = nullptr;
    }
}

static std::vector<DiskSnapshot> BuildDiskSnapshots(CAtaSmart& engine)
{
    std::vector<DiskSnapshot> disks;
    disks.reserve(static_cast<size_t>(engine.vars.GetCount()));
    for (INT_PTR i = 0; i < engine.vars.GetCount(); ++i)
    {
        const auto& source = engine.vars[static_cast<int>(i)];
        DiskSnapshot disk;
        disk.physicalDriveId = source.PhysicalDriveId;
        disk.model = source.Model.GetString();
        disk.connection = DiskMonitor::ConnectionName(source.CommandType);
        disk.temperature = source.Temperature;
        disk.hasTemperature = source.IsSmartEnabled && source.Temperature > 0 && source.Temperature < 200;
        disks.push_back(std::move(disk));
    }
    std::stable_sort(disks.begin(), disks.end(), [](const auto& left, const auto& right) {
        return left.physicalDriveId < right.physicalDriveId;
    });
    return disks;
}

void DiskMonitor::Run(HWND target)
{
    const HRESULT com = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    CAtaSmart engine;
    ConfigureHardwareAccess(engine);
    BOOL changed = FALSE;
    engine.Init(TRUE, TRUE, &changed, FALSE, FALSE, FALSE, FALSE, FALSE);

    unsigned seconds = 0;
    while (!stop_)
    {
        if (seconds > 0 && seconds % 60 == 0)
        {
            engine.Init(TRUE, TRUE, &changed, FALSE, FALSE, FALSE, FALSE, FALSE);
        }
        else if (seconds > 0 && seconds % 10 == 0)
        {
            for (INT_PTR i = 0; i < engine.vars.GetCount(); ++i)
            {
                engine.UpdateSmartInfo(static_cast<DWORD>(i));
            }
        }

        auto update = std::make_unique<MonitorSnapshot>();
        update->disks = BuildDiskSnapshots(engine);
        if (update->disks.empty())
        {
            update->error = L"Диски не обнаружены. Запустите NordDisk от администратора.";
        }

        std::vector<int> ids;
        ids.reserve(update->disks.size());
        for (const auto& disk : update->disks)
        {
            ids.push_back(disk.physicalDriveId);
        }
        const auto activity = SampleActivity(ids);
        for (auto& disk : update->disks)
        {
            const auto found = activity.find(disk.physicalDriveId);
            if (found != activity.end())
            {
                disk.activity = found->second;
                disk.hasActivity = true;
            }
        }

        if (!::PostMessageW(target, WM_NORDDISK_UPDATE, 0,
                reinterpret_cast<LPARAM>(update.get())))
        {
            break;
        }
        update.release();

        for (int tenth = 0; tenth < 10 && !stop_; ++tenth)
        {
            std::this_thread::sleep_for(100ms);
        }
        ++seconds;
    }

    if (SUCCEEDED(com))
    {
        ::CoUninitialize();
    }
    ClosePdh();
}

MonitorSnapshot DiskMonitor::CollectForProbe()
{
    const HRESULT com = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    CAtaSmart engine;
    ConfigureHardwareAccess(engine);
    BOOL changed = FALSE;
    engine.Init(TRUE, TRUE, &changed, FALSE, FALSE, FALSE, FALSE, FALSE);

    DiskMonitor sampler;
    MonitorSnapshot result;
    result.disks = BuildDiskSnapshots(engine);
    std::vector<int> ids;
    for (const auto& disk : result.disks)
    {
        ids.push_back(disk.physicalDriveId);
    }
    sampler.SampleActivity(ids);
    std::this_thread::sleep_for(1200ms);
    const auto activity = sampler.SampleActivity(ids);
    for (auto& disk : result.disks)
    {
        const auto found = activity.find(disk.physicalDriveId);
        if (found != activity.end())
        {
            disk.activity = found->second;
            disk.hasActivity = true;
        }
    }
    if (result.disks.empty())
    {
        result.error = L"No disks detected";
    }
    if (SUCCEEDED(com))
    {
        ::CoUninitialize();
    }
    sampler.ClosePdh();
    return result;
}

