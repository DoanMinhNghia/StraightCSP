// ============================================================================
// CSP Shift Line Helper - Universal Multi-Brand Tablet & Modern GUI Edition
// Supports: Wacom, Huion, XP-Pen, Gaomon, Veikk, Xencelabs, Ugee, Parblo, Surface
// Version: 1.0.4 (Optimized Anti-Jitter for slow strokes)
// ============================================================================

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define _WIN32_WINNT 0x0601

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <cmath>
#include <string>
#include <cwchar>
#include <vector>
#include <algorithm>
#include <commdlg.h>
#include <psapi.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comdlg32.lib")

using namespace Gdiplus;

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
DECLARE_HANDLE(DPI_AWARENESS_CONTEXT);
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

// ----------------------------------------------------------------------------
// Constants & Custom Messages
// ----------------------------------------------------------------------------
constexpr UINT WM_TRAYICON              = WM_APP + 1;
constexpr UINT WM_UPDATE_UI             = WM_APP + 2;
constexpr UINT_PTR TIMER_ID_WATCHDOG    = 101;

constexpr int  HOTKEY_ID_TOGGLE         = 1;
constexpr int  HOTKEY_ID_EXIT           = 2;

constexpr UINT IDM_TRAY_OPEN            = 1001;
constexpr UINT IDM_TRAY_TOGGLE          = 1002;
constexpr UINT IDM_TRAY_MODE_HYBRID     = 1003;
constexpr UINT IDM_TRAY_MODE_SENDINPUT  = 1004;
constexpr UINT IDM_TRAY_MODE_WACOM      = 1005;
constexpr UINT IDM_TRAY_STARTUP         = 1006;
constexpr UINT IDM_TRAY_EXIT            = 1007;

constexpr int  WIN_WIDTH                = 520;
constexpr int  WIN_HEIGHT               = 820;

constexpr ULONG_PTR kInjectedSignature  = 0x4353504C; // 'CSPL'

static const wchar_t* kWindowClassName  = L"StraightCSPWnd";
static const wchar_t* kAppTitle         = L"StraightCSP";
static const wchar_t* kMutexName        = L"Local\\StraightCSP_SingleInstance_Mutex";
static const wchar_t* kRunRegPath       = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"; // Giữ nguyên dòng này
static const wchar_t* kRunValueName     = L"StraightCSP";
static const wchar_t* kConfigRegPath    = L"Software\\StraightCSP";

UINT g_wmShowExisting = 0;

// ----------------------------------------------------------------------------
// State Types & Settings
// ----------------------------------------------------------------------------
enum class AxisLock { NONE = 0, HORIZONTAL, VERTICAL };
enum class TabletEngineMode { HYBRID_UNIVERSAL = 0, FORCE_SENDINPUT = 1, WACOM_CLASSIC = 2 };

HINSTANCE        g_hInst             = nullptr;
HWND             g_hMainWnd          = nullptr;
HHOOK            g_mouseHook         = nullptr;
HICON            g_hIconActive       = nullptr;
HICON            g_hIconPaused       = nullptr;
NOTIFYICONDATAW  g_nid               = {};

bool             g_toolEnabled       = true;
bool             g_runOnStartup      = false;
bool             g_closeToTray       = true;
bool             g_alwaysRunAsAdmin  = false;
bool             g_isExiting         = false;
bool             g_trayBalloonShown  = false;
bool             g_trackingMouse     = false;
bool             g_isRunningAsAdmin  = false;
bool             g_isSettingHotkey   = false;
int              g_dragDeadzonePx    = 2;
UINT             g_hotkeyToggle      = VK_F8;
TabletEngineMode g_engineMode        = TabletEngineMode::HYBRID_UNIVERSAL;

bool             g_isPenDown         = false;
bool             g_shiftAnchored     = false;
AxisLock         g_lockedAxis        = AxisLock::NONE;
POINT            g_downPt            = {0, 0};
POINT            g_lastClampedPt     = {0, 0};

std::vector<std::wstring> g_targetExes;

enum class HitZone {
    NONE = 0, TOGGLE_ACTIVE, MODE_PREV, MODE_NEXT, BTN_ELEVATE_ADMIN,
    DEADZONE_MINUS, DEADZONE_PLUS, TOGGLE_STARTUP, TOGGLE_CLOSE_TO_TRAY,
    TOGGLE_ALWAYS_ADMIN, BTN_HIDE_TRAY, BTN_EXIT_APP, BTN_ADD_EXE, 
    BTN_CLEAR_EXE, BTN_CHANGE_HOTKEY
};

HitZone g_hoveredZone = HitZone::NONE;
RectF g_rcToggleActive, g_rcModePrev, g_rcModeNext, g_rcBtnElevate;
RectF g_rcDeadzoneMinus, g_rcDeadzonePlus, g_rcToggleStartup, g_rcToggleCloseTray;
RectF g_rcToggleAlwaysAdmin, g_rcBtnHideTray, g_rcBtnExitApp, g_rcBtnAddExe, g_rcBtnClearExe;
RectF g_rcBtnChangeHotkey;

// ----------------------------------------------------------------------------
// Utility & Privilege Functions
// ----------------------------------------------------------------------------
bool CheckIsRunningAsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin != FALSE;
}

std::wstring GetExecutablePath() {
    wchar_t buffer[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return std::wstring(buffer);
}

void RelaunchAsAdmin(HWND hwnd) {
    std::wstring exePath = GetExecutablePath();
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = exePath.c_str();
    sei.nShow  = SW_SHOWNORMAL;
    if (ShellExecuteExW(&sei)) {
        g_isExiting = true;
        DestroyWindow(hwnd);
    }
}

void EnableAdvancedDPIAwareness() {
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        typedef HRESULT(WINAPI* PFN_SET_PROCESS_DPI_AWARENESS_CONTEXT)(DPI_AWARENESS_CONTEXT);
        PFN_SET_PROCESS_DPI_AWARENESS_CONTEXT setDpiContext =
            (PFN_SET_PROCESS_DPI_AWARENESS_CONTEXT)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (setDpiContext) { setDpiContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2); return; }
        typedef BOOL(WINAPI* PFN_SET_PROCESS_DPI_AWARE)(void);
        PFN_SET_PROCESS_DPI_AWARE setDpiAware = (PFN_SET_PROCESS_DPI_AWARE)GetProcAddress(hUser32, "SetProcessDPIAware");
        if (setDpiAware) setDpiAware();
    }
}

std::wstring GetKeyName(UINT vk) {
    wchar_t name[128];
    int scanCode = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    switch (vk) {
        case VK_LEFT: case VK_UP: case VK_RIGHT: case VK_DOWN:
        case VK_PRIOR: case VK_NEXT: case VK_END: case VK_HOME:
        case VK_INSERT: case VK_DELETE: case VK_DIVIDE: case VK_NUMLOCK:
            scanCode |= 0x100;
            break;
    }
    if (GetKeyNameTextW(scanCode << 16, name, 128) == 0) {
        swprintf_s(name, L"Key %d", vk);
    }
    return std::wstring(name);
}

// ----------------------------------------------------------------------------
// Registry
// ----------------------------------------------------------------------------
bool CheckRunOnStartup() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunRegPath, 0, KEY_READ, &hKey) != ERROR_SUCCESS) return false;
    wchar_t value[MAX_PATH * 2] = {0};
    DWORD size = sizeof(value), type = 0;
    bool exists = (RegQueryValueExW(hKey, kRunValueName, nullptr, &type, reinterpret_cast<LPBYTE>(value), &size) == ERROR_SUCCESS);
    RegCloseKey(hKey);
    return exists && (type == REG_SZ || type == REG_EXPAND_SZ);
}

bool SetRunOnStartup(bool enable) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunRegPath, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) return false;
    bool ok = false;
    if (enable) {
        std::wstring cmd = L"\"" + GetExecutablePath() + L"\" --startup";
        DWORD cbData = static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t));
        ok = (RegSetValueExW(hKey, kRunValueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()), cbData) == ERROR_SUCCESS);
    } else {
        LSTATUS status = RegDeleteValueW(hKey, kRunValueName);
        ok = (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND);
    }
    RegCloseKey(hKey);
    return ok;
}

void LoadUserSettings() {
    g_runOnStartup = CheckRunOnStartup();
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kConfigRegPath, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD val = 0, sz = sizeof(DWORD);
        if (RegQueryValueExW(hKey, L"DeadzonePx", nullptr, nullptr, reinterpret_cast<LPBYTE>(&val), &sz) == ERROR_SUCCESS) {
            if (val >= 1 && val <= 15) g_dragDeadzonePx = static_cast<int>(val);
        }
        sz = sizeof(DWORD);
        if (RegQueryValueExW(hKey, L"CloseToTray", nullptr, nullptr, reinterpret_cast<LPBYTE>(&val), &sz) == ERROR_SUCCESS) g_closeToTray = (val != 0);
        sz = sizeof(DWORD);
        if (RegQueryValueExW(hKey, L"EngineMode", nullptr, nullptr, reinterpret_cast<LPBYTE>(&val), &sz) == ERROR_SUCCESS) {
            if (val <= 2) g_engineMode = static_cast<TabletEngineMode>(val);
        }
        sz = sizeof(DWORD);
        if (RegQueryValueExW(hKey, L"HotkeyToggle", nullptr, nullptr, reinterpret_cast<LPBYTE>(&val), &sz) == ERROR_SUCCESS) {
            if (val > 0) g_hotkeyToggle = val;
        }
        sz = sizeof(DWORD);
        if (RegQueryValueExW(hKey, L"AlwaysAdmin", nullptr, nullptr, reinterpret_cast<LPBYTE>(&val), &sz) == ERROR_SUCCESS) g_alwaysRunAsAdmin = (val != 0);
        
        wchar_t exeList[2048] = {0};
        sz = sizeof(exeList);
        if (RegQueryValueExW(hKey, L"TargetExes", nullptr, nullptr, reinterpret_cast<LPBYTE>(exeList), &sz) == ERROR_SUCCESS) {
            std::wstring list(exeList);
            size_t pos = 0;
            while ((pos = list.find(L';')) != std::wstring::npos) {
                std::wstring token = list.substr(0, pos);
                if (!token.empty()) g_targetExes.push_back(token);
                list.erase(0, pos + 1);
            }
            if (!list.empty()) g_targetExes.push_back(list);
        }
        RegCloseKey(hKey);
    }
}

void SaveUserSettings() {
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kConfigRegPath, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        DWORD deadzone = static_cast<DWORD>(g_dragDeadzonePx);
        DWORD closeTray = g_closeToTray ? 1 : 0;
        DWORD mode = static_cast<DWORD>(g_engineMode);
        DWORD hk = static_cast<DWORD>(g_hotkeyToggle);
        DWORD alwaysAdmin = g_alwaysRunAsAdmin ? 1 : 0;
        
        RegSetValueExW(hKey, L"DeadzonePx", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&deadzone), sizeof(DWORD));
        RegSetValueExW(hKey, L"CloseToTray", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&closeTray), sizeof(DWORD));
        RegSetValueExW(hKey, L"EngineMode", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&mode), sizeof(DWORD));
        RegSetValueExW(hKey, L"HotkeyToggle", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&hk), sizeof(DWORD));
        RegSetValueExW(hKey, L"AlwaysAdmin", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&alwaysAdmin), sizeof(DWORD));
        
        std::wstring list;
        for (const auto& exe : g_targetExes) { list += exe + L";"; }
        RegSetValueExW(hKey, L"TargetExes", 0, REG_SZ, reinterpret_cast<const BYTE*>(list.c_str()), (list.size() + 1) * sizeof(wchar_t));
        
        RegCloseKey(hKey);
    }
}

// ----------------------------------------------------------------------------
// Core Hook Logic
// ----------------------------------------------------------------------------
bool IsActiveWindowInWhitelist() {
    if (g_targetExes.empty()) return true;

    static HWND s_lastForegroundWnd = nullptr;
    static bool s_lastWhitelistResult = true;

    HWND hForeground = GetForegroundWindow();
    if (!hForeground) return false;

    if (hForeground == s_lastForegroundWnd) return s_lastWhitelistResult;

    s_lastForegroundWnd = hForeground;
    s_lastWhitelistResult = false;

    DWORD pid = 0;
    GetWindowThreadProcessId(hForeground, &pid);
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProcess) {
        wchar_t path[MAX_PATH] = {0};
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(hProcess, 0, path, &size)) {
            std::wstring fullPath(path);
            size_t pos = fullPath.find_last_of(L"\\/");
            std::wstring exeName = (pos != std::wstring::npos) ? fullPath.substr(pos + 1) : fullPath;
            std::transform(exeName.begin(), exeName.end(), exeName.begin(), ::towlower);

            for (auto target : g_targetExes) {
                std::transform(target.begin(), target.end(), target.begin(), ::towlower);
                if (exeName == target) { s_lastWhitelistResult = true; break; }
            }
        }
        CloseHandle(hProcess);
    }
    return s_lastWhitelistResult;
}

bool IsShiftPressed() {
    return (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0 ||
           (GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0 ||
           (GetAsyncKeyState(VK_SHIFT)  & 0x8000) != 0 ||
           (GetKeyState(VK_SHIFT)       & 0x8000) != 0;
}

bool IsPrimaryButtonOrTipPhysicallyDown() {
    return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
}

void ApplyAxisClipCursor(AxisLock axis, POINT origin) {
    int screenLeft   = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int screenTop    = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int screenWidth  = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    RECT clipRect;
    if (axis == AxisLock::HORIZONTAL) {
        clipRect.left   = screenLeft; clipRect.right  = screenLeft + screenWidth;
        clipRect.top    = origin.y;   clipRect.bottom = origin.y + 2; 
    } else if (axis == AxisLock::VERTICAL) {
        clipRect.left   = origin.x;   clipRect.right  = origin.x + 2; 
        clipRect.top    = screenTop;  clipRect.bottom = screenTop + screenHeight;
    } else {
        ClipCursor(nullptr); return;
    }
    ClipCursor(&clipRect);
}

void ReleaseAxisClipCursor() { ClipCursor(nullptr); }

void UnlockAxisState() {
    bool wasLocked = (g_lockedAxis != AxisLock::NONE);
    g_lockedAxis    = AxisLock::NONE;
    g_shiftAnchored = false;
    ReleaseAxisClipCursor();
    if (wasLocked && g_hMainWnd && IsWindowVisible(g_hMainWnd)) {
        PostMessageW(g_hMainWnd, WM_UPDATE_UI, 0, 0);
    }
}

void InjectClampedAbsoluteMove(POINT clampedPt) {
    int vLeft   = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vTop    = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vWidth  = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (vWidth <= 1 || vHeight <= 1) return;

    LONG normX = static_cast<LONG>(((static_cast<double>(clampedPt.x - vLeft) * 65535.0) / (vWidth - 1)) + 0.5);
    LONG normY = static_cast<LONG>(((static_cast<double>(clampedPt.y - vTop)  * 65535.0) / (vHeight - 1)) + 0.5);

    INPUT inp = {};
    inp.type = INPUT_MOUSE; inp.mi.dx = normX; inp.mi.dy = normY;
    inp.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    inp.mi.dwExtraInfo = kInjectedSignature;
    SendInput(1, &inp, sizeof(INPUT));
}

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && g_toolEnabled && !g_isSettingHotkey) {
        auto* mouseInfo = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);

        if (mouseInfo->dwExtraInfo == kInjectedSignature) {
            return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
        }

        POINT pt = mouseInfo->pt;
        bool shiftHeld = IsShiftPressed();
        if (shiftHeld && !IsActiveWindowInWhitelist()) {
            shiftHeld = false; 
        }

        switch (wParam) {
            case WM_LBUTTONDOWN: {
                g_isPenDown     = true;
                g_downPt        = pt;
                g_lastClampedPt = pt;
                g_shiftAnchored = shiftHeld;
                if (g_lockedAxis != AxisLock::NONE) {
                    g_lockedAxis = AxisLock::NONE;
                    ReleaseAxisClipCursor();
                    if (g_hMainWnd && IsWindowVisible(g_hMainWnd)) PostMessageW(g_hMainWnd, WM_UPDATE_UI, 0, 0);
                }
                break;
            }

            case WM_MOUSEMOVE: {
                bool lbtnPhysicallyDown = IsPrimaryButtonOrTipPhysicallyDown();
                if (!g_isPenDown && lbtnPhysicallyDown) {
                    g_isPenDown     = true; g_downPt = pt; g_lastClampedPt = pt; g_shiftAnchored = shiftHeld;
                } else if (g_isPenDown && !lbtnPhysicallyDown && g_lockedAxis != AxisLock::NONE && !shiftHeld) {
                    g_isPenDown = false; UnlockAxisState(); break;
                }

                if (g_isPenDown && shiftHeld) {
                    if (!g_shiftAnchored) {
                        g_downPt = pt; g_lastClampedPt = pt; g_shiftAnchored = true; g_lockedAxis = AxisLock::NONE;
                    }

                    long dx = pt.x - g_downPt.x; long dy = pt.y - g_downPt.y;

                    if (g_lockedAxis == AxisLock::NONE) {
                        if (std::abs(dx) >= g_dragDeadzonePx || std::abs(dy) >= g_dragDeadzonePx) {
                            g_lockedAxis = (std::abs(dx) >= std::abs(dy)) ? AxisLock::HORIZONTAL : AxisLock::VERTICAL;
                            ApplyAxisClipCursor(g_lockedAxis, g_downPt);
                            if (g_hMainWnd && IsWindowVisible(g_hMainWnd)) PostMessageW(g_hMainWnd, WM_UPDATE_UI, 0, 0);
                        }
                    }

                    if (g_lockedAxis != AxisLock::NONE) {
                        POINT clampedPt = pt;
                        if (g_lockedAxis == AxisLock::HORIZONTAL) clampedPt.y = g_downPt.y;
                        else if (g_lockedAxis == AxisLock::VERTICAL) clampedPt.x = g_downPt.x;
                        g_lastClampedPt = clampedPt;

                        bool isOffAxis = (pt.x != clampedPt.x || pt.y != clampedPt.y);

                        // Anti-Jitter Filter for slow pen movement
                        static POINT s_prevSentPt = {-1, -1};
                        if (clampedPt.x == s_prevSentPt.x && clampedPt.y == s_prevSentPt.y) {
                            if (isOffAxis && g_engineMode != TabletEngineMode::HYBRID_UNIVERSAL) return 1;
                        }
                        s_prevSentPt = clampedPt;

                        if (g_engineMode == TabletEngineMode::HYBRID_UNIVERSAL) {
                            mouseInfo->pt = clampedPt;
                            return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
                        } else if (g_engineMode == TabletEngineMode::FORCE_SENDINPUT) {
                            if (isOffAxis) {
                                InjectClampedAbsoluteMove(clampedPt);
                                return 1;
                            }
                        } else {
                            if (isOffAxis) { SetCursorPos(clampedPt.x, clampedPt.y); return 1; }
                        }
                    }
                } else if (!shiftHeld) {
                    g_shiftAnchored = false;
                    if (g_lockedAxis != AxisLock::NONE) UnlockAxisState();
                }
                break;
            }
            case WM_LBUTTONUP: {
                g_isPenDown = false; UnlockAxisState(); break;
            }
        }
    }
    return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
}

// ----------------------------------------------------------------------------
// GDI+ Graphics & UI
// ----------------------------------------------------------------------------
void AddRoundedRectPath(GraphicsPath& path, RectF rc, REAL radius) {
    REAL d = radius * 2.0f; path.Reset();
    if (radius <= 0.0f) { path.AddRectangle(rc); return; }
    path.AddArc(rc.X, rc.Y, d, d, 180.0f, 90.0f);
    path.AddArc(rc.X + rc.Width - d, rc.Y, d, d, 270.0f, 90.0f);
    path.AddArc(rc.X + rc.Width - d, rc.Y + rc.Height - d, d, d, 0.0f, 90.0f);
    path.AddArc(rc.X, rc.Y + rc.Height - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

HICON CreateAppTrayIcon(bool active) {
    Bitmap bmp(32, 32, PixelFormat32bppARGB);
    Graphics g(&bmp); g.SetSmoothingMode(SmoothingModeAntiAlias); g.Clear(Color(0, 0, 0, 0));
    GraphicsPath bgPath; AddRoundedRectPath(bgPath, RectF(2.0f, 2.0f, 28.0f, 28.0f), 7.0f);
    Color cTop = active ? Color(255, 16, 185, 129) : Color(255, 71, 85, 105);
    Color cBot = active ? Color(255, 6, 148, 162)  : Color(255, 51, 65, 85);
    LinearGradientBrush bgBrush(PointF(0, 0), PointF(32, 32), cTop, cBot); g.FillPath(&bgBrush, &bgPath);
    Pen axisPen(Color(255, 255, 255, 255), 2.6f); axisPen.SetStartCap(LineCapRound); axisPen.SetEndCap(LineCapRound);
    g.DrawLine(&axisPen, 7.0f, 16.0f, 25.0f, 16.0f); g.DrawLine(&axisPen, 16.0f, 7.0f, 16.0f, 25.0f);
    SolidBrush centerBrush(active ? Color(255, 255, 255, 255) : Color(255, 251, 191, 36));
    g.FillEllipse(&centerBrush, 13.0f, 13.0f, 6.0f, 6.0f);
    HICON hIcon = nullptr; bmp.GetHICON(&hIcon); return hIcon;
}

void InitTrayIcon(HWND hwnd) {
    ZeroMemory(&g_nid, sizeof(g_nid)); g_nid.cbSize = sizeof(NOTIFYICONDATAW); g_nid.hWnd = hwnd; g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = g_toolEnabled ? g_hIconActive : g_hIconPaused;
    
    std::wstring hkName = GetKeyName(g_hotkeyToggle);
    std::wstring tip = L"StraightCSP - Active (" + hkName + L" to Pause)";
    wcscpy_s(g_nid.szTip, tip.c_str());
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void UpdateTrayIcon() {
    if (!g_nid.hWnd) return;
    g_nid.uFlags = NIF_ICON | NIF_TIP; g_nid.hIcon  = g_toolEnabled ? g_hIconActive : g_hIconPaused;
    std::wstring hkName = GetKeyName(g_hotkeyToggle);
    std::wstring tip = g_toolEnabled ? (L"StraightCSP - Active (" + hkName + L": Pause)") 
                                 	 : (L"StraightCSP - Paused (" + hkName + L": Resume)");
    wcscpy_s(g_nid.szTip, tip.c_str());
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void ShowTrayBalloon(const wchar_t* title, const wchar_t* message) {
    if (!g_nid.hWnd) return;
    g_nid.uFlags = NIF_INFO; g_nid.dwInfoFlags = NIIF_INFO;
    wcscpy_s(g_nid.szInfoTitle, title); wcscpy_s(g_nid.szInfo, message);
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void RemoveTrayIcon() { if (g_nid.hWnd) { Shell_NotifyIconW(NIM_DELETE, &g_nid); g_nid.hWnd = nullptr; } }
void RestoreMainWindow(HWND hwnd) { ShowWindow(hwnd, SW_SHOW); ShowWindow(hwnd, SW_RESTORE); SetForegroundWindow(hwnd); InvalidateRect(hwnd, nullptr, FALSE); }

void ShowTrayContextMenu(HWND hwnd) {
    POINT pt; GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_OPEN, L"Open Control Panel"); SetMenuDefaultItem(hMenu, IDM_TRAY_OPEN, FALSE);
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    std::wstring toggleStr = L"Toggle Axis Lock (" + GetKeyName(g_hotkeyToggle) + L")";
    AppendMenuW(hMenu, MF_STRING | (g_toolEnabled ? MF_CHECKED : MF_UNCHECKED), IDM_TRAY_TOGGLE, toggleStr.c_str());
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING | (g_engineMode == TabletEngineMode::HYBRID_UNIVERSAL ? MF_CHECKED : MF_UNCHECKED), IDM_TRAY_MODE_HYBRID, L"Mode: Universal (Wacom / Huion / XP-Pen / Veikk)");
    AppendMenuW(hMenu, MF_STRING | (g_engineMode == TabletEngineMode::FORCE_SENDINPUT ? MF_CHECKED : MF_UNCHECKED), IDM_TRAY_MODE_SENDINPUT, L"Mode: Force SendInput Clamp");
    AppendMenuW(hMenu, MF_STRING | (g_engineMode == TabletEngineMode::WACOM_CLASSIC ? MF_CHECKED : MF_UNCHECKED), IDM_TRAY_MODE_WACOM, L"Mode: Wacom Legacy");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING | (g_runOnStartup ? MF_CHECKED : MF_UNCHECKED), IDM_TRAY_STARTUP, L"Run at Windows Startup");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_EXIT, L"Exit Completely");
    SetForegroundWindow(hwnd); TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0); DestroyMenu(hMenu);
}

const wchar_t* GetEngineModeName(TabletEngineMode mode) {
    switch (mode) {
        case TabletEngineMode::HYBRID_UNIVERSAL: return L"Universal (Huion/XP-Pen/Wacom...)";
        case TabletEngineMode::FORCE_SENDINPUT:  return L"Force SendInput Clamp";
        case TabletEngineMode::WACOM_CLASSIC:    return L"Wacom Legacy";
    }
    return L"Universal";
}

void DrawToggleSwitch(Graphics& g, const RectF& rc, bool isOn, bool isHovered) {
    GraphicsPath pillPath; AddRoundedRectPath(pillPath, rc, rc.Height / 2.0f);
    Color trackColor = isOn ? (isHovered ? Color(255, 52, 211, 153) : Color(255, 16, 185, 129)) : (isHovered ? Color(255, 71, 85, 105) : Color(255, 51, 65, 85));
    SolidBrush trackBrush(trackColor); g.FillPath(&trackBrush, &pillPath);
    REAL pad = 3.0f, knobDiameter = rc.Height - pad * 2.0f;
    REAL knobX = isOn ? (rc.X + rc.Width - knobDiameter - pad) : (rc.X + pad), knobY = rc.Y + pad;
    SolidBrush knobBrush(Color(255, 255, 255, 255)); g.FillEllipse(&knobBrush, knobX, knobY, knobDiameter, knobDiameter);
}

void DrawCard(Graphics& g, const RectF& rc, REAL radius = 12.0f) {
    GraphicsPath path; AddRoundedRectPath(path, rc, radius);
    SolidBrush cardBrush(Color(255, 30, 41, 59));
    Pen borderPen(Color(255, 51, 65, 85), 1.0f);
    g.FillPath(&cardBrush, &path); g.DrawPath(&borderPen, &path);
}

void RenderModernUI(HWND hwnd, HDC hdc) {
    RECT rcClient; GetClientRect(hwnd, &rcClient);
    int width = rcClient.right - rcClient.left, height = rcClient.bottom - rcClient.top;
    if (width <= 0 || height <= 0) return;
    Bitmap backBuffer(width, height, PixelFormat32bppARGB);
    Graphics g(&backBuffer); g.SetSmoothingMode(SmoothingModeAntiAlias); g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.Clear(Color(255, 15, 23, 42));

    FontFamily fontFamily(L"Segoe UI");
    Font fontTitle(&fontFamily, 16.0f, FontStyleBold, UnitPixel); Font fontSub(&fontFamily, 12.0f, FontStyleRegular, UnitPixel);
    Font fontCardTitle(&fontFamily, 14.0f, FontStyleBold, UnitPixel); Font fontCardDesc(&fontFamily, 12.0f, FontStyleRegular, UnitPixel);
    Font fontSmall(&fontFamily, 11.5f, FontStyleRegular, UnitPixel); Font fontBadge(&fontFamily, 11.0f, FontStyleBold, UnitPixel);
    Font fontValue(&fontFamily, 15.0f, FontStyleBold, UnitPixel);
    SolidBrush whiteBrush(Color(255, 248, 250, 252)), mutedBrush(Color(255, 148, 163, 184));
    SolidBrush accentTextBrush(Color(255, 56, 189, 248)), emeraldTextBrush(Color(255, 52, 211, 153)), amberTextBrush(Color(255, 251, 191, 36));
    Pen divPen(Color(255, 51, 65, 85), 1.0f);

    StringFormat leftFmt, centerFmt; leftFmt.SetAlignment(StringAlignmentNear); leftFmt.SetLineAlignment(StringAlignmentCenter);
    centerFmt.SetAlignment(StringAlignmentCenter); centerFmt.SetLineAlignment(StringAlignmentCenter);

    auto drawSmallBtn = [&](const RectF& rc, const wchar_t* label, bool hovered, SolidBrush* customBg = nullptr) {
        GraphicsPath p; AddRoundedRectPath(p, rc, 7.0f);
        SolidBrush defaultBg(hovered ? Color(255, 71, 85, 105) : Color(255, 51, 65, 85));
        g.FillPath(customBg ? customBg : &defaultBg, &p); 
        g.DrawString(label, -1, &fontValue, rc, &centerFmt, &whiteBrush);
    };

    // Header
    RectF rcHeader(20.0f, 16.0f, static_cast<REAL>(width - 40), 74.0f);
    GraphicsPath headerPath; AddRoundedRectPath(headerPath, rcHeader, 14.0f);
    LinearGradientBrush headerGrad(PointF(rcHeader.X, rcHeader.Y), PointF(rcHeader.X + rcHeader.Width, rcHeader.Y + rcHeader.Height), Color(255, 30, 41, 59), Color(255, 15, 38, 58));
    g.FillPath(&headerGrad, &headerPath);
    Pen headerBorder(Color(60, 56, 189, 248), 1.0f); g.DrawPath(&headerBorder, &headerPath);

    RectF rcLogo(34.0f, 31.0f, 44.0f, 44.0f); GraphicsPath logoPath; AddRoundedRectPath(logoPath, rcLogo, 10.0f);
    LinearGradientBrush logoBrush(PointF(rcLogo.X, rcLogo.Y), PointF(rcLogo.X + 44.0f, rcLogo.Y + 44.0f), g_toolEnabled ? Color(255, 16, 185, 129) : Color(255, 100, 116, 139), g_toolEnabled ? Color(255, 6, 182, 212) : Color(255, 71, 85, 105));
    g.FillPath(&logoBrush, &logoPath);
    Pen logoAxisPen(Color(255, 255, 255, 255), 2.2f);
    g.DrawLine(&logoAxisPen, rcLogo.X + 10.0f, rcLogo.Y + 22.0f, rcLogo.X + 34.0f, rcLogo.Y + 22.0f);
    g.DrawLine(&logoAxisPen, rcLogo.X + 22.0f, rcLogo.Y + 10.0f, rcLogo.X + 22.0f, rcLogo.Y + 34.0f);
    g.FillEllipse(&whiteBrush, rcLogo.X + 19.0f, rcLogo.Y + 19.0f, 6.0f, 6.0f);

    g.DrawString(L"StraightCSP", -1, &fontTitle, RectF(90.0f, 26.0f, 255.0f, 24.0f), &leftFmt, &whiteBrush);
    g.DrawString(L"Supports Wacom, Huion, XP-Pen, Veikk...", -1, &fontSub, RectF(90.0f, 50.0f, 285.0f, 20.0f), &leftFmt, &mutedBrush);

    RectF rcBadge(static_cast<REAL>(width - 118), 39.0f, 84.0f, 28.0f); GraphicsPath badgePath; AddRoundedRectPath(badgePath, rcBadge, 14.0f);
    SolidBrush badgeBg(g_toolEnabled ? Color(45, 16, 185, 129) : Color(45, 245, 158, 11)); Pen badgePen(g_toolEnabled ? Color(180, 16, 185, 129) : Color(180, 245, 158, 11), 1.0f);
    SolidBrush badgeTextBrush(g_toolEnabled ? Color(255, 52, 211, 153) : Color(255, 251, 191, 36));
    g.FillPath(&badgeBg, &badgePath); g.DrawPath(&badgePen, &badgePath);
    g.DrawString(g_toolEnabled ? L"● ACTIVE" : L"● PAUSED", -1, &fontBadge, rcBadge, &centerFmt, &badgeTextBrush);

    // Card 1: Main Controls
    RectF rcCard1(20.0f, 102.0f, static_cast<REAL>(width - 40), 166.0f); DrawCard(g, rcCard1);
    g.DrawString(L"Quick Axis Lock (Shift + Pen Drag)", -1, &fontCardTitle, RectF(36.0f, 114.0f, 340.0f, 22.0f), &leftFmt, &whiteBrush);
    
    std::wstring hkDesc;
    if (g_isSettingHotkey) {
        hkDesc = L"Press any key to assign new hotkey (ESC to cancel)...";
    } else {
        hkDesc = L"Hotkey [" + GetKeyName(g_hotkeyToggle) + L"]: Toggle • Hold Shift before or after pen down";
    }
    g.DrawString(hkDesc.c_str(), -1, &fontSmall, RectF(36.0f, 136.0f, 320.0f, 20.0f), &leftFmt, g_isSettingHotkey ? &accentTextBrush : &mutedBrush);
    
    g_rcBtnChangeHotkey = RectF(350.0f, 133.0f, 60.0f, 22.0f);
    SolidBrush changeBtnBrush(g_hoveredZone == HitZone::BTN_CHANGE_HOTKEY ? Color(255, 71, 85, 105) : Color(255, 51, 65, 85));
    GraphicsPath pChange; AddRoundedRectPath(pChange, g_rcBtnChangeHotkey, 4.0f);
    g.FillPath(&changeBtnBrush, &pChange);
    g.DrawString(L"Change", -1, &fontBadge, g_rcBtnChangeHotkey, &centerFmt, &whiteBrush);

    g_rcToggleActive = RectF(static_cast<REAL>(width - 88), 120.0f, 50.0f, 26.0f); DrawToggleSwitch(g, g_rcToggleActive, g_toolEnabled, g_hoveredZone == HitZone::TOGGLE_ACTIVE);
    g.DrawLine(&divPen, 36.0f, 164.0f, static_cast<REAL>(width - 36), 164.0f);

    g.DrawString(L"Tablet Lock Engine:", -1, &fontCardDesc, RectF(36.0f, 173.0f, 140.0f, 22.0f), &leftFmt, &mutedBrush);
    g_rcModePrev = RectF(182.0f, 170.0f, 28.0f, 28.0f); g_rcModeNext = RectF(static_cast<REAL>(width - 66), 170.0f, 28.0f, 28.0f);
    drawSmallBtn(g_rcModePrev, L"‹", g_hoveredZone == HitZone::MODE_PREV); drawSmallBtn(g_rcModeNext, L"›", g_hoveredZone == HitZone::MODE_NEXT);
    g.DrawString(GetEngineModeName(g_engineMode), -1, &fontBadge, RectF(212.0f, 170.0f, static_cast<REAL>(width - 280), 28.0f), &centerFmt, &accentTextBrush);
    g.DrawLine(&divPen, 36.0f, 206.0f, static_cast<REAL>(width - 36), 206.0f);

    const wchar_t* axisText = L"Status: Ready to detect drag direction";
    if (!g_toolEnabled) axisText = L"Status: Tool is currently paused";
    else if (g_lockedAxis == AxisLock::HORIZONTAL) axisText = L"Status: Locked HORIZONTAL (↔)";
    else if (g_lockedAxis == AxisLock::VERTICAL) axisText = L"Status: Locked VERTICAL (↕)";
    g.DrawString(axisText, -1, &fontSmall, RectF(36.0f, 214.0f, 260.0f, 20.0f), &leftFmt, &emeraldTextBrush);

    if (g_isRunningAsAdmin) {
        g_rcBtnElevate = RectF(0, 0, 0, 0);
        g.DrawString(L"✓ Admin Rights: Enabled (100% Compatible with Huion/XP-Pen)", -1, &fontSmall, RectF(36.0f, 236.0f, static_cast<REAL>(width - 72), 20.0f), &leftFmt, &mutedBrush);
    } else {
        g.DrawString(L"⚠ Admin Rights required to fix coordinate jumping issues:", -1, &fontSmall, RectF(36.0f, 236.0f, 310.0f, 20.0f), &leftFmt, &amberTextBrush);
        g_rcBtnElevate = RectF(static_cast<REAL>(width - 158), 233.0f, 120.0f, 25.0f); GraphicsPath pElev; AddRoundedRectPath(pElev, g_rcBtnElevate, 6.0f);
        bool hov = (g_hoveredZone == HitZone::BTN_ELEVATE_ADMIN);
        SolidBrush bElev(hov ? Color(255, 245, 158, 11) : Color(55, 245, 158, 11)); Pen penElev(Color(180, 245, 158, 11), 1.0f);
        SolidBrush txtElev(hov ? Color(255, 15, 23, 42) : Color(255, 251, 191, 36));
        g.FillPath(&bElev, &pElev); g.DrawPath(&penElev, &pElev);
        g.DrawString(L"Grant Admin", -1, &fontBadge, g_rcBtnElevate, &centerFmt, &txtElev);
    }

    // Card 2: Deadzone
    RectF rcCard2(20.0f, 280.0f, static_cast<REAL>(width - 40), 76.0f); DrawCard(g, rcCard2);
    g.DrawString(L"Axis Lock Deadzone", -1, &fontCardTitle, RectF(36.0f, 292.0f, 285.0f, 22.0f), &leftFmt, &whiteBrush);
    g.DrawString(L"Recommended 2-4 px for Wacom/Huion/XP-Pen to prevent jitter", -1, &fontSmall, RectF(36.0f, 316.0f, 350.0f, 20.0f), &leftFmt, &mutedBrush);
    g_rcDeadzoneMinus = RectF(static_cast<REAL>(width - 144), 303.0f, 30.0f, 30.0f); g_rcDeadzonePlus = RectF(static_cast<REAL>(width - 68), 303.0f, 30.0f, 30.0f);
    drawSmallBtn(g_rcDeadzoneMinus, L"−", g_hoveredZone == HitZone::DEADZONE_MINUS); drawSmallBtn(g_rcDeadzonePlus, L"+", g_hoveredZone == HitZone::DEADZONE_PLUS);
    wchar_t szDeadzone[16]; swprintf_s(szDeadzone, L"%d px", g_dragDeadzonePx);
    g.DrawString(szDeadzone, -1, &fontCardTitle, RectF(static_cast<REAL>(width - 114), 303.0f, 46.0f, 30.0f), &centerFmt, &accentTextBrush);

    // Card 3: Whitelist Exe
    RectF rcCard3(20.0f, 368.0f, static_cast<REAL>(width - 40), 106.0f); DrawCard(g, rcCard3);
    g.DrawString(L"Active Scope (Only lock axis on these applications)", -1, &fontCardTitle, RectF(36.0f, 380.0f, 400.0f, 22.0f), &leftFmt, &whiteBrush);
    
    g_rcBtnAddExe = RectF(36.0f, 408.0f, 130.0f, 30.0f);
    SolidBrush addBtnBrush(g_hoveredZone == HitZone::BTN_ADD_EXE ? Color(255, 37, 99, 235) : Color(255, 59, 130, 246));
    drawSmallBtn(g_rcBtnAddExe, L"+ Add .exe File", g_hoveredZone == HitZone::BTN_ADD_EXE, &addBtnBrush);
    
    g_rcBtnClearExe = RectF(176.0f, 408.0f, 130.0f, 30.0f);
    drawSmallBtn(g_rcBtnClearExe, L"Clear List", g_hoveredZone == HitZone::BTN_CLEAR_EXE);
    
    std::wstring listStr = L"Applying to: ";
    if (g_targetExes.empty()) {
        listStr += L"ALL (Not recommended)";
    } else {
        for (size_t i = 0; i < g_targetExes.size(); ++i) {
            listStr += g_targetExes[i];
            if (i < g_targetExes.size() - 1) listStr += L", ";
        }
    }
    StringFormat sfTrim; sfTrim.SetTrimming(StringTrimmingEllipsisCharacter); sfTrim.SetFormatFlags(StringFormatFlagsNoWrap);
    g.DrawString(listStr.c_str(), -1, &fontSmall, RectF(36.0f, 444.0f, width - 72.0f, 20.0f), &sfTrim, g_targetExes.empty() ? &amberTextBrush : &emeraldTextBrush);

    // Card 4: System Options
    RectF rcCard4(20.0f, 486.0f, static_cast<REAL>(width - 40), 170.0f); DrawCard(g, rcCard4);
    
    // Startup
    g.DrawString(L"Run at Windows Startup", -1, &fontCardTitle, RectF(36.0f, 498.0f, 330.0f, 22.0f), &leftFmt, &whiteBrush);
    g.DrawString(L"Automatically run in the System Tray on boot", -1, &fontSmall, RectF(36.0f, 520.0f, 345.0f, 20.0f), &leftFmt, &mutedBrush);
    g_rcToggleStartup = RectF(static_cast<REAL>(width - 88), 506.0f, 50.0f, 26.0f); DrawToggleSwitch(g, g_rcToggleStartup, g_runOnStartup, g_hoveredZone == HitZone::TOGGLE_STARTUP);
    g.DrawLine(&divPen, 36.0f, 545.0f, static_cast<REAL>(width - 36), 545.0f);
    
    // Close to Tray
    g.DrawString(L"Close to System Tray", -1, &fontCardTitle, RectF(36.0f, 555.0f, 330.0f, 22.0f), &leftFmt, &whiteBrush);
    g.DrawString(L"Keep running in background when closing the window", -1, &fontSmall, RectF(36.0f, 577.0f, 345.0f, 20.0f), &leftFmt, &mutedBrush);
    g_rcToggleCloseTray = RectF(static_cast<REAL>(width - 88), 563.0f, 50.0f, 26.0f); DrawToggleSwitch(g, g_rcToggleCloseTray, g_closeToTray, g_hoveredZone == HitZone::TOGGLE_CLOSE_TO_TRAY);
    g.DrawLine(&divPen, 36.0f, 602.0f, static_cast<REAL>(width - 36), 602.0f);

    // Always Admin
    g.DrawString(L"Always Run as Admin", -1, &fontCardTitle, RectF(36.0f, 612.0f, 330.0f, 22.0f), &leftFmt, &whiteBrush);
    g.DrawString(L"Auto-request Admin privileges upon launching the software", -1, &fontSmall, RectF(36.0f, 634.0f, 345.0f, 20.0f), &leftFmt, &mutedBrush);
    g_rcToggleAlwaysAdmin = RectF(static_cast<REAL>(width - 88), 620.0f, 50.0f, 26.0f); DrawToggleSwitch(g, g_rcToggleAlwaysAdmin, g_alwaysRunAsAdmin, g_hoveredZone == HitZone::TOGGLE_ALWAYS_ADMIN);

    // Tip Card
    RectF rcTipCard(20.0f, 668.0f, static_cast<REAL>(width - 40), 42.0f); DrawCard(g, rcTipCard, 10.0f);
    g.DrawString(L"💡 Tip for Huion/XP-Pen: In CSP (File > Preferences > Tablet), select Wintab for 100% axis locking accuracy.", -1, &fontSmall, RectF(32.0f, 671.0f, static_cast<REAL>(width - 64), 36.0f), &leftFmt, &mutedBrush);

    // Footer
    REAL btnY = 726.0f, btnW = (static_cast<REAL>(width - 40) - 14.0f) / 2.0f;
    g_rcBtnHideTray = RectF(20.0f, btnY, btnW, 42.0f); g_rcBtnExitApp = RectF(20.0f + btnW + 14.0f, btnY, btnW, 42.0f);

    {
        GraphicsPath p; AddRoundedRectPath(p, g_rcBtnHideTray, 10.0f); bool hov = (g_hoveredZone == HitZone::BTN_HIDE_TRAY);
        SolidBrush bg(hov ? Color(255, 51, 65, 85) : Color(255, 30, 41, 59)); Pen border(Color(255, 71, 85, 105), 1.0f);
        g.FillPath(&bg, &p); g.DrawPath(&border, &p);
        g.DrawString(L"Hide to System Tray", -1, &fontCardTitle, g_rcBtnHideTray, &centerFmt, &whiteBrush);
    }
    {
        GraphicsPath p; AddRoundedRectPath(p, g_rcBtnExitApp, 10.0f); bool hov = (g_hoveredZone == HitZone::BTN_EXIT_APP);
        SolidBrush bg(hov ? Color(255, 220, 38, 38) : Color(45, 239, 68, 68)); Pen border(Color(180, 239, 68, 68), 1.0f);
        SolidBrush txtBrush(hov ? Color(255, 255, 255, 255) : Color(255, 252, 165, 165));
        g.FillPath(&bg, &p); g.DrawPath(&border, &p);
        g.DrawString(L"Exit Completely", -1, &fontCardTitle, g_rcBtnExitApp, &centerFmt, &txtBrush);
    }
    Graphics screenG(hdc); screenG.DrawImage(&backBuffer, 0, 0);
}

HitZone HitTestUI(POINT pt) {
    PointF p(static_cast<REAL>(pt.x), static_cast<REAL>(pt.y));
    if (g_rcToggleActive.Contains(p)) return HitZone::TOGGLE_ACTIVE;
    if (g_rcModePrev.Contains(p)) return HitZone::MODE_PREV;
    if (g_rcModeNext.Contains(p)) return HitZone::MODE_NEXT;
    if (!g_isRunningAsAdmin && g_rcBtnElevate.Contains(p)) return HitZone::BTN_ELEVATE_ADMIN;
    if (g_rcDeadzoneMinus.Contains(p)) return HitZone::DEADZONE_MINUS;
    if (g_rcDeadzonePlus.Contains(p)) return HitZone::DEADZONE_PLUS;
    if (g_rcToggleStartup.Contains(p)) return HitZone::TOGGLE_STARTUP;
    if (g_rcToggleCloseTray.Contains(p)) return HitZone::TOGGLE_CLOSE_TO_TRAY;
    if (g_rcToggleAlwaysAdmin.Contains(p)) return HitZone::TOGGLE_ALWAYS_ADMIN;
    if (g_rcBtnHideTray.Contains(p)) return HitZone::BTN_HIDE_TRAY;
    if (g_rcBtnExitApp.Contains(p)) return HitZone::BTN_EXIT_APP;
    if (g_rcBtnAddExe.Contains(p)) return HitZone::BTN_ADD_EXE;
    if (g_rcBtnClearExe.Contains(p)) return HitZone::BTN_CLEAR_EXE;
    if (g_rcBtnChangeHotkey.Contains(p)) return HitZone::BTN_CHANGE_HOTKEY;
    return HitZone::NONE;
}

void ToggleToolActiveState(HWND hwnd) {
    g_toolEnabled = !g_toolEnabled;
    if (!g_toolEnabled) UnlockAxisState();
    UpdateTrayIcon(); InvalidateRect(hwnd, nullptr, FALSE);
}

void CycleEngineMode(int delta, HWND hwnd) {
    int cur = static_cast<int>(g_engineMode); cur = (cur + delta + 3) % 3;
    g_engineMode = static_cast<TabletEngineMode>(cur);
    SaveUserSettings(); InvalidateRect(hwnd, nullptr, FALSE);
}

void HideWindowToTray(HWND hwnd) {
    ShowWindow(hwnd, SW_HIDE);
    if (!g_trayBalloonShown) {
        ShowTrayBalloon(L"StraightCSP running", L"The application has minimized to the System Tray. Double-click to open.");
        g_trayBalloonShown = true;
    }
}

void OpenFileDialogToAddExe(HWND hwnd) {
    OPENFILENAMEW ofn = {};
    wchar_t szFile[MAX_PATH] = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = L"Executable Files (*.exe)\0*.exe\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameW(&ofn)) {
        std::wstring path(szFile);
        size_t pos = path.find_last_of(L"\\/");
        std::wstring exeName = (pos != std::wstring::npos) ? path.substr(pos + 1) : path;
        
        bool exists = false;
        for (auto target : g_targetExes) {
            std::wstring lowerTarget = target, lowerExe = exeName;
            std::transform(lowerTarget.begin(), lowerTarget.end(), lowerTarget.begin(), ::towlower);
            std::transform(lowerExe.begin(), lowerExe.end(), lowerExe.begin(), ::towlower);
            if (lowerTarget == lowerExe) { exists = true; break; }
        }
        if (!exists) {
            g_targetExes.push_back(exeName);
            SaveUserSettings();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    }
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_wmShowExisting != 0 && msg == g_wmShowExisting) { RestoreMainWindow(hwnd); return 0; }
    switch (msg) {
        case WM_CREATE: {
            BOOL useDarkMode = TRUE; DwmSetWindowAttribute(hwnd, 20, &useDarkMode, sizeof(useDarkMode));
            DWORD cornerPref = 2; DwmSetWindowAttribute(hwnd, 33, &cornerPref, sizeof(cornerPref));
            SetTimer(hwnd, TIMER_ID_WATCHDOG, 10, nullptr);
            return 0;
        }
        case WM_TIMER: {
            if (wParam == TIMER_ID_WATCHDOG && g_lockedAxis != AxisLock::NONE) {
                if (!g_toolEnabled || !IsShiftPressed() || !IsPrimaryButtonOrTipPhysicallyDown()) {
                    g_isPenDown = false; UnlockAxisState();
                } else ApplyAxisClipCursor(g_lockedAxis, g_downPt);
            }
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps); RenderModernUI(hwnd, hdc); EndPaint(hwnd, &ps); return 0; }
        case WM_UPDATE_UI: { InvalidateRect(hwnd, nullptr, FALSE); return 0; }
        case WM_MOUSEMOVE: {
            if (!g_trackingMouse) { TRACKMOUSEEVENT tme = {}; tme.cbSize = sizeof(TRACKMOUSEEVENT); tme.dwFlags = TME_LEAVE; tme.hwndTrack = hwnd; TrackMouseEvent(&tme); g_trackingMouse = true; }
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }; HitZone zone = HitTestUI(pt);
            if (zone != g_hoveredZone) { g_hoveredZone = zone; InvalidateRect(hwnd, nullptr, FALSE); }
            SetCursor(LoadCursorW(nullptr, (zone != HitZone::NONE) ? IDC_HAND : IDC_ARROW)); return 0;
        }
        case WM_MOUSELEAVE: { g_trackingMouse = false; if (g_hoveredZone != HitZone::NONE) { g_hoveredZone = HitZone::NONE; InvalidateRect(hwnd, nullptr, FALSE); } return 0; }
        case WM_LBUTTONDOWN: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (g_isSettingHotkey) {
                g_isSettingHotkey = false; InvalidateRect(hwnd, nullptr, FALSE); return 0;
            }
            switch (HitTestUI(pt)) {
                case HitZone::TOGGLE_ACTIVE: ToggleToolActiveState(hwnd); break;
                case HitZone::MODE_PREV: CycleEngineMode(-1, hwnd); break;
                case HitZone::MODE_NEXT: CycleEngineMode(+1, hwnd); break;
                case HitZone::BTN_ELEVATE_ADMIN: RelaunchAsAdmin(hwnd); break;
                case HitZone::DEADZONE_MINUS: if (g_dragDeadzonePx > 1) { g_dragDeadzonePx--; SaveUserSettings(); InvalidateRect(hwnd, nullptr, FALSE); } break;
                case HitZone::DEADZONE_PLUS: if (g_dragDeadzonePx < 15) { g_dragDeadzonePx++; SaveUserSettings(); InvalidateRect(hwnd, nullptr, FALSE); } break;
                case HitZone::TOGGLE_STARTUP: g_runOnStartup = !g_runOnStartup; SetRunOnStartup(g_runOnStartup); InvalidateRect(hwnd, nullptr, FALSE); break;
                case HitZone::TOGGLE_CLOSE_TO_TRAY: g_closeToTray = !g_closeToTray; SaveUserSettings(); InvalidateRect(hwnd, nullptr, FALSE); break;
                case HitZone::TOGGLE_ALWAYS_ADMIN: g_alwaysRunAsAdmin = !g_alwaysRunAsAdmin; SaveUserSettings(); InvalidateRect(hwnd, nullptr, FALSE); break;
                case HitZone::BTN_HIDE_TRAY: HideWindowToTray(hwnd); break;
                case HitZone::BTN_EXIT_APP: g_isExiting = true; DestroyWindow(hwnd); break;
                case HitZone::BTN_ADD_EXE: OpenFileDialogToAddExe(hwnd); break;
                case HitZone::BTN_CLEAR_EXE: g_targetExes.clear(); SaveUserSettings(); InvalidateRect(hwnd, nullptr, FALSE); break;
                case HitZone::BTN_CHANGE_HOTKEY: g_isSettingHotkey = true; InvalidateRect(hwnd, nullptr, FALSE); break;
                default: break;
            } return 0;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            if (g_isSettingHotkey) {
                if (wParam != VK_ESCAPE && wParam != VK_SHIFT && wParam != VK_CONTROL && wParam != VK_MENU) {
                    UnregisterHotKey(hwnd, HOTKEY_ID_TOGGLE);
                    g_hotkeyToggle = static_cast<UINT>(wParam);
                    RegisterHotKey(hwnd, HOTKEY_ID_TOGGLE, 0, g_hotkeyToggle);
                    SaveUserSettings();
                    UpdateTrayIcon();
                }
                g_isSettingHotkey = false;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            break;
        }
        case WM_HOTKEY: { 
            if (wParam == HOTKEY_ID_TOGGLE) ToggleToolActiveState(hwnd); 
            else if (wParam == HOTKEY_ID_EXIT) { g_isExiting = true; DestroyWindow(hwnd); } 
            return 0; 
        }
        case WM_TRAYICON: {
            if (lParam == WM_LBUTTONDBLCLK || lParam == WM_LBUTTONUP) { if (IsWindowVisible(hwnd)) SetForegroundWindow(hwnd); else RestoreMainWindow(hwnd); }
            else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) ShowTrayContextMenu(hwnd);
            return 0;
        }
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDM_TRAY_OPEN: RestoreMainWindow(hwnd); break;
                case IDM_TRAY_TOGGLE: ToggleToolActiveState(hwnd); break;
                case IDM_TRAY_MODE_HYBRID: g_engineMode = TabletEngineMode::HYBRID_UNIVERSAL; SaveUserSettings(); InvalidateRect(hwnd, nullptr, FALSE); break;
                case IDM_TRAY_MODE_SENDINPUT: g_engineMode = TabletEngineMode::FORCE_SENDINPUT; SaveUserSettings(); InvalidateRect(hwnd, nullptr, FALSE); break;
                case IDM_TRAY_MODE_WACOM: g_engineMode = TabletEngineMode::WACOM_CLASSIC; SaveUserSettings(); InvalidateRect(hwnd, nullptr, FALSE); break;
                case IDM_TRAY_STARTUP: g_runOnStartup = !g_runOnStartup; SetRunOnStartup(g_runOnStartup); InvalidateRect(hwnd, nullptr, FALSE); break;
                case IDM_TRAY_EXIT: g_isExiting = true; DestroyWindow(hwnd); break;
            } return 0;
        }
        case WM_CLOSE: { if (g_closeToTray && !g_isExiting) { HideWindowToTray(hwnd); return 0; } DestroyWindow(hwnd); return 0; }
        case WM_ENDSESSION: { if (wParam == TRUE) { g_isExiting = true; DestroyWindow(hwnd); } return 0; }
        case WM_DESTROY: { KillTimer(hwnd, TIMER_ID_WATCHDOG); ReleaseAxisClipCursor(); UnregisterHotKey(hwnd, HOTKEY_ID_TOGGLE); UnregisterHotKey(hwnd, HOTKEY_ID_EXIT); RemoveTrayIcon(); PostQuitMessage(0); return 0; }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool ShouldStartMinimizedInTray() {
    int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool startHidden = false;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (_wcsicmp(argv[i], L"--startup") == 0 || _wcsicmp(argv[i], L"-startup") == 0 || _wcsicmp(argv[i], L"/startup") == 0 || _wcsicmp(argv[i], L"--minimized") == 0 || _wcsicmp(argv[i], L"-minimized") == 0) { startHidden = true; break; }
        }
        LocalFree(argv);
    } return startHidden;
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    g_hInst = hInstance;
    
    LoadUserSettings();
    
    g_isRunningAsAdmin = CheckIsRunningAsAdmin();
    
    if (g_alwaysRunAsAdmin && !g_isRunningAsAdmin) {
        std::wstring exePath = GetExecutablePath();
        SHELLEXECUTEINFOW sei = {};
        sei.cbSize = sizeof(sei);
        sei.lpVerb = L"runas";
        sei.lpFile = exePath.c_str();
        
        std::wstring params = L"";
        if (ShouldStartMinimizedInTray()) params = L"--startup";
        if (!params.empty()) sei.lpParameters = params.c_str();
        
        sei.nShow = SW_SHOWNORMAL;
        
        if (ShellExecuteExW(&sei)) {
            return 0; 
        }
    }

    EnableAdvancedDPIAwareness(); 

    g_wmShowExisting = RegisterWindowMessageW(L"CSPStraightLineHelper_ShowExistingMsg");
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (GetLastError() == ERROR_ALREADY_EXISTS) { PostMessageW(HWND_BROADCAST, g_wmShowExisting, 0, 0); if (hMutex) CloseHandle(hMutex); return 0; }

    GdiplusStartupInput gdiplusStartupInput; ULONG_PTR gdiplusToken = 0; GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);
    
    g_hIconActive = CreateAppTrayIcon(true); g_hIconPaused = CreateAppTrayIcon(false);

    WNDCLASSEXW wc = {}; wc.cbSize = sizeof(WNDCLASSEXW); wc.style = CS_HREDRAW | CS_VREDRAW; wc.lpfnWndProc = MainWndProc; wc.hInstance = hInstance; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hIcon = g_hIconActive; wc.hIconSm = g_hIconActive; wc.lpszClassName = kWindowClassName; RegisterClassExW(&wc);
    DWORD dwStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX; RECT rcWnd = { 0, 0, WIN_WIDTH, WIN_HEIGHT }; AdjustWindowRect(&rcWnd, dwStyle, FALSE);
    int realW = rcWnd.right - rcWnd.left, realH = rcWnd.bottom - rcWnd.top;
    int posX = (GetSystemMetrics(SM_CXSCREEN) - realW) / 2, posY = (GetSystemMetrics(SM_CYSCREEN) - realH) / 2;

    g_hMainWnd = CreateWindowExW(WS_EX_APPWINDOW, kWindowClassName, kAppTitle, dwStyle, posX, posY, realW, realH, nullptr, nullptr, hInstance, nullptr);
    if (!g_hMainWnd) { GdiplusShutdown(gdiplusToken); if (hMutex) CloseHandle(hMutex); return 1; }

    g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, GetModuleHandleW(nullptr), 0);
    if (!g_mouseHook) { MessageBoxW(g_hMainWnd, L"Mouse Hook Error! System denied permission.", kAppTitle, MB_ICONERROR | MB_OK); DestroyWindow(g_hMainWnd); GdiplusShutdown(gdiplusToken); if (hMutex) CloseHandle(hMutex); return 1; }

    RegisterHotKey(g_hMainWnd, HOTKEY_ID_TOGGLE, 0, g_hotkeyToggle); 
    RegisterHotKey(g_hMainWnd, HOTKEY_ID_EXIT, MOD_ALT, VK_F8);
    InitTrayIcon(g_hMainWnd);

    if (ShouldStartMinimizedInTray()) { g_trayBalloonShown = true; ShowWindow(g_hMainWnd, SW_HIDE); }
    else { ShowWindow(g_hMainWnd, SW_SHOW); UpdateWindow(g_hMainWnd); }

    MSG msg; while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }

    ReleaseAxisClipCursor();
    if (g_mouseHook) { UnhookWindowsHookEx(g_mouseHook); g_mouseHook = nullptr; }
    if (g_hIconActive) DestroyIcon(g_hIconActive); if (g_hIconPaused) DestroyIcon(g_hIconPaused);
    GdiplusShutdown(gdiplusToken); if (hMutex) CloseHandle(hMutex);
    return static_cast<int>(msg.wParam);
}

int main() { HWND hConsole = GetConsoleWindow(); if (hConsole) ShowWindow(hConsole, SW_HIDE); return wWinMain(GetModuleHandleW(nullptr), nullptr, GetCommandLineW(), SW_SHOWDEFAULT); }