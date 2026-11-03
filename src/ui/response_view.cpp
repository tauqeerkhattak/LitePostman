#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include "ui/response_view.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <cstring>

#include "ui/controls.h"
#include "ui/theme.h"
#include "ui/util.h"
#include "core/json.h"

namespace lite {
namespace ui {

namespace {
const wchar_t kResponseClass[] = L"LitePostmanResponseView";

std::wstring clipboardText(const std::string& text) {
    return normalizeNewlines(toWide(text));
}
} // namespace

ResponseView::ResponseView() {}

ResponseView::~ResponseView() {
    if (hwnd_) DestroyWindow(hwnd_);
}

HWND ResponseView::create(HWND parent, int id) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = ResponseView::wndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kResponseClass;

    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return nullptr;

    hwnd_ = CreateWindowExW(0, kResponseClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                            0, 0, 700, 300, parent,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                            GetModuleHandleW(nullptr), this);
    if (!hwnd_) return nullptr;

    tabs_ = createTabStrip(hwnd_, 3001);
    tabAdd(tabs_, L"Body");
    tabAdd(tabs_, L"Headers");
    tabAdd(tabs_, L"Cookies");
    tabSetCurSel(tabs_, 0);

    copyBtn_ = createFlatButton(hwnd_, 3002, L"Copy", kButtonGhost);

    edit_ = CreateWindowExW(0, WC_EDITW, L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE |
                                ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_VSCROLL | WS_HSCROLL |
                                ES_READONLY,
                            0, 0, 400, 200, hwnd_,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(3003)),
                            GetModuleHandleW(nullptr), nullptr);
    SendMessageW(edit_, EM_SETLIMITTEXT, 0, 0);

    applyFonts();
    clear();
    return hwnd_;
}

void ResponseView::applyFonts() {
    const Theme& t = theme();
    if (edit_) {
        SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(t.fontMono), TRUE);
        SendMessageW(edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                     MAKELONG(t.scale(8), t.scale(8)));
    }
    if (tabs_) tabSetBackdrop(tabs_, t.surface);
    if (copyBtn_) setButtonBackdrop(copyBtn_, t.surface);
    themeApplyScrollbars(edit_, t.dark);
}

void ResponseView::resize(int x, int y, int width, int height) {
    if (!hwnd_) return;
    MoveWindow(hwnd_, x, y, width, height, TRUE);
    layout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ResponseView::layout() {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const Theme& t = theme();

    const int pad = t.scale(12);
    if (tabs_) MoveWindow(tabs_, pad, t.scale(46), rc.right - pad * 2, t.scale(34), TRUE);
    if (copyBtn_) {
        MoveWindow(copyBtn_, rc.right - pad - t.scale(70), t.scale(10), t.scale(70), t.scale(26), TRUE);
    }
    if (edit_) {
        MoveWindow(edit_, pad + t.scale(8), t.scale(88) + t.scale(8),
                   rc.right - pad * 2 - t.scale(16),
                   std::max(t.scale(40), static_cast<int>(rc.bottom) - t.scale(88) - pad - t.scale(8)), TRUE);
    }
}

void ResponseView::updateTab() {
    tab_ = tabs_ ? tabGetCurSel(tabs_) : 0;
    if (tab_ < 0 || tab_ > 2) tab_ = 0;
    if (edit_) {
        const std::wstring text = normalizeNewlines(toWide(contents_[tab_]));
        SetWindowTextW(edit_, text.c_str());
        SendMessageW(edit_, EM_SETSEL, 0, 0);
        SendMessageW(edit_, EM_SCROLLCARET, 0, 0);
    }
}

void ResponseView::setResponse(const std::string& status, const std::string& meta,
                               const std::string& body, const std::string& headers,
                               const std::string& cookies, int code) {
    status_ = status.empty() ? "Response" : status;
    meta_ = meta;
    contents_[0] = Json::format(body);
    contents_[1] = headers;
    contents_[2] = cookies;
    code_ = code;
    error_ = false;
    loading_ = false;
    hasResponse_ = true;

    if (tabs_) ShowWindow(tabs_, SW_SHOW);
    if (copyBtn_) {
        ShowWindow(copyBtn_, SW_SHOW);
        EnableWindow(copyBtn_, TRUE);
    }
    if (edit_) ShowWindow(edit_, SW_SHOW);
    updateTab();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ResponseView::setLoading() {
    status_ = "Sending...";
    meta_.clear();
    contents_[0].clear();
    contents_[1].clear();
    contents_[2].clear();
    code_ = 0;
    error_ = false;
    loading_ = true;
    hasResponse_ = false;

    if (tabs_) ShowWindow(tabs_, SW_HIDE);
    if (copyBtn_) ShowWindow(copyBtn_, SW_HIDE);
    if (edit_) ShowWindow(edit_, SW_HIDE);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ResponseView::setError(const std::string& title, const std::string& detail) {
    status_ = title.empty() ? "Request failed" : title;
    meta_.clear();
    contents_[0] = detail;
    contents_[1].clear();
    contents_[2].clear();
    code_ = 0;
    error_ = true;
    loading_ = false;
    hasResponse_ = true;

    if (tabs_) ShowWindow(tabs_, SW_SHOW);
    if (copyBtn_) {
        ShowWindow(copyBtn_, SW_SHOW);
        EnableWindow(copyBtn_, TRUE);
    }
    if (edit_) ShowWindow(edit_, SW_SHOW);
    updateTab();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ResponseView::clear() {
    status_ = "Ready";
    meta_.clear();
    contents_[0].clear();
    contents_[1].clear();
    contents_[2].clear();
    code_ = 0;
    error_ = false;
    loading_ = false;
    hasResponse_ = false;

    if (tabs_) ShowWindow(tabs_, SW_HIDE);
    if (copyBtn_) ShowWindow(copyBtn_, SW_HIDE);
    if (edit_) ShowWindow(edit_, SW_HIDE);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ResponseView::copyCurrent() {
    if (!hasResponse_ || !hwnd_) return;
    const std::wstring text = clipboardText(contents_[tab_]);
    if (text.empty()) return;

    if (!OpenClipboard(hwnd_)) return;
    EmptyClipboard();
    const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        void* target = GlobalLock(memory);
        if (target) {
            std::memcpy(target, text.c_str(), bytes);
            GlobalUnlock(memory);
            if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
        } else {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
}

void ResponseView::paint() {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd_, &ps);
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const Theme& t = theme();

    fillRect(dc, rc, t.bg);
    fillRound(dc, rc, t.scale(10), t.surface);
    RECT cardBorder = rc;
    InflateRect(&cardBorder, -1, -1);
    strokeRound(dc, cardBorder, t.scale(10), t.border, 1);

    COLORREF pillColor = t.textMuted;
    COLORREF pillBg = t.surfaceAlt;
    std::wstring pillText = L"Ready";
    if (loading_) {
        pillColor = t.accent;
        pillBg = t.accentSoft;
        pillText = L"Sending...";
    } else if (hasResponse_) {
        pillColor = statusColor(code_, error_);
        pillBg = blend(pillColor, t.surface, t.dark ? 0.82 : 0.88);
        pillText = toWide(status_);
    }

    const SIZE pillSize = measureText(dc, pillText, t.fontSmallBold);
    const int barY = t.scale(10);
    const int barH = t.scale(26);
    RECT pill = {t.scale(16), barY, t.scale(16) + pillSize.cx + t.scale(20), barY + barH};
    drawPill(dc, pill, pillText, t.fontSmallBold, pillColor, pillBg);

    if (!meta_.empty()) {
        RECT meta = {pill.right + t.scale(10), barY, rc.right - t.scale(100), barY + barH};
        drawText(dc, toWide(meta_), meta, t.fontSmall, t.textMuted,
                 DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    if (!hasResponse_) {
        const int centerY = (t.scale(46) + rc.bottom) / 2;
        RECT icon = {rc.right / 2 - t.scale(22), centerY - t.scale(64),
                     rc.right / 2 + t.scale(22), centerY - t.scale(20)};
        fillRound(dc, icon, t.scale(10), t.accentSoft);
        drawText(dc, loading_ ? L"\u2192" : L"\u2191", icon, t.fontTitle, t.accent,
                 DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);

        RECT title = {t.scale(24), centerY - t.scale(8), rc.right - t.scale(24), centerY + t.scale(16)};
        drawText(dc, loading_ ? L"Sending request..." : L"No response yet", title, t.fontUiMedium,
                 t.text, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);

        RECT hint = {t.scale(24), centerY + t.scale(18), rc.right - t.scale(24), centerY + t.scale(44)};
        drawText(dc, loading_ ? L"Waiting for the server to respond."
                              : L"Send a request and the result will appear here.",
                 hint, t.fontSmall, t.textMuted,
                 DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
    } else {
        RECT content = {t.scale(12), t.scale(88), rc.right - t.scale(12), rc.bottom - t.scale(12)};
        fillRound(dc, content, t.scale(8), t.codeBg);
        RECT border = content;
        InflateRect(&border, -1, -1);
        strokeRound(dc, border, t.scale(8), t.border, 1);
    }

    EndPaint(hwnd_, &ps);
}

LRESULT CALLBACK ResponseView::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ResponseView* view = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        view = reinterpret_cast<ResponseView*>(cs->lpCreateParams);
        if (view) {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(view));
            view->hwnd_ = hwnd;
        }
    } else {
        view = reinterpret_cast<ResponseView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (view) return view->handleMessage(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT ResponseView::handleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        paint();
        return 0;
    case WM_SIZE:
        layout();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_COMMAND: {
        const HWND control = reinterpret_cast<HWND>(lParam);
        if (control == copyBtn_ && HIWORD(wParam) == BN_CLICKED) {
            copyCurrent();
            return 0;
        }
        return 0;
    }
    case WM_NOTIFY: {
        const NMHDR* header = reinterpret_cast<const NMHDR*>(lParam);
        if (header && header->idFrom == 3001 && header->code == TCN_SELCHANGE) {
            updateTab();
            return 0;
        }
        break;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, theme().text);
        SetBkColor(dc, theme().codeBg);
        return reinterpret_cast<LRESULT>(themeBrush(theme().codeBg));
    }
    case kThemeChanged:
        applyFonts();
        if (edit_) InvalidateRect(edit_, nullptr, TRUE);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

} // namespace ui
} // namespace lite
