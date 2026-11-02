#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <string>

namespace lite {
namespace ui {

// Sent to every child view when the palette or DPI changes.
constexpr UINT kThemeChanged = WM_APP + 3;

struct Theme {
    bool dark = false;
    int dpi = 96;
    int radius = 8;

    COLORREF bg = RGB(244, 245, 247);
    COLORREF surface = RGB(255, 255, 255);
    COLORREF surfaceAlt = RGB(240, 241, 244);
    COLORREF surfaceHover = RGB(236, 238, 242);
    COLORREF border = RGB(225, 228, 232);
    COLORREF borderStrong = RGB(201, 206, 214);
    COLORREF text = RGB(27, 31, 36);
    COLORREF textMuted = RGB(107, 114, 128);
    COLORREF textFaint = RGB(154, 161, 172);
    COLORREF accent = RGB(242, 107, 58);
    COLORREF accentHover = RGB(232, 94, 44);
    COLORREF accentPressed = RGB(217, 83, 31);
    COLORREF accentSoft = RGB(255, 240, 234);
    COLORREF onAccent = RGB(255, 255, 255);
    COLORREF success = RGB(31, 157, 85);
    COLORREF warning = RGB(217, 119, 6);
    COLORREF danger = RGB(220, 38, 38);
    COLORREF info = RGB(37, 99, 235);
    COLORREF codeBg = RGB(251, 251, 252);
    COLORREF selection = RGB(255, 233, 223);
    COLORREF inputBg = RGB(255, 255, 255);

    HFONT fontUi = nullptr;
    HFONT fontUiMedium = nullptr;
    HFONT fontUiBold = nullptr;
    HFONT fontSmall = nullptr;
    HFONT fontSmallBold = nullptr;
    HFONT fontTitle = nullptr;
    HFONT fontMono = nullptr;
    HFONT fontMonoSmall = nullptr;

    int scale(int px) const { return MulDiv(px, dpi, 96); }
};

Theme& theme();

void themeInit(HWND hwnd);
void themeSetDark(bool dark);
void themeReloadForDpi(HWND hwnd);
void themeShutdown();

void themeApplyTitleBar(HWND hwnd, bool dark);
void themeApplyScrollbars(HWND hwnd, bool dark);
bool systemPrefersDark();

// --- Painting helpers -------------------------------------------------------
void fillRect(HDC dc, const RECT& rc, COLORREF color);
void fillRound(HDC dc, const RECT& rc, int radius, COLORREF color);
void strokeRound(HDC dc, const RECT& rc, int radius, COLORREF color, int width = 1);
void drawText(HDC dc, const std::wstring& text, const RECT& rc, HFONT font, COLORREF color, UINT flags);
SIZE measureText(HDC dc, const std::wstring& text, HFONT font);
void drawDot(HDC dc, int cx, int cy, int radius, COLORREF color);
void drawChevron(HDC dc, int cx, int cy, int size, COLORREF color, bool down);
void drawPill(HDC dc, RECT rc, const std::wstring& text, HFONT font, COLORREF fg, COLORREF bg);
void drawLine(HDC dc, int x1, int y1, int x2, int y2, COLORREF color, int width = 1);
HBRUSH themeBrush(COLORREF color);

COLORREF blend(COLORREF a, COLORREF b, double t);
COLORREF methodColor(const std::string& method);
COLORREF statusColor(int code, bool error);

} // namespace ui
} // namespace lite
