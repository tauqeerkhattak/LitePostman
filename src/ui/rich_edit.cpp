#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include "ui/rich_edit.h"

#include <commctrl.h>
#include <richedit.h>

#include <cstring>
#include <string>

#include "ui/theme.h"

namespace lite {
namespace ui {

namespace {

const wchar_t* g_richClass = nullptr;
VariableListProvider g_variableProvider = nullptr;

struct RichState {
    std::wstring cue;
    bool multiline = false;
    bool mono = false;
};

LRESULT CALLBACK richSubclass(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                              UINT_PTR id, DWORD_PTR refData);

bool isRichEdit(HWND edit) {
    if (!edit) return false;
    wchar_t name[64] = {};
    GetClassNameW(edit, name, 64);
    return wcscmp(name, L"RICHEDIT50W") == 0 || wcscmp(name, L"RichEdit20W") == 0;
}

RichState* stateOf(HWND edit) {
    if (!edit) return nullptr;
    DWORD_PTR data = 0;
    if (!GetWindowSubclass(edit, richSubclass, 1, &data)) return nullptr;
    return reinterpret_cast<RichState*>(data);
}

void paintCue(HWND hwnd, RichState* state) {
    if (!state || state->cue.empty()) return;
    if (GetFocus() == hwnd) return;
    if (GetWindowTextLengthW(hwnd) > 0) return;

    HDC dc = GetDC(hwnd);
    if (!dc) return;
    RECT rc;
    GetClientRect(hwnd, &rc);
    const Theme& t = theme();

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, t.textFaint);
    HFONT font = state->mono ? t.fontMono : t.fontUi;
    HGDIOBJ oldFont = SelectObject(dc, font);

    RECT text = rc;
    text.left += t.scale(5);
    text.right -= t.scale(5);
    UINT flags = DT_NOPREFIX;
    if (state->multiline) {
        text.top += t.scale(5);
        flags |= DT_LEFT | DT_TOP | DT_WORDBREAK;
    } else {
        flags |= DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
    }
    DrawTextW(dc, state->cue.c_str(), -1, &text, flags);

    SelectObject(dc, oldFont);
    ReleaseDC(hwnd, dc);
}

static bool showVariableAutocompleteMenu(HWND hwnd, DWORD start, DWORD end, bool isRichEdit) {
    if (!g_variableProvider || start != end || start < 2) return false;

    bool matchTwoBraces = false;
    bool hasTrailing = false;

    if (isRichEdit) {
        TEXTRANGEW tr = {};
        tr.chrg.cpMin = static_cast<LONG>(start - 2);
        tr.chrg.cpMax = static_cast<LONG>(start);
        wchar_t buf[3] = {};
        tr.lpstrText = buf;
        SendMessageW(hwnd, EM_GETTEXTRANGE, 0, reinterpret_cast<LPARAM>(&tr));
        if (buf[0] == L'{' && buf[1] == L'{') {
            matchTwoBraces = true;
            TEXTRANGEW nextTr = {};
            nextTr.chrg.cpMin = static_cast<LONG>(start);
            nextTr.chrg.cpMax = static_cast<LONG>(start + 2);
            wchar_t nextBuf[3] = {};
            nextTr.lpstrText = nextBuf;
            SendMessageW(hwnd, EM_GETTEXTRANGE, 0, reinterpret_cast<LPARAM>(&nextTr));
            if (nextBuf[0] == L'}' && nextBuf[1] == L'}') {
                hasTrailing = true;
            }
        }
    } else {
        int len = GetWindowTextLengthW(hwnd);
        if (len >= 2 && static_cast<int>(start) <= len) {
            std::vector<wchar_t> buf(len + 1, 0);
            GetWindowTextW(hwnd, buf.data(), len + 1);
            if (buf[start - 2] == L'{' && buf[start - 1] == L'{') {
                matchTwoBraces = true;
                if (static_cast<int>(start + 1) < len && buf[start] == L'}' && buf[start + 1] == L'}') {
                    hasTrailing = true;
                }
            }
        }
    }

    if (!matchTwoBraces) return false;

    auto vars = g_variableProvider();
    HMENU menu = CreatePopupMenu();
    if (vars.empty()) {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"No environment variables defined");
    } else {
        for (std::size_t i = 0; i < vars.size(); ++i) {
            std::wstring label = vars[i].first;
            if (!vars[i].second.empty()) {
                std::wstring val = vars[i].second;
                if (val.size() > 36) val = val.substr(0, 33) + L"...";
                label += L"\t" + val;
            }
            AppendMenuW(menu, MF_STRING, 1000 + i, label.c_str());
        }
    }

    POINT caretPt = {};
    GetCaretPos(&caretPt);
    if (caretPt.x == 0 && caretPt.y == 0 && isRichEdit) {
        POINTL ptl = {};
        SendMessageW(hwnd, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&ptl), start);
        caretPt.x = ptl.x;
        caretPt.y = ptl.y;
    }
    caretPt.y += theme().scale(20);
    ClientToScreen(hwnd, &caretPt);

    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                             caretPt.x, caretPt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);

    if (cmd >= 1000 && cmd < 1000 + static_cast<int>(vars.size())) {
        const std::wstring& varName = vars[cmd - 1000].first;
        std::wstring insertText = varName + (hasTrailing ? L"" : L"}}");
        SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(insertText.c_str()));
        if (isRichEdit) {
            richEditHighlight(hwnd);
        }
        return true;
    }
    return false;
}

LRESULT CALLBACK plainEditVarSubclass(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                      UINT_PTR id, DWORD_PTR /*refData*/) {
    switch (msg) {
    case WM_CHAR: {
        const LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
        if (wParam == L'{') {
            DWORD start = 0, end = 0;
            SendMessageW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
            showVariableAutocompleteMenu(hwnd, start, end, false);
        }
        return result;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, plainEditVarSubclass, id);
        break;
    default:
        break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK richSubclass(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                              UINT_PTR id, DWORD_PTR refData) {
    RichState* state = reinterpret_cast<RichState*>(refData);

    switch (msg) {
    case WM_PAINT: {
        const LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
        paintCue(hwnd, state);
        return result;
    }
    case WM_KEYDOWN: {
        if (wParam == 'F' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(kCommandToggleFind, 0), reinterpret_cast<LPARAM>(hwnd));
            return 0;
        }
        break;
    }
    case WM_CHAR: {
        const LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
        if (wParam == L'{') {
            DWORD start = 0, end = 0;
            SendMessageW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
            showVariableAutocompleteMenu(hwnd, start, end, true);
        } else if (wParam == L'}') {
            richEditHighlight(hwnd);
        }
        return result;
    }
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(hwnd, nullptr, FALSE);
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, richSubclass, id);
        delete state;
        break;
    default:
        break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

} // namespace

bool richEditInit() {
    static bool tried = false;
    static bool ready = false;
    if (!tried) {
        tried = true;
        if (LoadLibraryW(L"Msftedit.dll")) {
            g_richClass = MSFTEDIT_CLASS;
            ready = true;
        } else if (LoadLibraryW(L"riched20.dll")) {
            g_richClass = L"RichEdit20W";
            ready = true;
        }
    }
    return ready;
}

HWND createRichEdit(HWND parent, int id, const wchar_t* cue, bool multiline, bool mono) {
    if (!richEditInit()) {
        DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
        style |= multiline ? (ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL)
                           : ES_AUTOHSCROLL;
        HWND edit = CreateWindowExW(0, L"Edit", L"", style, 0, 0, 200, 30, parent,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                    GetModuleHandleW(nullptr), nullptr);
        if (edit && cue) {
            SendMessageW(edit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(cue));
        }
        return edit;
    }

    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NOHIDESEL;
    if (multiline) {
        style |= ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL;
    } else {
        style |= ES_AUTOHSCROLL;
    }

    HWND edit = CreateWindowExW(0, g_richClass, L"", style, 0, 0, 200, 30, parent,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                GetModuleHandleW(nullptr), nullptr);
    if (!edit) return nullptr;

    auto* state = new RichState();
    state->cue = cue ? cue : L"";
    state->multiline = multiline;
    state->mono = mono;
    SetWindowSubclass(edit, richSubclass, 1, reinterpret_cast<DWORD_PTR>(state));

    SendMessageW(edit, EM_SETLIMITTEXT, 0, 0);
    SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELONG(theme().scale(2), theme().scale(2)));
    richEditSetTheme(edit);
    return edit;
}

void richEditSetText(HWND edit, const std::wstring& text) {
    if (!edit) return;
    const BOOL readOnly = (GetWindowLongPtrW(edit, GWL_STYLE) & ES_READONLY) != 0;
    if (readOnly) SendMessageW(edit, EM_SETREADONLY, FALSE, 0);
    SetWindowTextW(edit, text.c_str());
    richEditHighlight(edit);
    if (readOnly) SendMessageW(edit, EM_SETREADONLY, TRUE, 0);
    InvalidateRect(edit, nullptr, FALSE);
}

void richEditSetTheme(HWND edit) {
    if (!edit) return;
    const Theme& t = theme();
    RichState* state = stateOf(edit);
    const bool mono = state ? state->mono : false;

    if (!isRichEdit(edit)) {
        SendMessageW(edit, WM_SETFONT,
                     reinterpret_cast<WPARAM>(mono ? t.fontMono : t.fontUi), TRUE);
        themeApplyScrollbars(edit, t.dark);
        InvalidateRect(edit, nullptr, FALSE);
        return;
    }

    const COLORREF background = mono ? t.codeBg : t.inputBg;
    SendMessageW(edit, EM_SETBKGNDCOLOR, 0, static_cast<LPARAM>(background));

    CHARFORMAT2W format = {};
    format.cbSize = sizeof(format);
    format.dwMask = CFM_COLOR | CFM_FACE | CFM_SIZE | CFM_BOLD;
    format.crTextColor = t.text;
    format.dwEffects = 0;
    format.yHeight = 10 * 20;
    lstrcpynW(format.szFaceName, mono ? L"Consolas" : L"Segoe UI", LF_FACESIZE);
    SendMessageW(edit, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&format));
    SendMessageW(edit, EM_SETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&format));

    themeApplyScrollbars(edit, t.dark);
    richEditHighlight(edit);
}

void richEditHighlight(HWND edit) {
    if (!edit || !isRichEdit(edit)) return;
    const Theme& t = theme();

    CHARRANGE previous = {0, 0};
    SendMessageW(edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&previous));

    SendMessageW(edit, WM_SETREDRAW, FALSE, 0);
    SendMessageW(edit, EM_HIDESELECTION, TRUE, 0);

    CHARFORMAT2W base = {};
    base.cbSize = sizeof(base);
    base.dwMask = CFM_COLOR | CFM_BOLD;
    base.crTextColor = t.text;
    base.dwEffects = 0;
    SendMessageW(edit, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&base));

    const int length = GetWindowTextLengthW(edit);
    if (length > 0) {
        std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
        GetWindowTextW(edit, text.data(), length + 1);
        text.resize(static_cast<std::size_t>(length));

        const COLORREF colKey = t.dark ? RGB(121, 192, 255) : RGB(10, 75, 175);
        const COLORREF colString = t.dark ? RGB(142, 215, 142) : RGB(22, 125, 55);
        const COLORREF colNumber = t.dark ? RGB(255, 171, 112) : RGB(185, 80, 15);
        const COLORREF colKeyword = t.dark ? RGB(255, 123, 114) : RGB(200, 25, 45);
        const COLORREF colVariable = t.accent;

        auto applyFormat = [&](size_t start, size_t end, COLORREF color, bool bold) {
            if (start >= end) return;
            CHARRANGE range = {static_cast<LONG>(start), static_cast<LONG>(end)};
            SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
            CHARFORMAT2W fmt = {};
            fmt.cbSize = sizeof(fmt);
            fmt.dwMask = CFM_COLOR | (bold ? CFM_BOLD : 0);
            fmt.crTextColor = color;
            fmt.dwEffects = bold ? CFE_BOLD : 0;
            SendMessageW(edit, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&fmt));
        };

        const size_t maxScan = std::min(text.size(), static_cast<size_t>(120000));
        size_t i = 0;
        while (i < maxScan) {
            const wchar_t c = text[i];
            if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') {
                ++i;
                continue;
            }

            // Variable {{var}}
            if (c == L'{' && i + 1 < maxScan && text[i + 1] == L'{') {
                size_t end = text.find(L"}}", i + 2);
                if (end != std::wstring::npos && end < maxScan) {
                    applyFormat(i, end + 2, colVariable, true);
                    i = end + 2;
                    continue;
                }
            }

            // String literal "..."
            if (c == L'"') {
                size_t start = i;
                ++i;
                while (i < maxScan && text[i] != L'"') {
                    if (text[i] == L'\\' && i + 1 < maxScan) i += 2;
                    else ++i;
                }
                if (i < maxScan) ++i;
                size_t strEnd = i;

                // Check if followed by ':'
                size_t look = strEnd;
                while (look < maxScan && (text[look] == L' ' || text[look] == L'\t' || text[look] == L'\r' || text[look] == L'\n')) {
                    ++look;
                }
                if (look < maxScan && text[look] == L':') {
                    applyFormat(start, strEnd, colKey, false);
                } else {
                    applyFormat(start, strEnd, colString, false);
                }
                continue;
            }

            // Number: digit or negative digit
            if ((c >= L'0' && c <= L'9') || (c == L'-' && i + 1 < maxScan && text[i + 1] >= L'0' && text[i + 1] <= L'9')) {
                size_t start = i;
                if (c == L'-') ++i;
                while (i < maxScan && ((text[i] >= L'0' && text[i] <= L'9') || text[i] == L'.' || text[i] == L'e' || text[i] == L'E' || text[i] == L'+' || text[i] == L'-')) {
                    ++i;
                }
                applyFormat(start, i, colNumber, false);
                continue;
            }

            // Keywords: true, false, null
            if (text.compare(i, 4, L"true") == 0 && (i + 4 >= maxScan || !iswalnum(text[i + 4]))) {
                applyFormat(i, i + 4, colKeyword, false);
                i += 4;
                continue;
            }
            if (text.compare(i, 5, L"false") == 0 && (i + 5 >= maxScan || !iswalnum(text[i + 5]))) {
                applyFormat(i, i + 5, colKeyword, false);
                i += 5;
                continue;
            }
            if (text.compare(i, 4, L"null") == 0 && (i + 4 >= maxScan || !iswalnum(text[i + 4]))) {
                applyFormat(i, i + 4, colKeyword, false);
                i += 4;
                continue;
            }

            ++i;
        }
    }

    SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&previous));
    SendMessageW(edit, EM_HIDESELECTION, FALSE, 0);
    SendMessageW(edit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(edit, nullptr, FALSE);
}

bool richEditFindText(HWND edit, const std::wstring& query, bool forward, bool matchCase,
                      int* outCurIndex, int* outTotalCount) {
    if (!edit || query.empty()) {
        if (outCurIndex) *outCurIndex = 0;
        if (outTotalCount) *outTotalCount = 0;
        return false;
    }

    const int totalLen = GetWindowTextLengthW(edit);
    if (totalLen <= 0) {
        if (outCurIndex) *outCurIndex = 0;
        if (outTotalCount) *outTotalCount = 0;
        return false;
    }

    std::wstring text(static_cast<std::size_t>(totalLen) + 1, L'\0');
    GetWindowTextW(edit, text.data(), totalLen + 1);
    text.resize(static_cast<std::size_t>(totalLen));

    std::wstring searchLower = query;
    std::wstring textLower = text;
    if (!matchCase) {
        for (auto& ch : searchLower) ch = static_cast<wchar_t>(towlower(ch));
        for (auto& ch : textLower) ch = static_cast<wchar_t>(towlower(ch));
    }

    std::vector<size_t> matches;
    size_t pos = 0;
    while ((pos = textLower.find(searchLower, pos)) != std::wstring::npos) {
        matches.push_back(pos);
        pos += searchLower.size();
    }

    if (outTotalCount) *outTotalCount = static_cast<int>(matches.size());
    if (matches.empty()) {
        if (outCurIndex) *outCurIndex = 0;
        return false;
    }

    CHARRANGE sel = {0, 0};
    SendMessageW(edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&sel));

    int targetIdx = 0;
    if (forward) {
        auto it = std::lower_bound(matches.begin(), matches.end(), static_cast<size_t>(sel.cpMax));
        if (it != matches.end()) {
            targetIdx = static_cast<int>(std::distance(matches.begin(), it));
        } else {
            targetIdx = 0;
        }
    } else {
        int found = -1;
        for (int idx = static_cast<int>(matches.size()) - 1; idx >= 0; --idx) {
            if (matches[static_cast<size_t>(idx)] < static_cast<size_t>(sel.cpMin)) {
                found = idx;
                break;
            }
        }
        if (found >= 0) {
            targetIdx = found;
        } else {
            targetIdx = static_cast<int>(matches.size()) - 1;
        }
    }

    size_t matchPos = matches[static_cast<size_t>(targetIdx)];
    CHARRANGE targetSel = {static_cast<LONG>(matchPos), static_cast<LONG>(matchPos + query.size())};
    SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&targetSel));
    SendMessageW(edit, EM_SCROLLCARET, 0, 0);

    if (outCurIndex) *outCurIndex = targetIdx + 1;
    return true;
}

void richEditSetVariableProvider(VariableListProvider provider) {
    g_variableProvider = provider;
}

void attachVariableAutocomplete(HWND edit) {
    if (!edit) return;
    wchar_t cls[64] = {};
    GetClassNameW(edit, cls, 64);
    if (lstrcmpiW(cls, L"Edit") == 0) {
        SetWindowSubclass(edit, plainEditVarSubclass, 9999, 0);
    }
}

} // namespace ui
} // namespace lite
