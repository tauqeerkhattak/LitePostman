#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include "ui/theme.h"

#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>

namespace lite {
namespace ui {

namespace {

Theme g_theme;
std::map<COLORREF, HBRUSH> g_brushes;
std::map<std::uint64_t, HPEN> g_pens;
ULONG_PTR g_gdiplusToken = 0;

Gdiplus::Color toGp(COLORREF color, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color));
}

void buildRoundRectPath(Gdiplus::GraphicsPath& path, Gdiplus::REAL x, Gdiplus::REAL y,
                        Gdiplus::REAL w, Gdiplus::REAL h, Gdiplus::REAL r) {
    if (w <= 0.0f || h <= 0.0f) return;
    r = std::max(0.0f, std::min(r, std::min(w, h) * 0.5f));
    if (r < 0.5f) {
        path.AddRectangle(Gdiplus::RectF(x, y, w, h));
        return;
    }
    const Gdiplus::REAL d = r * 2.0f;
    path.AddArc(x, y, d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

int CALLBACK fontEnumProc(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM lParam) {
    *reinterpret_cast<bool*>(lParam) = true;
    return 0;
}

bool fontExists(const wchar_t* face) {
    HDC dc = GetDC(nullptr);
    if (!dc) return false;
    LOGFONTW lf = {};
    lf.lfCharSet = DEFAULT_CHARSET;
    lstrcpynW(lf.lfFaceName, face, LF_FACESIZE);
    bool found = false;
    EnumFontFamiliesExW(dc, &lf, fontEnumProc, reinterpret_cast<LPARAM>(&found), 0);
    ReleaseDC(nullptr, dc);
    return found;
}

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
    if (fontExists(L"Segoe UI Variable Text")) {
        g_theme.uiFontName = L"Segoe UI Variable Text";
    } else {
        g_theme.uiFontName = L"Segoe UI";
    }
    if (fontExists(L"Cascadia Mono")) {
        g_theme.monoFontName = L"Cascadia Mono";
    } else {
        g_theme.monoFontName = L"Consolas";
    }

    const wchar_t* ui = g_theme.uiFontName.c_str();
    const wchar_t* mono = g_theme.monoFontName.c_str();
    g_theme.fontUi = makeFont(10, FW_NORMAL, ui, dpi);
    g_theme.fontUiMedium = makeFont(10, FW_SEMIBOLD, ui, dpi);
    g_theme.fontUiBold = makeFont(10, FW_BOLD, ui, dpi);
    g_theme.fontSmall = makeFont(9, FW_NORMAL, ui, dpi);
    g_theme.fontSmallBold = makeFont(9, FW_BOLD, ui, dpi);
    g_theme.fontTitle = makeFont(13, FW_SEMIBOLD, ui, dpi);
    g_theme.fontMono = makeFont(10, FW_NORMAL, mono, dpi);
    g_theme.fontMonoSmall = makeFont(9, FW_NORMAL, mono, dpi);
}

void applyPalette() {
    Theme& t = g_theme;
    if (t.dark) {
        t.bg = RGB(15, 17, 21);
        t.surface = RGB(22, 25, 32);
        t.surfaceAlt = RGB(30, 34, 43);
        t.surfaceHover = RGB(38, 43, 54);
        t.border = RGB(38, 43, 54);
        t.borderStrong = RGB(56, 63, 79);
        t.text = RGB(235, 238, 244);
        t.textMuted = RGB(152, 161, 178);
        t.textFaint = RGB(102, 111, 128);
        t.accent = RGB(255, 108, 55);
        t.accentHover = RGB(255, 129, 82);
        t.accentPressed = RGB(236, 92, 42);
        t.accentSoft = RGB(54, 31, 24);
        t.onAccent = RGB(255, 255, 255);
        t.success = RGB(52, 211, 153);
        t.warning = RGB(251, 191, 36);
        t.danger = RGB(248, 113, 113);
        t.info = RGB(96, 165, 250);
        t.codeBg = RGB(18, 20, 26);
        t.selection = RGB(64, 41, 31);
        t.inputBg = RGB(17, 19, 24);
    } else {
        t.bg = RGB(244, 246, 249);
        t.surface = RGB(255, 255, 255);
        t.surfaceAlt = RGB(238, 241, 246);
        t.surfaceHover = RGB(230, 234, 241);
        t.border = RGB(224, 228, 236);
        t.borderStrong = RGB(194, 201, 214);
        t.text = RGB(20, 24, 31);
        t.textMuted = RGB(96, 105, 120);
        t.textFaint = RGB(144, 152, 166);
        t.accent = RGB(242, 98, 46);
        t.accentHover = RGB(230, 84, 34);
        t.accentPressed = RGB(209, 72, 26);
        t.accentSoft = RGB(255, 239, 232);
        t.onAccent = RGB(255, 255, 255);
        t.success = RGB(22, 163, 74);
        t.warning = RGB(217, 119, 6);
        t.danger = RGB(220, 38, 38);
        t.info = RGB(37, 99, 235);
        t.codeBg = RGB(250, 251, 253);
        t.selection = RGB(255, 232, 220);
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

DoubleBufferDC::DoubleBufferDC(HWND hwnd) : hwnd_(hwnd) {
    paintDc_ = BeginPaint(hwnd_, &ps_);
    GetClientRect(hwnd_, &rc_);
    const int w = std::max(1, static_cast<int>(rc_.right - rc_.left));
    const int h = std::max(1, static_cast<int>(rc_.bottom - rc_.top));
    memDc_ = CreateCompatibleDC(paintDc_);
    if (memDc_) {
        bitmap_ = CreateCompatibleBitmap(paintDc_, w, h);
        if (bitmap_) {
            oldBitmap_ = SelectObject(memDc_, bitmap_);
        } else {
            DeleteDC(memDc_);
            memDc_ = nullptr;
        }
    }
}

DoubleBufferDC::~DoubleBufferDC() {
    if (memDc_) {
        const int w = std::max(1, static_cast<int>(rc_.right - rc_.left));
        const int h = std::max(1, static_cast<int>(rc_.bottom - rc_.top));
        BitBlt(paintDc_, 0, 0, w, h, memDc_, 0, 0, SRCCOPY);
        if (oldBitmap_) SelectObject(memDc_, oldBitmap_);
        if (bitmap_) DeleteObject(bitmap_);
        DeleteDC(memDc_);
    }
    EndPaint(hwnd_, &ps_);
}

Theme& theme() { return g_theme; }

void themeInit(HWND hwnd) {
    if (g_gdiplusToken == 0) {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&g_gdiplusToken, &input, nullptr);
    }
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
    if (g_gdiplusToken != 0) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
    }
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
        // 33 = DWMWA_WINDOW_CORNER_PREFERENCE (DWMWCP_ROUND = 2) on Windows 11
        DWORD cornerPref = 2;
        fn(hwnd, 33, &cornerPref, sizeof(cornerPref));
        // 35 = DWMWA_CAPTION_COLOR on Windows 11 - match top bar surface
        COLORREF captionColor = g_theme.surface;
        fn(hwnd, 35, &captionColor, sizeof(captionColor));
        COLORREF borderColor = g_theme.border;
        fn(hwnd, 34, &borderColor, sizeof(borderColor));
    }
    FreeLibrary(dwm);

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

// --- Painting (GDI+ Anti-Aliased) -------------------------------------------

void fillRect(HDC dc, const RECT& rc, COLORREF color) {
    FillRect(dc, &rc, brushFor(color));
}

void fillRound(HDC dc, const RECT& rc, int radius, COLORREF color) {
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    Gdiplus::GraphicsPath path;
    buildRoundRectPath(path, static_cast<Gdiplus::REAL>(rc.left), static_cast<Gdiplus::REAL>(rc.top),
                       static_cast<Gdiplus::REAL>(w), static_cast<Gdiplus::REAL>(h),
                       static_cast<Gdiplus::REAL>(radius));
    Gdiplus::SolidBrush brush(toGp(color));
    g.FillPath(&brush, &path);
}

void strokeRound(HDC dc, const RECT& rc, int radius, COLORREF color, int width) {
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    const Gdiplus::REAL fw = static_cast<Gdiplus::REAL>(std::max(1, width));
    const Gdiplus::REAL inset = fw * 0.5f;
    Gdiplus::GraphicsPath path;
    buildRoundRectPath(path, static_cast<Gdiplus::REAL>(rc.left) + inset,
                       static_cast<Gdiplus::REAL>(rc.top) + inset,
                       std::max(1.0f, static_cast<Gdiplus::REAL>(w) - fw),
                       std::max(1.0f, static_cast<Gdiplus::REAL>(h) - fw),
                       static_cast<Gdiplus::REAL>(radius));
    Gdiplus::Pen pen(toGp(color), fw);
    g.DrawPath(&pen, &path);
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
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::SolidBrush brush(toGp(color));
    const Gdiplus::REAL r = static_cast<Gdiplus::REAL>(radius);
    g.FillEllipse(&brush, static_cast<Gdiplus::REAL>(cx) - r,
                  static_cast<Gdiplus::REAL>(cy) - r, r * 2.0f, r * 2.0f);
}

void drawChevron(HDC dc, int cx, int cy, int size, COLORREF color, bool down) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const Gdiplus::REAL thickness = std::max(1.4f, static_cast<Gdiplus::REAL>(size) / 5.5f);
    Gdiplus::Pen pen(toGp(color), thickness);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);

    const Gdiplus::REAL half = static_cast<Gdiplus::REAL>(size) * 0.45f;
    const Gdiplus::REAL quarter = std::max(2.0f, static_cast<Gdiplus::REAL>(size) * 0.24f);
    Gdiplus::PointF pts[3];
    if (down) {
        pts[0] = Gdiplus::PointF(cx - half, cy - quarter);
        pts[1] = Gdiplus::PointF(static_cast<Gdiplus::REAL>(cx), cy + quarter);
        pts[2] = Gdiplus::PointF(cx + half, cy - quarter);
    } else {
        pts[0] = Gdiplus::PointF(cx - quarter, cy - half);
        pts[1] = Gdiplus::PointF(cx + quarter, static_cast<Gdiplus::REAL>(cy));
        pts[2] = Gdiplus::PointF(cx - quarter, cy + half);
    }
    g.DrawLines(&pen, pts, 3);
}

void drawPill(HDC dc, RECT rc, const std::wstring& text, HFONT font, COLORREF fg, COLORREF bg) {
    const int radius = std::max(2, static_cast<int>(rc.bottom - rc.top) / 2);
    fillRound(dc, rc, radius, bg);
    drawText(dc, text, rc, font, fg, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
}

void drawLine(HDC dc, int x1, int y1, int x2, int y2, COLORREF color, int width) {
    if (width <= 1 && (x1 == x2 || y1 == y2)) {
        HPEN pen = penFor(color, 1);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        MoveToEx(dc, x1, y1, nullptr);
        LineTo(dc, x2, y2);
        SelectObject(dc, oldPen);
        return;
    }
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::Pen pen(toGp(color), static_cast<Gdiplus::REAL>(std::max(1, width)));
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    g.DrawLine(&pen, static_cast<Gdiplus::REAL>(x1), static_cast<Gdiplus::REAL>(y1),
               static_cast<Gdiplus::REAL>(x2), static_cast<Gdiplus::REAL>(y2));
}

void drawFolderIcon(HDC dc, RECT rc, COLORREF stroke, COLORREF fill, bool open) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const Gdiplus::REAL x = static_cast<Gdiplus::REAL>(rc.left);
    const Gdiplus::REAL y = static_cast<Gdiplus::REAL>(rc.top);
    const Gdiplus::REAL w = static_cast<Gdiplus::REAL>(rc.right - rc.left);
    const Gdiplus::REAL h = static_cast<Gdiplus::REAL>(rc.bottom - rc.top);

    Gdiplus::GraphicsPath path;
    Gdiplus::PointF pts[6] = {
        Gdiplus::PointF(x, y + h * 0.15f),
        Gdiplus::PointF(x + w * 0.38f, y + h * 0.15f),
        Gdiplus::PointF(x + w * 0.50f, y + h * 0.30f),
        Gdiplus::PointF(x + w, y + h * 0.30f),
        Gdiplus::PointF(x + w, y + h),
        Gdiplus::PointF(x, y + h)
    };
    path.AddPolygon(pts, 6);
    Gdiplus::SolidBrush brush(toGp(fill, open ? 200 : 140));
    Gdiplus::Pen pen(toGp(stroke), 1.3f);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    g.FillPath(&brush, &path);
    g.DrawPath(&pen, &path);
}

void drawCollectionIcon(HDC dc, RECT rc, COLORREF color, bool active) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const Gdiplus::REAL x = static_cast<Gdiplus::REAL>(rc.left);
    const Gdiplus::REAL y = static_cast<Gdiplus::REAL>(rc.top);
    const Gdiplus::REAL w = static_cast<Gdiplus::REAL>(rc.right - rc.left);
    const Gdiplus::REAL h = static_cast<Gdiplus::REAL>(rc.bottom - rc.top);

    Gdiplus::GraphicsPath outer;
    buildRoundRectPath(outer, x, y, w, h, 3.0f);
    Gdiplus::SolidBrush fill(toGp(color, active ? 235 : 42));
    Gdiplus::Pen pen(toGp(color), 1.3f);
    g.FillPath(&fill, &outer);
    g.DrawPath(&pen, &outer);

    const COLORREF lineCol = active ? g_theme.onAccent : color;
    Gdiplus::Pen innerPen(toGp(lineCol), 1.3f);
    innerPen.SetStartCap(Gdiplus::LineCapRound);
    innerPen.SetEndCap(Gdiplus::LineCapRound);
    g.DrawLine(&innerPen, x + w * 0.28f, y + h * 0.38f, x + w * 0.72f, y + h * 0.38f);
    g.DrawLine(&innerPen, x + w * 0.28f, y + h * 0.64f, x + w * 0.58f, y + h * 0.64f);
}

void drawSearchIcon(HDC dc, int cx, int cy, int size, COLORREF color) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const Gdiplus::REAL s = static_cast<Gdiplus::REAL>(size);
    const Gdiplus::REAL r = s * 0.34f;
    const Gdiplus::REAL gx = static_cast<Gdiplus::REAL>(cx) - s * 0.08f;
    const Gdiplus::REAL gy = static_cast<Gdiplus::REAL>(cy) - s * 0.08f;
    Gdiplus::Pen pen(toGp(color), std::max(1.4f, s * 0.13f));
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    g.DrawEllipse(&pen, gx - r, gy - r, r * 2.0f, r * 2.0f);
    g.DrawLine(&pen, gx + r * 0.72f, gy + r * 0.72f,
               static_cast<Gdiplus::REAL>(cx) + s * 0.42f,
               static_cast<Gdiplus::REAL>(cy) + s * 0.42f);
}

void drawSunMoonIcon(HDC dc, int cx, int cy, int size, COLORREF color, bool dark) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const Gdiplus::REAL s = static_cast<Gdiplus::REAL>(size);
    if (dark) {
        // Draw crisp sun icon (switch to light)
        const Gdiplus::REAL r = s * 0.26f;
        Gdiplus::Pen pen(toGp(color), 1.5f);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        g.DrawEllipse(&pen, cx - r, cy - r, r * 2.0f, r * 2.0f);
        for (int k = 0; k < 8; ++k) {
            const double angle = k * 3.14159265358979323846 / 4.0;
            const Gdiplus::REAL cs = static_cast<Gdiplus::REAL>(std::cos(angle));
            const Gdiplus::REAL sn = static_cast<Gdiplus::REAL>(std::sin(angle));
            g.DrawLine(&pen, cx + cs * (s * 0.40f), cy + sn * (s * 0.40f),
                       cx + cs * (s * 0.50f), cy + sn * (s * 0.50f));
        }
    } else {
        // Draw crisp crescent moon (switch to dark)
        const Gdiplus::REAL r = s * 0.36f;
        Gdiplus::GraphicsPath path;
        path.AddArc(cx - r, cy - r, r * 2.0f, r * 2.0f, 30.0f, 250.0f);
        Gdiplus::Pen pen(toGp(color), 1.6f);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        g.DrawPath(&pen, &path);
        const Gdiplus::REAL ir = r * 0.72f;
        g.DrawArc(&pen, cx - r * 0.1f, cy - r * 1.05f, ir * 2.0f, ir * 2.0f, 95.0f, 155.0f);
    }
}

void drawSendIcon(HDC dc, int cx, int cy, int size, COLORREF color) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const Gdiplus::REAL s = static_cast<Gdiplus::REAL>(size) * 0.48f;
    Gdiplus::PointF pts[4] = {
        Gdiplus::PointF(cx - s, cy - s * 0.75f),
        Gdiplus::PointF(cx + s, static_cast<Gdiplus::REAL>(cy)),
        Gdiplus::PointF(cx - s, cy + s * 0.75f),
        Gdiplus::PointF(cx - s * 0.45f, static_cast<Gdiplus::REAL>(cy))
    };
    Gdiplus::SolidBrush brush(toGp(color));
    g.FillPolygon(&brush, pts, 4);
}

void drawPlusIcon(HDC dc, int cx, int cy, int arm, COLORREF color) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::Pen pen(toGp(color), 1.6f);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    const Gdiplus::REAL a = static_cast<Gdiplus::REAL>(arm);
    g.DrawLine(&pen, cx - a, static_cast<Gdiplus::REAL>(cy), cx + a, static_cast<Gdiplus::REAL>(cy));
    g.DrawLine(&pen, static_cast<Gdiplus::REAL>(cx), cy - a, static_cast<Gdiplus::REAL>(cx), cy + a);
}

void drawCloseIcon(HDC dc, int cx, int cy, int arm, COLORREF color) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::Pen pen(toGp(color), 1.5f);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    const Gdiplus::REAL a = static_cast<Gdiplus::REAL>(arm);
    g.DrawLine(&pen, cx - a, cy - a, cx + a, cy + a);
    g.DrawLine(&pen, cx + a, cy - a, cx - a, cy + a);
}

void drawLogoIcon(HDC dc, RECT rc, COLORREF accent, COLORREF fg) {
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    Gdiplus::GraphicsPath bgPath;
    buildRoundRectPath(bgPath, static_cast<Gdiplus::REAL>(rc.left), static_cast<Gdiplus::REAL>(rc.top),
                       static_cast<Gdiplus::REAL>(w), static_cast<Gdiplus::REAL>(h),
                       static_cast<Gdiplus::REAL>(w) * 0.28f);
    Gdiplus::LinearGradientBrush grad(
        Gdiplus::PointF(static_cast<Gdiplus::REAL>(rc.left), static_cast<Gdiplus::REAL>(rc.top)),
        Gdiplus::PointF(static_cast<Gdiplus::REAL>(rc.right), static_cast<Gdiplus::REAL>(rc.bottom)),
        toGp(blend(accent, RGB(255, 255, 255), 0.14)),
        toGp(blend(accent, RGB(0, 0, 0), 0.12)));
    g.FillPath(&grad, &bgPath);

    // Crisp lightning / paper-plane bolt mark inside badge
    const Gdiplus::REAL cx = rc.left + w * 0.5f;
    const Gdiplus::REAL cy = rc.top + h * 0.5f;
    const Gdiplus::REAL s = w * 0.26f;
    Gdiplus::PointF bolt[6] = {
        Gdiplus::PointF(cx + s * 0.2f, cy - s * 1.05f),
        Gdiplus::PointF(cx - s * 0.78f, cy + s * 0.08f),
        Gdiplus::PointF(cx - s * 0.05f, cy + s * 0.08f),
        Gdiplus::PointF(cx - s * 0.22f, cy + s * 1.05f),
        Gdiplus::PointF(cx + s * 0.78f, cy - s * 0.08f),
        Gdiplus::PointF(cx + s * 0.05f, cy - s * 0.08f)
    };
    Gdiplus::SolidBrush fgBrush(toGp(fg));
    g.FillPolygon(&fgBrush, bolt, 6);
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
