#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include "ui/controls.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <vector>

#include "ui/theme.h"
#include "ui/util.h"

namespace lite {
namespace ui {

namespace {

const wchar_t kButtonClass[] = L"LiteFlatButton";
const wchar_t kDropdownClass[] = L"LiteDropdown";
const wchar_t kPopupClass[] = L"LiteDropdownPopup";
const wchar_t kTabClass[] = L"LiteTabStrip";

bool registerClass(const wchar_t* name, WNDPROC proc) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = name;
    if (RegisterClassExW(&wc)) return true;
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

void notifyClick(HWND hwnd) {
    SendMessageW(GetParent(hwnd), WM_COMMAND,
                 MAKEWPARAM(GetDlgCtrlID(hwnd), BN_CLICKED), reinterpret_cast<LPARAM>(hwnd));
}

// --- Flat button ------------------------------------------------------------

struct ButtonState {
    int style = kButtonSecondary;
    COLORREF backdrop = CLR_INVALID;
    bool hover = false;
    bool pressed = false;
    bool tracking = false;
};

LRESULT CALLBACK buttonProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT paintButton(HWND hwnd, ButtonState* state) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);

    const Theme& t = theme();
    const bool enabled = IsWindowEnabled(hwnd) != FALSE;
    const bool focused = GetFocus() == hwnd;
    const COLORREF backdrop = state->backdrop == CLR_INVALID ? t.surface : state->backdrop;

    fillRect(dc, rc, backdrop);

    COLORREF bg = backdrop;
    COLORREF fg = t.text;
    bool drawBorder = false;
    COLORREF borderColor = t.borderStrong;

    switch (state->style) {
    case kButtonPrimary:
        bg = enabled ? (state->pressed ? t.accentPressed : (state->hover ? t.accentHover : t.accent))
                     : blend(t.accent, backdrop, 0.55);
        fg = t.onAccent;
        break;
    case kButtonSecondary:
        bg = state->pressed ? t.surfaceAlt : (state->hover ? t.surfaceHover : t.surface);
        fg = enabled ? t.text : t.textFaint;
        drawBorder = true;
        borderColor = state->hover && enabled ? t.borderStrong : t.border;
        break;
    case kButtonSubtle:
        bg = state->pressed ? t.selection : (state->hover ? t.accentSoft : backdrop);
        fg = enabled ? t.accent : t.textFaint;
        break;
    case kButtonGhost:
    default:
        bg = state->pressed ? t.surfaceAlt : (state->hover ? t.surfaceHover : backdrop);
        fg = enabled ? t.textMuted : t.textFaint;
        break;
    }

    const int radius = t.scale(8);
    if (state->style != kButtonGhost || state->hover || state->pressed) {
        fillRound(dc, rc, radius, bg);
    }
    if (drawBorder) {
        RECT border = rc;
        InflateRect(&border, -1, -1);
        strokeRound(dc, border, radius, borderColor, 1);
    }
    if (focused && enabled) {
        RECT ring = rc;
        InflateRect(&ring, -t.scale(3), -t.scale(3));
        strokeRound(dc, ring, radius - 2, state->style == kButtonPrimary ? t.onAccent : t.accent, 1);
    }

    const std::wstring text = windowTextW(hwnd);
    RECT textRect = rc;
    textRect.left += t.scale(10);
    textRect.right -= t.scale(10);
    drawText(dc, text, textRect, t.fontUiMedium, fg,
             DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

    EndPaint(hwnd, &ps);
    return 0;
}

LRESULT CALLBACK buttonProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ButtonState* state = reinterpret_cast<ButtonState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = new ButtonState();
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        (void)cs;
        DefWindowProcW(hwnd, msg, wParam, lParam);
        return TRUE;
    }
    case WM_NCDESTROY:
        delete state;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        if (state) return paintButton(hwnd, state);
        break;
    case WM_SETTEXT:
        InvalidateRect(hwnd, nullptr, FALSE);
        break;
    case WM_ENABLE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case FBM_SETSTYLE:
        if (state) {
            state->style = static_cast<int>(wParam);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case FBM_SETBACKDROP:
        if (state) {
            state->backdrop = static_cast<COLORREF>(wParam);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (state && !state->tracking) {
            TRACKMOUSEEVENT track = {sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&track);
            state->tracking = true;
        }
        if (state && !state->hover) {
            state->hover = true;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSELEAVE:
        if (state) {
            state->hover = false;
            state->tracking = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (!IsWindowEnabled(hwnd)) return 0;
        SetFocus(hwnd);
        SetCapture(hwnd);
        if (state) {
            state->pressed = true;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP: {
        if (!state) return 0;
        const bool pressed = state->pressed;
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        RECT rc;
        GetClientRect(hwnd, &rc);
        const bool inRect = PtInRect(&rc, pt) != FALSE;
        if (GetCapture() == hwnd) ReleaseCapture();
        state->pressed = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        if (pressed && inRect) notifyClick(hwnd);
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (state && state->pressed) {
            state->pressed = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_KEYDOWN:
        if ((wParam == VK_SPACE || wParam == VK_RETURN) && IsWindowEnabled(hwnd)) {
            if (state && !state->pressed) {
                state->pressed = true;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        break;
    case WM_KEYUP:
        if ((wParam == VK_SPACE || wParam == VK_RETURN) && state && state->pressed) {
            state->pressed = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            notifyClick(hwnd);
            return 0;
        }
        break;
    case WM_GETDLGCODE:
        return DLGC_BUTTON | DLGC_WANTARROWS;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// --- Dropdown ---------------------------------------------------------------

struct DropdownState {
    std::vector<std::wstring> items;
    std::vector<COLORREF> colors;
    int sel = -1;
    std::wstring placeholder;
    COLORREF backdrop = CLR_INVALID;
    bool hover = false;
    bool tracking = false;
    HWND popup = nullptr;
};

struct PopupState {
    HWND owner = nullptr;
    int hover = -1;
    std::vector<std::wstring> items;
    std::vector<COLORREF> colors;
    int sel = -1;
    bool ignoreNextUp = true;
};

LRESULT CALLBACK dropdownProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK popupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

void openDropdown(HWND hwnd, DropdownState* state) {
    if (!state || state->items.empty()) return;
    if (state->popup) {
        DestroyWindow(state->popup);
        state->popup = nullptr;
        return;
    }

    const Theme& t = theme();
    RECT ownerRect;
    GetWindowRect(hwnd, &ownerRect);

    const int rowH = t.scale(34);
    const int pad = t.scale(6);
    const int width = std::max<int>(ownerRect.right - ownerRect.left, t.scale(180));
    const int height = pad * 2 + rowH * static_cast<int>(state->items.size());

    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &info);

    int x = ownerRect.left;
    int y = ownerRect.bottom + t.scale(4);
    if (y + height > info.rcWork.bottom) y = ownerRect.top - height - t.scale(4);
    if (x + width > info.rcWork.right) x = info.rcWork.right - width;
    if (x < info.rcWork.left) x = info.rcWork.left;
    if (y < info.rcWork.top) y = info.rcWork.top;

    auto* popupState = new PopupState();
    popupState->owner = hwnd;
    popupState->items = state->items;
    popupState->colors = state->colors;
    popupState->sel = state->sel;

    HWND popup = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                                 kPopupClass, L"", WS_POPUP, x, y, width, height,
                                 hwnd, nullptr, GetModuleHandleW(nullptr), popupState);
    if (!popup) {
        delete popupState;
        return;
    }

    HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1, t.scale(8) * 2, t.scale(8) * 2);
    SetWindowRgn(popup, region, TRUE);

    state->popup = popup;
    ShowWindow(popup, SW_SHOWNOACTIVATE);
    SetCapture(popup);
    InvalidateRect(popup, nullptr, FALSE);
}

int popupIndexAt(const PopupState* state, int y) {
    const int rowH = theme().scale(34);
    const int pad = theme().scale(6);
    const int index = (y - pad) / rowH;
    if (index < 0 || index >= static_cast<int>(state->items.size())) return -1;
    if (y < pad) return -1;
    return index;
}

void paintDropdown(HWND hwnd, DropdownState* state) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);

    const Theme& t = theme();
    const COLORREF backdrop = state->backdrop == CLR_INVALID ? t.surface : state->backdrop;
    fillRect(dc, rc, backdrop);

    RECT box = rc;
    InflateRect(&box, -1, -1);
    const bool focused = GetFocus() == hwnd;
    fillRound(dc, box, t.scale(8), t.inputBg);
    strokeRound(dc, box, t.scale(8), focused ? t.accent : (state->hover ? t.borderStrong : t.border),
                focused ? 2 : 1);

    RECT textRect = box;
    textRect.left += t.scale(12);
    textRect.right -= t.scale(32);
    const bool hasSelection = state->sel >= 0 && state->sel < static_cast<int>(state->items.size());
    const std::wstring text = hasSelection ? state->items[static_cast<std::size_t>(state->sel)]
                                           : state->placeholder;
    COLORREF color = t.textMuted;
    if (hasSelection) {
        const COLORREF custom = state->colors[static_cast<std::size_t>(state->sel)];
        color = custom == CLR_INVALID ? t.text : custom;
    }
    drawText(dc, text, textRect, t.fontUiMedium, color,
             DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

    drawChevron(dc, box.right - t.scale(16), (box.top + box.bottom) / 2, t.scale(8),
                t.textMuted, true);

    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK dropdownProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    DropdownState* state = reinterpret_cast<DropdownState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_NCCREATE:
        state = new DropdownState();
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        return TRUE;
    case WM_NCDESTROY:
        if (state && state->popup) DestroyWindow(state->popup);
        delete state;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        if (state) paintDropdown(hwnd, state);
        return 0;
    case WM_MOUSEMOVE:
        if (state && !state->tracking) {
            TRACKMOUSEEVENT track = {sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&track);
            state->tracking = true;
        }
        if (state && !state->hover) {
            state->hover = true;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSELEAVE:
        if (state) {
            state->hover = false;
            state->tracking = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN:
        SetFocus(hwnd);
        if (state) openDropdown(hwnd, state);
        return 0;
    case WM_KEYDOWN:
        if ((wParam == VK_SPACE || wParam == VK_RETURN || wParam == VK_DOWN) && state) {
            openDropdown(hwnd, state);
            return 0;
        }
        break;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case FBM_SETBACKDROP:
    case DDM_SETBACKDROP:
        if (state) {
            state->backdrop = static_cast<COLORREF>(wParam);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case DDM_ADDSTRING:
        if (state) {
            state->items.emplace_back(reinterpret_cast<const wchar_t*>(lParam));
            state->colors.push_back(CLR_INVALID);
            if (state->sel < 0) state->sel = 0;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case DDM_SETITEMCOLOR:
        if (state && wParam < state->colors.size()) {
            state->colors[static_cast<std::size_t>(wParam)] = static_cast<COLORREF>(lParam);
        }
        return 0;
    case DDM_RESETCONTENT:
        if (state) {
            state->items.clear();
            state->colors.clear();
            state->sel = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case DDM_SETCURSEL:
        if (state) {
            const int index = static_cast<int>(wParam);
            state->sel = (index >= 0 && index < static_cast<int>(state->items.size())) ? index : -1;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case DDM_GETCURSEL:
        return state ? state->sel : -1;
    case DDM_GETCOUNT:
        return state ? static_cast<LRESULT>(state->items.size()) : 0;
    case DDM_SETPLACEHOLDER:
        if (state) {
            state->placeholder = reinterpret_cast<const wchar_t*>(lParam);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void paintPopup(HWND hwnd, PopupState* state) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);

    const Theme& t = theme();
    fillRect(dc, rc, t.surface);
    RECT border = rc;
    InflateRect(&border, -1, -1);
    strokeRound(dc, border, t.scale(8), t.border, 1);

    const int rowH = t.scale(34);
    const int pad = t.scale(6);
    for (int i = 0; i < static_cast<int>(state->items.size()); ++i) {
        RECT row = {pad, pad + i * rowH, rc.right - pad, pad + (i + 1) * rowH};
        if (i == state->sel) {
            fillRound(dc, row, t.scale(6), t.accentSoft);
        } else if (i == state->hover) {
            fillRound(dc, row, t.scale(6), t.surfaceHover);
        }
        RECT textRect = row;
        textRect.left += t.scale(10);
        textRect.right -= t.scale(26);
        const COLORREF custom = state->colors[static_cast<std::size_t>(i)];
        const COLORREF color = custom == CLR_INVALID ? t.text : custom;
        drawText(dc, state->items[static_cast<std::size_t>(i)], textRect, t.fontUiMedium, color,
                 DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        if (i == state->sel) {
            RECT check = row;
            check.left = check.right - t.scale(26);
            drawText(dc, L"\u2713", check, t.fontSmallBold, t.accent,
                     DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
        }
    }

    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK popupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    PopupState* state = reinterpret_cast<PopupState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = reinterpret_cast<PopupState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        return TRUE;
    }
    case WM_NCDESTROY:
        if (state && state->owner) {
            DropdownState* ownerState = reinterpret_cast<DropdownState*>(GetWindowLongPtrW(state->owner, GWLP_USERDATA));
            if (ownerState && ownerState->popup == hwnd) ownerState->popup = nullptr;
        }
        delete state;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        if (state) paintPopup(hwnd, state);
        return 0;
    case WM_MOUSEMOVE:
        if (state) {
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            RECT rc;
            GetClientRect(hwnd, &rc);
            int index = PtInRect(&rc, pt) ? popupIndexAt(state, pt.y) : -1;
            if (index != state->hover) {
                state->hover = index;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    case WM_LBUTTONDOWN: {
        if (!state) return 0;
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (!PtInRect(&rc, pt)) {
            if (GetCapture() == hwnd) ReleaseCapture();
            DestroyWindow(hwnd);
            return 0;
        }
        const int index = popupIndexAt(state, pt.y);
        if (index >= 0) {
            HWND owner = state->owner;
            const int id = GetDlgCtrlID(owner);
            HWND parent = GetParent(owner);
            DropdownState* ownerState = reinterpret_cast<DropdownState*>(GetWindowLongPtrW(owner, GWLP_USERDATA));
            if (ownerState) {
                ownerState->sel = index;
                InvalidateRect(owner, nullptr, FALSE);
            }
            if (GetCapture() == hwnd) ReleaseCapture();
            DestroyWindow(hwnd);
            if (parent) {
                SendMessageW(parent, WM_COMMAND, MAKEWPARAM(id, CBN_SELCHANGE), reinterpret_cast<LPARAM>(owner));
            }
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (!state) return 0;
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (state->ignoreNextUp) {
            state->ignoreNextUp = false;
            if (PtInRect(&rc, pt)) {
                const int index = popupIndexAt(state, pt.y);
                if (index >= 0) {
                    HWND owner = state->owner;
                    const int id = GetDlgCtrlID(owner);
                    HWND parent = GetParent(owner);
                    DropdownState* ownerState = reinterpret_cast<DropdownState*>(GetWindowLongPtrW(owner, GWLP_USERDATA));
                    if (ownerState) {
                        ownerState->sel = index;
                        InvalidateRect(owner, nullptr, FALSE);
                    }
                    if (GetCapture() == hwnd) ReleaseCapture();
                    DestroyWindow(hwnd);
                    if (parent) {
                        SendMessageW(parent, WM_COMMAND, MAKEWPARAM(id, CBN_SELCHANGE), reinterpret_cast<LPARAM>(owner));
                    }
                    return 0;
                }
            }
            return 0;
        }
        if (PtInRect(&rc, pt)) {
            const int index = popupIndexAt(state, pt.y);
            if (index >= 0) {
                HWND owner = state->owner;
                const int id = GetDlgCtrlID(owner);
                HWND parent = GetParent(owner);
                DropdownState* ownerState = reinterpret_cast<DropdownState*>(GetWindowLongPtrW(owner, GWLP_USERDATA));
                if (ownerState) {
                    ownerState->sel = index;
                    InvalidateRect(owner, nullptr, FALSE);
                }
                if (GetCapture() == hwnd) ReleaseCapture();
                DestroyWindow(hwnd);
                if (parent) {
                    SendMessageW(parent, WM_COMMAND, MAKEWPARAM(id, CBN_SELCHANGE), reinterpret_cast<LPARAM>(owner));
                }
                return 0;
            }
        }
        if (GetCapture() == hwnd) ReleaseCapture();
        DestroyWindow(hwnd);
        return 0;
    }
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            if (GetCapture() == hwnd) ReleaseCapture();
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
        if (GetCapture() == hwnd) ReleaseCapture();
        DestroyWindow(hwnd);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// --- Tab strip --------------------------------------------------------------

struct TabState {
    std::vector<std::wstring> items;
    int sel = 0;
    int hover = -1;
    bool tracking = false;
    COLORREF backdrop = CLR_INVALID;
};

LRESULT CALLBACK tabProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

int tabHitTest(TabState* state, HDC dc, int x) {
    const Theme& t = theme();
    int cursor = 0;
    for (int i = 0; i < static_cast<int>(state->items.size()); ++i) {
        const SIZE size = measureText(dc, state->items[static_cast<std::size_t>(i)], t.fontUiMedium);
        const int width = size.cx + t.scale(14) * 2;
        if (x >= cursor && x < cursor + width) return i;
        cursor += width;
    }
    return -1;
}

void notifyTabChange(HWND hwnd, int index) {
    NMHDR header = {};
    header.hwndFrom = hwnd;
    header.idFrom = static_cast<UINT_PTR>(GetDlgCtrlID(hwnd));
    header.code = TCN_SELCHANGE;
    SendMessageW(GetParent(hwnd), WM_NOTIFY, header.idFrom, reinterpret_cast<LPARAM>(&header));
    (void)index;
}

void paintTabs(HWND hwnd, TabState* state) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);

    const Theme& t = theme();
    const COLORREF backdrop = state->backdrop == CLR_INVALID ? t.surface : state->backdrop;
    fillRect(dc, rc, backdrop);

    int cursor = 0;
    for (int i = 0; i < static_cast<int>(state->items.size()); ++i) {
        const std::wstring& label = state->items[static_cast<std::size_t>(i)];
        const SIZE size = measureText(dc, label, t.fontUiMedium);
        const int width = size.cx + t.scale(14) * 2;
        RECT item = {cursor, 0, cursor + width, rc.bottom};

        if (i == state->hover && i != state->sel) {
            RECT hoverRect = item;
            hoverRect.left += t.scale(4);
            hoverRect.right -= t.scale(4);
            hoverRect.top += t.scale(4);
            hoverRect.bottom -= t.scale(4);
            fillRound(dc, hoverRect, t.scale(6), t.surfaceHover);
        }

        drawText(dc, label, item, t.fontUiMedium,
                 i == state->sel ? t.accent : (i == state->hover ? t.text : t.textMuted),
                 DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);

        if (i == state->sel) {
            RECT underline = {item.left + t.scale(14), rc.bottom - t.scale(3),
                              item.right - t.scale(14), rc.bottom - t.scale(1)};
            fillRound(dc, underline, t.scale(1), t.accent);
        }
        cursor += width;
    }

    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK tabProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    TabState* state = reinterpret_cast<TabState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_NCCREATE:
        state = new TabState();
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        return TRUE;
    case WM_NCDESTROY:
        delete state;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        if (state) paintTabs(hwnd, state);
        return 0;
    case WM_MOUSEMOVE: {
        if (!state) return 0;
        if (!state->tracking) {
            TRACKMOUSEEVENT track = {sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&track);
            state->tracking = true;
        }
        HDC dc = GetDC(hwnd);
        const int index = tabHitTest(state, dc, GET_X_LPARAM(lParam));
        ReleaseDC(hwnd, dc);
        if (index != state->hover) {
            state->hover = index;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (state) {
            state->hover = -1;
            state->tracking = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN: {
        if (!state) return 0;
        HDC dc = GetDC(hwnd);
        const int index = tabHitTest(state, dc, GET_X_LPARAM(lParam));
        ReleaseDC(hwnd, dc);
        if (index >= 0 && index != state->sel) {
            state->sel = index;
            InvalidateRect(hwnd, nullptr, FALSE);
            notifyTabChange(hwnd, index);
        }
        SetFocus(hwnd);
        return 0;
    }
    case WM_KEYDOWN: {
        if (!state) return 0;
        int index = state->sel;
        if (wParam == VK_LEFT) --index;
        else if (wParam == VK_RIGHT) ++index;
        else return 0;
        index = std::max(0, std::min(index, static_cast<int>(state->items.size()) - 1));
        if (index != state->sel) {
            state->sel = index;
            InvalidateRect(hwnd, nullptr, FALSE);
            notifyTabChange(hwnd, index);
        }
        return 0;
    }
    case TSM_ADDSTRING:
        if (state) {
            state->items.emplace_back(reinterpret_cast<const wchar_t*>(lParam));
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case TSM_RESETCONTENT:
        if (state) {
            state->items.clear();
            state->sel = 0;
            state->hover = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case TSM_SETCURSEL:
        if (state) {
            const int index = static_cast<int>(wParam);
            state->sel = (index >= 0 && index < static_cast<int>(state->items.size())) ? index : 0;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case TSM_GETCURSEL:
        return state ? state->sel : 0;
    case TSM_SETBACKDROP:
        if (state) {
            state->backdrop = static_cast<COLORREF>(wParam);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

HWND createFlatButton(HWND parent, int id, const wchar_t* text, int style) {
    if (!registerClass(kButtonClass, buttonProc)) return nullptr;
    HWND hwnd = CreateWindowExW(0, kButtonClass, text, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                0, 0, 100, 32, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                GetModuleHandleW(nullptr), nullptr);
    if (hwnd) SendMessageW(hwnd, FBM_SETSTYLE, static_cast<WPARAM>(style), 0);
    return hwnd;
}

void setButtonStyle(HWND button, int style) {
    if (button) SendMessageW(button, FBM_SETSTYLE, static_cast<WPARAM>(style), 0);
}

void setButtonBackdrop(HWND button, COLORREF color) {
    if (button) SendMessageW(button, FBM_SETBACKDROP, static_cast<WPARAM>(color), 0);
}

HWND createDropdown(HWND parent, int id) {
    if (!registerClass(kDropdownClass, dropdownProc) || !registerClass(kPopupClass, popupProc)) return nullptr;
    return CreateWindowExW(0, kDropdownClass, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                           0, 0, 160, 36, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           GetModuleHandleW(nullptr), nullptr);
}

void dropdownAdd(HWND dropdown, const std::wstring& text, COLORREF color) {
    if (!dropdown) return;
    SendMessageW(dropdown, DDM_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
    if (color != CLR_INVALID) {
        const int count = static_cast<int>(SendMessageW(dropdown, DDM_GETCOUNT, 0, 0));
        if (count > 0) {
            SendMessageW(dropdown, DDM_SETITEMCOLOR, static_cast<WPARAM>(count - 1),
                         static_cast<LPARAM>(color));
        }
    }
}

void dropdownReset(HWND dropdown) {
    if (dropdown) SendMessageW(dropdown, DDM_RESETCONTENT, 0, 0);
}

void dropdownSetCurSel(HWND dropdown, int index) {
    if (dropdown) SendMessageW(dropdown, DDM_SETCURSEL, static_cast<WPARAM>(index), 0);
}

int dropdownGetCurSel(HWND dropdown) {
    return dropdown ? static_cast<int>(SendMessageW(dropdown, DDM_GETCURSEL, 0, 0)) : -1;
}

void dropdownSetPlaceholder(HWND dropdown, const std::wstring& text) {
    if (dropdown) SendMessageW(dropdown, DDM_SETPLACEHOLDER, 0, reinterpret_cast<LPARAM>(text.c_str()));
}

void dropdownSetBackdrop(HWND dropdown, COLORREF color) {
    if (dropdown) SendMessageW(dropdown, DDM_SETBACKDROP, static_cast<WPARAM>(color), 0);
}

HWND createTabStrip(HWND parent, int id) {
    if (!registerClass(kTabClass, tabProc)) return nullptr;
    return CreateWindowExW(0, kTabClass, L"", WS_CHILD | WS_VISIBLE,
                           0, 0, 300, 34, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           GetModuleHandleW(nullptr), nullptr);
}

void tabAdd(HWND tabs, const std::wstring& text) {
    if (tabs) SendMessageW(tabs, TSM_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
}

void tabReset(HWND tabs) {
    if (tabs) SendMessageW(tabs, TSM_RESETCONTENT, 0, 0);
}

void tabSetCurSel(HWND tabs, int index) {
    if (tabs) SendMessageW(tabs, TSM_SETCURSEL, static_cast<WPARAM>(index), 0);
}

int tabGetCurSel(HWND tabs) {
    return tabs ? static_cast<int>(SendMessageW(tabs, TSM_GETCURSEL, 0, 0)) : 0;
}

void tabSetBackdrop(HWND tabs, COLORREF color) {
    if (tabs) SendMessageW(tabs, TSM_SETBACKDROP, static_cast<WPARAM>(color), 0);
}

} // namespace ui
} // namespace lite
