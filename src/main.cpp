#include "stdafx.h"
#include "disk_monitor.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <shellapi.h>
#include <string>
#include <vector>
#include <dwmapi.h>
#include <windowsx.h>

#pragma comment(lib, "dwmapi.lib")

namespace
{
constexpr wchar_t kClassName[] = L"NordDisk.Native.Window";
constexpr wchar_t kTitle[] = L"NordDisk";
constexpr int kBaseWidth = 424;
constexpr int kHeaderHeight = 72;
constexpr int kRowHeight = 70;
constexpr int kFooterHeight = 14;
constexpr int kMargin = 12;

struct AppState
{
    DiskMonitor monitor;
    std::vector<DiskSnapshot> disks;
    std::wstring error;
    HFONT titleFont = nullptr;
    HFONT modelFont = nullptr;
    HFONT valueFont = nullptr;
    HFONT smallFont = nullptr;
    int dpi = 96;
    int scroll = 0;
    int visibleRows = 1;
    bool loading = true;
    bool alwaysOnTop = true;
    bool userMoved = false;
};

int Scale(const AppState& state, int value)
{
    return ::MulDiv(value, state.dpi, 96);
}

void DeleteFonts(AppState& state)
{
    for (HFONT* font : {&state.titleFont, &state.modelFont, &state.valueFont, &state.smallFont})
    {
        if (*font)
        {
            ::DeleteObject(*font);
            *font = nullptr;
        }
    }
}

HFONT MakeFont(const AppState& state, int points, int weight)
{
    return ::CreateFontW(-::MulDiv(points, state.dpi, 72), 0, 0, 0, weight,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
        L"Segoe UI");
}

void CreateFonts(AppState& state)
{
    DeleteFonts(state);
    state.titleFont = MakeFont(state, 17, FW_SEMIBOLD);
    state.modelFont = MakeFont(state, 10, FW_SEMIBOLD);
    state.valueFont = MakeFont(state, 13, FW_BOLD);
    state.smallFont = MakeFont(state, 9, FW_NORMAL);
}

void Fill(HDC dc, const RECT& rect, COLORREF color)
{
    HBRUSH brush = ::CreateSolidBrush(color);
    ::FillRect(dc, &rect, brush);
    ::DeleteObject(brush);
}

void Rounded(HDC dc, const RECT& rect, COLORREF color, int radius)
{
    HBRUSH brush = ::CreateSolidBrush(color);
    HPEN pen = ::CreatePen(PS_SOLID, 1, color);
    const HGDIOBJ oldBrush = ::SelectObject(dc, brush);
    const HGDIOBJ oldPen = ::SelectObject(dc, pen);
    ::RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    ::SelectObject(dc, oldPen);
    ::SelectObject(dc, oldBrush);
    ::DeleteObject(pen);
    ::DeleteObject(brush);
}

void Text(HDC dc, const std::wstring& text, RECT rect, HFONT font,
    COLORREF color, UINT format)
{
    const HGDIOBJ old = ::SelectObject(dc, font);
    ::SetTextColor(dc, color);
    ::SetBkMode(dc, TRANSPARENT);
    ::DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect, format);
    ::SelectObject(dc, old);
}

COLORREF TemperatureColor(const DiskSnapshot& disk)
{
    if (!disk.hasTemperature) return RGB(126, 136, 148);
    if (disk.temperature >= 55) return RGB(255, 91, 91);
    if (disk.temperature >= 45) return RGB(255, 190, 73);
    return RGB(74, 222, 128);
}

void PaintWindow(HWND window, AppState& state)
{
    PAINTSTRUCT paint{};
    HDC target = ::BeginPaint(window, &paint);
    RECT client{};
    ::GetClientRect(window, &client);
    HDC dc = ::CreateCompatibleDC(target);
    HBITMAP bitmap = ::CreateCompatibleBitmap(target, client.right, client.bottom);
    const HGDIOBJ oldBitmap = ::SelectObject(dc, bitmap);

    Fill(dc, client, RGB(14, 18, 23));
    const int margin = Scale(state, kMargin);
    RECT titleRect{margin, Scale(state, 12), client.right - Scale(state, 52), Scale(state, 40)};
    Text(dc, L"NordDisk", titleRect, state.titleFont, RGB(239, 244, 250),
        DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    RECT subtitle{margin, Scale(state, 39), client.right - margin, Scale(state, 61)};
    std::wstring status = state.loading ? L"Поиск дисков и SMART…" :
        std::to_wstring(state.disks.size()) + L" физических дисков • SMART + нагрузка";
    Text(dc, status, subtitle, state.smallFont, RGB(118, 132, 149),
        DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    RECT closeRect{client.right - Scale(state, 42), Scale(state, 10),
        client.right - Scale(state, 10), Scale(state, 42)};
    Rounded(dc, closeRect, RGB(27, 33, 41), Scale(state, 9));
    Text(dc, L"×", closeRect, state.valueFont, RGB(161, 174, 189),
        DT_CENTER | DT_SINGLELINE | DT_VCENTER);

    if (!state.error.empty() && state.disks.empty())
    {
        RECT errorRect{margin, Scale(state, kHeaderHeight), client.right - margin,
            client.bottom - margin};
        Rounded(dc, errorRect, RGB(29, 35, 43), Scale(state, 12));
        ::InflateRect(&errorRect, -Scale(state, 16), -Scale(state, 12));
        Text(dc, state.error, errorRect, state.modelFont, RGB(255, 190, 73),
            DT_LEFT | DT_WORDBREAK | DT_VCENTER);
    }
    else
    {
        const int start = std::clamp(state.scroll, 0,
            std::max(0, static_cast<int>(state.disks.size()) - state.visibleRows));
        const int finish = std::min(static_cast<int>(state.disks.size()), start + state.visibleRows);
        for (int index = start; index < finish; ++index)
        {
            const int visual = index - start;
            const int top = Scale(state, kHeaderHeight + visual * kRowHeight);
            RECT row{margin, top, client.right - margin, top + Scale(state, kRowHeight - 8)};
            Rounded(dc, row, RGB(25, 31, 38), Scale(state, 12));

            RECT icon{row.left + Scale(state, 12), row.top + Scale(state, 13),
                row.left + Scale(state, 44), row.top + Scale(state, 45)};
            Rounded(dc, icon, RGB(34, 43, 53), Scale(state, 8));
            RECT dot{icon.left + Scale(state, 7), icon.top + Scale(state, 8),
                icon.right - Scale(state, 7), icon.bottom - Scale(state, 8)};
            Rounded(dc, dot, RGB(72, 155, 255), Scale(state, 4));

            const auto& disk = state.disks[static_cast<size_t>(index)];
            RECT model{row.left + Scale(state, 54), row.top + Scale(state, 8),
                row.right - Scale(state, 125), row.top + Scale(state, 31)};
            Text(dc, disk.model, model, state.modelFont, RGB(226, 233, 241),
                DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            RECT details{model.left, row.top + Scale(state, 31), model.right,
                row.top + Scale(state, 50)};
            std::wstring detail = L"Диск " + std::to_wstring(disk.physicalDriveId)
                + L" • " + disk.connection;
            Text(dc, detail, details, state.smallFont, RGB(112, 126, 143),
                DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

            RECT temperature{row.right - Scale(state, 118), row.top + Scale(state, 7),
                row.right - Scale(state, 60), row.top + Scale(state, 34)};
            const std::wstring tempText = disk.hasTemperature
                ? std::to_wstring(disk.temperature) + L"°C" : L"N/A";
            Text(dc, tempText, temperature, state.valueFont, TemperatureColor(disk),
                DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
            RECT activityText{row.right - Scale(state, 57), row.top + Scale(state, 8),
                row.right - Scale(state, 12), row.top + Scale(state, 33)};
            const std::wstring activity = disk.hasActivity
                ? std::to_wstring(static_cast<int>(std::lround(disk.activity))) + L"%" : L"—";
            Text(dc, activity, activityText, state.modelFont, RGB(91, 169, 255),
                DT_RIGHT | DT_SINGLELINE | DT_VCENTER);

            RECT track{row.left + Scale(state, 54), row.bottom - Scale(state, 10),
                row.right - Scale(state, 12), row.bottom - Scale(state, 6)};
            Rounded(dc, track, RGB(40, 48, 58), Scale(state, 3));
            if (disk.hasActivity && disk.activity > 0.0)
            {
                RECT level = track;
                level.right = level.left + static_cast<int>((track.right - track.left)
                    * std::clamp(disk.activity, 0.0, 100.0) / 100.0);
                if (level.right <= level.left) level.right = level.left + 1;
                Rounded(dc, level, RGB(72, 155, 255), Scale(state, 3));
            }
        }
    }

    ::BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
    ::SelectObject(dc, oldBitmap);
    ::DeleteObject(bitmap);
    ::DeleteDC(dc);
    ::EndPaint(window, &paint);
}

void ResizeWindow(HWND window, AppState& state, bool placeAtTopRight)
{
    RECT work{};
    ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int width = Scale(state, kBaseWidth);
    const int availableHeight = static_cast<int>(work.bottom - work.top) - Scale(state, 110);
    const int maxRows = std::max(1, availableHeight / Scale(state, kRowHeight));
    state.visibleRows = std::clamp(static_cast<int>(state.disks.size()), 1, std::min(9, maxRows));
    const int height = Scale(state, kHeaderHeight + state.visibleRows * kRowHeight + kFooterHeight);
    RECT current{};
    ::GetWindowRect(window, &current);
    int x = current.left;
    int y = current.top;
    if (placeAtTopRight || !state.userMoved)
    {
        x = work.right - width - Scale(state, 16);
        y = work.top + Scale(state, 16);
    }
    ::SetWindowPos(window, state.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
        x, y, width, height, SWP_NOACTIVATE);
    state.scroll = std::clamp(state.scroll, 0,
        std::max(0, static_cast<int>(state.disks.size()) - state.visibleRows));
}

bool PointInClose(HWND window, AppState& state, LPARAM position)
{
    RECT client{};
    ::GetClientRect(window, &client);
    const int x = GET_X_LPARAM(position);
    const int y = GET_Y_LPARAM(position);
    return x >= client.right - Scale(state, 48) && y <= Scale(state, 48);
}

void ShowContextMenu(HWND window, AppState& state)
{
    HMENU menu = ::CreatePopupMenu();
    ::AppendMenuW(menu, MF_STRING | (state.alwaysOnTop ? MF_CHECKED : 0), 1, L"Всегда сверху");
    ::AppendMenuW(menu, MF_STRING, 2, L"О программе");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, 3, L"Выход");
    POINT cursor{};
    ::GetCursorPos(&cursor);
    const UINT command = ::TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
        cursor.x, cursor.y, 0, window, nullptr);
    ::DestroyMenu(menu);
    if (command == 1)
    {
        state.alwaysOnTop = !state.alwaysOnTop;
        ::SetWindowPos(window, state.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
            0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    else if (command == 2)
    {
        ::MessageBoxW(window,
            L"NordDisk\nЛёгкий монитор температуры и нагрузки физических дисков.\n\n"
            L"Единый встроенный модуль диагностики SATA, NVMe, USB и RAID.\n"
            L"Сведения о сторонних лицензиях находятся в документации проекта.",
            L"О программе", MB_OK | MB_ICONINFORMATION);
    }
    else if (command == 3)
    {
        ::DestroyWindow(window);
    }
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* state = reinterpret_cast<AppState*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message)
    {
    case WM_NCCREATE:
    {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<AppState*>(create->lpCreateParams);
        ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        return TRUE;
    }
    case WM_CREATE:
    {
        state->dpi = static_cast<int>(::GetDpiForWindow(window));
        CreateFonts(*state);
        const BOOL dark = TRUE;
        ::DwmSetWindowAttribute(window, 20, &dark, sizeof(dark));
        const int rounded = 2;
        ::DwmSetWindowAttribute(window, 33, &rounded, sizeof(rounded));
        ResizeWindow(window, *state, true);
        state->monitor.Start(window);
        return 0;
    }
    case WM_NCHITTEST:
    {
        const LRESULT hit = ::DefWindowProcW(window, message, wParam, lParam);
        if (hit != HTCLIENT || !state) return hit;
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ::ScreenToClient(window, &point);
        RECT client{};
        ::GetClientRect(window, &client);
        if (point.y < Scale(*state, 58) && point.x < client.right - Scale(*state, 52))
            return HTCAPTION;
        return HTCLIENT;
    }
    case WM_LBUTTONUP:
        if (state && PointInClose(window, *state, lParam)) ::DestroyWindow(window);
        return 0;
    case WM_RBUTTONUP:
        if (state) ShowContextMenu(window, *state);
        return 0;
    case WM_MOUSEWHEEL:
        if (state && static_cast<int>(state->disks.size()) > state->visibleRows)
        {
            state->scroll += GET_WHEEL_DELTA_WPARAM(wParam) < 0 ? 1 : -1;
            state->scroll = std::clamp(state->scroll, 0,
                std::max(0, static_cast<int>(state->disks.size()) - state->visibleRows));
            ::InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_DPICHANGED:
        if (state)
        {
            state->dpi = HIWORD(wParam);
            CreateFonts(*state);
            const RECT* suggested = reinterpret_cast<RECT*>(lParam);
            ::SetWindowPos(window, nullptr, suggested->left, suggested->top,
                suggested->right - suggested->left, suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
            ResizeWindow(window, *state, false);
        }
        return 0;
    case WM_EXITSIZEMOVE:
        if (state) state->userMoved = true;
        return 0;
    case WM_NORDDISK_UPDATE:
        if (state)
        {
            std::unique_ptr<MonitorSnapshot> update(reinterpret_cast<MonitorSnapshot*>(lParam));
            const size_t oldCount = state->disks.size();
            state->disks = std::move(update->disks);
            state->error = std::move(update->error);
            state->loading = false;
            if (oldCount != state->disks.size()) ResizeWindow(window, *state, false);
            ::InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_PAINT:
        if (state) PaintWindow(window, *state);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        if (state)
        {
            state->monitor.Stop();
            MSG pending{};
            while (::PeekMessageW(&pending, window, WM_NORDDISK_UPDATE,
                WM_NORDDISK_UPDATE, PM_REMOVE))
            {
                delete reinterpret_cast<MonitorSnapshot*>(pending.lParam);
            }
            DeleteFonts(*state);
        }
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(window, message, wParam, lParam);
}

std::string Utf8(const std::wstring& value)
{
    if (value.empty()) return {};
    const int size = ::WideCharToMultiByte(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(size), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        result.data(), size, nullptr, nullptr);
    return result;
}

int RunProbe(const wchar_t* outputPath)
{
    const MonitorSnapshot snapshot = DiskMonitor::CollectForProbe();
    std::string text = "physical_drive\tmodel\ttemperature_c\tactivity_percent\tactivity_available\n";
    for (const auto& disk : snapshot.disks)
    {
        text += std::to_string(disk.physicalDriveId) + "\t" + Utf8(disk.model) + "\t";
        text += disk.hasTemperature ? std::to_string(disk.temperature) : "NA";
        text += "\t" + std::to_string(disk.activity) + "\t" + (disk.hasActivity ? "1" : "0") + "\n";
    }
    if (!snapshot.error.empty()) text += "error\t" + Utf8(snapshot.error) + "\n";
    HANDLE file = ::CreateFileW(outputPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return 3;
    DWORD written = 0;
    const BOOL ok = ::WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    ::CloseHandle(file);
    return ok && written == text.size() ? 0 : 4;
}
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int)
{
    if (!AfxWinInit(instance, nullptr, ::GetCommandLineW(), 0)) return 2;

    int argumentCount = 0;
    LPWSTR* arguments = ::CommandLineToArgvW(::GetCommandLineW(), &argumentCount);
    if (arguments && argumentCount == 3 && _wcsicmp(arguments[1], L"--probe") == 0)
    {
        const std::wstring path = arguments[2];
        ::LocalFree(arguments);
        return RunProbe(path.c_str());
    }
    if (arguments) ::LocalFree(arguments);

    HANDLE instanceMutex = ::CreateMutexW(nullptr, TRUE, L"Local\\NordDisk.Native.SingleInstance");
    if (!instanceMutex || ::GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (instanceMutex) ::CloseHandle(instanceMutex);
        return 0;
    }

    const auto setDpi = reinterpret_cast<BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT)>(
        ::GetProcAddress(::GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
    if (setDpi) setDpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = ::LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hIconSm = windowClass.hIcon;
    windowClass.hbrBackground = static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH));
    windowClass.lpszClassName = kClassName;
    if (!::RegisterClassExW(&windowClass)) return 5;

    AppState state;
    HWND window = ::CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        kClassName, kTitle, WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT,
        kBaseWidth, 200, nullptr, nullptr, instance, &state);
    if (!window) return 6;
    ::ShowWindow(window, SW_SHOWNOACTIVATE);
    ::InvalidateRect(window, nullptr, TRUE);
    ::UpdateWindow(window);

    MSG message{};
    while (::GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }
    ::ReleaseMutex(instanceMutex);
    ::CloseHandle(instanceMutex);
    return static_cast<int>(message.wParam);
}

