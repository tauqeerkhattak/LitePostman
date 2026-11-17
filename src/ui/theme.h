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

    COLORREF bg = RGB(245, 246, 248);
    COLORREF surface = RGB(255, 255, 255);
    COLORREF surfaceAlt = RGB(238, 240, 244);
    COLORREF surfaceHover = RGB(232, 235, 240);
    COLORREF border = RGB(224, 227, 234);
    COLORREF borderStrong = RGB(196, 202, 212);
    COLORREF text = RGB(22, 25, 31);
    COLORREF textMuted = RGB(100, 108, 122);
    COLORREF textFaint = RGB(148, 156, 170);
    COLORREF accent = RGB(255, 108, 55);
    COLORREF accentHover = RGB(244, 96, 42);
    COLORREF accentPressed = RGB(222, 82, 32);
    COLORREF accentSoft = RGB(255, 239, 232);
    COLORREF onAccent = RGB(255, 255, 255);
    COLORREF success = RGB(22, 163, 74);
    COLORREF warning = RGB(217, 119, 6);
    COLORREF danger = RGB(220, 38, 38);
    COLORREF info = RGB(37, 99, 235);
    COLORREF codeBg = RGB(250, 251, 253);
    COLORREF selection = RGB(255, 232, 220);
    COLORREF inputBg = RGB(255, 255, 255);

    HFONT fontUi = nullptr;
    HFONT fontUiMedium = nullptr;
    HFONT fontUiBold = nullptr;
    HFONT fontSmall = nullptr;
    HFONT fontSmallBold = nullptr;
    HFONT fontTitle = nullptr;
    HFONT fontMono = nullptr;
    HFONT fontMonoSmall = nullptr;

    std::wstring uiFontName = L"Segoe UI";
    std::wstring monoFontName = L"Consolas";

    int scale(int px) const { return MulDiv(px, dpi, 96); }
};

class DoubleBufferDC {
public:
    explicit DoubleBufferDC(HWND hwnd);
    ~DoubleBufferDC();

    DoubleBufferDC(const DoubleBufferDC&) = delete;
    DoubleBufferDC& operator=(const DoubleBufferDC&) = delete;

    HDC dc() const { return memDc_ ? memDc_ : paintDc_; }
    const RECT& rect() const { return rc_; }

private:
    HWND hwnd_ = nullptr;
    PAINTSTRUCT ps_ = {};
    HDC paintDc_ = nullptr;
    HDC memDc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ oldBitmap_ = nullptr;
    RECT rc_ = {};
};

Theme& theme();

void themeInit(HWND hwnd);
void themeSetDark(bool dark);
void themeReloadForDpi(HWND hwnd);
void themeShutdown();

void themeApplyTitleBar(HWND hwnd, bool dark);
void themeApplyScrollbars(HWND hwnd, bool dark);
bool systemPrefersDark();

// --- Painting helpers (GDI+ anti-aliased) -----------------------------------
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

// --- Crisp vector icons -----------------------------------------------------
void drawFolderIcon(HDC dc, RECT rc, COLORREF stroke, COLORREF fill, bool open);
void drawCollectionIcon(HDC dc, RECT rc, COLORREF color, bool active);
void drawSearchIcon(HDC dc, int cx, int cy, int size, COLORREF color);
void drawSunMoonIcon(HDC dc, int cx, int cy, int size, COLORREF color, bool dark);
void drawSendIcon(HDC dc, int cx, int cy, int size, COLORREF color);
void drawPlusIcon(HDC dc, int cx, int cy, int arm, COLORREF color);
void drawCloseIcon(HDC dc, int cx, int cy, int arm, COLORREF color);
void drawLogoIcon(HDC dc, RECT rc, COLORREF accent, COLORREF fg);

COLORREF blend(COLORREF a, COLORREF b, double t);
COLORREF methodColor(const std::string& method);
COLORREF statusColor(int code, bool error);

} // namespace ui
} // namespace lite
