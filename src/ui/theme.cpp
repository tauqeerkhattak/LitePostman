#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include "ui/theme.h"

#include <algorithm>
#include <cstdint>
#include <map>

namespace lite {
namespace ui {

namespace {

Theme g_theme;
std::map<COLORREF, HBRUSH> g_brushes;
std::map<std::uint64_t, HPEN> g_pens;

HBRUSH brushFor(COLORREF color) {
    auto it = g_brushes.find(color);
    if (it != g_brushes.end()) return it->second;
    HBRUSH created = CreateSolidBrush(color);
    g_brushes[color] = created;
    return created;
}

HPEN penFor(COLORREF color, int width) {
    const std::uint64_t key = (static_cast<std::uint64_t>(width) << 32) | color;
    auto it = g_pens.find(key);
    if (it != g_pens.end()) return it->second;
    HPEN created = CreatePen(PS_SOLID, width, color);
    g_pens[key] = created;
    return created;
}

void clearCaches() {
    for (auto& entry : g_brushes) DeleteObject(entry.second);
    g_brushes.clear();
    for (auto& entry : g_pens) DeleteObject(entry.second);
    g_pens.clear();
}

HFONT makeFont(int pointSize, int weight, const wchar_t* face, int dpi) {
    return CreateFontW(-MulDiv(pointSize, dpi, 72), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

void destroyFonts() {
    HFONT fonts[] = {g_theme.fontUi, g_theme.fontUiMedium, g_theme.fontUiBold, g_theme.fontSmall,
                     g_theme.fontSmallBold, g_theme.fontTitle, g_theme.fontMono, g_theme.fontMonoSmall};
    for (HFONT font : fonts) {
        if (font) DeleteObject(font);
    }
    g_theme.fontUi = g_theme.fontUiMedium = g_theme.fontUiBold = nullptr;
    g_theme.fontSmall = g_theme.fontSmallBold = g_theme.fontTitle = nullptr;
    g_theme.fontMono = g_theme.fontMonoSmall = nullptr;
}

void createFonts() {
    destroyFonts();
    const int dpi = g_theme.dpi;
    g_theme.fontUi = makeFont(10, FW_NORMAL, L"Segoe UI", dpi);
    g_theme.fontUiMedium = makeFont(10, FW_SEMIBOLD, L"Segoe UI", dpi);
    g_theme.fontUiBold = makeFont(10, FW_BOLD, L"Segoe UI", dpi);
    g_theme.fontSmall = makeFont(9, FW_NORMAL, L"Segoe UI", dpi);
    g_theme.fontSmallBold = makeFont(9, FW_BOLD, L"Segoe UI", dpi);
    g_theme.fontTitle = makeFont(13, FW_SEMIBOLD, L"Segoe UI", dpi);
    g_theme.fontMono = makeFont(10, FW_NORMAL, L"Consolas", dpi);
    g_theme.fontMonoSmall = makeFont(9, FW_NORMAL, L"Consolas", dpi);
}

void applyPalette() {
    Theme& t = g_theme;
    if (t.dark) {
        t.bg = RGB(20, 22, 26);
        t.surface = RGB(27, 30, 36);
        t.surfaceAlt = RGB(34, 38, 46);
        t.surfaceHover = RGB(42, 47, 56);
        t.border = RGB(44, 49, 58);
        t.borderStrong = RGB(58, 64, 75);
        t.text = RGB(231, 233, 237);
        t.textMuted = RGB(155, 163, 175);
        t.textFaint = RGB(107, 114, 128);
        t.accent = RGB(255, 122, 69);
        t.accentHover = RGB(255, 139, 92);
        t.accentPressed = RGB(242, 107, 58);
        t.accentSoft = RGB(58, 36, 27);
        t.onAccent = RGB(255, 255, 255);
        t.success = RGB(52, 211, 153);
        t.warning = RGB(251, 191, 36);
        t.danger = RGB(248, 113, 113);
        t.info = RGB(96, 165, 250);
        t.codeBg = RGB(23, 26, 31);
        t.selection = RGB(58, 42, 34);
        t.inputBg = RGB(31, 35, 42);
    } else {
        t.bg = RGB(244, 245, 247);
        t.surface = RGB(255, 255, 255);
        t.surfaceAlt = RGB(240, 241, 244);
        t.surfaceHover = RGB(236, 238, 242);
        t.border = RGB(225, 228, 232);
        t.borderStrong = RGB(201, 206, 214);
        t.text = RGB(27, 31, 36);
        t.textMuted = RGB(107, 114, 128);
        t.textFaint = RGB(154, 161, 172);
        t.accent = RGB(242, 107, 58);
        t.accentHover = RGB(232, 94, 44);
        t.accentPressed = RGB(217, 83, 31);
        t.accentSoft = RGB(255, 240, 234);
        t.onAccent = RGB(255, 255, 255);
        t.success = RGB(31, 157, 85);
        t.warning = RGB(217, 119, 6);
        t.danger = RGB(220, 38, 38);
        t.info = RGB(37, 99, 235);
        t.codeBg = RGB(251, 251, 252);
        t.selection = RGB(255, 233, 223);
        t.inputBg = RGB(255, 255, 255);
    }
    t.radius = 8;
}

UINT dpiForWindow(HWND hwnd) {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static GetDpiForWindowFn fn = reinterpret_cast<GetDpiForWindowFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
    if (fn && hwnd) {
        const UINT dpi = fn(hwnd);
        if (dpi >= 72) return dpi;
    }
    return 96;
}

} // namespace

Theme& theme() { return g_theme; }

void themeInit(HWND hwnd) {
    g_theme.dpi = static_cast<int>(dpiForWindow(hwnd));
    applyPalette();
    createFonts();
}

void themeSetDark(bool dark) {
    if (g_theme.dark == dark) return;
    g_theme.dark = dark;
    applyPalette();
    clearCaches();
}

void themeReloadForDpi(HWND hwnd) {
    const int dpi = static_cast<int>(dpiForWindow(hwnd));
    if (dpi == g_theme.dpi) return;
    g_theme.dpi = dpi;
    createFonts();
}

void themeShutdown() {
    destroyFonts();
    clearCaches();
}

void themeApplyTitleBar(HWND hwnd, bool dark) {
    if (!hwnd) return;
    using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (!dwm) return;
    auto fn = reinterpret_cast<DwmSetWindowAttributeFn>(
        reinterpret_cast<void*>(GetProcAddress(dwm, "DwmSetWindowAttribute")));
    if (fn) {
        BOOL value = dark ? TRUE : FALSE;
        // 20 = DWMWA_USE_IMMERSIVE_DARK_MODE, 19 = pre-20H1 variant.
        if (FAILED(fn(hwnd, 20, &value, sizeof(value)))) {
            fn(hwnd, 19, &value, sizeof(value));
        }
    }
    FreeLibrary(dwm);

    // The non-client area does not always repaint when the attribute flips.
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    RedrawWindow(hwnd, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);
}

void themeApplyScrollbars(HWND hwnd, bool dark) {
    if (!hwnd) return;
    using SetWindowThemeFn = HRESULT(WINAPI*)(HWND, LPCWSTR, LPCWSTR);
    static SetWindowThemeFn fn = nullptr;
    static bool resolved = false;
    if (!resolved) {
        HMODULE uxtheme = LoadLibraryW(L"uxtheme.dll");
        if (uxtheme) {
            fn = reinterpret_cast<SetWindowThemeFn>(
                reinterpret_cast<void*>(GetProcAddress(uxtheme, "SetWindowTheme")));
        }
        resolved = true;
    }
    if (fn) fn(hwnd, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
}

bool systemPrefersDark() {
    DWORD value = 1;
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS) {
        return value == 0;
    }
    return false;
}

// --- Painting ---------------------------------------------------------------

void fillRect(HDC dc, const RECT& rc, COLORREF color) {
    FillRect(dc, &rc, brushFor(color));
}

void fillRound(HDC dc, const RECT& rc, int radius, COLORREF color) {
    HBRUSH brush = brushFor(color);
    HPEN pen = penFor(color, 1);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, radius * 2, radius * 2);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
}

void strokeRound(HDC dc, const RECT& rc, int radius, COLORREF color, int width) {
    HPEN pen = penFor(color, width);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, radius * 2, radius * 2);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
}

void drawText(HDC dc, const std::wstring& text, const RECT& rc, HFONT font, COLORREF color, UINT flags) {
    if (text.empty()) return;
    HGDIOBJ oldFont = font ? SelectObject(dc, font) : nullptr;
    const COLORREF oldColor = SetTextColor(dc, color);
    const int oldMode = SetBkMode(dc, TRANSPARENT);
    RECT target = rc;
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &target, flags);
    SetBkMode(dc, oldMode);
    SetTextColor(dc, oldColor);
    if (oldFont) SelectObject(dc, oldFont);
}

SIZE measureText(HDC dc, const std::wstring& text, HFONT font) {
    SIZE size = {0, 0};
    HGDIOBJ oldFont = font ? SelectObject(dc, font) : nullptr;
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
    if (oldFont) SelectObject(dc, oldFont);
    return size;
}

void drawDot(HDC dc, int cx, int cy, int radius, COLORREF color) {
    HBRUSH brush = brushFor(color);
    HPEN pen = penFor(color, 1);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    Ellipse(dc, cx - radius, cy - radius, cx + radius + 1, cy + radius + 1);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
}

void drawChevron(HDC dc, int cx, int cy, int size, COLORREF color, bool down) {
    HPEN pen = penFor(color, std::max(1, size / 6));
    HGDIOBJ oldPen = SelectObject(dc, pen);
    POINT points[3];
    const int half = size / 2;
    const int quarter = std::max(2, size / 4);
    if (down) {
        points[0] = {cx - half, cy - quarter};
        points[1] = {cx, cy + quarter};
        points[2] = {cx + half, cy - quarter};
    } else {
        points[0] = {cx - quarter, cy - half};
        points[1] = {cx + quarter, cy};
        points[2] = {cx - quarter, cy + half};
    }
    Polyline(dc, points, 3);
    SelectObject(dc, oldPen);
}

void drawPill(HDC dc, RECT rc, const std::wstring& text, HFONT font, COLORREF fg, COLORREF bg) {
    const int radius = std::max(2, static_cast<int>(rc.bottom - rc.top) / 2);
    fillRound(dc, rc, radius, bg);
    drawText(dc, text, rc, font, fg, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
}

void drawLine(HDC dc, int x1, int y1, int x2, int y2, COLORREF color, int width) {
    HPEN pen = penFor(color, width);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    MoveToEx(dc, x1, y1, nullptr);
    LineTo(dc, x2, y2);
    SelectObject(dc, oldPen);
}

HBRUSH themeBrush(COLORREF color) { return brushFor(color); }

COLORREF blend(COLORREF a, COLORREF b, double t) {
    t = std::max(0.0, std::min(1.0, t));
    const int r = static_cast<int>(GetRValue(a) * (1.0 - t) + GetRValue(b) * t);
    const int g = static_cast<int>(GetGValue(a) * (1.0 - t) + GetGValue(b) * t);
    const int bl = static_cast<int>(GetBValue(a) * (1.0 - t) + GetBValue(b) * t);
    return RGB(r, g, bl);
}

COLORREF methodColor(const std::string& method) {
    const bool dark = g_theme.dark;
    if (method == "GET") return dark ? RGB(74, 222, 128) : RGB(22, 163, 74);
    if (method == "POST") return dark ? RGB(250, 204, 21) : RGB(202, 138, 4);
    if (method == "PUT") return dark ? RGB(96, 165, 250) : RGB(37, 99, 235);
    if (method == "PATCH") return dark ? RGB(192, 132, 252) : RGB(147, 51, 234);
    if (method == "DELETE") return dark ? RGB(248, 113, 113) : RGB(220, 38, 38);
    if (method == "HEAD") return dark ? RGB(148, 163, 184) : RGB(100, 116, 139);
    if (method == "OPTIONS") return dark ? RGB(34, 211, 238) : RGB(8, 145, 178);
    return g_theme.textMuted;
}

COLORREF statusColor(int code, bool error) {
    if (error) return g_theme.danger;
    if (code <= 0) return g_theme.textMuted;
    if (code < 300) return g_theme.success;
    if (code < 400) return g_theme.info;
    if (code < 500) return g_theme.warning;
    return g_theme.danger;
}

} // namespace ui
} // namespace lite
