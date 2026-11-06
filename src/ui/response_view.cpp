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
#include "ui/rich_edit.h"
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

std::string buildJsonPreview(const std::string& raw) {
    if (raw.empty()) return "No content to preview.";
    std::string err;
    Json j = Json::parse(raw, &err);
    if (!err.empty()) {
        return "Not a valid JSON payload:\r\n" + err;
    }
    std::string out = "JSON Structure Preview\r\n";
    out += "======================\r\n\r\n";
    if (j.isObject()) {
        out += "Root: JSON Object\r\n";
        out += "Top-level properties: " + std::to_string(j.members().size()) + "\r\n\r\n";
        out += "Properties:\r\n";
        for (const auto& member : j.members()) {
            const std::string& k = member.first;
            const Json& v = member.second;
            std::string tName;
            if (v.isObject()) tName = "Object (" + std::to_string(v.members().size()) + " keys)";
            else if (v.isArray()) tName = "Array (" + std::to_string(v.items().size()) + " items)";
            else if (v.isString()) tName = "String (" + std::to_string(v.asString().size()) + " chars)";
            else if (v.isNumber()) tName = "Number (" + v.asString() + ")";
            else if (v.isBool()) tName = std::string("Boolean (") + (v.asBool() ? "true" : "false") + ")";
            else if (v.isNull()) tName = "Null";
            out += "  \u2022 " + k + " : " + tName + "\r\n";
        }
    } else if (j.isArray()) {
        out += "Root: JSON Array\r\n";
        out += "Total elements: " + std::to_string(j.items().size()) + "\r\n\r\n";
        if (!j.items().empty()) {
            const auto& first = j.items().front();
            if (first.isObject()) {
                out += "Properties in first element:\r\n";
                for (const auto& member : first.members()) {
                    out += "  \u2022 " + member.first + "\r\n";
                }
            } else if (first.isString()) out += "Element Type: String\r\n";
            else if (first.isNumber()) out += "Element Type: Number\r\n";
            else out += "Element Type: Primitive / Value\r\n";
        }
    }
    return out;
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

    prettyBtn_ = createFlatButton(hwnd_, 3004, L"Pretty", kButtonPrimary);
    rawBtn_ = createFlatButton(hwnd_, 3005, L"Raw", kButtonGhost);
    previewBtn_ = createFlatButton(hwnd_, 3006, L"Preview", kButtonGhost);
    findBtn_ = createFlatButton(hwnd_, 3007, L"Find", kButtonGhost);
    copyBtn_ = createFlatButton(hwnd_, 3002, L"Copy", kButtonGhost);
    expandBtn_ = createFlatButton(hwnd_, 3008, L"Expand", kButtonGhost);

    findEdit_ = CreateWindowExW(0, WC_EDITW, L"", WS_CHILD | ES_AUTOHSCROLL,
                                0, 0, 150, 24, hwnd_,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(3010)),
                                GetModuleHandleW(nullptr), nullptr);
    SendMessageW(findEdit_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Find in response..."));
    findPrevBtn_ = createFlatButton(hwnd_, 3011, L"\u25B2", kButtonGhost);
    findNextBtn_ = createFlatButton(hwnd_, 3012, L"\u25BC", kButtonGhost);
    findCloseBtn_ = createFlatButton(hwnd_, 3013, L"\u2715", kButtonGhost);

    edit_ = createRichEdit(hwnd_, 3003, nullptr, true, true);
    SendMessageW(edit_, EM_SETREADONLY, TRUE, 0);

    applyFonts();
    clear();
    return hwnd_;
}

void ResponseView::applyFonts() {
    const Theme& t = theme();
    if (prettyBtn_) setButtonBackdrop(prettyBtn_, t.surface);
    if (rawBtn_) setButtonBackdrop(rawBtn_, t.surface);
    if (previewBtn_) setButtonBackdrop(previewBtn_, t.surface);
    if (findBtn_) setButtonBackdrop(findBtn_, t.surface);
    if (expandBtn_) setButtonBackdrop(expandBtn_, t.surface);
    if (findPrevBtn_) setButtonBackdrop(findPrevBtn_, t.surface);
    if (findNextBtn_) setButtonBackdrop(findNextBtn_, t.surface);
    if (findCloseBtn_) setButtonBackdrop(findCloseBtn_, t.surface);
    if (findEdit_) SendMessageW(findEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(t.fontUi), TRUE);

    if (tabs_) tabSetBackdrop(tabs_, t.surface);
    if (copyBtn_) setButtonBackdrop(copyBtn_, t.surface);
    if (edit_) richEditSetTheme(edit_);
}

void ResponseView::resize(int x, int y, int width, int height) {
    if (!hwnd_) return;
    MoveWindow(hwnd_, x, y, width, height, TRUE);
    layout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ResponseView::setExpanded(bool expanded) {
    expanded_ = expanded;
    if (expandBtn_) {
        SetWindowTextW(expandBtn_, expanded_ ? L"Restore" : L"Expand");
    }
}

void ResponseView::layout() {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const Theme& t = theme();

    const int pad = t.scale(12);
    const int gap = t.scale(6);
    const int prettyW = t.scale(56);
    const int rawW = t.scale(46);
    const int prevW = t.scale(62);
    const int findW = t.scale(48);
    const int copyW = t.scale(52);
    const int expandW = t.scale(62);
    const int actionW = prettyW + rawW + prevW + findW + copyW + expandW + gap * 5;

    const int tabW = std::max(t.scale(120), static_cast<int>(rc.right) - pad * 2 - actionW - gap);
    if (tabs_) MoveWindow(tabs_, pad, t.scale(46), tabW, t.scale(34), TRUE);

    int curBtnX = rc.right - pad - actionW;
    const int btnY = t.scale(48);
    const int btnH = t.scale(28);

    if (prettyBtn_) { MoveWindow(prettyBtn_, curBtnX, btnY, prettyW, btnH, TRUE); curBtnX += prettyW + gap; }
    if (rawBtn_) { MoveWindow(rawBtn_, curBtnX, btnY, rawW, btnH, TRUE); curBtnX += rawW + gap; }
    if (previewBtn_) { MoveWindow(previewBtn_, curBtnX, btnY, prevW, btnH, TRUE); curBtnX += prevW + gap; }
    if (findBtn_) { MoveWindow(findBtn_, curBtnX, btnY, findW, btnH, TRUE); curBtnX += findW + gap; }
    if (copyBtn_) { MoveWindow(copyBtn_, curBtnX, btnY, copyW, btnH, TRUE); curBtnX += copyW + gap; }
    if (expandBtn_) { MoveWindow(expandBtn_, curBtnX, btnY, expandW, btnH, TRUE); }

    int contentTop = t.scale(88);
    if (findOpen_ && tab_ == 0 && hasResponse_) {
        const int findBarH = t.scale(28);
        const int findY = contentTop + t.scale(6);
        const int findEditW = t.scale(150);
        const int btnSmallW = t.scale(26);
        int findX = pad + t.scale(10);
        if (findEdit_) MoveWindow(findEdit_, findX, findY + t.scale(2), findEditW, t.scale(22), TRUE);
        findX += findEditW + t.scale(6);
        if (findPrevBtn_) MoveWindow(findPrevBtn_, findX, findY, btnSmallW, findBarH, TRUE);
        findX += btnSmallW + t.scale(4);
        if (findNextBtn_) MoveWindow(findNextBtn_, findX, findY, btnSmallW, findBarH, TRUE);
        if (findCloseBtn_) MoveWindow(findCloseBtn_, rc.right - pad - t.scale(10) - btnSmallW, findY, btnSmallW, findBarH, TRUE);
        contentTop += findBarH + t.scale(6);
    }

    const int statusH = (tab_ == 0 && hasResponse_) ? t.scale(24) : 0;
    if (edit_) {
        MoveWindow(edit_, pad + t.scale(8), contentTop + t.scale(8),
                   rc.right - pad * 2 - t.scale(16),
                   std::max(t.scale(40), static_cast<int>(rc.bottom) - contentTop - pad - t.scale(8) - statusH), TRUE);
    }
}

void ResponseView::setViewMode(int mode) {
    viewMode_ = mode;
    setButtonStyle(prettyBtn_, mode == 0 ? kButtonPrimary : kButtonGhost);
    setButtonStyle(rawBtn_, mode == 1 ? kButtonPrimary : kButtonGhost);
    setButtonStyle(previewBtn_, mode == 2 ? kButtonPrimary : kButtonGhost);

    if (tab_ == 0 && edit_) {
        std::string current;
        if (mode == 0) current = prettyBody_;
        else if (mode == 1) current = rawBody_;
        else current = previewBody_;

        richEditSetText(edit_, normalizeNewlines(toWide(current)));
        if (mode == 0) richEditHighlight(edit_);
        SendMessageW(edit_, EM_SETSEL, 0, 0);
        SendMessageW(edit_, EM_SCROLLCARET, 0, 0);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ResponseView::toggleFind(bool open) {
    findOpen_ = open;
    const bool show = findOpen_ && (tab_ == 0 && hasResponse_);
    if (findEdit_) ShowWindow(findEdit_, show ? SW_SHOW : SW_HIDE);
    if (findPrevBtn_) ShowWindow(findPrevBtn_, show ? SW_SHOW : SW_HIDE);
    if (findNextBtn_) ShowWindow(findNextBtn_, show ? SW_SHOW : SW_HIDE);
    if (findCloseBtn_) ShowWindow(findCloseBtn_, show ? SW_SHOW : SW_HIDE);
    layout();
    if (show && findEdit_) {
        SetFocus(findEdit_);
        SendMessageW(findEdit_, EM_SETSEL, 0, -1);
    } else if (edit_) {
        SetFocus(edit_);
    }
}

void ResponseView::doFind(bool forward) {
    if (!findEdit_ || !edit_) return;
    std::wstring q = windowTextW(findEdit_);
    richEditFindText(edit_, q, forward, false, &findMatchCur_, &findMatchTotal_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ResponseView::updateTab() {
    tab_ = tabs_ ? tabGetCurSel(tabs_) : 0;
    if (tab_ < 0 || tab_ > 2) tab_ = 0;
    const bool isBody = (tab_ == 0 && hasResponse_);

    if (prettyBtn_) ShowWindow(prettyBtn_, isBody ? SW_SHOW : SW_HIDE);
    if (rawBtn_) ShowWindow(rawBtn_, isBody ? SW_SHOW : SW_HIDE);
    if (previewBtn_) ShowWindow(previewBtn_, isBody ? SW_SHOW : SW_HIDE);
    if (findBtn_) ShowWindow(findBtn_, isBody ? SW_SHOW : SW_HIDE);

    const bool showFind = isBody && findOpen_;
    if (findEdit_) ShowWindow(findEdit_, showFind ? SW_SHOW : SW_HIDE);
    if (findPrevBtn_) ShowWindow(findPrevBtn_, showFind ? SW_SHOW : SW_HIDE);
    if (findNextBtn_) ShowWindow(findNextBtn_, SW_HIDE);
    if (findCloseBtn_) ShowWindow(findCloseBtn_, showFind ? SW_SHOW : SW_HIDE);

    layout();

    if (edit_) {
        std::string text;
        if (tab_ == 0) {
            if (viewMode_ == 0) text = prettyBody_;
            else if (viewMode_ == 1) text = rawBody_;
            else text = previewBody_;
        } else {
            text = contents_[tab_];
        }
        richEditSetText(edit_, normalizeNewlines(toWide(text)));
        if (tab_ == 0 && viewMode_ == 0) {
            richEditHighlight(edit_);
        }
        SendMessageW(edit_, EM_SETSEL, 0, 0);
        SendMessageW(edit_, EM_SCROLLCARET, 0, 0);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ResponseView::setResponse(const std::string& status, const std::string& meta,
                               const std::string& body, const std::string& headers,
                               const std::string& cookies, int code) {
    status_ = status.empty() ? "Response" : status;
    meta_ = meta;
    rawBody_ = body;
    prettyBody_ = Json::format(body);
    previewBody_ = buildJsonPreview(body);

    contents_[0] = prettyBody_;
    contents_[1] = headers;
    contents_[2] = cookies;
    code_ = code;
    error_ = false;
    loading_ = false;
    hasResponse_ = true;

    std::string err;
    int errL = 1, errC = 1;
    jsonValid_ = Json::validate(body, &err, &errL, &errC);
    if (jsonValid_ && !body.empty()) {
        int lines = 1;
        for (char c : prettyBody_) if (c == '\n') lines++;
        std::string sizeStr;
        if (body.size() < 1024) sizeStr = std::to_string(body.size()) + " B";
        else sizeStr = std::to_string(body.size() / 1024) + " KB";
        jsonStatusText_ = L"\u2713 Valid JSON \u2022 " + std::to_wstring(lines) + L" lines \u2022 " + toWide(sizeStr);
    } else if (!body.empty()) {
        std::wstring wErr = toWide(err);
        if (wErr.size() > 50) wErr = wErr.substr(0, 47) + L"...";
        jsonStatusText_ = L"\u26A0 " + wErr;
    } else {
        jsonStatusText_ = L"Response body empty";
    }

    if (tabs_) ShowWindow(tabs_, SW_SHOW);
    if (copyBtn_) {
        ShowWindow(copyBtn_, SW_SHOW);
        EnableWindow(copyBtn_, TRUE);
    }
    if (edit_) ShowWindow(edit_, SW_SHOW);

    viewMode_ = 0;
    setButtonStyle(prettyBtn_, kButtonPrimary);
    setButtonStyle(rawBtn_, kButtonGhost);
    setButtonStyle(previewBtn_, kButtonGhost);

    updateTab();
}

void ResponseView::setLoading() {
    status_ = "Sending...";
    meta_.clear();
    rawBody_.clear();
    prettyBody_.clear();
    previewBody_.clear();
    contents_[0].clear();
    contents_[1].clear();
    contents_[2].clear();
    code_ = 0;
    error_ = false;
    loading_ = true;
    hasResponse_ = false;

    if (tabs_) ShowWindow(tabs_, SW_HIDE);
    if (copyBtn_) ShowWindow(copyBtn_, SW_HIDE);
    if (prettyBtn_) ShowWindow(prettyBtn_, SW_HIDE);
    if (rawBtn_) ShowWindow(rawBtn_, SW_HIDE);
    if (previewBtn_) ShowWindow(previewBtn_, SW_HIDE);
    if (findBtn_) ShowWindow(findBtn_, SW_HIDE);
    if (findEdit_) ShowWindow(findEdit_, SW_HIDE);
    if (findPrevBtn_) ShowWindow(findPrevBtn_, SW_HIDE);
    if (findNextBtn_) ShowWindow(findNextBtn_, SW_HIDE);
    if (findCloseBtn_) ShowWindow(findCloseBtn_, SW_HIDE);
    if (edit_) ShowWindow(edit_, SW_HIDE);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ResponseView::setError(const std::string& title, const std::string& detail) {
    status_ = title.empty() ? "Request failed" : title;
    meta_.clear();
    rawBody_ = detail;
    prettyBody_ = detail;
    previewBody_ = detail;
    contents_[0] = detail;
    contents_[1].clear();
    contents_[2].clear();
    code_ = 0;
    error_ = true;
    loading_ = false;
    hasResponse_ = true;
    jsonValid_ = false;
    jsonStatusText_ = L"Error Response";

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
    rawBody_.clear();
    prettyBody_.clear();
    previewBody_.clear();
    contents_[0].clear();
    contents_[1].clear();
    contents_[2].clear();
    code_ = 0;
    error_ = false;
    loading_ = false;
    hasResponse_ = false;

    if (tabs_) ShowWindow(tabs_, SW_HIDE);
    if (copyBtn_) ShowWindow(copyBtn_, SW_HIDE);
    if (prettyBtn_) ShowWindow(prettyBtn_, SW_HIDE);
    if (rawBtn_) ShowWindow(rawBtn_, SW_HIDE);
    if (previewBtn_) ShowWindow(previewBtn_, SW_HIDE);
    if (findBtn_) ShowWindow(findBtn_, SW_HIDE);
    if (findEdit_) ShowWindow(findEdit_, SW_HIDE);
    if (findPrevBtn_) ShowWindow(findPrevBtn_, SW_HIDE);
    if (findNextBtn_) ShowWindow(findNextBtn_, SW_HIDE);
    if (findCloseBtn_) ShowWindow(findCloseBtn_, SW_HIDE);
    if (edit_) ShowWindow(edit_, SW_HIDE);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ResponseView::copyCurrent() {
    if (!hasResponse_ || !hwnd_) return;
    std::string textToCopy;
    if (tab_ == 0) {
        if (viewMode_ == 0) textToCopy = prettyBody_;
        else if (viewMode_ == 1) textToCopy = rawBody_;
        else textToCopy = previewBody_;
    } else {
        textToCopy = contents_[tab_];
    }
    const std::wstring text = clipboardText(textToCopy);
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

        if (tab_ == 0) {
            const int statusY = content.bottom - t.scale(22);
            RECT statusRc = {content.left + t.scale(10), statusY, content.right - t.scale(10), content.bottom - t.scale(2)};
            COLORREF statusCol = jsonValid_ ? t.success : t.textMuted;
            drawText(dc, jsonStatusText_, statusRc, t.fontSmall, statusCol, DT_LEFT | DT_VCENTER | DT_NOPREFIX);

            std::wstring caretInfo = L"Ln " + std::to_wstring(caretLine_) + L", Col " + std::to_wstring(caretCol_);
            drawText(dc, caretInfo, statusRc, t.fontSmall, t.textMuted, DT_RIGHT | DT_VCENTER | DT_NOPREFIX);

            if (findOpen_) {
                std::wstring matchText;
                if (findMatchTotal_ > 0) {
                    matchText = std::to_wstring(findMatchCur_) + L" of " + std::to_wstring(findMatchTotal_);
                } else {
                    matchText = L"No matches";
                }
                RECT matchRc = {content.left + t.scale(230), content.top + t.scale(6),
                                content.right - t.scale(42), content.top + t.scale(34)};
                drawText(dc, matchText, matchRc, t.fontSmall, t.textMuted, DT_LEFT | DT_VCENTER | DT_NOPREFIX);
            }
        }
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
        const WORD notification = HIWORD(wParam);
        if (control == copyBtn_ && notification == BN_CLICKED) {
            copyCurrent();
            return 0;
        }
        if (control == expandBtn_ && notification == BN_CLICKED) {
            SendMessageW(GetParent(hwnd_), WM_COMMAND, MAKEWPARAM(1003, 1), reinterpret_cast<LPARAM>(hwnd_));
            return 0;
        }
        if (control == prettyBtn_ && notification == BN_CLICKED) {
            setViewMode(0);
            return 0;
        }
        if (control == rawBtn_ && notification == BN_CLICKED) {
            setViewMode(1);
            return 0;
        }
        if (control == previewBtn_ && notification == BN_CLICKED) {
            setViewMode(2);
            return 0;
        }
        if (control == findBtn_ && notification == BN_CLICKED) {
            toggleFind(!findOpen_);
            return 0;
        }
        if (control == findNextBtn_ && notification == BN_CLICKED) {
            doFind(true);
            return 0;
        }
        if (control == findPrevBtn_ && notification == BN_CLICKED) {
            doFind(false);
            return 0;
        }
        if (control == findCloseBtn_ && notification == BN_CLICKED) {
            toggleFind(false);
            return 0;
        }
        if (LOWORD(wParam) == kCommandToggleFind) {
            toggleFind(true);
            return 0;
        }
        if (notification == EN_CHANGE && control == findEdit_) {
            doFind(true);
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
