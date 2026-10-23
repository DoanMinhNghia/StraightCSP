// ============================================================================
// CSP Shift Line Helper - Universal Multi-Brand Tablet & Modern GUI Edition
// Supports: Wacom, Huion, XP-Pen, Gaomon, Veikk, Xencelabs, Ugee, Parblo, Surface
// Version: 1.5.0 (Per-App Custom Hotkey Profiles + Compact HUD Badges + 4K Opt)
// ============================================================================

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define _WIN32_WINNT 0x0601
#define NOMINMAX

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
#include <atomic>
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

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

// ----------------------------------------------------------------------------
// Constants & Custom Messages
// ----------------------------------------------------------------------------
constexpr UINT WM_TRAYICON              = WM_APP + 1;
constexpr UINT WM_UPDATE_UI             = WM_APP + 2;
constexpr UINT WM_UPDATE_OVERLAY        = WM_APP + 3;
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

constexpr int  LOGICAL_WIDTH            = 520;
constexpr int  LOGICAL_HEIGHT           = 826;

constexpr ULONG_PTR kInjectedSignature  = 0x4353504C; // 'CSPL'

static const wchar_t* kWindowClassName  = L"StraightCSPWnd";
static const wchar_t* kOverlayClassName = L"StraightCSPOverlayWnd";
static const wchar_t* kAppTitle         = L"StraightCSP";
static const wchar_t* kMutexName        = L"Local\\StraightCSP_SingleInstance_Mutex";
static const wchar_t* kRunRegPath       = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kRunValueName     = L"StraightCSP";
static const wchar_t* kConfigRegPath    = L"Software\\StraightCSP";

UINT g_wmShowExisting = 0;

// ----------------------------------------------------------------------------
// State Types, Per-App Profiles & Settings
// ----------------------------------------------------------------------------
enum class AxisLock { NONE = 0, HORIZONTAL, VERTICAL, CUSTOM_RULER };
enum class TabletEngineMode { HYBRID_UNIVERSAL = 0, FORCE_SENDINPUT = 1, WACOM_CLASSIC = 2 };
enum class RulerModeKey { NONE = 0, Z_KEY, X_KEY, C_KEY, E_KEY };

enum class RulerState {
    NONE = 0,
    Z_WAIT_P1,      // 2-Pt Ruler: Chờ chấm điểm 1
    Z_WAIT_P2,      // 2-Pt Ruler: Chờ chấm điểm 2
    Z_ACTIVE,       // 2-Pt Ruler: Đã khóa góc thước 2 điểm
    X_WAIT_CENTER,  // Radial Ruler: Chờ chấm tâm hội tụ
    X_ACTIVE,       // Radial Ruler: Đã có tâm, vẽ mọi đường thẳng đi qua tâm
    C_WAIT_CENTER,  // Circle Ruler: Chờ chấm tâm đường tròn
    C_ACTIVE,       // Circle Ruler: Đã có tâm, vẽ đường tròn quanh tâm
    E_WAIT_P1,      // Ellipse Ruler: Chờ chấm điểm đầu trục chính
    E_WAIT_P2,      // Ellipse Ruler: Chờ chấm điểm cuối trục chính
    E_ACTIVE        // Ellipse Ruler: Đã khóa trục chính, vẽ elip/tròn
};

enum class RebindTarget {
    NONE = 0,
    GLOBAL_TOGGLE,
    PROF_MODIFIER,
    PROF_RULER_Z,
    PROF_RULER_X,
    PROF_RULER_C,
    PROF_RULER_E
};

struct AppProfile {
    std::wstring exeName;
    UINT vkModifier = VK_SHIFT; // Phím giữ (Mặc định: Shift)
    UINT vkRuler2Pt = 'Z';      // Phím thước 2 điểm
    UINT vkRadial   = 'X';      // Phím thước tâm hội tụ
    UINT vkCircle   = 'C';      // Phím thước hình tròn
    UINT vkEllipse  = 'E';      // Phím thước hình elip
};

HINSTANCE        g_hInst                = nullptr;
HWND             g_hMainWnd             = nullptr;
HWND             g_hOverlayWnd          = nullptr;
HHOOK            g_mouseHook            = nullptr;
HHOOK            g_keyboardHook         = nullptr;
HANDLE           g_hHookThread          = nullptr;
DWORD            g_hookThreadId         = 0;
HICON            g_hIconActive          = nullptr;
HICON            g_hIconPaused          = nullptr;
NOTIFYICONDATAW  g_nid                  = {};

bool             g_toolEnabled          = true;
bool             g_runOnStartup         = false;
bool             g_closeToTray          = true;
bool             g_alwaysRunAsAdmin     = false;
bool             g_isExiting            = false;
bool             g_trayBalloonShown     = false;
bool             g_trackingMouse        = false;
bool             g_isRunningAsAdmin     = false;
RebindTarget     g_rebindTarget         = RebindTarget::NONE;
int              g_dragDeadzonePx       = 2;
UINT             g_hotkeyToggle         = VK_F8;
TabletEngineMode g_engineMode           = TabletEngineMode::HYBRID_UNIVERSAL;
UINT             g_currentDpi           = 96;

bool             g_isPenDown            = false;
bool             g_shiftAnchored        = false;
AxisLock         g_lockedAxis           = AxisLock::NONE;
POINT            g_downPt               = {0, 0};
POINT            g_lastClampedPt        = {0, 0};

// Special Rulers State
RulerState       g_rulerState           = RulerState::NONE;
RulerModeKey     g_activeRulerKey       = RulerModeKey::NONE;
POINT            g_rulerPt1             = {0, 0};
POINT            g_rulerPt2             = {0, 0};
POINT            g_rulerPreviewPt       = {0, 0};
double           g_rulerDirX            = 1.0;
double           g_rulerDirY            = 0.0;
double           g_rulerAngleDeg        = 0.0;
double           g_circleLockedRadius   = 0.0;

// Ellipse Ruler Parameters
double           g_ellipseCenterX       = 0.0;
double           g_ellipseCenterY       = 0.0;
double           g_ellipseRadiusA       = 0.0;
double           g_ellipseLockedB       = 0.0;

bool             g_swallowRulerClick    = false;

// Trạng thái vật lý của 4 công cụ thước (theo dõi cả mã VK lúc nhấn để nhả phím an toàn)
bool             g_zKeyPhysicalDown     = false;
bool             g_zKeySwallowed        = false;
UINT             g_zKeyActiveVk         = 0;

bool             g_xKeyPhysicalDown     = false;
bool             g_xKeySwallowed        = false;
UINT             g_xKeyActiveVk         = 0;

bool             g_cKeyPhysicalDown     = false;
bool             g_cKeySwallowed        = false;
UINT             g_cKeyActiveVk         = 0;

bool             g_eKeyPhysicalDown     = false;
bool             g_eKeySwallowed        = false;
UINT             g_eKeyActiveVk         = 0;

DWORD                 g_lastOverlayTick      = 0;
POINT                 g_lastOverlayCursorPt  = {-99999, -99999};
std::atomic<bool>     g_overlayMsgPending{false};

// Quản lý danh sách Profile theo từng phần mềm
std::vector<AppProfile> g_appProfiles;
AppProfile              g_defaultProfile = { L"Global (All Apps)", VK_SHIFT, 'Z', 'X', 'C', 'E' };
int                     g_selectedProfileIdx = 0;
std::atomic<uint32_t>   g_profileConfigRevision{1};
CRITICAL_SECTION        g_exeListLock;

// Cached GDI+ Fonts
FontFamily*      g_pFontFamily          = nullptr;
Font*            g_pFontTitle           = nullptr;
Font*            g_pFontSub             = nullptr;
Font*            g_pFontCardTitle       = nullptr;
Font*            g_pFontCardDesc        = nullptr;
Font*            g_pFontSmall           = nullptr;
Font*            g_pFontBadge           = nullptr;
Font*            g_pFontValue           = nullptr;
Font*            g_pFontOverlayBadge    = nullptr;

// Persistent Overlay Surface
struct OverlaySurface {
    HDC     hdcMem          = nullptr;
    HBITMAP hbmMem          = nullptr;
    HBITMAP hbmOld          = nullptr;
    void*   pvBits          = nullptr;
    int     width           = 0;
    int     height          = 0;
    int     monLeft         = 0;
    int     monTop          = 0;
    RECT    prevDirtyRect   = {0, 0, 0, 0};
    bool    hasPrevDirty    = false;
    bool    needsFullUpdate = true;

    void Release() {
        if (hdcMem) {
            if (hbmOld) SelectObject(hdcMem, hbmOld);
            if (hbmMem) DeleteObject(hbmMem);
            DeleteDC(hdcMem);
        }
        hdcMem = nullptr; hbmMem = nullptr; hbmOld = nullptr; pvBits = nullptr;
        width = 0; height = 0; hasPrevDirty = false; needsFullUpdate = true;
    }

    bool EnsureSize(int w, int h) {
        if (w <= 0 || h <= 0) return false;
        if (hdcMem && hbmMem && pvBits && width == w && height == h) return true;
        Release();
        HDC hdcScreen = GetDC(nullptr);
        hdcMem = CreateCompatibleDC(hdcScreen);
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth       = w;
        bmi.bmiHeader.biHeight      = -h; // Top-down DIB
        bmi.bmiHeader.biPlanes      = 1;
        bmi.bmiHeader.biBitCount    = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        hbmMem = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pvBits, nullptr, 0);
        ReleaseDC(nullptr, hdcScreen);
        if (!hbmMem || !pvBits) { Release(); return false; }
        hbmOld = (HBITMAP)SelectObject(hdcMem, hbmMem);
        width = w; height = h;
        ZeroMemory(pvBits, static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
        hasPrevDirty = false;
        needsFullUpdate = true;
        return true;
    }

    void ClearRectFast(const RECT& rc) {
        if (!pvBits || width <= 0 || height <= 0) return;
        int x1 = (std::max)(0L, rc.left);
        int y1 = (std::max)(0L, rc.top);
        int x2 = (std::min)(static_cast<LONG>(width), rc.right);
        int y2 = (std::min)(static_cast<LONG>(height), rc.bottom);
        if (x2 <= x1 || y2 <= y1) return;
        size_t rowBytes = static_cast<size_t>(x2 - x1) * 4;
        BYTE* base = static_cast<BYTE*>(pvBits);
        for (int y = y1; y < y2; ++y) {
            ZeroMemory(base + (static_cast<size_t>(y) * width + x1) * 4, rowBytes);
        }
    }
} g_overlaySurf;

enum class HitZone {
    NONE = 0, TOGGLE_ACTIVE, MODE_PREV, MODE_NEXT, BTN_ELEVATE_ADMIN,
    DEADZONE_MINUS, DEADZONE_PLUS, TOGGLE_STARTUP, TOGGLE_CLOSE_TO_TRAY,
    TOGGLE_ALWAYS_ADMIN, BTN_HIDE_TRAY, BTN_EXIT_APP, BTN_ADD_EXE, 
    BTN_REMOVE_EXE, BTN_CHANGE_HOTKEY,
    PROFILE_PREV, PROFILE_NEXT,
    BIND_MOD, BIND_RULER_Z, BIND_RULER_X, BIND_RULER_C, BIND_RULER_E
};

HitZone g_hoveredZone = HitZone::NONE;
RectF g_rcToggleActive, g_rcModePrev, g_rcModeNext, g_rcBtnElevate;
RectF g_rcDeadzoneMinus, g_rcDeadzonePlus, g_rcToggleStartup, g_rcToggleCloseTray;
RectF g_rcToggleAlwaysAdmin, g_rcBtnHideTray, g_rcBtnExitApp, g_rcBtnAddExe, g_rcBtnRemoveExe;
RectF g_rcBtnChangeHotkey, g_rcProfPrev, g_rcProfNext;
RectF g_rcBindMod, g_rcBindZ, g_rcBindX, g_rcBindC, g_rcBindE;

// ----------------------------------------------------------------------------
// Ellipse Geometry Helper Functions
// ----------------------------------------------------------------------------
double ComputeEllipseBFromCursor(POINT pt) {
    double a = g_ellipseRadiusA;
    if (a <= 1.0) return 1.0;

    double relX = static_cast<double>(pt.x) - g_ellipseCenterX;
    double relY = static_cast<double>(pt.y) - g_ellipseCenterY;

    double xLocal =  relX * g_rulerDirX + relY * g_rulerDirY;
    double yLocal = -relX * g_rulerDirY + relY * g_rulerDirX;

    double distSq = xLocal * xLocal + yLocal * yLocal;
    if (std::abs(xLocal) >= a || distSq >= a * a) {
        return a; // Giới hạn tối đa là hình tròn bán kính a
    }

    double ratio = xLocal / a;
    double denom = std::sqrt((std::max)(1e-6, 1.0 - ratio * ratio));
    double b = std::abs(yLocal) / denom;
    return (std::max)(1.0, (std::min)(a, b));
}

POINT ProjectPointOntoEllipse(POINT pt, double b) {
    double a = (std::max)(1.0, g_ellipseRadiusA);
    double safeB = (std::max)(1.0, (std::min)(a, b));

    double relX = static_cast<double>(pt.x) - g_ellipseCenterX;
    double relY = static_cast<double>(pt.y) - g_ellipseCenterY;

    double xLocal =  relX * g_rulerDirX + relY * g_rulerDirY;
    double yLocal = -relX * g_rulerDirY + relY * g_rulerDirX;

    double normDist = std::hypot(xLocal / a, yLocal / safeB);
    if (normDist < 1e-4) {
        return g_lastClampedPt;
    }

    double ex = xLocal / normDist;
    double ey = yLocal / normDist;

    double screenX = g_ellipseCenterX + ex * g_rulerDirX - ey * g_rulerDirY;
    double screenY = g_ellipseCenterY + ex * g_rulerDirY + ey * g_rulerDirX;

    POINT res;
    res.x = static_cast<LONG>(std::llround(screenX));
    res.y = static_cast<LONG>(std::llround(screenY));
    return res;
}

// ----------------------------------------------------------------------------
// Utility, Key Matching, DPI & Privilege Functions
// ----------------------------------------------------------------------------
UINT NormalizeModifierVk(UINT vk) {
    if (vk == VK_LSHIFT   || vk == VK_RSHIFT)   return VK_SHIFT;
    if (vk == VK_LCONTROL || vk == VK_RCONTROL) return VK_CONTROL;
    if (vk == VK_LMENU    || vk == VK_RMENU)    return VK_MENU;
    return vk;
}

bool MatchesConfiguredKey(UINT pressedVk, UINT configuredVk) {
    if (pressedVk == configuredVk) return true;
    return NormalizeModifierVk(pressedVk) == NormalizeModifierVk(configuredVk);
}

bool IsConfiguredKeyPhysicallyDown(UINT configuredVk) {
    UINT norm = NormalizeModifierVk(configuredVk);
    if (norm == VK_SHIFT) {
        return (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0 ||
               (GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0 ||
               (GetAsyncKeyState(VK_SHIFT)  & 0x8000) != 0;
    }
    if (norm == VK_CONTROL) {
        return (GetAsyncKeyState(VK_LCONTROL) & 0x8000) != 0 ||
               (GetAsyncKeyState(VK_RCONTROL) & 0x8000) != 0 ||
               (GetAsyncKeyState(VK_CONTROL)  & 0x8000) != 0;
    }
    if (norm == VK_MENU) {
        return (GetAsyncKeyState(VK_LMENU) & 0x8000) != 0 ||
               (GetAsyncKeyState(VK_RMENU) & 0x8000) != 0 ||
               (GetAsyncKeyState(VK_MENU)  & 0x8000) != 0;
    }
    return (GetAsyncKeyState( static_cast<int>(configuredVk) ) & 0x8000) != 0;
}

// Đảm bảo không kích hoạt nhầm khi người dùng bấm tổ hợp phím tắt hệ thống (như Ctrl+Shift+Z)
bool HasUnintendedSystemModifiers(const AppProfile& prof) {
    UINT modNorm = NormalizeModifierVk(prof.vkModifier);
    bool ctrlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    bool altDown  = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;
    bool shfDown  = (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0;
    bool winDown  = (GetAsyncKeyState(VK_LWIN)    & 0x8000) != 0 || (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;

    if (winDown) return true;
    if (ctrlDown && modNorm != VK_CONTROL) return true;
    if (altDown  && modNorm != VK_MENU)    return true;
    if (shfDown  && modNorm != VK_SHIFT && modNorm != 0) {
        if (modNorm == VK_CONTROL || modNorm == VK_MENU) return true;
    }
    return false;
}

std::wstring GetKeyName(UINT vk) {
    UINT norm = NormalizeModifierVk(vk);
    if (norm == VK_SHIFT)   return L"Shift";
    if (norm == VK_CONTROL) return L"Ctrl";
    if (norm == VK_MENU)    return L"Alt";
    if (vk == VK_SPACE)     return L"Space";
    if (vk == VK_TAB)       return L"Tab";
    if (vk == VK_CAPITAL)   return L"CapsLock";
    if (vk == VK_OEM_3)     return L"~";

    wchar_t name[128] = {0};
    int scanCode = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    switch (vk) {
        case VK_LEFT: case VK_UP: case VK_RIGHT: case VK_DOWN:
        case VK_PRIOR: case VK_NEXT: case VK_END: case VK_HOME:
        case VK_INSERT: case VK_DELETE: case VK_DIVIDE: case VK_NUMLOCK:
            scanCode |= 0x100;
            break;
    }
    if (GetKeyNameTextW(scanCode << 16, name, 128) == 0) {
        swprintf_s(name, L"Key %u", vk);
    }
    return std::wstring(name);
}

void InitCachedFonts() {
    g_pFontFamily       = new FontFamily(L"Segoe UI");
    g_pFontTitle        = new Font(g_pFontFamily, 16.0f, FontStyleBold, UnitPixel);
    g_pFontSub          = new Font(g_pFontFamily, 10.8f, FontStyleRegular, UnitPixel);
    g_pFontCardTitle    = new Font(g_pFontFamily, 14.0f, FontStyleBold, UnitPixel);
    g_pFontCardDesc     = new Font(g_pFontFamily, 12.0f, FontStyleRegular, UnitPixel);
    g_pFontSmall        = new Font(g_pFontFamily, 11.5f, FontStyleRegular, UnitPixel);
    g_pFontBadge        = new Font(g_pFontFamily, 11.0f, FontStyleBold, UnitPixel);
    g_pFontValue        = new Font(g_pFontFamily, 13.0f, FontStyleBold, UnitPixel);
    g_pFontOverlayBadge = new Font(g_pFontFamily, 11.5f, FontStyleBold, UnitPixel);
}

void CleanupCachedFonts() {
    delete g_pFontOverlayBadge; g_pFontOverlayBadge = nullptr;
    delete g_pFontValue;        g_pFontValue        = nullptr;
    delete g_pFontBadge;        g_pFontBadge        = nullptr;
    delete g_pFontSmall;        g_pFontSmall        = nullptr;
    delete g_pFontCardDesc;     g_pFontCardDesc     = nullptr;
    delete g_pFontCardTitle;    g_pFontCardTitle    = nullptr;
    delete g_pFontSub;          g_pFontSub          = nullptr;
    delete g_pFontTitle;        g_pFontTitle        = nullptr;
    delete g_pFontFamily;       g_pFontFamily       = nullptr;
}

UINT QueryWindowDpi(HWND hwnd) {
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        typedef UINT(WINAPI* PFN_GetDpiForWindow)(HWND);
        auto getDpiForWindow = reinterpret_cast<PFN_GetDpiForWindow>(GetProcAddress(hUser32, "GetDpiForWindow"));
        if (getDpiForWindow && hwnd) {
            UINT dpi = getDpiForWindow(hwnd);
            if (dpi > 0) return dpi;
        }
        typedef UINT(WINAPI* PFN_GetDpiForSystem)(void);
        auto getDpiForSystem = reinterpret_cast<PFN_GetDpiForSystem>(GetProcAddress(hUser32, "GetDpiForSystem"));
        if (getDpiForSystem) {
            UINT dpi = getDpiForSystem();
            if (dpi > 0) return dpi;
        }
    }
    HDC hdc = GetDC(hwnd);
    UINT dpi = static_cast<UINT>(GetDeviceCaps(hdc, LOGPIXELSX));
    ReleaseDC(hwnd, hdc);
    return (dpi > 0) ? dpi : 96;
}

void ResizeWindowForDpi(HWND hwnd, UINT dpi, const RECT* prcSuggested = nullptr) {
    g_currentDpi = (dpi > 0) ? dpi : 96;
    int targetClientW = MulDiv(LOGICAL_WIDTH, g_currentDpi, 96);
    int targetClientH = MulDiv(LOGICAL_HEIGHT, g_currentDpi, 96);

    DWORD dwStyle   = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
    DWORD dwExStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));

    RECT rcCalc = {0, 0, targetClientW, targetClientH};
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    bool adjusted = false;
    if (hUser32) {
        typedef BOOL(WINAPI* PFN_AdjustWindowRectExForDpi)(LPRECT, DWORD, BOOL, DWORD, UINT);
        auto adjustForDpi = reinterpret_cast<PFN_AdjustWindowRectExForDpi>(GetProcAddress(hUser32, "AdjustWindowRectExForDpi"));
        if (adjustForDpi) {
            adjusted = (adjustForDpi(&rcCalc, dwStyle, FALSE, dwExStyle, g_currentDpi) != FALSE);
        }
    }
    if (!adjusted) {
        AdjustWindowRectEx(&rcCalc, dwStyle, FALSE, dwExStyle);
    }

    int winW = rcCalc.right - rcCalc.left;
    int winH = rcCalc.bottom - rcCalc.top;
    int posX = 0, posY = 0;

    if (prcSuggested) {
        posX = prcSuggested->left;
        posY = prcSuggested->top;
    } else {
        RECT rcCur;
        GetWindowRect(hwnd, &rcCur);
        posX = rcCur.left;
        posY = rcCur.top;
    }

    SetWindowPos(hwnd, nullptr, posX, posY, winW, winH, SWP_NOZORDER | SWP_NOACTIVATE);

    RECT rcClient;
    if (GetClientRect(hwnd, &rcClient)) {
        int diffW = targetClientW - (rcClient.right - rcClient.left);
        int diffH = targetClientH - (rcClient.bottom - rcClient.top);
        if (diffW != 0 || diffH != 0) {
            SetWindowPos(hwnd, nullptr, posX, posY, winW + diffW, winH + diffH, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
        }
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

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

// ----------------------------------------------------------------------------
// Registry & Profile Serialization
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

bool ParseProfileRecord(const std::wstring& rec, AppProfile& outProf) {
    std::vector<std::wstring> parts;
    size_t start = 0, pos = 0;
    while ((pos = rec.find(L'|', start)) != std::wstring::npos) {
        parts.push_back(rec.substr(start, pos - start));
        start = pos + 1;
    }
    parts.push_back(rec.substr(start));
    if (parts.empty() || parts[0].empty()) return false;

    outProf.exeName    = parts[0];
    outProf.vkModifier = (parts.size() > 1) ? static_cast<UINT>(_wtoi(parts[1].c_str())) : VK_SHIFT;
    outProf.vkRuler2Pt = (parts.size() > 2) ? static_cast<UINT>(_wtoi(parts[2].c_str())) : 'Z';
    outProf.vkRadial   = (parts.size() > 3) ? static_cast<UINT>(_wtoi(parts[3].c_str())) : 'X';
    outProf.vkCircle   = (parts.size() > 4) ? static_cast<UINT>(_wtoi(parts[4].c_str())) : 'C';
    outProf.vkEllipse  = (parts.size() > 5) ? static_cast<UINT>(_wtoi(parts[5].c_str())) : 'E';

    if (outProf.vkModifier == 0) outProf.vkModifier = VK_SHIFT;
    if (outProf.vkRuler2Pt == 0) outProf.vkRuler2Pt = 'Z';
    if (outProf.vkRadial   == 0) outProf.vkRadial   = 'X';
    if (outProf.vkCircle   == 0) outProf.vkCircle   = 'C';
    if (outProf.vkEllipse  == 0) outProf.vkEllipse  = 'E';
    return true;
}

std::wstring SerializeProfileRecord(const AppProfile& prof) {
    wchar_t buf[512];
    swprintf_s(buf, L"%s|%u|%u|%u|%u|%u",
               prof.exeName.c_str(),
               prof.vkModifier, prof.vkRuler2Pt, prof.vkRadial, prof.vkCircle, prof.vkEllipse);
    return std::wstring(buf);
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

        EnterCriticalSection(&g_exeListLock);
        g_appProfiles.clear();

        // Đọc DefaultProfile
        wchar_t defBuf[512] = {0};
        sz = sizeof(defBuf);
        if (RegQueryValueExW(hKey, L"DefaultProfile", nullptr, nullptr, reinterpret_cast<LPBYTE>(defBuf), &sz) == ERROR_SUCCESS) {
            AppProfile parsedDef;
            if (ParseProfileRecord(defBuf, parsedDef)) {
                parsedDef.exeName = L"Global (All Apps)";
                g_defaultProfile = parsedDef;
            }
        }

        // Đọc danh sách AppProfiles (Bao gồm cả hotkey riêng cho từng .exe)
        wchar_t profBuf[4096] = {0};
        sz = sizeof(profBuf);
        bool loadedNewProfiles = false;
        if (RegQueryValueExW(hKey, L"AppProfiles", nullptr, nullptr, reinterpret_cast<LPBYTE>(profBuf), &sz) == ERROR_SUCCESS) {
            loadedNewProfiles = true;
            std::wstring list(profBuf);
            size_t pos = 0;
            while ((pos = list.find(L';')) != std::wstring::npos) {
                std::wstring token = list.substr(0, pos);
                AppProfile prof;
                if (ParseProfileRecord(token, prof)) g_appProfiles.push_back(prof);
                list.erase(0, pos + 1);
            }
            if (!list.empty()) {
                AppProfile prof;
                if (ParseProfileRecord(list, prof)) g_appProfiles.push_back(prof);
            }
        }

        // Tương thích ngược: Nếu chưa có AppProfiles nhưng có TargetExes từ bản cũ
        if (!loadedNewProfiles) {
            wchar_t exeList[2048] = {0};
            sz = sizeof(exeList);
            if (RegQueryValueExW(hKey, L"TargetExes", nullptr, nullptr, reinterpret_cast<LPBYTE>(exeList), &sz) == ERROR_SUCCESS) {
                std::wstring list(exeList);
                size_t pos = 0;
                while ((pos = list.find(L';')) != std::wstring::npos) {
                    std::wstring token = list.substr(0, pos);
                    if (!token.empty()) {
                        AppProfile p = g_defaultProfile;
                        p.exeName = token;
                        g_appProfiles.push_back(p);
                    }
                    list.erase(0, pos + 1);
                }
                if (!list.empty()) {
                    AppProfile p = g_defaultProfile;
                    p.exeName = list;
                    g_appProfiles.push_back(p);
                }
            }
        }
        g_selectedProfileIdx = 0;
        g_profileConfigRevision.fetch_add(1, std::memory_order_release);
        LeaveCriticalSection(&g_exeListLock);

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
        
        EnterCriticalSection(&g_exeListLock);
        std::wstring defStr = SerializeProfileRecord(g_defaultProfile);
        std::wstring profList;
        std::wstring legacyExeList;
        for (const auto& prof : g_appProfiles) {
            profList      += SerializeProfileRecord(prof) + L";";
            legacyExeList += prof.exeName + L";";
        }
        g_profileConfigRevision.fetch_add(1, std::memory_order_release);
        LeaveCriticalSection(&g_exeListLock);

        RegSetValueExW(hKey, L"DefaultProfile", 0, REG_SZ, reinterpret_cast<const BYTE*>(defStr.c_str()), static_cast<DWORD>((defStr.size() + 1) * sizeof(wchar_t)));
        RegSetValueExW(hKey, L"AppProfiles", 0, REG_SZ, reinterpret_cast<const BYTE*>(profList.c_str()), static_cast<DWORD>((profList.size() + 1) * sizeof(wchar_t)));
        RegSetValueExW(hKey, L"TargetExes", 0, REG_SZ, reinterpret_cast<const BYTE*>(legacyExeList.c_str()), static_cast<DWORD>((legacyExeList.size() + 1) * sizeof(wchar_t)));
        
        RegCloseKey(hKey);
    }
}

AppProfile& GetCurrentlySelectedUIProfile() {
    if (g_appProfiles.empty()) return g_defaultProfile;
    if (g_selectedProfileIdx < 0) g_selectedProfileIdx = 0;
    if (g_selectedProfileIdx >= static_cast<int>(g_appProfiles.size())) {
        g_selectedProfileIdx = static_cast<int>(g_appProfiles.size()) - 1;
    }
    return g_appProfiles[g_selectedProfileIdx];
}

// ----------------------------------------------------------------------------
// Fast Transparent Ruler Overlay HUD (Compact Badges + Dirty-Rect Optimized)
// ----------------------------------------------------------------------------
void AddRoundedRectPath(GraphicsPath& path, RectF rc, REAL radius);

void ExpandDirtyRect(RECT& rcDirty, int x, int y, int pad) {
    if (x - pad < rcDirty.left)   rcDirty.left   = x - pad;
    if (y - pad < rcDirty.top)    rcDirty.top    = y - pad;
    if (x + pad > rcDirty.right)  rcDirty.right  = x + pad;
    if (y + pad > rcDirty.bottom) rcDirty.bottom = y + pad;
}

bool ClipInfiniteLineToBox(REAL cx, REAL cy, REAL ux, REAL uy, REAL w, REAL h, PointF& outP1, PointF& outP2) {
    REAL tMin = -1e6f, tMax = 1e6f;
    if (std::abs(ux) > 1e-5f) {
        REAL tx1 = (0.0f - cx) / ux;
        REAL tx2 = (w    - cx) / ux;
        if (tx1 > tx2) std::swap(tx1, tx2);
        tMin = (std::max)(tMin, tx1);
        tMax = (std::min)(tMax, tx2);
    } else if (cx < 0.0f || cx > w) {
        return false;
    }
    if (std::abs(uy) > 1e-5f) {
        REAL ty1 = (0.0f - cy) / uy;
        REAL ty2 = (h    - cy) / uy;
        if (ty1 > ty2) std::swap(ty1, ty2);
        tMin = (std::max)(tMin, ty1);
        tMax = (std::min)(tMax, ty2);
    } else if (cy < 0.0f || cy > h) {
        return false;
    }
    if (tMin > tMax) return false;
    outP1 = PointF(cx + ux * tMin, cy + uy * tMin);
    outP2 = PointF(cx + ux * tMax, cy + uy * tMax);
    return true;
}

void RenderRulerOverlay() {
    g_overlayMsgPending.store(false, std::memory_order_release);
    if (!g_hOverlayWnd) return;

    RulerState state = g_rulerState;
    if (state == RulerState::NONE || !g_toolEnabled) {
        if (IsWindowVisible(g_hOverlayWnd)) {
            ShowWindow(g_hOverlayWnd, SW_HIDE);
            g_overlaySurf.needsFullUpdate = true;
        }
        return;
    }

    POINT refPt = g_rulerPreviewPt;
    if (state == RulerState::Z_ACTIVE || state == RulerState::X_ACTIVE ||
        state == RulerState::C_ACTIVE || state == RulerState::E_ACTIVE) {
        refPt = g_rulerPt1;
    }
    HMONITOR hMon = MonitorFromPoint(refPt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    int monLeft = 0, monTop = 0, monWidth = 0, monHeight = 0;
    if (GetMonitorInfoW(hMon, &mi)) {
        monLeft   = mi.rcMonitor.left;
        monTop    = mi.rcMonitor.top;
        monWidth  = mi.rcMonitor.right - mi.rcMonitor.left;
        monHeight = mi.rcMonitor.bottom - mi.rcMonitor.top;
    } else {
        monLeft   = GetSystemMetrics(SM_XVIRTUALSCREEN);
        monTop    = GetSystemMetrics(SM_YVIRTUALSCREEN);
        monWidth  = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        monHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    }
    if (monWidth <= 0 || monHeight <= 0) return;

    bool monitorChanged = (g_overlaySurf.monLeft != monLeft || g_overlaySurf.monTop != monTop ||
                           g_overlaySurf.width != monWidth || g_overlaySurf.height != monHeight);
    if (!g_overlaySurf.EnsureSize(monWidth, monHeight)) return;
    if (monitorChanged) {
        g_overlaySurf.monLeft = monLeft;
        g_overlaySurf.monTop  = monTop;
        g_overlaySurf.ClearRectFast({0, 0, monWidth, monHeight});
        g_overlaySurf.hasPrevDirty    = false;
        g_overlaySurf.needsFullUpdate = true;
    }

    if (g_overlaySurf.hasPrevDirty) {
        g_overlaySurf.ClearRectFast(g_overlaySurf.prevDirtyRect);
    }

    RECT currDirty = { monWidth, monHeight, 0, 0 };

    {
        Graphics g(g_overlaySurf.hdcMem);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

        StringFormat centerFmt;
        centerFmt.SetAlignment(StringAlignmentCenter);
        centerFmt.SetLineAlignment(StringAlignmentCenter);
        centerFmt.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsNoClip);

        // Vẽ điểm neo kèm ô thông báo tự động co gọn kích thước theo độ dài chữ
        auto drawAnchorPoint = [&](POINT ptScreen, Color color, const wchar_t* label) {
            REAL x = static_cast<REAL>(ptScreen.x - monLeft);
            REAL y = static_cast<REAL>(ptScreen.y - monTop);
            ExpandDirtyRect(currDirty, static_cast<int>(x), static_cast<int>(y), 14);

            SolidBrush glowBrush(Color(40, color.GetR(), color.GetG(), color.GetB()));
            SolidBrush coreBrush(Color(160, color.GetR(), color.GetG(), color.GetB()));
            Pen ringPen(Color(150, 255, 255, 255), 1.2f);
            g.FillEllipse(&glowBrush, x - 9.0f, y - 9.0f, 18.0f, 18.0f);
            g.FillEllipse(&coreBrush, x - 4.0f, y - 4.0f, 8.0f, 8.0f);
            g.DrawEllipse(&ringPen, x - 4.0f, y - 4.0f, 8.0f, 8.0f);

            if (label && label[0] != L'\0') {
                RectF measuredBox;
                g.MeasureString(label, -1, g_pFontOverlayBadge, PointF(0.0f, 0.0f), &measuredBox);
                REAL tagW = (std::max)(58.0f, measuredBox.Width + 16.0f);
                REAL tagH = 22.0f;

                RectF rcTag(x + 11.0f, y - 24.0f, tagW, tagH);
                ExpandDirtyRect(currDirty, static_cast<int>(rcTag.X), static_cast<int>(rcTag.Y), 4);
                ExpandDirtyRect(currDirty, static_cast<int>(rcTag.GetRight()), static_cast<int>(rcTag.GetBottom()), 4);

                GraphicsPath path;
                AddRoundedRectPath(path, rcTag, 6.0f);
                SolidBrush tagBg(Color(190, 15, 23, 42));
                Pen tagBorder(Color(140, color.GetR(), color.GetG(), color.GetB()), 1.0f);
                SolidBrush textBrush(Color(240, 248, 250, 252));
                g.FillPath(&tagBg, &path);
                g.DrawPath(&tagBorder, &path);
                g.DrawString(label, -1, g_pFontOverlayBadge, rcTag, &centerFmt, &textBrush);
            }
        };

        // 1. 2-POINT RULER (Pink Accent - Nhãn rút gọn: "Point 1", "Point 2", "%.1f°")
        if (state == RulerState::Z_WAIT_P1) {
            drawAnchorPoint(g_rulerPreviewPt, Color(255, 244, 114, 182), L"Point 1");
        } else if (state == RulerState::Z_WAIT_P2) {
            REAL x1 = static_cast<REAL>(g_rulerPt1.x - monLeft);
            REAL y1 = static_cast<REAL>(g_rulerPt1.y - monTop);
            REAL x2 = static_cast<REAL>(g_rulerPreviewPt.x - monLeft);
            REAL y2 = static_cast<REAL>(g_rulerPreviewPt.y - monTop);

            ExpandDirtyRect(currDirty, static_cast<int>(x1), static_cast<int>(y1), 6);
            ExpandDirtyRect(currDirty, static_cast<int>(x2), static_cast<int>(y2), 6);

            Pen previewDashPen(Color(75, 244, 114, 182), 1.2f);
            previewDashPen.SetDashStyle(DashStyleDash);
            g.DrawLine(&previewDashPen, x1, y1, x2, y2);

            drawAnchorPoint(g_rulerPt1, Color(255, 244, 114, 182), L"Point 1");
            drawAnchorPoint(g_rulerPreviewPt, Color(255, 244, 114, 182), L"Point 2");
        } else if (state == RulerState::Z_ACTIVE) {
            REAL cx = static_cast<REAL>(g_rulerPt1.x - monLeft);
            REAL cy = static_cast<REAL>(g_rulerPt1.y - monTop);
            REAL ux = static_cast<REAL>(g_rulerDirX);
            REAL uy = static_cast<REAL>(g_rulerDirY);

            PointF p1, p2;
            if (ClipInfiniteLineToBox(cx, cy, ux, uy, static_cast<REAL>(monWidth), static_cast<REAL>(monHeight), p1, p2)) {
                ExpandDirtyRect(currDirty, static_cast<int>(p1.X), static_cast<int>(p1.Y), 6);
                ExpandDirtyRect(currDirty, static_cast<int>(p2.X), static_cast<int>(p2.Y), 6);
                Pen rulerPen(Color(65, 244, 114, 182), 1.2f);
                rulerPen.SetDashStyle(DashStyleDash);
                g.DrawLine(&rulerPen, p1, p2);
            }

            wchar_t badgeText[32];
            swprintf_s(badgeText, L"%.1f\u00B0", g_rulerAngleDeg);
            drawAnchorPoint(g_rulerPt1, Color(255, 244, 114, 182), nullptr);
            drawAnchorPoint(g_rulerPt2, Color(255, 244, 114, 182), badgeText);
        }
        // 2. RADIAL CENTER RULER (Mint Green Accent - Nhãn rút gọn: "Center", "%.1f°")
        else if (state == RulerState::X_WAIT_CENTER) {
            drawAnchorPoint(g_rulerPreviewPt, Color(255, 52, 211, 153), L"Center");
        } else if (state == RulerState::X_ACTIVE) {
            REAL cx = static_cast<REAL>(g_rulerPt1.x - monLeft);
            REAL cy = static_cast<REAL>(g_rulerPt1.y - monTop);
            POINT targetPt = g_isPenDown ? g_lastClampedPt : g_rulerPreviewPt;
            REAL px = static_cast<REAL>(targetPt.x - monLeft);
            REAL py = static_cast<REAL>(targetPt.y - monTop);

            double dx = static_cast<double>(px - cx);
            double dy = static_cast<double>(py - cy);
            double len = std::hypot(dx, dy);

            double ux = g_rulerDirX;
            double uy = g_rulerDirY;
            if (!g_isPenDown && len >= 1.0) {
                ux = dx / len;
                uy = dy / len;
            }

            if (len >= 1.0 || g_isPenDown) {
                PointF p1, p2;
                if (ClipInfiniteLineToBox(cx, cy, static_cast<REAL>(ux), static_cast<REAL>(uy),
                                          static_cast<REAL>(monWidth), static_cast<REAL>(monHeight), p1, p2)) {
                    ExpandDirtyRect(currDirty, static_cast<int>(p1.X), static_cast<int>(p1.Y), 6);
                    ExpandDirtyRect(currDirty, static_cast<int>(p2.X), static_cast<int>(p2.Y), 6);
                    Pen radialDashPen(Color(75, 52, 211, 153), 1.3f);
                    radialDashPen.SetDashStyle(DashStyleDash);
                    g.DrawLine(&radialDashPen, p1, p2);
                }

                ExpandDirtyRect(currDirty, static_cast<int>(cx), static_cast<int>(cy), 6);
                ExpandDirtyRect(currDirty, static_cast<int>(px), static_cast<int>(py), 6);
                Pen connectPen(Color(115, 52, 211, 153), 1.2f);
                connectPen.SetDashStyle(DashStyleDot);
                g.DrawLine(&connectPen, cx, cy, px, py);
            }

            double angleDeg = std::atan2(uy, ux) * (180.0 / 3.14159265358979323846);
            wchar_t badgeText[32];
            swprintf_s(badgeText, L"%.1f\u00B0", angleDeg);
            drawAnchorPoint(g_rulerPt1, Color(255, 52, 211, 153), badgeText);
        }
        // 3. CIRCLE COMPASS (Amber Gold Accent - Nhãn rút gọn: "Center", "R: %.0f")
        else if (state == RulerState::C_WAIT_CENTER) {
            drawAnchorPoint(g_rulerPreviewPt, Color(255, 251, 191, 36), L"Center");
        } else if (state == RulerState::C_ACTIVE) {
            REAL cx = static_cast<REAL>(g_rulerPt1.x - monLeft);
            REAL cy = static_cast<REAL>(g_rulerPt1.y - monTop);
            POINT targetPt = g_isPenDown ? g_lastClampedPt : g_rulerPreviewPt;
            REAL px = static_cast<REAL>(targetPt.x - monLeft);
            REAL py = static_cast<REAL>(targetPt.y - monTop);

            double radius = g_isPenDown ? g_circleLockedRadius : std::hypot(static_cast<double>(px - cx), static_cast<double>(py - cy));
            REAL r = static_cast<REAL>(radius);

            if (r >= 1.0f) {
                ExpandDirtyRect(currDirty, static_cast<int>(cx - r), static_cast<int>(cy - r), 6);
                ExpandDirtyRect(currDirty, static_cast<int>(cx + r), static_cast<int>(cy + r), 6);
                ExpandDirtyRect(currDirty, static_cast<int>(px), static_cast<int>(py), 6);

                Pen circleDashPen(Color(85, 251, 191, 36), 1.4f);
                circleDashPen.SetDashStyle(DashStyleDash);
                g.DrawEllipse(&circleDashPen, cx - r, cy - r, r * 2.0f, r * 2.0f);

                Pen radiusPen(Color(65, 251, 191, 36), 1.0f);
                radiusPen.SetDashStyle(DashStyleDot);
                g.DrawLine(&radiusPen, cx, cy, px, py);
            }

            wchar_t badgeText[32];
            swprintf_s(badgeText, L"R: %.0f", radius);
            drawAnchorPoint(g_rulerPt1, Color(255, 251, 191, 36), badgeText);
        }
        // 4. TILTED ELLIPSE & MAX-CIRCLE RULER (Sky Blue Accent - Nhãn rút gọn: "Point 1", "Point 2", "%.0f×%.0f")
        else if (state == RulerState::E_WAIT_P1) {
            drawAnchorPoint(g_rulerPreviewPt, Color(255, 56, 189, 248), L"Point 1");
        } else if (state == RulerState::E_WAIT_P2) {
            REAL x1 = static_cast<REAL>(g_rulerPt1.x - monLeft);
            REAL y1 = static_cast<REAL>(g_rulerPt1.y - monTop);
            REAL x2 = static_cast<REAL>(g_rulerPreviewPt.x - monLeft);
            REAL y2 = static_cast<REAL>(g_rulerPreviewPt.y - monTop);
            REAL mx = (x1 + x2) * 0.5f;
            REAL my = (y1 + y2) * 0.5f;
            REAL rad = static_cast<REAL>(std::hypot(x2 - x1, y2 - y1) * 0.5);

            ExpandDirtyRect(currDirty, static_cast<int>(mx - rad), static_cast<int>(my - rad), 8);
            ExpandDirtyRect(currDirty, static_cast<int>(mx + rad), static_cast<int>(mx + rad), 8);

            if (rad >= 2.0f) {
                Pen maxCirclePen(Color(40, 56, 189, 248), 1.0f);
                maxCirclePen.SetDashStyle(DashStyleDot);
                g.DrawEllipse(&maxCirclePen, mx - rad, my - rad, rad * 2.0f, rad * 2.0f);
            }

            Pen axisDashPen(Color(95, 56, 189, 248), 1.3f);
            axisDashPen.SetDashStyle(DashStyleDash);
            g.DrawLine(&axisDashPen, x1, y1, x2, y2);

            drawAnchorPoint(g_rulerPt1, Color(255, 56, 189, 248), L"Point 1");
            drawAnchorPoint(g_rulerPreviewPt, Color(255, 56, 189, 248), L"Point 2");
        } else if (state == RulerState::E_ACTIVE) {
            REAL cx = static_cast<REAL>(g_ellipseCenterX - monLeft);
            REAL cy = static_cast<REAL>(g_ellipseCenterY - monTop);
            double a = (std::max)(1.0, g_ellipseRadiusA);
            double b = g_isPenDown ? g_ellipseLockedB : ComputeEllipseBFromCursor(g_rulerPreviewPt);

            double vx = -g_rulerDirY;
            double vy =  g_rulerDirX;
            int boundHalfW = static_cast<int>(std::ceil(std::hypot(a * g_rulerDirX, b * vx))) + 8;
            int boundHalfH = static_cast<int>(std::ceil(std::hypot(a * g_rulerDirY, b * vy))) + 8;
            ExpandDirtyRect(currDirty, static_cast<int>(cx) - boundHalfW, static_cast<int>(cy) - boundHalfH, 4);
            ExpandDirtyRect(currDirty, static_cast<int>(cx) + boundHalfW, static_cast<int>(cy) + boundHalfH, 4);

            REAL x1 = static_cast<REAL>(g_rulerPt1.x - monLeft);
            REAL y1 = static_cast<REAL>(g_rulerPt1.y - monTop);
            REAL x2 = static_cast<REAL>(g_rulerPt2.x - monLeft);
            REAL y2 = static_cast<REAL>(g_rulerPt2.y - monTop);
            Pen majorAxisPen(Color(60, 56, 189, 248), 1.1f);
            majorAxisPen.SetDashStyle(DashStyleDot);
            g.DrawLine(&majorAxisPen, x1, y1, x2, y2);

            REAL bx1 = cx - static_cast<REAL>(vx * b);
            REAL by1 = cy - static_cast<REAL>(vy * b);
            REAL bx2 = cx + static_cast<REAL>(vx * b);
            REAL by2 = cy + static_cast<REAL>(vy * b);
            g.DrawLine(&majorAxisPen, bx1, by1, bx2, by2);

            bool isMaxCircle = (b >= a - 0.25);
            GraphicsState gState = g.Save();
            g.TranslateTransform(cx, cy);
            g.RotateTransform(static_cast<REAL>(g_rulerAngleDeg));
            Pen ellipsePen(isMaxCircle ? Color(110, 251, 191, 36) : Color(95, 56, 189, 248), 1.5f);
            ellipsePen.SetDashStyle(DashStyleDash);
            g.DrawEllipse(&ellipsePen,
                          -static_cast<REAL>(a), -static_cast<REAL>(b),
                          static_cast<REAL>(a * 2.0), static_cast<REAL>(b * 2.0));
            g.Restore(gState);

            POINT centerScreenPt = {
                static_cast<LONG>(std::llround(g_ellipseCenterX)),
                static_cast<LONG>(std::llround(g_ellipseCenterY))
            };
            wchar_t badgeText[48];
            if (isMaxCircle) {
                swprintf_s(badgeText, L"R: %.0f", a);
            } else {
                swprintf_s(badgeText, L"%.0f\u00D7%.0f", a, b);
            }
            drawAnchorPoint(g_rulerPt1, Color(255, 56, 189, 248), nullptr);
            drawAnchorPoint(g_rulerPt2, Color(255, 56, 189, 248), nullptr);
            drawAnchorPoint(centerScreenPt, isMaxCircle ? Color(255, 251, 191, 36) : Color(255, 56, 189, 248), badgeText);
        }
    }

    currDirty.left   = (std::max)(0L, currDirty.left);
    currDirty.top    = (std::max)(0L, currDirty.top);
    currDirty.right  = (std::min)(static_cast<LONG>(monWidth), currDirty.right);
    currDirty.bottom = (std::min)(static_cast<LONG>(monHeight), currDirty.bottom);

    RECT uploadRect = currDirty;
    if (g_overlaySurf.hasPrevDirty) {
        UnionRect(&uploadRect, &currDirty, &g_overlaySurf.prevDirtyRect);
    }
    if (currDirty.right > currDirty.left && currDirty.bottom > currDirty.top) {
        g_overlaySurf.prevDirtyRect = currDirty;
        g_overlaySurf.hasPrevDirty  = true;
    } else {
        g_overlaySurf.hasPrevDirty  = false;
    }

    HDC hdcScreen = GetDC(nullptr);
    POINT ptSrc = {0, 0};
    POINT ptDst = {monLeft, monTop};
    SIZE wndSize = {monWidth, monHeight};
    BLENDFUNCTION blend = {};
    blend.BlendOp             = AC_SRC_OVER;
    blend.BlendFlags          = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat         = AC_SRC_ALPHA;

    if (!IsWindowVisible(g_hOverlayWnd) || monitorChanged || g_overlaySurf.needsFullUpdate) {
        SetWindowPos(g_hOverlayWnd, HWND_TOPMOST, monLeft, monTop, monWidth, monHeight, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        UpdateLayeredWindow(g_hOverlayWnd, hdcScreen, &ptDst, &wndSize, g_overlaySurf.hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);
        g_overlaySurf.needsFullUpdate = false;
    } else if (uploadRect.right > uploadRect.left && uploadRect.bottom > uploadRect.top) {
        UPDATELAYEREDWINDOWINFO ulwi = {};
        ulwi.cbSize   = sizeof(ulwi);
        ulwi.hdcDst   = hdcScreen;
        ulwi.pptDst   = &ptDst;
        ulwi.psize    = &wndSize;
        ulwi.hdcSrc   = g_overlaySurf.hdcMem;
        ulwi.pptSrc   = &ptSrc;
        ulwi.crKey    = 0;
        ulwi.pblend   = &blend;
        ulwi.dwFlags  = ULW_ALPHA;
        ulwi.prcDirty = &uploadRect;
        UpdateLayeredWindowIndirect(g_hOverlayWnd, &ulwi);
    }
    ReleaseDC(nullptr, hdcScreen);
}

void RequestOverlayRefresh(bool throttle = false) {
    if (!g_hMainWnd) return;
    if (throttle) {
        if (g_rulerPreviewPt.x == g_lastOverlayCursorPt.x && g_rulerPreviewPt.y == g_lastOverlayCursorPt.y) {
            return;
        }
        DWORD now = GetTickCount();
        if (now - g_lastOverlayTick < 16) return;
        g_lastOverlayTick     = now;
        g_lastOverlayCursorPt = g_rulerPreviewPt;
    }
    bool expected = false;
    if (g_overlayMsgPending.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        PostMessageW(g_hMainWnd, WM_UPDATE_OVERLAY, 0, 0);
    }
}

// ----------------------------------------------------------------------------
// Core Hook & Per-Application Profile Resolution (Real-Time Thread)
// ----------------------------------------------------------------------------
bool ResolveActiveWindowProfile(AppProfile& outProfile) {
    static HWND       s_lastForegroundWnd = nullptr;
    static DWORD      s_lastCheckTick     = 0;
    static uint32_t   s_lastConfigRev     = 0;
    static bool       s_lastMatched       = true;
    static AppProfile s_cachedProfile     = g_defaultProfile;

    HWND hForeground = GetForegroundWindow();
    if (!hForeground) return false;

    DWORD now = GetTickCount();
    uint32_t curRev = g_profileConfigRevision.load(std::memory_order_acquire);

    if (hForeground == s_lastForegroundWnd && curRev == s_lastConfigRev && (now - s_lastCheckTick < 400)) {
        outProfile = s_cachedProfile;
        return s_lastMatched;
    }

    s_lastForegroundWnd = hForeground;
    s_lastCheckTick     = now;
    s_lastConfigRev     = curRev;

    EnterCriticalSection(&g_exeListLock);
    if (g_appProfiles.empty()) {
        s_cachedProfile = g_defaultProfile;
        s_lastMatched   = true;
        outProfile      = s_cachedProfile;
        LeaveCriticalSection(&g_exeListLock);
        return true;
    }
    LeaveCriticalSection(&g_exeListLock);

    s_lastMatched = false;
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

            EnterCriticalSection(&g_exeListLock);
            for (const auto& prof : g_appProfiles) {
                std::wstring target = prof.exeName;
                std::transform(target.begin(), target.end(), target.begin(), ::towlower);
                if (exeName == target) {
                    s_cachedProfile = prof;
                    s_lastMatched   = true;
                    break;
                }
            }
            LeaveCriticalSection(&g_exeListLock);
        }
        CloseHandle(hProcess);
    }

    outProfile = s_cachedProfile;
    return s_lastMatched;
}

bool IsZPressed() { return g_zKeyPhysicalDown; }
bool IsXPressed() { return g_xKeyPhysicalDown; }
bool IsCPressed() { return g_cKeyPhysicalDown; }
bool IsEPressed() { return g_eKeyPhysicalDown; }

bool IsPrimaryButtonOrTipPhysicallyDown() {
    return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
}

bool IsZModeState(RulerState st) {
    return st == RulerState::Z_WAIT_P1 || st == RulerState::Z_WAIT_P2 || st == RulerState::Z_ACTIVE;
}

bool IsXModeState(RulerState st) {
    return st == RulerState::X_WAIT_CENTER || st == RulerState::X_ACTIVE;
}

bool IsCModeState(RulerState st) {
    return st == RulerState::C_WAIT_CENTER || st == RulerState::C_ACTIVE;
}

bool IsEModeState(RulerState st) {
    return st == RulerState::E_WAIT_P1 || st == RulerState::E_WAIT_P2 || st == RulerState::E_ACTIVE;
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

void ResetRulerState() {
    bool hadRuler = (g_rulerState != RulerState::NONE);
    g_rulerState        = RulerState::NONE;
    g_activeRulerKey    = RulerModeKey::NONE;
    g_swallowRulerClick = false;
    if (g_lockedAxis == AxisLock::CUSTOM_RULER) {
        g_lockedAxis = AxisLock::NONE;
    }
    ReleaseAxisClipCursor();
    if (hadRuler) {
        g_overlayMsgPending.store(false, std::memory_order_release);
        if (g_hMainWnd) PostMessageW(g_hMainWnd, WM_UPDATE_OVERLAY, 0, 0);
        if (g_hMainWnd && IsWindowVisible(g_hMainWnd)) {
            PostMessageW(g_hMainWnd, WM_UPDATE_UI, 0, 0);
        }
    }
}

void ActivateSpecificRulerMode(RulerModeKey targetKey) {
    AppProfile prof;
    bool inScope = ResolveActiveWindowProfile(prof);
    if (!g_toolEnabled || g_rebindTarget != RebindTarget::NONE || !inScope || HasUnintendedSystemModifiers(prof)) {
        ResetRulerState();
        return;
    }

    if (g_lockedAxis == AxisLock::HORIZONTAL || g_lockedAxis == AxisLock::VERTICAL) {
        UnlockAxisState();
    }
    g_isPenDown = false;
    g_swallowRulerClick = false;
    GetCursorPos(&g_rulerPreviewPt);

    g_activeRulerKey = targetKey;
    if (targetKey == RulerModeKey::Z_KEY) {
        g_rulerState = RulerState::Z_WAIT_P1;
    } else if (targetKey == RulerModeKey::X_KEY) {
        g_rulerState = RulerState::X_WAIT_CENTER;
    } else if (targetKey == RulerModeKey::C_KEY) {
        g_rulerState = RulerState::C_WAIT_CENTER;
    } else if (targetKey == RulerModeKey::E_KEY) {
        g_rulerState = RulerState::E_WAIT_P1;
    } else {
        ResetRulerState();
        return;
    }

    g_overlayMsgPending.store(false, std::memory_order_release);
    RequestOverlayRefresh(false);
    if (g_hMainWnd && IsWindowVisible(g_hMainWnd)) {
        PostMessageW(g_hMainWnd, WM_UPDATE_UI, 0, 0);
    }
}

void SyncRulerModeAfterKeyRelease() {
    AppProfile prof;
    bool inScope = ResolveActiveWindowProfile(prof);
    bool modHeld = inScope && IsConfiguredKeyPhysicallyDown(prof.vkModifier);

    if (!g_toolEnabled || !modHeld || HasUnintendedSystemModifiers(prof) || !inScope) {
        if (g_rulerState == RulerState::Z_ACTIVE && IsZPressed() && g_toolEnabled && inScope) {
            return;
        }
        ResetRulerState();
        return;
    }

    if (IsZPressed()) {
        if (!IsZModeState(g_rulerState)) ActivateSpecificRulerMode(RulerModeKey::Z_KEY);
    } else if (IsXPressed()) {
        if (!IsXModeState(g_rulerState)) ActivateSpecificRulerMode(RulerModeKey::X_KEY);
    } else if (IsCPressed()) {
        if (!IsCModeState(g_rulerState)) ActivateSpecificRulerMode(RulerModeKey::C_KEY);
    } else if (IsEPressed()) {
        if (!IsEModeState(g_rulerState)) ActivateSpecificRulerMode(RulerModeKey::E_KEY);
    } else {
        ResetRulerState();
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

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        auto* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        bool isKeyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        bool isKeyUp   = (wParam == WM_KEYUP   || wParam == WM_SYSKEYUP);

        AppProfile prof;
        bool inScope = ResolveActiveWindowProfile(prof);
        bool canIntercept = g_toolEnabled && (g_rebindTarget == RebindTarget::NONE) && inScope &&
                            IsConfiguredKeyPhysicallyDown(prof.vkModifier) && !HasUnintendedSystemModifiers(prof);

        // 1. Kiểm tra phím thước 2-Pt (Mặc định: Z)
        if ((isKeyDown && MatchesConfiguredKey(kb->vkCode, prof.vkRuler2Pt)) ||
            (isKeyUp   && (kb->vkCode == g_zKeyActiveVk || MatchesConfiguredKey(kb->vkCode, prof.vkRuler2Pt)))) {
            if (isKeyDown) {
                bool wasDown = g_zKeyPhysicalDown;
                g_zKeyPhysicalDown = true;
                g_zKeyActiveVk     = kb->vkCode;
                if (canIntercept) {
                    g_zKeySwallowed = true;
                    if (!wasDown || !IsZModeState(g_rulerState)) {
                        ActivateSpecificRulerMode(RulerModeKey::Z_KEY);
                    }
                    return 1;
                }
            } else if (isKeyUp) {
                g_zKeyPhysicalDown = false;
                g_zKeyActiveVk     = 0;
                bool swallowUp     = g_zKeySwallowed;
                g_zKeySwallowed    = false;
                if (IsZModeState(g_rulerState)) SyncRulerModeAfterKeyRelease();
                if (swallowUp) return 1;
            }
        }
        // 2. Kiểm tra phím thước Radial (Mặc định: X)
        else if ((isKeyDown && MatchesConfiguredKey(kb->vkCode, prof.vkRadial)) ||
                 (isKeyUp   && (kb->vkCode == g_xKeyActiveVk || MatchesConfiguredKey(kb->vkCode, prof.vkRadial)))) {
            if (isKeyDown) {
                bool wasDown = g_xKeyPhysicalDown;
                g_xKeyPhysicalDown = true;
                g_xKeyActiveVk     = kb->vkCode;
                if (canIntercept) {
                    g_xKeySwallowed = true;
                    if (!wasDown || !IsXModeState(g_rulerState)) {
                        ActivateSpecificRulerMode(RulerModeKey::X_KEY);
                    }
                    return 1;
                }
            } else if (isKeyUp) {
                g_xKeyPhysicalDown = false;
                g_xKeyActiveVk     = 0;
                bool swallowUp     = g_xKeySwallowed;
                g_xKeySwallowed    = false;
                if (IsXModeState(g_rulerState)) SyncRulerModeAfterKeyRelease();
                if (swallowUp) return 1;
            }
        }
        // 3. Kiểm tra phím thước Circle (Mặc định: C)
        else if ((isKeyDown && MatchesConfiguredKey(kb->vkCode, prof.vkCircle)) ||
                 (isKeyUp   && (kb->vkCode == g_cKeyActiveVk || MatchesConfiguredKey(kb->vkCode, prof.vkCircle)))) {
            if (isKeyDown) {
                bool wasDown = g_cKeyPhysicalDown;
                g_cKeyPhysicalDown = true;
                g_cKeyActiveVk     = kb->vkCode;
                if (canIntercept) {
                    g_cKeySwallowed = true;
                    if (!wasDown || !IsCModeState(g_rulerState)) {
                        ActivateSpecificRulerMode(RulerModeKey::C_KEY);
                    }
                    return 1;
                }
            } else if (isKeyUp) {
                g_cKeyPhysicalDown = false;
                g_cKeyActiveVk     = 0;
                bool swallowUp     = g_cKeySwallowed;
                g_cKeySwallowed    = false;
                if (IsCModeState(g_rulerState)) SyncRulerModeAfterKeyRelease();
                if (swallowUp) return 1;
            }
        }
        // 4. Kiểm tra phím thước Ellipse (Mặc định: E)
        else if ((isKeyDown && MatchesConfiguredKey(kb->vkCode, prof.vkEllipse)) ||
                 (isKeyUp   && (kb->vkCode == g_eKeyActiveVk || MatchesConfiguredKey(kb->vkCode, prof.vkEllipse)))) {
            if (isKeyDown) {
                bool wasDown = g_eKeyPhysicalDown;
                g_eKeyPhysicalDown = true;
                g_eKeyActiveVk     = kb->vkCode;
                if (canIntercept) {
                    g_eKeySwallowed = true;
                    if (!wasDown || !IsEModeState(g_rulerState)) {
                        ActivateSpecificRulerMode(RulerModeKey::E_KEY);
                    }
                    return 1;
                }
            } else if (isKeyUp) {
                g_eKeyPhysicalDown = false;
                g_eKeyActiveVk     = 0;
                bool swallowUp     = g_eKeySwallowed;
                g_eKeySwallowed    = false;
                if (IsEModeState(g_rulerState)) SyncRulerModeAfterKeyRelease();
                if (swallowUp) return 1;
            }
        }
        // 5. Kiểm tra phím giữ Modifier của Profile (Mặc định: Shift)
        else if (MatchesConfiguredKey(kb->vkCode, prof.vkModifier)) {
            if (isKeyDown) {
                if (g_toolEnabled && g_rebindTarget == RebindTarget::NONE && inScope &&
                    !HasUnintendedSystemModifiers(prof) && g_rulerState == RulerState::NONE) {
                    if (IsZPressed())      ActivateSpecificRulerMode(RulerModeKey::Z_KEY);
                    else if (IsXPressed()) ActivateSpecificRulerMode(RulerModeKey::X_KEY);
                    else if (IsCPressed()) ActivateSpecificRulerMode(RulerModeKey::C_KEY);
                    else if (IsEPressed()) ActivateSpecificRulerMode(RulerModeKey::E_KEY);
                }
            } else if (isKeyUp) {
                if (IsXModeState(g_rulerState) || IsCModeState(g_rulerState) || IsEModeState(g_rulerState) ||
                    g_rulerState == RulerState::Z_WAIT_P1 || g_rulerState == RulerState::Z_WAIT_P2 || !IsZPressed()) {
                    ResetRulerState();
                }
                if (g_lockedAxis == AxisLock::HORIZONTAL || g_lockedAxis == AxisLock::VERTICAL) {
                    UnlockAxisState();
                }
            }
        }
    }
    return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
}

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && g_toolEnabled && g_rebindTarget == RebindTarget::NONE) {
        auto* mouseInfo = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);

        if (mouseInfo->dwExtraInfo == kInjectedSignature) {
            return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
        }

        POINT pt = mouseInfo->pt;
        AppProfile prof;
        bool inWhitelist = ResolveActiveWindowProfile(prof);
        bool noExtraMod  = !HasUnintendedSystemModifiers(prof);
        bool shiftHeld   = inWhitelist && noExtraMod && IsConfiguredKeyPhysicallyDown(prof.vkModifier);
        bool zHeld       = inWhitelist && noExtraMod && IsZPressed();
        bool xHeld       = inWhitelist && noExtraMod && IsXPressed();
        bool cHeld       = inWhitelist && noExtraMod && IsCPressed();
        bool eHeld       = inWhitelist && noExtraMod && IsEPressed();

        if (g_rulerState != RulerState::NONE) {
            if (IsZModeState(g_rulerState)) {
                if (!zHeld || (!shiftHeld && g_rulerState != RulerState::Z_ACTIVE)) SyncRulerModeAfterKeyRelease();
            } else if (IsXModeState(g_rulerState)) {
                if (!xHeld || !shiftHeld) SyncRulerModeAfterKeyRelease();
            } else if (IsCModeState(g_rulerState)) {
                if (!cHeld || !shiftHeld) SyncRulerModeAfterKeyRelease();
            } else if (IsEModeState(g_rulerState)) {
                if (!eHeld || !shiftHeld) SyncRulerModeAfterKeyRelease();
            }
        } else if (shiftHeld && !g_isPenDown) {
            if (zHeld)      ActivateSpecificRulerMode(RulerModeKey::Z_KEY);
            else if (xHeld) ActivateSpecificRulerMode(RulerModeKey::X_KEY);
            else if (cHeld) ActivateSpecificRulerMode(RulerModeKey::C_KEY);
            else if (eHeld) ActivateSpecificRulerMode(RulerModeKey::E_KEY);
        }

        // ====================================================================
        // PHASE 1: SPECIAL RULER MODES (2-Pt / Radial / Circle / Ellipse)
        // ====================================================================
        if (g_rulerState != RulerState::NONE) {
            static POINT s_prevRulerSentPt = {-99999, -99999};

            switch (wParam) {
                case WM_LBUTTONDOWN: {
                    s_prevRulerSentPt = {-99999, -99999};
                    if (g_rulerState == RulerState::Z_WAIT_P1) {
                        g_rulerPt1          = pt;
                        g_rulerPreviewPt    = pt;
                        g_rulerState        = RulerState::Z_WAIT_P2;
                        g_swallowRulerClick = true;
                        RequestOverlayRefresh(false);
                        if (g_hMainWnd && IsWindowVisible(g_hMainWnd)) PostMessageW(g_hMainWnd, WM_UPDATE_UI, 0, 0);
                        return 1; 
                    }
                    if (g_rulerState == RulerState::Z_WAIT_P2) {
                        double dx = static_cast<double>(pt.x - g_rulerPt1.x);
                        double dy = static_cast<double>(pt.y - g_rulerPt1.y);
                        double len = std::hypot(dx, dy);
                        g_swallowRulerClick = true;
                        if (len >= 2.0) {
                            g_rulerPt2      = pt;
                            g_rulerDirX     = dx / len;
                            g_rulerDirY     = dy / len;
                            g_rulerAngleDeg = std::atan2(dy, dx) * (180.0 / 3.14159265358979323846);
                            g_rulerState    = RulerState::Z_ACTIVE;
                            RequestOverlayRefresh(false);
                            if (g_hMainWnd && IsWindowVisible(g_hMainWnd)) PostMessageW(g_hMainWnd, WM_UPDATE_UI, 0, 0);
                        }
                        return 1; 
                    }
                    if (g_rulerState == RulerState::X_WAIT_CENTER) {
                        g_rulerPt1          = pt;
                        g_rulerPreviewPt    = pt;
                        g_rulerState        = RulerState::X_ACTIVE;
                        g_swallowRulerClick = true;
                        RequestOverlayRefresh(false);
                        if (g_hMainWnd && IsWindowVisible(g_hMainWnd)) PostMessageW(g_hMainWnd, WM_UPDATE_UI, 0, 0);
                        return 1;
                    }
                    if (g_rulerState == RulerState::C_WAIT_CENTER) {
                        g_rulerPt1          = pt;
                        g_rulerPreviewPt    = pt;
                        g_rulerState        = RulerState::C_ACTIVE;
                        g_swallowRulerClick = true;
                        RequestOverlayRefresh(false);
                        if (g_hMainWnd && IsWindowVisible(g_hMainWnd)) PostMessageW(g_hMainWnd, WM_UPDATE_UI, 0, 0);
                        return 1;
                    }
                    if (g_rulerState == RulerState::E_WAIT_P1) {
                        g_rulerPt1          = pt;
                        g_rulerPreviewPt    = pt;
                        g_rulerState        = RulerState::E_WAIT_P2;
                        g_swallowRulerClick = true;
                        RequestOverlayRefresh(false);
                        if (g_hMainWnd && IsWindowVisible(g_hMainWnd)) PostMessageW(g_hMainWnd, WM_UPDATE_UI, 0, 0);
                        return 1;
                    }
                    if (g_rulerState == RulerState::E_WAIT_P2) {
                        double dx = static_cast<double>(pt.x - g_rulerPt1.x);
                        double dy = static_cast<double>(pt.y - g_rulerPt1.y);
                        double len = std::hypot(dx, dy);
                        g_swallowRulerClick = true;
                        if (len >= 4.0) {
                            g_rulerPt2       = pt;
                            g_ellipseCenterX = (static_cast<double>(g_rulerPt1.x) + static_cast<double>(pt.x)) * 0.5;
                            g_ellipseCenterY = (static_cast<double>(g_rulerPt1.y) + static_cast<double>(pt.y)) * 0.5;
                            g_ellipseRadiusA = len * 0.5;
                            g_rulerDirX      = dx / len;
                            g_rulerDirY      = dy / len;
                            g_rulerAngleDeg  = std::atan2(dy, dx) * (180.0 / 3.14159265358979323846);
                            g_ellipseLockedB = g_ellipseRadiusA * 0.5;
                            g_rulerState     = RulerState::E_ACTIVE;
                            RequestOverlayRefresh(false);
                            if (g_hMainWnd && IsWindowVisible(g_hMainWnd)) PostMessageW(g_hMainWnd, WM_UPDATE_UI, 0, 0);
                        }
                        return 1;
                    }

                    if (g_rulerState == RulerState::Z_ACTIVE) {
                        g_isPenDown     = true;
                        g_downPt        = pt;
                        g_lastClampedPt = pt;
                        g_lockedAxis    = AxisLock::CUSTOM_RULER;
                        ReleaseAxisClipCursor();
                        RequestOverlayRefresh(false);
                        if (g_hMainWnd && IsWindowVisible(g_hMainWnd)) PostMessageW(g_hMainWnd, WM_UPDATE_UI, 0, 0);
                    } else if (g_rulerState == RulerState::X_ACTIVE) {
                        g_isPenDown     = true;
                        g_downPt        = pt;
                        g_lastClampedPt = pt;
                        g_lockedAxis    = AxisLock::CUSTOM_RULER;
                        double dx = static_cast<double>(pt.x - g_rulerPt1.x);
                        double dy = static_cast<double>(pt.y - g_rulerPt1.y);
                        double len = std::hypot(dx, dy);
                        if (len >= 2.0) {
                            g_rulerDirX     = dx / len;
                            g_rulerDirY     = dy / len;
                            g_rulerAngleDeg = std::atan2(dy, dx) * (180.0 / 3.14159265358979323846);
                        } else {
                            g_rulerDirX = 0.0;
                            g_rulerDirY = 0.0;
                        }
                        ReleaseAxisClipCursor();
                        RequestOverlayRefresh(false);
                        if (g_hMainWnd && IsWindowVisible(g_hMainWnd)) PostMessageW(g_hMainWnd, WM_UPDATE_UI, 0, 0);
                    } else if (g_rulerState == RulerState::C_ACTIVE) {
                        g_isPenDown     = true;
                        g_downPt        = pt;
                        g_lastClampedPt = pt;
                        g_lockedAxis    = AxisLock::CUSTOM_RULER;
                        double dx = static_cast<double>(pt.x - g_rulerPt1.x);
                        double dy = static_cast<double>(pt.y - g_rulerPt1.y);
                        g_circleLockedRadius = (std::max)(1.0, std::hypot(dx, dy));
                        ReleaseAxisClipCursor();
                        RequestOverlayRefresh(false);
                        if (g_hMainWnd && IsWindowVisible(g_hMainWnd)) PostMessageW(g_hMainWnd, WM_UPDATE_UI, 0, 0);
                    } else if (g_rulerState == RulerState::E_ACTIVE) {
                        g_isPenDown      = true;
                        g_lockedAxis     = AxisLock::CUSTOM_RULER;
                        g_ellipseLockedB = ComputeEllipseBFromCursor(pt);
                        POINT startPt    = ProjectPointOntoEllipse(pt, g_ellipseLockedB);
                        g_downPt         = startPt;
                        g_lastClampedPt  = startPt;
                        ReleaseAxisClipCursor();
                        RequestOverlayRefresh(false);
                        if (g_hMainWnd && IsWindowVisible(g_hMainWnd)) PostMessageW(g_hMainWnd, WM_UPDATE_UI, 0, 0);

                        if (pt.x != startPt.x || pt.y != startPt.y) {
                            mouseInfo->pt = startPt;
                            if (g_engineMode == TabletEngineMode::WACOM_CLASSIC) SetCursorPos(startPt.x, startPt.y);
                            else InjectClampedAbsoluteMove(startPt);
                        }
                    }
                    break;
                }

                case WM_MOUSEMOVE: {
                    g_rulerPreviewPt = pt;

                    if (g_rulerState == RulerState::Z_WAIT_P1 ||
                        g_rulerState == RulerState::Z_WAIT_P2 ||
                        g_rulerState == RulerState::X_WAIT_CENTER ||
                        g_rulerState == RulerState::C_WAIT_CENTER ||
                        g_rulerState == RulerState::E_WAIT_P1 ||
                        g_rulerState == RulerState::E_WAIT_P2) {
                        RequestOverlayRefresh(true);
                        if (g_swallowRulerClick) return 1;
                        break;
                    }

                    if (g_rulerState == RulerState::Z_ACTIVE ||
                        g_rulerState == RulerState::X_ACTIVE ||
                        g_rulerState == RulerState::C_ACTIVE ||
                        g_rulerState == RulerState::E_ACTIVE) {

                        if (g_swallowRulerClick) {
                            if (!IsPrimaryButtonOrTipPhysicallyDown()) g_swallowRulerClick = false;
                            else {
                                RequestOverlayRefresh(true);
                                return 1;
                            }
                        }

                        bool lbtnPhysicallyDown = IsPrimaryButtonOrTipPhysicallyDown();
                        if (!g_isPenDown && lbtnPhysicallyDown) {
                            g_isPenDown       = true;
                            g_downPt          = pt;
                            g_lastClampedPt   = pt;
                            s_prevRulerSentPt = {-99999, -99999};
                            g_lockedAxis      = AxisLock::CUSTOM_RULER;
                            if (g_rulerState == RulerState::X_ACTIVE) {
                                double dx = static_cast<double>(pt.x - g_rulerPt1.x);
                                double dy = static_cast<double>(pt.y - g_rulerPt1.y);
                                double len = std::hypot(dx, dy);
                                if (len >= 2.0) { g_rulerDirX = dx / len; g_rulerDirY = dy / len; }
                                else { g_rulerDirX = 0.0; g_rulerDirY = 0.0; }
                            } else if (g_rulerState == RulerState::C_ACTIVE) {
                                double dx = static_cast<double>(pt.x - g_rulerPt1.x);
                                double dy = static_cast<double>(pt.y - g_rulerPt1.y);
                                g_circleLockedRadius = (std::max)(1.0, std::hypot(dx, dy));
                            } else if (g_rulerState == RulerState::E_ACTIVE) {
                                g_ellipseLockedB = ComputeEllipseBFromCursor(pt);
                                g_lastClampedPt  = ProjectPointOntoEllipse(pt, g_ellipseLockedB);
                                g_downPt         = g_lastClampedPt;
                            }
                            RequestOverlayRefresh(false);
                        } else if (g_isPenDown && !lbtnPhysicallyDown) {
                            g_isPenDown  = false;
                            g_lockedAxis = AxisLock::NONE;
                            RequestOverlayRefresh(false);
                            break;
                        }

                        if (!g_isPenDown) {
                            if (g_rulerState == RulerState::X_ACTIVE ||
                                g_rulerState == RulerState::C_ACTIVE ||
                                g_rulerState == RulerState::E_ACTIVE) {
                                RequestOverlayRefresh(true);
                            }
                            break;
                        }

                        POINT clampedPt = pt;

                        if (g_rulerState == RulerState::Z_ACTIVE) {
                            double vx   = static_cast<double>(pt.x - g_downPt.x);
                            double vy   = static_cast<double>(pt.y - g_downPt.y);
                            double proj = vx * g_rulerDirX + vy * g_rulerDirY;
                            clampedPt.x = g_downPt.x + static_cast<LONG>(std::llround(proj * g_rulerDirX));
                            clampedPt.y = g_downPt.y + static_cast<LONG>(std::llround(proj * g_rulerDirY));
                        } else if (g_rulerState == RulerState::X_ACTIVE) {
                            if (g_rulerDirX == 0.0 && g_rulerDirY == 0.0) {
                                double dx = static_cast<double>(pt.x - g_rulerPt1.x);
                                double dy = static_cast<double>(pt.y - g_rulerPt1.y);
                                double len = std::hypot(dx, dy);
                                if (len >= 2.0) {
                                    g_rulerDirX = dx / len;
                                    g_rulerDirY = dy / len;
                                }
                            }
                            if (g_rulerDirX != 0.0 || g_rulerDirY != 0.0) {
                                double vx   = static_cast<double>(pt.x - g_rulerPt1.x);
                                double vy   = static_cast<double>(pt.y - g_rulerPt1.y);
                                double proj = vx * g_rulerDirX + vy * g_rulerDirY;
                                clampedPt.x = g_rulerPt1.x + static_cast<LONG>(std::llround(proj * g_rulerDirX));
                                clampedPt.y = g_rulerPt1.y + static_cast<LONG>(std::llround(proj * g_rulerDirY));
                            }
                            RequestOverlayRefresh(true);
                        } else if (g_rulerState == RulerState::C_ACTIVE) {
                            double vx = static_cast<double>(pt.x - g_rulerPt1.x);
                            double vy = static_cast<double>(pt.y - g_rulerPt1.y);
                            double len = std::hypot(vx, vy);
                            if (len >= 0.5) {
                                clampedPt.x = g_rulerPt1.x + static_cast<LONG>(std::llround((vx / len) * g_circleLockedRadius));
                                clampedPt.y = g_rulerPt1.y + static_cast<LONG>(std::llround((vy / len) * g_circleLockedRadius));
                            } else {
                                clampedPt = g_lastClampedPt;
                            }
                            RequestOverlayRefresh(true);
                        } else if (g_rulerState == RulerState::E_ACTIVE) {
                            clampedPt = ProjectPointOntoEllipse(pt, g_ellipseLockedB);
                            RequestOverlayRefresh(true);
                        }

                        g_lastClampedPt = clampedPt;
                        bool isOffAxis = (pt.x != clampedPt.x || pt.y != clampedPt.y);

                        if (clampedPt.x == s_prevRulerSentPt.x && clampedPt.y == s_prevRulerSentPt.y) {
                            if (isOffAxis) return 1;
                        }
                        s_prevRulerSentPt = clampedPt;

                        if (g_engineMode == TabletEngineMode::WACOM_CLASSIC) {
                            if (isOffAxis) {
                                SetCursorPos(clampedPt.x, clampedPt.y);
                                return 1;
                            }
                        } else {
                            if (isOffAxis) {
                                InjectClampedAbsoluteMove(clampedPt);
                                return 1;
                            } else {
                                return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
                            }
                        }
                    }
                    break;
                }

                case WM_LBUTTONUP: {
                    s_prevRulerSentPt = {-99999, -99999};
                    if (g_swallowRulerClick) {
                        g_swallowRulerClick = false;
                        return 1;
                    }
                    if (g_isPenDown) {
                        g_isPenDown  = false;
                        g_lockedAxis = AxisLock::NONE;
                        RequestOverlayRefresh(false);
                        if (g_hMainWnd && IsWindowVisible(g_hMainWnd)) PostMessageW(g_hMainWnd, WM_UPDATE_UI, 0, 0);
                    }
                    break;
                }
            }
            return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
        }

        // ====================================================================
        // PHASE 2: STANDARD HORIZONTAL / VERTICAL AXIS LOCK
        // ====================================================================
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

DWORD WINAPI HookWorkerThreadProc(LPVOID lpParam) {
    HANDLE hReadyEvent = static_cast<HANDLE>(lpParam);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    MSG msg;
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

    HMODULE hMod = GetModuleHandleW(nullptr);
    g_mouseHook    = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, hMod, 0);
    g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, hMod, 0);

    if (hReadyEvent) SetEvent(hReadyEvent);

    if (!g_mouseHook || !g_keyboardHook) {
        if (g_mouseHook)    { UnhookWindowsHookEx(g_mouseHook);    g_mouseHook = nullptr; }
        if (g_keyboardHook) { UnhookWindowsHookEx(g_keyboardHook); g_keyboardHook = nullptr; }
        return 1;
    }

    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_mouseHook)    { UnhookWindowsHookEx(g_mouseHook);    g_mouseHook = nullptr; }
    if (g_keyboardHook) { UnhookWindowsHookEx(g_keyboardHook); g_keyboardHook = nullptr; }
    return 0;
}

// ----------------------------------------------------------------------------
// GDI+ Graphics & Per-Monitor DPI UI
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
    int clientW = rcClient.right - rcClient.left;
    int clientH = rcClient.bottom - rcClient.top;
    if (clientW <= 0 || clientH <= 0) return;

    Bitmap backBuffer(clientW, clientH, PixelFormat32bppARGB);
    Graphics g(&backBuffer);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.Clear(Color(255, 15, 23, 42));

    REAL scaleX = static_cast<REAL>(clientW) / static_cast<REAL>(LOGICAL_WIDTH);
    REAL scaleY = static_cast<REAL>(clientH) / static_cast<REAL>(LOGICAL_HEIGHT);
    g.ScaleTransform(scaleX, scaleY);

    constexpr REAL width = static_cast<REAL>(LOGICAL_WIDTH);

    SolidBrush whiteBrush(Color(255, 248, 250, 252)), mutedBrush(Color(255, 148, 163, 184));
    SolidBrush accentTextBrush(Color(255, 52, 211, 153)), emeraldTextBrush(Color(255, 52, 211, 153)), amberTextBrush(Color(255, 251, 191, 36));
    Pen divPen(Color(255, 51, 65, 85), 1.0f);

    StringFormat leftFmt, leftNoWrapFmt, centerFmt;
    leftFmt.SetAlignment(StringAlignmentNear);
    leftFmt.SetLineAlignment(StringAlignmentCenter);
    leftFmt.SetFormatFlags(StringFormatFlagsNoClip);

    leftNoWrapFmt.SetAlignment(StringAlignmentNear);
    leftNoWrapFmt.SetLineAlignment(StringAlignmentCenter);
    leftNoWrapFmt.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsNoClip);

    centerFmt.SetAlignment(StringAlignmentCenter);
    centerFmt.SetLineAlignment(StringAlignmentCenter);
    centerFmt.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsNoClip);

    auto drawSmallBtn = [&](const RectF& rc, const wchar_t* label, bool hovered, SolidBrush* customBg = nullptr) {
        GraphicsPath p; AddRoundedRectPath(p, rc, 7.0f);
        SolidBrush defaultBg(hovered ? Color(255, 71, 85, 105) : Color(255, 51, 65, 85));
        g.FillPath(customBg ? customBg : &defaultBg, &p); 
        g.DrawString(label, -1, g_pFontValue, rc, &centerFmt, &whiteBrush);
    };

    auto drawArrowBtn = [&](const RectF& rc, bool isLeft, bool hovered) {
        GraphicsPath p; AddRoundedRectPath(p, rc, 7.0f);
        SolidBrush bg(hovered ? Color(255, 71, 85, 105) : Color(255, 51, 65, 85));
        g.FillPath(&bg, &p);

        Pen arrowPen(Color(255, 248, 250, 252), 2.0f);
        arrowPen.SetStartCap(LineCapRound);
        arrowPen.SetEndCap(LineCapRound);
        arrowPen.SetLineJoin(LineJoinRound);

        REAL cx = rc.X + rc.Width / 2.0f;
        REAL cy = rc.Y + rc.Height / 2.0f;
        REAL aw = 3.5f;
        REAL ah = 5.5f;

        if (isLeft) {
            PointF pts[3] = {
                PointF(cx + aw * 0.5f, cy - ah),
                PointF(cx - aw * 0.8f, cy),
                PointF(cx + aw * 0.5f, cy + ah)
            };
            g.DrawLines(&arrowPen, pts, 3);
        } else {
            PointF pts[3] = {
                PointF(cx - aw * 0.5f, cy - ah),
                PointF(cx + aw * 0.8f, cy),
                PointF(cx - aw * 0.5f, cy + ah)
            };
            g.DrawLines(&arrowPen, pts, 3);
        }
    };

    EnterCriticalSection(&g_exeListLock);
    AppProfile uiProf = GetCurrentlySelectedUIProfile();
    int totalProfiles = static_cast<int>(g_appProfiles.size());
    int curProfIdx    = g_selectedProfileIdx;
    LeaveCriticalSection(&g_exeListLock);

    std::wstring modName = GetKeyName(uiProf.vkModifier);
    std::wstring zName   = GetKeyName(uiProf.vkRuler2Pt);
    std::wstring xName   = GetKeyName(uiProf.vkRadial);
    std::wstring cName   = GetKeyName(uiProf.vkCircle);
    std::wstring eName   = GetKeyName(uiProf.vkEllipse);

    // Header
    RectF rcHeader(20.0f, 16.0f, width - 40.0f, 74.0f);
    GraphicsPath headerPath; AddRoundedRectPath(headerPath, rcHeader, 14.0f);
    LinearGradientBrush headerGrad(PointF(rcHeader.X, rcHeader.Y), PointF(rcHeader.X + rcHeader.Width, rcHeader.Y + rcHeader.Height), Color(255, 30, 41, 59), Color(255, 15, 38, 58));
    g.FillPath(&headerGrad, &headerPath);
    Pen headerBorder(Color(60, 52, 211, 153), 1.0f); g.DrawPath(&headerBorder, &headerPath);

    RectF rcLogo(34.0f, 31.0f, 44.0f, 44.0f); GraphicsPath logoPath; AddRoundedRectPath(logoPath, rcLogo, 10.0f);
    LinearGradientBrush logoBrush(PointF(rcLogo.X, rcLogo.Y), PointF(rcLogo.X + 44.0f, rcLogo.Y + 44.0f), g_toolEnabled ? Color(255, 16, 185, 129) : Color(255, 100, 116, 139), g_toolEnabled ? Color(255, 5, 150, 105) : Color(255, 71, 85, 105));
    g.FillPath(&logoBrush, &logoPath);
    Pen logoAxisPen(Color(255, 255, 255, 255), 2.2f);
    g.DrawLine(&logoAxisPen, rcLogo.X + 10.0f, rcLogo.Y + 22.0f, rcLogo.X + 34.0f, rcLogo.Y + 22.0f);
    g.DrawLine(&logoAxisPen, rcLogo.X + 22.0f, rcLogo.Y + 10.0f, rcLogo.X + 22.0f, rcLogo.Y + 34.0f);
    g.FillEllipse(&whiteBrush, rcLogo.X + 19.0f, rcLogo.Y + 19.0f, 6.0f, 6.0f);

    g.DrawString(L"StraightCSP", -1, g_pFontTitle, RectF(88.0f, 26.0f, 280.0f, 24.0f), &leftNoWrapFmt, &whiteBrush);
    std::wstring subLine = modName + L": X/Y \u2022 +" + zName + L": 2-Pt \u2022 +" + xName + L": Radial \u2022 +" + cName + L": Circle \u2022 +" + eName + L": Ellipse";
    g.DrawString(subLine.c_str(), -1, g_pFontSub, RectF(88.0f, 52.0f, 316.0f, 22.0f), &leftNoWrapFmt, &mutedBrush);

    RectF rcBadge(width - 112.0f, 39.0f, 78.0f, 28.0f); GraphicsPath badgePath; AddRoundedRectPath(badgePath, rcBadge, 14.0f);
    SolidBrush badgeBg(g_toolEnabled ? Color(45, 16, 185, 129) : Color(45, 245, 158, 11)); Pen badgePen(g_toolEnabled ? Color(180, 16, 185, 129) : Color(180, 245, 158, 11), 1.0f);
    SolidBrush badgeTextBrush(g_toolEnabled ? Color(255, 52, 211, 153) : Color(255, 251, 191, 36));
    g.FillPath(&badgeBg, &badgePath); g.DrawPath(&badgePen, &badgePath);
    g.DrawString(g_toolEnabled ? L"\u25CF ACTIVE" : L"\u25CF PAUSED", -1, g_pFontBadge, rcBadge, &centerFmt, &badgeTextBrush);

    // Card 1: Main Controls
    RectF rcCard1(20.0f, 102.0f, width - 40.0f, 166.0f); DrawCard(g, rcCard1);
    g.DrawString(L"Axis Lock & Multi-Ruler Engine", -1, g_pFontCardTitle, RectF(36.0f, 114.0f, 360.0f, 22.0f), &leftNoWrapFmt, &whiteBrush);
    
    std::wstring hkDesc;
    if (g_rebindTarget == RebindTarget::GLOBAL_TOGGLE) {
        hkDesc = L"Press any key for Global Toggle (ESC to cancel)...";
    } else {
        hkDesc = L"Global Toggle [" + GetKeyName(g_hotkeyToggle) + L"] \u2022 Per-App Hotkeys below";
    }
    g.DrawString(hkDesc.c_str(), -1, g_pFontSmall, RectF(36.0f, 136.0f, 310.0f, 20.0f), &leftNoWrapFmt, (g_rebindTarget == RebindTarget::GLOBAL_TOGGLE) ? &accentTextBrush : &mutedBrush);
    
    g_rcBtnChangeHotkey = RectF(350.0f, 133.0f, 60.0f, 22.0f);
    SolidBrush changeBtnBrush(g_hoveredZone == HitZone::BTN_CHANGE_HOTKEY ? Color(255, 71, 85, 105) : Color(255, 51, 65, 85));
    GraphicsPath pChange; AddRoundedRectPath(pChange, g_rcBtnChangeHotkey, 4.0f);
    g.FillPath(&changeBtnBrush, &pChange);
    g.DrawString(L"Change", -1, g_pFontBadge, g_rcBtnChangeHotkey, &centerFmt, &whiteBrush);

    g_rcToggleActive = RectF(width - 88.0f, 120.0f, 50.0f, 26.0f); DrawToggleSwitch(g, g_rcToggleActive, g_toolEnabled, g_hoveredZone == HitZone::TOGGLE_ACTIVE);
    g.DrawLine(&divPen, 36.0f, 164.0f, width - 36.0f, 164.0f);

    g.DrawString(L"Tablet Lock Engine:", -1, g_pFontCardDesc, RectF(36.0f, 173.0f, 140.0f, 22.0f), &leftNoWrapFmt, &mutedBrush);
    g_rcModePrev = RectF(182.0f, 170.0f, 28.0f, 28.0f); g_rcModeNext = RectF(width - 66.0f, 170.0f, 28.0f, 28.0f);
    drawArrowBtn(g_rcModePrev, true, g_hoveredZone == HitZone::MODE_PREV);
    drawArrowBtn(g_rcModeNext, false, g_hoveredZone == HitZone::MODE_NEXT);
    g.DrawString(GetEngineModeName(g_engineMode), -1, g_pFontBadge, RectF(212.0f, 170.0f, width - 280.0f, 28.0f), &centerFmt, &accentTextBrush);
    g.DrawLine(&divPen, 36.0f, 206.0f, width - 36.0f, 206.0f);

    wchar_t axisText[128] = L"Status: Ready";
    if (!g_toolEnabled) wcscpy_s(axisText, L"Status: Tool is currently paused");
    else if (g_rulerState == RulerState::Z_WAIT_P1) wcscpy_s(axisText, L"Status: [2-Pt] Tap Point 1...");
    else if (g_rulerState == RulerState::Z_WAIT_P2) wcscpy_s(axisText, L"Status: [2-Pt] Tap Point 2...");
    else if (g_rulerState == RulerState::Z_ACTIVE) swprintf_s(axisText, L"Status: 2-Pt Locked (%.1f\u00B0)", g_rulerAngleDeg);
    else if (g_rulerState == RulerState::X_WAIT_CENTER) wcscpy_s(axisText, L"Status: [Radial] Tap Center...");
    else if (g_rulerState == RulerState::X_ACTIVE) wcscpy_s(axisText, L"Status: [Radial] Active");
    else if (g_rulerState == RulerState::C_WAIT_CENTER) wcscpy_s(axisText, L"Status: [Circle] Tap Center...");
    else if (g_rulerState == RulerState::C_ACTIVE) wcscpy_s(axisText, L"Status: [Circle] Active");
    else if (g_rulerState == RulerState::E_WAIT_P1) wcscpy_s(axisText, L"Status: [Ellipse] Tap Point 1...");
    else if (g_rulerState == RulerState::E_WAIT_P2) wcscpy_s(axisText, L"Status: [Ellipse] Tap Point 2...");
    else if (g_rulerState == RulerState::E_ACTIVE) swprintf_s(axisText, L"Status: [Ellipse] Active (%.1f\u00B0)", g_rulerAngleDeg);
    else if (g_lockedAxis == AxisLock::HORIZONTAL) wcscpy_s(axisText, L"Status: Locked HORIZONTAL (\u2194)");
    else if (g_lockedAxis == AxisLock::VERTICAL) wcscpy_s(axisText, L"Status: Locked VERTICAL (\u2195)");
    g.DrawString(axisText, -1, g_pFontSmall, RectF(36.0f, 214.0f, width - 72.0f, 20.0f), &leftNoWrapFmt, &emeraldTextBrush);

    if (g_isRunningAsAdmin) {
        g_rcBtnElevate = RectF(0, 0, 0, 0);
        g.DrawString(L"\u2713 Admin Rights: Enabled (100% Compatible with Huion/XP-Pen)", -1, g_pFontSmall, RectF(36.0f, 236.0f, width - 72.0f, 20.0f), &leftNoWrapFmt, &mutedBrush);
    } else {
        g.DrawString(L"\u26A0 Admin Rights required to fix coordinate jumping issues:", -1, g_pFontSmall, RectF(36.0f, 236.0f, 310.0f, 20.0f), &leftNoWrapFmt, &amberTextBrush);
        g_rcBtnElevate = RectF(width - 158.0f, 233.0f, 120.0f, 25.0f); GraphicsPath pElev; AddRoundedRectPath(pElev, g_rcBtnElevate, 6.0f);
        bool hov = (g_hoveredZone == HitZone::BTN_ELEVATE_ADMIN);
        SolidBrush bElev(hov ? Color(255, 245, 158, 11) : Color(55, 245, 158, 11)); Pen penElev(Color(180, 245, 158, 11), 1.0f);
        SolidBrush txtElev(hov ? Color(255, 15, 23, 42) : Color(255, 251, 191, 36));
        g.FillPath(&bElev, &pElev); g.DrawPath(&penElev, &pElev);
        g.DrawString(L"Grant Admin", -1, g_pFontBadge, g_rcBtnElevate, &centerFmt, &txtElev);
    }

    // Card 2: Deadzone
    RectF rcCard2(20.0f, 280.0f, width - 40.0f, 74.0f); DrawCard(g, rcCard2);
    g.DrawString(L"Axis Lock Deadzone", -1, g_pFontCardTitle, RectF(36.0f, 290.0f, 285.0f, 20.0f), &leftNoWrapFmt, &whiteBrush);
    g.DrawString(L"Recommended 2-4 px for Wacom/Huion/XP-Pen to prevent jitter", -1, g_pFontSmall, RectF(36.0f, 312.0f, 305.0f, 34.0f), &leftFmt, &mutedBrush);
    g_rcDeadzoneMinus = RectF(width - 144.0f, 302.0f, 30.0f, 30.0f); g_rcDeadzonePlus = RectF(width - 68.0f, 302.0f, 30.0f, 30.0f);
    drawArrowBtn(g_rcDeadzoneMinus, true, g_hoveredZone == HitZone::DEADZONE_MINUS);
    drawArrowBtn(g_rcDeadzonePlus, false, g_hoveredZone == HitZone::DEADZONE_PLUS);
    wchar_t szDeadzone[16]; swprintf_s(szDeadzone, L"%d px", g_dragDeadzonePx);
    g.DrawString(szDeadzone, -1, g_pFontCardTitle, RectF(width - 114.0f, 302.0f, 46.0f, 30.0f), &centerFmt, &accentTextBrush);

    // Card 3: Active Scope & Per-App Custom Hotkey Profiles
    RectF rcCard3(20.0f, 366.0f, width - 40.0f, 144.0f); DrawCard(g, rcCard3);
    g.DrawString(L"App Profiles & Custom Hotkeys (Click any key slot to rebind)", -1, g_pFontCardTitle, RectF(36.0f, 376.0f, 448.0f, 22.0f), &leftNoWrapFmt, &whiteBrush);

    // Dòng điều hướng Profile: [+ Add .exe]  [<] Tên_App.exe (1/N) [>]  [Remove]
    g_rcBtnAddExe = RectF(36.0f, 404.0f, 96.0f, 28.0f);
    SolidBrush addBtnBrush(g_hoveredZone == HitZone::BTN_ADD_EXE ? Color(255, 5, 150, 105) : Color(255, 16, 185, 129));
    drawSmallBtn(g_rcBtnAddExe, L"+ Add .exe", g_hoveredZone == HitZone::BTN_ADD_EXE, &addBtnBrush);

    g_rcProfPrev = RectF(140.0f, 404.0f, 28.0f, 28.0f);
    g_rcProfNext = RectF(368.0f, 404.0f, 28.0f, 28.0f);
    drawArrowBtn(g_rcProfPrev, true, g_hoveredZone == HitZone::PROFILE_PREV);
    drawArrowBtn(g_rcProfNext, false, g_hoveredZone == HitZone::PROFILE_NEXT);

    std::wstring profLabel;
    if (totalProfiles == 0) {
        profLabel = L"Global (All Apps)";
    } else {
        wchar_t idxBuf[32];
        swprintf_s(idxBuf, L" (%d/%d)", curProfIdx + 1, totalProfiles);
        profLabel = uiProf.exeName + idxBuf;
    }
    RectF rcProfBox(172.0f, 404.0f, 192.0f, 28.0f);
    GraphicsPath pProfBox; AddRoundedRectPath(pProfBox, rcProfBox, 6.0f);
    SolidBrush profBoxBg(Color(255, 15, 23, 42));
    g.FillPath(&profBoxBg, &pProfBox);
    g.DrawString(profLabel.c_str(), -1, g_pFontBadge, rcProfBox, &centerFmt, totalProfiles == 0 ? &amberTextBrush : &emeraldTextBrush);

    g_rcBtnRemoveExe = RectF(404.0f, 404.0f, 80.0f, 28.0f);
    drawSmallBtn(g_rcBtnRemoveExe, totalProfiles > 0 ? L"Remove" : L"Reset", g_hoveredZone == HitZone::BTN_REMOVE_EXE);

    g.DrawLine(&divPen, 36.0f, 441.0f, width - 36.0f, 441.0f);

    // 5 Ô gán phím tắt độc lập cho Profile đang chọn
    auto drawHotkeySlot = [&](const RectF& rc, const wchar_t* title, const std::wstring& keyStr, bool isWaiting, bool isHovered, Color accentCol) {
        GraphicsPath p; AddRoundedRectPath(p, rc, 7.0f);
        SolidBrush bg(isWaiting ? Color(65, 16, 185, 129) : (isHovered ? Color(255, 51, 65, 85) : Color(255, 15, 23, 42)));
        Pen border(isWaiting ? Color(255, 52, 211, 153) : Color(160, accentCol.GetR(), accentCol.GetG(), accentCol.GetB()), 1.1f);
        g.FillPath(&bg, &p);
        g.DrawPath(&border, &p);

        RectF rcTop(rc.X, rc.Y + 3.0f, rc.Width, 16.0f);
        RectF rcBot(rc.X, rc.Y + 19.0f, rc.Width, 22.0f);
        g.DrawString(title, -1, g_pFontSub, rcTop, &centerFmt, &mutedBrush);
        SolidBrush keyBrush(isWaiting ? Color(255, 52, 211, 153) : accentCol);
        g.DrawString(isWaiting ? L"Press..." : keyStr.c_str(), -1, g_pFontBadge, rcBot, &centerFmt, &keyBrush);
    };

    REAL slotY = 450.0f, slotW = 84.8f, slotGap = 6.0f, startX = 36.0f;
    g_rcBindMod = RectF(startX + (slotW + slotGap) * 0, slotY, slotW, 46.0f);
    g_rcBindZ   = RectF(startX + (slotW + slotGap) * 1, slotY, slotW, 46.0f);
    g_rcBindX   = RectF(startX + (slotW + slotGap) * 2, slotY, slotW, 46.0f);
    g_rcBindC   = RectF(startX + (slotW + slotGap) * 3, slotY, slotW, 46.0f);
    g_rcBindE   = RectF(startX + (slotW + slotGap) * 4, slotY, slotW, 46.0f);

    drawHotkeySlot(g_rcBindMod, L"Hold Mod", modName, g_rebindTarget == RebindTarget::PROF_MODIFIER, g_hoveredZone == HitZone::BIND_MOD,     Color(255, 248, 250, 252));
    drawHotkeySlot(g_rcBindZ,   L"2-Pt Line", zName,  g_rebindTarget == RebindTarget::PROF_RULER_Z,  g_hoveredZone == HitZone::BIND_RULER_Z, Color(255, 244, 114, 182));
    drawHotkeySlot(g_rcBindX,   L"Radial",    xName,  g_rebindTarget == RebindTarget::PROF_RULER_X,  g_hoveredZone == HitZone::BIND_RULER_X, Color(255, 52, 211, 153));
    drawHotkeySlot(g_rcBindC,   L"Circle",    cName,  g_rebindTarget == RebindTarget::PROF_RULER_C,  g_hoveredZone == HitZone::BIND_RULER_C, Color(255, 251, 191, 36));
    drawHotkeySlot(g_rcBindE,   L"Ellipse",   eName,  g_rebindTarget == RebindTarget::PROF_RULER_E,  g_hoveredZone == HitZone::BIND_RULER_E, Color(255, 56, 189, 248));

    // Card 4: System Options
    RectF rcCard4(20.0f, 522.0f, width - 40.0f, 170.0f); DrawCard(g, rcCard4);
    
    g.DrawString(L"Run at Windows Startup", -1, g_pFontCardTitle, RectF(36.0f, 534.0f, 330.0f, 22.0f), &leftNoWrapFmt, &whiteBrush);
    g.DrawString(L"Automatically run in the System Tray on boot", -1, g_pFontSmall, RectF(36.0f, 556.0f, 345.0f, 20.0f), &leftNoWrapFmt, &mutedBrush);
    g_rcToggleStartup = RectF(width - 88.0f, 542.0f, 50.0f, 26.0f); DrawToggleSwitch(g, g_rcToggleStartup, g_runOnStartup, g_hoveredZone == HitZone::TOGGLE_STARTUP);
    g.DrawLine(&divPen, 36.0f, 581.0f, width - 36.0f, 581.0f);
    
    g.DrawString(L"Close to System Tray", -1, g_pFontCardTitle, RectF(36.0f, 591.0f, 330.0f, 22.0f), &leftNoWrapFmt, &whiteBrush);
    g.DrawString(L"Keep running in background when closing the window", -1, g_pFontSmall, RectF(36.0f, 613.0f, 345.0f, 20.0f), &leftNoWrapFmt, &mutedBrush);
    g_rcToggleCloseTray = RectF(width - 88.0f, 599.0f, 50.0f, 26.0f); DrawToggleSwitch(g, g_rcToggleCloseTray, g_closeToTray, g_hoveredZone == HitZone::TOGGLE_CLOSE_TO_TRAY);
    g.DrawLine(&divPen, 36.0f, 638.0f, width - 36.0f, 638.0f);

    g.DrawString(L"Always Run as Admin", -1, g_pFontCardTitle, RectF(36.0f, 648.0f, 330.0f, 22.0f), &leftNoWrapFmt, &whiteBrush);
    g.DrawString(L"Auto-request Admin privileges upon launching the software", -1, g_pFontSmall, RectF(36.0f, 670.0f, 345.0f, 20.0f), &leftNoWrapFmt, &mutedBrush);
    g_rcToggleAlwaysAdmin = RectF(width - 88.0f, 656.0f, 50.0f, 26.0f); DrawToggleSwitch(g, g_rcToggleAlwaysAdmin, g_alwaysRunAsAdmin, g_hoveredZone == HitZone::TOGGLE_ALWAYS_ADMIN);

    // Tip Card
    RectF rcTipCard(20.0f, 704.0f, width - 40.0f, 42.0f); DrawCard(g, rcTipCard, 10.0f);
    g.DrawString(L"\u2726 Tip: Each added .exe has its own hotkey profile! Switch appsautomatically loads its keys.", -1, g_pFontSmall, RectF(32.0f, 707.0f, width - 64.0f, 36.0f), &leftNoWrapFmt, &mutedBrush);

    // Footer
    REAL btnY = 762.0f, btnW = (width - 40.0f - 14.0f) / 2.0f;
    g_rcBtnHideTray = RectF(20.0f, btnY, btnW, 42.0f); g_rcBtnExitApp = RectF(20.0f + btnW + 14.0f, btnY, btnW, 42.0f);

    {
        GraphicsPath p; AddRoundedRectPath(p, g_rcBtnHideTray, 10.0f); bool hov = (g_hoveredZone == HitZone::BTN_HIDE_TRAY);
        SolidBrush bg(hov ? Color(255, 51, 65, 85) : Color(255, 30, 41, 59)); Pen border(Color(255, 71, 85, 105), 1.0f);
        g.FillPath(&bg, &p); g.DrawPath(&border, &p);
        g.DrawString(L"Hide to System Tray", -1, g_pFontCardTitle, g_rcBtnHideTray, &centerFmt, &whiteBrush);
    }
    {
        GraphicsPath p; AddRoundedRectPath(p, g_rcBtnExitApp, 10.0f); bool hov = (g_hoveredZone == HitZone::BTN_EXIT_APP);
        SolidBrush bg(hov ? Color(255, 220, 38, 38) : Color(45, 239, 68, 68)); Pen border(Color(180, 239, 68, 68), 1.0f);
        SolidBrush txtBrush(hov ? Color(255, 255, 255, 255) : Color(255, 252, 165, 165));
        g.FillPath(&bg, &p); g.DrawPath(&border, &p);
        g.DrawString(L"Exit Completely", -1, g_pFontCardTitle, g_rcBtnExitApp, &centerFmt, &txtBrush);
    }
    Graphics screenG(hdc); screenG.DrawImage(&backBuffer, 0, 0);
}

HitZone HitTestUI(HWND hwnd, POINT pt) {
    RECT rcClient; GetClientRect(hwnd, &rcClient);
    int clientW = rcClient.right - rcClient.left;
    int clientH = rcClient.bottom - rcClient.top;
    if (clientW <= 0 || clientH <= 0) return HitZone::NONE;

    REAL scaleX = static_cast<REAL>(clientW) / static_cast<REAL>(LOGICAL_WIDTH);
    REAL scaleY = static_cast<REAL>(clientH) / static_cast<REAL>(LOGICAL_HEIGHT);
    PointF p(static_cast<REAL>(pt.x) / scaleX, static_cast<REAL>(pt.y) / scaleY);

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
    if (g_rcBtnRemoveExe.Contains(p)) return HitZone::BTN_REMOVE_EXE;
    if (g_rcBtnChangeHotkey.Contains(p)) return HitZone::BTN_CHANGE_HOTKEY;
    if (g_rcProfPrev.Contains(p)) return HitZone::PROFILE_PREV;
    if (g_rcProfNext.Contains(p)) return HitZone::PROFILE_NEXT;
    if (g_rcBindMod.Contains(p)) return HitZone::BIND_MOD;
    if (g_rcBindZ.Contains(p)) return HitZone::BIND_RULER_Z;
    if (g_rcBindX.Contains(p)) return HitZone::BIND_RULER_X;
    if (g_rcBindC.Contains(p)) return HitZone::BIND_RULER_C;
    if (g_rcBindE.Contains(p)) return HitZone::BIND_RULER_E;
    return HitZone::NONE;
}

void ToggleToolActiveState(HWND hwnd) {
    g_toolEnabled = !g_toolEnabled;
    if (!g_toolEnabled) {
        g_zKeyPhysicalDown = g_xKeyPhysicalDown = g_cKeyPhysicalDown = g_eKeyPhysicalDown = false;
        UnlockAxisState();
        ResetRulerState();
    }
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
        EnterCriticalSection(&g_exeListLock);
        for (size_t i = 0; i < g_appProfiles.size(); ++i) {
            std::wstring lowerTarget = g_appProfiles[i].exeName, lowerExe = exeName;
            std::transform(lowerTarget.begin(), lowerTarget.end(), lowerTarget.begin(), ::towlower);
            std::transform(lowerExe.begin(), lowerExe.end(), lowerExe.begin(), ::towlower);
            if (lowerTarget == lowerExe) {
                exists = true;
                g_selectedProfileIdx = static_cast<int>(i);
                break;
            }
        }
        if (!exists) {
            AppProfile newProf = g_defaultProfile;
            newProf.exeName = exeName;
            g_appProfiles.push_back(newProf);
            g_selectedProfileIdx = static_cast<int>(g_appProfiles.size()) - 1;
        }
        LeaveCriticalSection(&g_exeListLock);
        SaveUserSettings();
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_wmShowExisting != 0 && msg == g_wmShowExisting) { RestoreMainWindow(hwnd); return 0; }
    switch (msg) {
        case WM_CREATE: {
            BOOL useDarkMode = TRUE; DwmSetWindowAttribute(hwnd, 20, &useDarkMode, sizeof(useDarkMode));
            DWORD cornerPref = 2; DwmSetWindowAttribute(hwnd, 33, &cornerPref, sizeof(cornerPref));
            SetTimer(hwnd, TIMER_ID_WATCHDOG, 50, nullptr);
            return 0;
        }
        case WM_DPICHANGED: {
            UINT newDpi = HIWORD(wParam);
            const RECT* prcNewWindow = reinterpret_cast<const RECT*>(lParam);
            ResizeWindowForDpi(hwnd, newDpi, prcNewWindow);
            return 0;
        }
        case WM_TIMER: {
            if (wParam == TIMER_ID_WATCHDOG) {
                AppProfile prof;
                bool inScope = ResolveActiveWindowProfile(prof);
                bool modHeld = inScope && IsConfiguredKeyPhysicallyDown(prof.vkModifier);

                if (g_rulerState != RulerState::NONE) {
                    bool cancel = !g_toolEnabled || !inScope;
                    if (IsZModeState(g_rulerState)) {
                        if (!IsZPressed() || (!modHeld && g_rulerState != RulerState::Z_ACTIVE)) cancel = true;
                    } else if (IsXModeState(g_rulerState)) {
                        if (!IsXPressed() || !modHeld) cancel = true;
                    } else if (IsCModeState(g_rulerState)) {
                        if (!IsCPressed() || !modHeld) cancel = true;
                    } else if (IsEModeState(g_rulerState)) {
                        if (!IsEPressed() || !modHeld) cancel = true;
                    }
                    if (cancel) {
                        g_isPenDown = false;
                        SyncRulerModeAfterKeyRelease();
                    }
                } else if (g_lockedAxis == AxisLock::HORIZONTAL || g_lockedAxis == AxisLock::VERTICAL) {
                    if (!g_toolEnabled || !modHeld || !IsPrimaryButtonOrTipPhysicallyDown()) {
                        g_isPenDown = false; UnlockAxisState();
                    } else ApplyAxisClipCursor(g_lockedAxis, g_downPt);
                }
            }
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps); RenderModernUI(hwnd, hdc); EndPaint(hwnd, &ps); return 0; }
        case WM_UPDATE_UI: { InvalidateRect(hwnd, nullptr, FALSE); return 0; }
        case WM_UPDATE_OVERLAY: { RenderRulerOverlay(); return 0; }
        case WM_MOUSEMOVE: {
            if (!g_trackingMouse) { TRACKMOUSEEVENT tme = {}; tme.cbSize = sizeof(TRACKMOUSEEVENT); tme.dwFlags = TME_LEAVE; tme.hwndTrack = hwnd; TrackMouseEvent(&tme); g_trackingMouse = true; }
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }; HitZone zone = HitTestUI(hwnd, pt);
            if (zone != g_hoveredZone) { g_hoveredZone = zone; InvalidateRect(hwnd, nullptr, FALSE); }
            SetCursor(LoadCursorW(nullptr, (zone != HitZone::NONE) ? IDC_HAND : IDC_ARROW)); return 0;
        }
        case WM_MOUSELEAVE: { g_trackingMouse = false; if (g_hoveredZone != HitZone::NONE) { g_hoveredZone = HitZone::NONE; InvalidateRect(hwnd, nullptr, FALSE); } return 0; }
        case WM_LBUTTONDOWN: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            HitZone zone = HitTestUI(hwnd, pt);
            if (g_rebindTarget != RebindTarget::NONE &&
                zone != HitZone::BTN_CHANGE_HOTKEY && zone != HitZone::BIND_MOD &&
                zone != HitZone::BIND_RULER_Z && zone != HitZone::BIND_RULER_X &&
                zone != HitZone::BIND_RULER_C && zone != HitZone::BIND_RULER_E) {
                g_rebindTarget = RebindTarget::NONE;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            switch (zone) {
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
                case HitZone::BTN_REMOVE_EXE: {
                    EnterCriticalSection(&g_exeListLock);
                    if (!g_appProfiles.empty()) {
                        if (g_selectedProfileIdx >= 0 && g_selectedProfileIdx < static_cast<int>(g_appProfiles.size())) {
                            g_appProfiles.erase(g_appProfiles.begin() + g_selectedProfileIdx);
                        }
                        if (g_selectedProfileIdx >= static_cast<int>(g_appProfiles.size())) {
                            g_selectedProfileIdx = (std::max)(0, static_cast<int>(g_appProfiles.size()) - 1);
                        }
                    } else {
                        g_defaultProfile = { L"Global (All Apps)", VK_SHIFT, 'Z', 'X', 'C', 'E' };
                    }
                    LeaveCriticalSection(&g_exeListLock);
                    SaveUserSettings();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    break;
                }
                case HitZone::PROFILE_PREV: {
                    EnterCriticalSection(&g_exeListLock);
                    if (!g_appProfiles.empty()) {
                        int n = static_cast<int>(g_appProfiles.size());
                        g_selectedProfileIdx = (g_selectedProfileIdx - 1 + n) % n;
                    }
                    LeaveCriticalSection(&g_exeListLock);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    break;
                }
                case HitZone::PROFILE_NEXT: {
                    EnterCriticalSection(&g_exeListLock);
                    if (!g_appProfiles.empty()) {
                        int n = static_cast<int>(g_appProfiles.size());
                        g_selectedProfileIdx = (g_selectedProfileIdx + 1) % n;
                    }
                    LeaveCriticalSection(&g_exeListLock);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    break;
                }
                case HitZone::BTN_CHANGE_HOTKEY: g_rebindTarget = RebindTarget::GLOBAL_TOGGLE; InvalidateRect(hwnd, nullptr, FALSE); break;
                case HitZone::BIND_MOD:          g_rebindTarget = RebindTarget::PROF_MODIFIER; InvalidateRect(hwnd, nullptr, FALSE); break;
                case HitZone::BIND_RULER_Z:      g_rebindTarget = RebindTarget::PROF_RULER_Z;  InvalidateRect(hwnd, nullptr, FALSE); break;
                case HitZone::BIND_RULER_X:      g_rebindTarget = RebindTarget::PROF_RULER_X;  InvalidateRect(hwnd, nullptr, FALSE); break;
                case HitZone::BIND_RULER_C:      g_rebindTarget = RebindTarget::PROF_RULER_C;  InvalidateRect(hwnd, nullptr, FALSE); break;
                case HitZone::BIND_RULER_E:      g_rebindTarget = RebindTarget::PROF_RULER_E;  InvalidateRect(hwnd, nullptr, FALSE); break;
                default: break;
            } return 0;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            if (g_rebindTarget != RebindTarget::NONE) {
                UINT vk = static_cast<UINT>(wParam);
                if (vk == VK_ESCAPE) {
                    g_rebindTarget = RebindTarget::NONE;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }

                if (g_rebindTarget == RebindTarget::GLOBAL_TOGGLE) {
                    if (vk != VK_SHIFT && vk != VK_CONTROL && vk != VK_MENU) {
                        UnregisterHotKey(hwnd, HOTKEY_ID_TOGGLE);
                        g_hotkeyToggle = vk;
                        RegisterHotKey(hwnd, HOTKEY_ID_TOGGLE, 0, g_hotkeyToggle);
                        SaveUserSettings();
                        UpdateTrayIcon();
                        g_rebindTarget = RebindTarget::NONE;
                    }
                } else if (g_rebindTarget == RebindTarget::PROF_MODIFIER) {
                    EnterCriticalSection(&g_exeListLock);
                    GetCurrentlySelectedUIProfile().vkModifier = NormalizeModifierVk(vk);
                    LeaveCriticalSection(&g_exeListLock);
                    SaveUserSettings();
                    g_rebindTarget = RebindTarget::NONE;
                } else {
                    // Không gán nhầm phím giữ (Shift/Ctrl/Alt) vào 4 nút công cụ Z/X/C/E khi người dùng lỡ đè phím
                    UINT norm = NormalizeModifierVk(vk);
                    if (norm != VK_SHIFT && norm != VK_CONTROL && norm != VK_MENU) {
                        EnterCriticalSection(&g_exeListLock);
                        AppProfile& curP = GetCurrentlySelectedUIProfile();
                        if (g_rebindTarget == RebindTarget::PROF_RULER_Z)      curP.vkRuler2Pt = vk;
                        else if (g_rebindTarget == RebindTarget::PROF_RULER_X) curP.vkRadial   = vk;
                        else if (g_rebindTarget == RebindTarget::PROF_RULER_C) curP.vkCircle   = vk;
                        else if (g_rebindTarget == RebindTarget::PROF_RULER_E) curP.vkEllipse  = vk;
                        LeaveCriticalSection(&g_exeListLock);
                        SaveUserSettings();
                        g_rebindTarget = RebindTarget::NONE;
                    }
                }
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
        case WM_DESTROY: {
            KillTimer(hwnd, TIMER_ID_WATCHDOG);
            ReleaseAxisClipCursor();
            if (g_hOverlayWnd) { DestroyWindow(g_hOverlayWnd); g_hOverlayWnd = nullptr; }
            g_overlaySurf.Release();
            UnregisterHotKey(hwnd, HOTKEY_ID_TOGGLE);
            UnregisterHotKey(hwnd, HOTKEY_ID_EXIT);
            RemoveTrayIcon();
            PostQuitMessage(0);
            return 0;
        }
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
    InitializeCriticalSection(&g_exeListLock);
    
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
            DeleteCriticalSection(&g_exeListLock);
            return 0; 
        }
    }

    EnableAdvancedDPIAwareness(); 

    g_wmShowExisting = RegisterWindowMessageW(L"CSPStraightLineHelper_ShowExistingMsg");
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        PostMessageW(HWND_BROADCAST, g_wmShowExisting, 0, 0);
        if (hMutex) CloseHandle(hMutex);
        DeleteCriticalSection(&g_exeListLock);
        return 0;
    }

    GdiplusStartupInput gdiplusStartupInput; ULONG_PTR gdiplusToken = 0; GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);
    InitCachedFonts();
    
    g_hIconActive = CreateAppTrayIcon(true); g_hIconPaused = CreateAppTrayIcon(false);

    // Tự động trích xuất Icon từ app.res (nếu có), nếu không có sẽ dùng g_hIconActive
    HICON hResIcon = nullptr;
    wchar_t exePath[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    hResIcon = ExtractIconW(hInstance, exePath, 0);
    if (!hResIcon || hResIcon == reinterpret_cast<HICON>(1)) hResIcon = g_hIconActive;

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = MainWndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon         = hResIcon;
    wc.hIconSm       = hResIcon;
    wc.lpszClassName = kWindowClassName;
    RegisterClassExW(&wc);

    WNDCLASSEXW wcOverlay = {}; wcOverlay.cbSize = sizeof(WNDCLASSEXW); wcOverlay.lpfnWndProc = OverlayWndProc; wcOverlay.hInstance = hInstance; wcOverlay.lpszClassName = kOverlayClassName; RegisterClassExW(&wcOverlay);

    DWORD dwStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rcWnd = { 0, 0, LOGICAL_WIDTH, LOGICAL_HEIGHT };
    AdjustWindowRect(&rcWnd, dwStyle, FALSE);
    int realW = rcWnd.right - rcWnd.left, realH = rcWnd.bottom - rcWnd.top;
    int posX = (GetSystemMetrics(SM_CXSCREEN) - realW) / 2, posY = (GetSystemMetrics(SM_CYSCREEN) - realH) / 2;

    g_hMainWnd = CreateWindowExW(WS_EX_APPWINDOW, kWindowClassName, kAppTitle, dwStyle, posX, posY, realW, realH, nullptr, nullptr, hInstance, nullptr);
    if (!g_hMainWnd) {
        CleanupCachedFonts();
        GdiplusShutdown(gdiplusToken);
        if (hMutex) CloseHandle(hMutex);
        DeleteCriticalSection(&g_exeListLock);
        return 1;
    }

    ResizeWindowForDpi(g_hMainWnd, QueryWindowDpi(g_hMainWnd));

    g_hOverlayWnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kOverlayClassName, L"StraightCSPRulerOverlay", WS_POPUP,
        0, 0, 1, 1, nullptr, nullptr, hInstance, nullptr);

    HANDLE hHookReadyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_hHookThread = CreateThread(nullptr, 0, HookWorkerThreadProc, hHookReadyEvent, 0, &g_hookThreadId);
    if (hHookReadyEvent) {
        WaitForSingleObject(hHookReadyEvent, 3000);
        CloseHandle(hHookReadyEvent);
    }

    if (!g_mouseHook || !g_keyboardHook) {
        MessageBoxW(g_hMainWnd, L"Hook Error! System denied permission.", kAppTitle, MB_ICONERROR | MB_OK);
        if (g_hookThreadId) PostThreadMessageW(g_hookThreadId, WM_QUIT, 0, 0);
        if (g_hHookThread) { WaitForSingleObject(g_hHookThread, 1000); CloseHandle(g_hHookThread); }
        DestroyWindow(g_hMainWnd);
        CleanupCachedFonts();
        GdiplusShutdown(gdiplusToken);
        if (hMutex) CloseHandle(hMutex);
        DeleteCriticalSection(&g_exeListLock);
        return 1;
    }

    RegisterHotKey(g_hMainWnd, HOTKEY_ID_TOGGLE, 0, g_hotkeyToggle); 
    RegisterHotKey(g_hMainWnd, HOTKEY_ID_EXIT, MOD_ALT, VK_F8);
    InitTrayIcon(g_hMainWnd);

    if (ShouldStartMinimizedInTray()) { g_trayBalloonShown = true; ShowWindow(g_hMainWnd, SW_HIDE); }
    else { ShowWindow(g_hMainWnd, SW_SHOW); UpdateWindow(g_hMainWnd); }

    MSG msg; while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }

    ReleaseAxisClipCursor();
    if (g_hookThreadId) PostThreadMessageW(g_hookThreadId, WM_QUIT, 0, 0);
    if (g_hHookThread) {
        WaitForSingleObject(g_hHookThread, 1500);
        CloseHandle(g_hHookThread);
        g_hHookThread = nullptr;
    }
    if (hResIcon && hResIcon != g_hIconActive) DestroyIcon(hResIcon);
    if (g_hIconActive) DestroyIcon(g_hIconActive);
    if (g_hIconPaused) DestroyIcon(g_hIconPaused);
    CleanupCachedFonts();
    GdiplusShutdown(gdiplusToken);
    if (hMutex) CloseHandle(hMutex);
    DeleteCriticalSection(&g_exeListLock);
    return static_cast<int>(msg.wParam);
}

int main() { HWND hConsole = GetConsoleWindow(); if (hConsole) ShowWindow(hConsole, SW_HIDE); return wWinMain(GetModuleHandleW(nullptr), nullptr, GetCommandLineW(), SW_SHOWDEFAULT); }