#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include "ui/env_dialog.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "ui/controls.h"
#include "ui/rich_edit.h"
#include "ui/theme.h"
#include "ui/util.h"

namespace lite {
namespace ui {

namespace {

void envLog(const char* text) {
    std::ofstream out("C:\\Users\\Tauqeer\\Downloads\\postman_clone\\build\\envlog.txt", std::ios::app);
    out << text << "\n";
}

const wchar_t kEnvClass[] = L"LitePostmanEnvDialog";
const wchar_t kEnvListClass[] = L"LitePostmanEnvList";
const wchar_t kCheckClass[] = L"LitePostmanCheck";

enum {
    CKM_GETCHECK = WM_APP + 70,
    CKM_SETCHECK
};

struct EnvState;
struct EnvListState;

struct CheckState {
    bool checked = false;
    bool hover = false;
    bool tracking = false;
};

struct EnvListState {
    EnvState* env = nullptr;
    int selected = -1;
    int hover = -1;
    int scroll = 0;
    bool tracking = false;
};

struct EnvState {
    EnvironmentStore* store = nullptr;
    EnvironmentStore original;
    HWND hwnd = nullptr;
    HWND dropdown = nullptr;
    HWND newBtn = nullptr;
    HWND deleteBtn = nullptr;
    HWND list = nullptr;
    HWND keyEdit = nullptr;
    HWND valueEdit = nullptr;
    HWND secretCheck = nullptr;
    HWND addBtn = nullptr;
    HWND removeBtn = nullptr;
    HWND doneBtn = nullptr;
    HWND cancelBtn = nullptr;

    RECT keyBox = {};
    RECT valueBox = {};
    HWND focusedEdit = nullptr;
    std::wstring error;
    bool changed = false;
    bool done = false;
};

EnvListState* listState(HWND list) {
    return reinterpret_cast<EnvListState*>(GetWindowLongPtrW(list, GWLP_USERDATA));
}

Environment* selectedEnvironment(EnvState* state) {
    if (!state || !state->store || !state->dropdown) return nullptr;
    const int index = dropdownGetCurSel(state->dropdown);
    if (index <= 0) return nullptr;
    const int envIndex = index - 1;
    if (envIndex < 0 || envIndex >= static_cast<int>(state->store->environments.size())) return nullptr;
    return &state->store->environments[static_cast<std::size_t>(envIndex)];
}

void refreshList(EnvState* state) {
    if (state && state->list) InvalidateRect(state->list, nullptr, FALSE);
}

void updateActions(EnvState* state) {
    if (!state) return;
    if (state->deleteBtn) EnableWindow(state->deleteBtn, selectedEnvironment(state) != nullptr);
    if (state->removeBtn) {
        EnvListState* ls = listState(state->list);
        const Environment* env = selectedEnvironment(state);
        const bool hasRow = env && ls && ls->selected >= 0 &&
                            ls->selected < static_cast<int>(env->variables.size());
        EnableWindow(state->removeBtn, hasRow ? TRUE : FALSE);
    }
}

void refreshEnvironments(EnvState* state) {
    if (!state || !state->dropdown || !state->store) return;
    dropdownReset(state->dropdown);
    dropdownAdd(state->dropdown, L"No Environment");
    int selection = 0;
    for (std::size_t i = 0; i < state->store->environments.size(); ++i) {
        const Environment& env = state->store->environments[i];
        dropdownAdd(state->dropdown, toWide(env.name));
        if (env.id == state->store->activeId) selection = static_cast<int>(i) + 1;
    }
    dropdownSetCurSel(state->dropdown, selection);
    dropdownSetBackdrop(state->dropdown, theme().bg);
    updateActions(state);
    refreshList(state);
}

void loadSelectionIntoEditors(EnvState* state) {
    if (!state) return;
    Environment* env = selectedEnvironment(state);
    EnvListState* ls = listState(state->list);
    if (!env || !ls || ls->selected < 0 ||
        ls->selected >= static_cast<int>(env->variables.size())) {
        return;
    }
    const EnvironmentVar& variable = env->variables[static_cast<std::size_t>(ls->selected)];
    setWindowTextUtf8(state->keyEdit, variable.key);
    setWindowTextUtf8(state->valueEdit, variable.value);
    SendMessageW(state->secretCheck, CKM_SETCHECK, variable.secret ? TRUE : FALSE, 0);
    state->error.clear();
    InvalidateRect(state->hwnd, nullptr, FALSE);
}

void clearEditors(EnvState* state) {
    if (!state) return;
    SetWindowTextW(state->keyEdit, L"");
    SetWindowTextW(state->valueEdit, L"");
    SendMessageW(state->secretCheck, CKM_SETCHECK, FALSE, 0);
    state->error.clear();
    if (state->hwnd) InvalidateRect(state->hwnd, nullptr, FALSE);
}

Environment& ensureEnvironment(EnvState* state) {
    static Environment fallback;
    if (!state || !state->store) return fallback;
    if (state->store->environments.empty()) {
        Environment env;
        env.id = newId("environment");
        env.name = "Environment 1";
        state->store->activeId = env.id;
        state->store->environments.push_back(std::move(env));
        refreshEnvironments(state);
    }
    Environment* env = selectedEnvironment(state);
    if (!env) {
        env = &state->store->environments.front();
        state->store->activeId = env->id;
        refreshEnvironments(state);
    }
    return *env;
}

void addOrUpdate(EnvState* state) {
    if (!state) return;
    const std::string key = windowTextUtf8(state->keyEdit);
    if (key.empty()) {
        state->error = L"Enter a variable name before adding it.";
        InvalidateRect(state->hwnd, nullptr, FALSE);
        SetFocus(state->keyEdit);
        return;
    }

    Environment& env = ensureEnvironment(state);
    const std::string value = windowTextUtf8(state->valueEdit);
    const bool secret = SendMessageW(state->secretCheck, CKM_GETCHECK, 0, 0) == TRUE;

    EnvListState* ls = listState(state->list);
    int row = ls ? ls->selected : -1;
    if (row >= 0 && row < static_cast<int>(env.variables.size())) {
        EnvironmentVar& variable = env.variables[static_cast<std::size_t>(row)];
        variable.key = key;
        variable.value = value;
        variable.secret = secret;
        variable.enabled = true;
    } else {
        EnvironmentVar variable;
        variable.key = key;
        variable.value = value;
        variable.secret = secret;
        env.variables.push_back(std::move(variable));
        row = static_cast<int>(env.variables.size()) - 1;
    }
    if (ls) ls->selected = row;

    state->changed = true;
    state->error.clear();
    refreshList(state);
    updateActions(state);
    InvalidateRect(state->hwnd, nullptr, FALSE);
}

void removeSelected(EnvState* state) {
    if (!state) return;
    Environment* env = selectedEnvironment(state);
    EnvListState* ls = listState(state->list);
    if (!env || !ls || ls->selected < 0 ||
        ls->selected >= static_cast<int>(env->variables.size())) {
        return;
    }
    env->variables.erase(env->variables.begin() + ls->selected);
    ls->selected = -1;
    state->changed = true;
    clearEditors(state);
    refreshList(state);
    updateActions(state);
}

void newEnvironment(EnvState* state) {
    if (!state || !state->store) return;
    Environment env;
    env.id = newId("environment");
    env.name = "Environment " + std::to_string(state->store->environments.size() + 1);
    state->store->activeId = env.id;
    state->store->environments.push_back(std::move(env));
    state->changed = true;
    EnvListState* ls = listState(state->list);
    if (ls) ls->selected = -1;
    clearEditors(state);
    refreshEnvironments(state);
    updateActions(state);
}

void deleteEnvironment(EnvState* state) {
    if (!state || !state->store) return;
    Environment* env = selectedEnvironment(state);
    if (!env) return;
    if (MessageBoxW(state->hwnd, L"Delete this environment and all of its variables?",
                    L"Delete environment", MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }
    const std::string id = env->id;
    for (auto it = state->store->environments.begin(); it != state->store->environments.end(); ++it) {
        if (it->id == id) {
            state->store->environments.erase(it);
            break;
        }
    }
    if (state->store->activeId == id) state->store->activeId.clear();
    state->changed = true;
    EnvListState* ls = listState(state->list);
    if (ls) ls->selected = -1;
    clearEditors(state);
    refreshEnvironments(state);
    updateActions(state);
}

// --- Variable list ----------------------------------------------------------

void paintList(HWND hwnd, EnvListState* ls) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    const Theme& t = theme();
    const bool focused = GetFocus() == hwnd;

    fillRound(dc, rc, t.scale(10), t.surface);
    RECT border = rc;
    InflateRect(&border, -1, -1);
    strokeRound(dc, border, t.scale(10), focused ? t.accent : t.border, focused ? 2 : 1);

    const int pad = t.scale(12);
    const int headerH = t.scale(30);
    const int rowH = t.scale(34);
    const int innerLeft = pad;
    const int innerRight = static_cast<int>(rc.right) - pad;
    const int innerWidth = std::max(t.scale(60), innerRight - innerLeft);
    const int keyW = innerWidth * 36 / 100;
    const int valueW = innerWidth * 44 / 100;

    RECT keyHeader = {innerLeft, 0, innerLeft + keyW, headerH};
    RECT valueHeader = {innerLeft + keyW, 0, innerLeft + keyW + valueW, headerH};
    RECT secretHeader = {innerLeft + keyW + valueW, 0, innerRight, headerH};
    drawText(dc, L"KEY", keyHeader, t.fontSmallBold, t.textMuted,
             DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
    drawText(dc, L"VALUE", valueHeader, t.fontSmallBold, t.textMuted,
             DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
    drawText(dc, L"SECRET", secretHeader, t.fontSmallBold, t.textMuted,
             DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
    drawLine(dc, innerLeft, headerH, innerRight, headerH, t.border, 1);

    Environment* env = selectedEnvironment(ls->env);
    if (!env || env->variables.empty()) {
        RECT title = {innerLeft, headerH + t.scale(18), innerRight, headerH + t.scale(44)};
        drawText(dc, env ? L"No variables yet" : L"No environment selected",
                 title, t.fontUiMedium, t.textMuted,
                 DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
        RECT hint = {innerLeft, headerH + t.scale(44), innerRight, headerH + t.scale(68)};
        drawText(dc, env ? L"Add a key and value below to get started."
                         : L"Create an environment to start adding variables.",
                 hint, t.fontSmall, t.textFaint,
                 DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
        EndPaint(hwnd, &ps);
        return;
    }

    const int visible = std::max(0, static_cast<int>(rc.bottom) - headerH);
    const int contentHeight = static_cast<int>(env->variables.size()) * rowH;
    ls->scroll = std::max(0, std::min(ls->scroll, std::max(0, contentHeight - visible)));

    SaveDC(dc);
    IntersectClipRect(dc, 1, headerH, static_cast<int>(rc.right) - 1,
                      static_cast<int>(rc.bottom) - 1);
    for (int i = 0; i < static_cast<int>(env->variables.size()); ++i) {
        const int top = headerH + i * rowH - ls->scroll;
        if (top + rowH <= headerH || top >= static_cast<int>(rc.bottom)) continue;
        RECT row = {t.scale(6), top, static_cast<int>(rc.right) - t.scale(6), top + rowH - t.scale(2)};
        const bool selected = i == ls->selected;
        if (selected) {
            fillRound(dc, row, t.scale(6), t.accentSoft);
        } else if (i == ls->hover) {
            fillRound(dc, row, t.scale(6), t.surfaceHover);
        }

        const EnvironmentVar& variable = env->variables[static_cast<std::size_t>(i)];
        RECT keyRect = {innerLeft + t.scale(4), top, innerLeft + keyW - t.scale(8), top + rowH};
        drawText(dc, toWide(variable.key), keyRect, t.fontUiMedium,
                 selected ? t.accent : t.text,
                 DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

        const std::wstring valueText = variable.secret ? std::wstring(8, L'\u2022')
                                                       : toWide(variable.value);
        RECT valueRect = {innerLeft + keyW, top, innerLeft + keyW + valueW - t.scale(8), top + rowH};
        drawText(dc, valueText, valueRect, t.fontMonoSmall, t.textMuted,
                 DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

        if (variable.secret) {
            const std::wstring badgeText = L"secret";
            const SIZE size = measureText(dc, badgeText, t.fontSmallBold);
            RECT badge = {innerLeft + keyW + valueW, top + (rowH - t.scale(18)) / 2,
                          innerLeft + keyW + valueW + size.cx + t.scale(16),
                          top + (rowH + t.scale(18)) / 2};
            drawPill(dc, badge, badgeText, t.fontSmallBold, t.accent, t.accentSoft);
        } else {
            RECT dash = {innerLeft + keyW + valueW, top, innerRight, top + rowH};
            drawText(dc, L"\u2014", dash, t.fontSmall, t.textFaint,
                     DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
        }
    }
    RestoreDC(dc, -1);

    if (contentHeight > visible) {
        const int trackX = static_cast<int>(rc.right) - t.scale(7);
        const int thumbHeight = std::max(t.scale(24), visible * visible / contentHeight);
        const int maxScroll = contentHeight - visible;
        const int thumbY = headerH + (maxScroll > 0 ? ls->scroll * (visible - thumbHeight) / maxScroll : 0);
        RECT thumb = {trackX, thumbY, trackX + t.scale(3), thumbY + thumbHeight};
        fillRound(dc, thumb, t.scale(2), t.borderStrong);
    }

    EndPaint(hwnd, &ps);
}

int listIndexAt(EnvListState* ls, int y) {
    if (!ls || !ls->env) return -1;
    const Theme& t = theme();
    const int headerH = t.scale(30);
    if (y < headerH) return -1;
    const int index = (y - headerH + ls->scroll) / t.scale(34);
    Environment* env = selectedEnvironment(ls->env);
    if (!env || index < 0 || index >= static_cast<int>(env->variables.size())) return -1;
    return index;
}

LRESULT CALLBACK listProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    EnvListState* ls = listState(hwnd);

    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        ls = reinterpret_cast<EnvListState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ls));
        return TRUE;
    }
    case WM_NCDESTROY:
        delete ls;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        if (ls) paintList(hwnd, ls);
        return 0;
    case WM_SIZE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_MOUSEMOVE: {
        if (!ls) return 0;
        if (!ls->tracking) {
            TRACKMOUSEEVENT track = {sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&track);
            ls->tracking = true;
        }
        const int index = listIndexAt(ls, GET_Y_LPARAM(lParam));
        if (index != ls->hover) {
            ls->hover = index;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (ls) {
            ls->tracking = false;
            ls->hover = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN: {
        SetFocus(hwnd);
        if (!ls) return 0;
        const int index = listIndexAt(ls, GET_Y_LPARAM(lParam));
        ls->selected = index;
        InvalidateRect(hwnd, nullptr, FALSE);
        if (index >= 0) loadSelectionIntoEditors(ls->env);
        updateActions(ls->env);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        if (!ls) return 0;
        Environment* env = selectedEnvironment(ls->env);
        if (!env) return 0;
        RECT rc;
        GetClientRect(hwnd, &rc);
        const int visible = std::max(0, static_cast<int>(rc.bottom) - theme().scale(30));
        const int contentHeight = static_cast<int>(env->variables.size()) * theme().scale(34);
        ls->scroll -= (GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA) * theme().scale(68);
        ls->scroll = std::max(0, std::min(ls->scroll, std::max(0, contentHeight - visible)));
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_KEYDOWN: {
        if (!ls) return 0;
        Environment* env = selectedEnvironment(ls->env);
        if (!env) return 0;
        const int count = static_cast<int>(env->variables.size());
        if (wParam == VK_DOWN) {
            ls->selected = std::min(ls->selected + 1, count - 1);
            if (ls->selected >= 0) loadSelectionIntoEditors(ls->env);
            updateActions(ls->env);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (wParam == VK_UP) {
            ls->selected = std::max(ls->selected - 1, 0);
            if (count > 0) loadSelectionIntoEditors(ls->env);
            updateActions(ls->env);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (wParam == VK_DELETE) {
            removeSelected(ls->env);
            return 0;
        }
        break;
    }
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS;
    case kThemeChanged:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// --- Secret checkbox --------------------------------------------------------

void paintCheck(HWND hwnd, CheckState* state) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    const Theme& t = theme();

    fillRect(dc, rc, t.bg);

    const int boxSize = t.scale(18);
    const int top = (static_cast<int>(rc.bottom) - boxSize) / 2;
    RECT box = {t.scale(1), top, t.scale(1) + boxSize, top + boxSize};
    fillRound(dc, box, t.scale(5), state->checked ? t.accent : t.inputBg);
    strokeRound(dc, box, t.scale(5),
                state->checked ? t.accent : (state->hover ? t.borderStrong : t.border), 1);

    if (state->checked) {
        const int cx = box.left;
        const int cy = box.top;
        drawLine(dc, cx + t.scale(5), cy + t.scale(9), cx + t.scale(8), cy + t.scale(12),
                 t.onAccent, std::max(2, t.scale(2)));
        drawLine(dc, cx + t.scale(8), cy + t.scale(12), cx + t.scale(13), cy + t.scale(5),
                 t.onAccent, std::max(2, t.scale(2)));
    }

    if (GetFocus() == hwnd) {
        RECT ring = box;
        InflateRect(&ring, t.scale(3), t.scale(3));
        strokeRound(dc, ring, t.scale(7), t.accent, 1);
    }

    RECT textRect = {box.right + t.scale(9), 0, rc.right, rc.bottom};
    drawText(dc, windowTextW(hwnd), textRect, t.fontUi,
             state->checked ? t.text : t.textMuted,
             DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK checkProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    CheckState* state = reinterpret_cast<CheckState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_NCCREATE: {
        state = new CheckState();
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
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
        if (state) paintCheck(hwnd, state);
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
    case WM_LBUTTONUP:
        if (!state) return 0;
        state->checked = !state->checked;
        InvalidateRect(hwnd, nullptr, FALSE);
        SendMessageW(GetParent(hwnd), WM_COMMAND,
                     MAKEWPARAM(GetDlgCtrlID(hwnd), BN_CLICKED), reinterpret_cast<LPARAM>(hwnd));
        return 0;
    case WM_KEYDOWN:
        if (state && (wParam == VK_SPACE || wParam == VK_RETURN)) {
            state->checked = !state->checked;
            InvalidateRect(hwnd, nullptr, FALSE);
            SendMessageW(GetParent(hwnd), WM_COMMAND,
                         MAKEWPARAM(GetDlgCtrlID(hwnd), BN_CLICKED), reinterpret_cast<LPARAM>(hwnd));
            return 0;
        }
        break;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_GETDLGCODE:
        return DLGC_BUTTON;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    case CKM_SETCHECK:
        if (state) {
            state->checked = wParam != 0;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case CKM_GETCHECK:
        return state && state->checked ? TRUE : FALSE;
    case kThemeChanged:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

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

HWND createCheck(HWND parent, int id, const wchar_t* text) {
    if (!registerClass(kCheckClass, checkProc)) return nullptr;
    return CreateWindowExW(0, kCheckClass, text, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                           0, 0, 180, 26, parent,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           GetModuleHandleW(nullptr), nullptr);
}

// --- Dialog -----------------------------------------------------------------

void drawInputBox(HDC dc, const RECT& rc, bool focused) {
    const Theme& t = theme();
    fillRound(dc, rc, t.scale(8), t.inputBg);
    RECT border = rc;
    InflateRect(&border, -1, -1);
    strokeRound(dc, border, t.scale(8), focused ? t.accent : t.border, focused ? 2 : 1);
}

void paintDialog(EnvState* state) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(state->hwnd, &ps);
    RECT rc;
    GetClientRect(state->hwnd, &rc);
    const Theme& t = theme();
    const int pad = t.scale(24);

    fillRect(dc, rc, t.bg);

    RECT title = {pad, t.scale(16), static_cast<int>(rc.right) - pad, t.scale(16) + t.scale(28)};
    drawText(dc, L"Manage environments", title, t.fontTitle, t.text,
             DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

    RECT subtitle = {pad, t.scale(46), static_cast<int>(rc.right) - pad, t.scale(46) + t.scale(20)};
    drawText(dc, L"Switch the active environment and edit the variables it exposes.",
             subtitle, t.fontSmall, t.textMuted,
             DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

    RECT envLabel = {pad, t.scale(86), static_cast<int>(rc.right) - pad, t.scale(86) + t.scale(18)};
    drawText(dc, L"ENVIRONMENT", envLabel, t.fontSmallBold, t.textMuted,
             DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

    RECT varLabel = {pad, t.scale(156), static_cast<int>(rc.right) - pad, t.scale(156) + t.scale(18)};
    drawText(dc, L"VARIABLES", varLabel, t.fontSmallBold, t.textMuted,
             DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

    drawInputBox(dc, state->keyBox, state->focusedEdit == state->keyEdit);
    drawInputBox(dc, state->valueBox, state->focusedEdit == state->valueEdit);

    const int footerTop = static_cast<int>(rc.bottom) - t.scale(72);
    drawLine(dc, pad, footerTop, static_cast<int>(rc.right) - pad, footerTop, t.border, 1);

    if (!state->error.empty() && state->cancelBtn) {
        RECT error = {pad, footerTop + t.scale(18), t.scale(24) + t.scale(420),
                      footerTop + t.scale(18) + t.scale(34)};
        drawText(dc, state->error, error, t.fontSmall, t.danger,
                 DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    EndPaint(state->hwnd, &ps);
}

void placeEdit(HWND edit, const RECT& box) {
    if (!edit) return;
    const Theme& t = theme();
    MoveWindow(edit, box.left + t.scale(10), box.top + t.scale(5),
               box.right - box.left - t.scale(20), box.bottom - box.top - t.scale(10), TRUE);
}

void layoutDialog(EnvState* state) {
    if (!state || !state->hwnd) return;
    RECT rc;
    GetClientRect(state->hwnd, &rc);
    const int width = static_cast<int>(rc.right);
    const int height = static_cast<int>(rc.bottom);
    const Theme& t = theme();
    const int pad = t.scale(24);

    const int dropY = t.scale(106);
    const int dropH = t.scale(36);
    const int dropW = std::min(t.scale(340), width / 2);
    if (state->dropdown) MoveWindow(state->dropdown, pad, dropY, dropW, dropH, TRUE);

    int buttonX = pad + dropW + t.scale(10);
    const int newW = t.scale(148);
    const int deleteW = t.scale(96);
    if (state->newBtn) MoveWindow(state->newBtn, buttonX, dropY, newW, dropH, TRUE);
    buttonX += newW + t.scale(8);
    if (state->deleteBtn) MoveWindow(state->deleteBtn, buttonX, dropY, deleteW, dropH, TRUE);

    const int listY = t.scale(176);
    const int editorsH = t.scale(112);
    const int footerH = t.scale(72);
    int listH = height - listY - editorsH - footerH;
    if (listH < t.scale(120)) listH = t.scale(120);
    if (state->list) MoveWindow(state->list, pad, listY, width - pad * 2, listH, TRUE);

    const int editTop = listY + listH + t.scale(14);
    const int gap = t.scale(12);
    const int halfW = (width - pad * 2 - gap) / 2;
    state->keyBox = {pad, editTop, pad + halfW, editTop + t.scale(36)};
    state->valueBox = {pad + halfW + gap, editTop, width - pad, editTop + t.scale(36)};
    placeEdit(state->keyEdit, state->keyBox);
    placeEdit(state->valueEdit, state->valueBox);

    const int row2 = editTop + t.scale(36) + t.scale(12);
    if (state->secretCheck) {
        MoveWindow(state->secretCheck, pad, row2 + t.scale(4), t.scale(180), t.scale(26), TRUE);
    }
    const int addW = t.scale(132);
    const int removeW = t.scale(96);
    if (state->removeBtn) {
        MoveWindow(state->removeBtn, width - pad - removeW, row2, removeW, t.scale(34), TRUE);
    }
    if (state->addBtn) {
        MoveWindow(state->addBtn, width - pad - removeW - t.scale(8) - addW, row2,
                   addW, t.scale(34), TRUE);
    }

    const int footerY = height - pad - t.scale(36);
    if (state->doneBtn) MoveWindow(state->doneBtn, width - pad - t.scale(100), footerY,
                                   t.scale(100), t.scale(36), TRUE);
    if (state->cancelBtn) MoveWindow(state->cancelBtn, width - pad - t.scale(100) - t.scale(8) - t.scale(100),
                                     footerY, t.scale(100), t.scale(36), TRUE);
}

void applyFonts(EnvState* state) {
    if (!state) return;
    const Theme& t = theme();
    HWND edits[] = {state->keyEdit, state->valueEdit};
    for (HWND edit : edits) {
        if (!edit) continue;
        SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(t.fontUi), TRUE);
        SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                     MAKELONG(t.scale(2), t.scale(2)));
        themeApplyScrollbars(edit, t.dark);
    }

    HWND buttons[] = {state->newBtn, state->deleteBtn, state->addBtn,
                      state->removeBtn, state->doneBtn, state->cancelBtn};
    for (HWND button : buttons) setButtonBackdrop(button, t.bg);
    if (state->dropdown) dropdownSetBackdrop(state->dropdown, t.bg);
    if (state->secretCheck) {
        SendMessageW(state->secretCheck, WM_SETFONT, reinterpret_cast<WPARAM>(t.fontUi), TRUE);
        InvalidateRect(state->secretCheck, nullptr, FALSE);
    }
}

LRESULT CALLBACK envProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    EnvState* state = reinterpret_cast<EnvState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = reinterpret_cast<EnvState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        if (state) state->hwnd = hwnd;
        return TRUE;
    }
    case WM_CREATE: {
        envLog("WM_CREATE enter");
        if (!state) return -1;
        const HINSTANCE instance = GetModuleHandleW(nullptr);

        state->dropdown = createDropdown(hwnd, 2201);

        state->newBtn = createFlatButton(hwnd, 2202, L"New environment", kButtonSecondary);
        state->deleteBtn = createFlatButton(hwnd, 2203, L"Delete", kButtonGhost);

        auto* list = new EnvListState();
        list->env = state;
        state->list = CreateWindowExW(0, kEnvListClass, L"",
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                      0, 0, 100, 100, hwnd,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(2205)),
                                      instance, list);

        state->keyEdit = CreateWindowExW(0, WC_EDITW, L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                         0, 0, 100, 30, hwnd,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(2206)),
                                         instance, nullptr);
        SendMessageW(state->keyEdit, EM_SETCUEBANNER, TRUE,
                     reinterpret_cast<LPARAM>(L"Variable name, e.g. baseUrl"));
        state->valueEdit = CreateWindowExW(0, WC_EDITW, L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                           0, 0, 100, 30, hwnd,
                                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(2207)),
                                           instance, nullptr);
        SendMessageW(state->valueEdit, EM_SETCUEBANNER, TRUE,
                     reinterpret_cast<LPARAM>(L"Value"));
        attachVariableAutocomplete(state->valueEdit);

        state->secretCheck = createCheck(hwnd, 2208, L"Secret value");
        state->addBtn = createFlatButton(hwnd, 2209, L"Add / Update", kButtonPrimary);
        state->removeBtn = createFlatButton(hwnd, 2210, L"Remove", kButtonSecondary);
        state->doneBtn = createFlatButton(hwnd, IDOK, L"Done", kButtonPrimary);
        state->cancelBtn = createFlatButton(hwnd, IDCANCEL, L"Cancel", kButtonSecondary);

        applyFonts(state);
        refreshEnvironments(state);
        layoutDialog(state);
        envLog("WM_CREATE done");
        return 0;
    }
    case WM_SIZE:
        layoutDialog(state);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        if (state) paintDialog(state);
        return 0;
    case WM_COMMAND: {
        if (!state) return 0;
        const HWND control = reinterpret_cast<HWND>(lParam);
        const WORD notification = HIWORD(wParam);
        switch (LOWORD(wParam)) {
        case 2201:
            if (notification == CBN_SELCHANGE) {
                const int index = dropdownGetCurSel(state->dropdown);
                if (index <= 0) {
                    state->store->activeId.clear();
                } else if (index - 1 < static_cast<int>(state->store->environments.size())) {
                    state->store->activeId =
                        state->store->environments[static_cast<std::size_t>(index - 1)].id;
                }
                EnvListState* ls = listState(state->list);
                if (ls) ls->selected = -1;
                state->changed = true;
                clearEditors(state);
                refreshList(state);
                updateActions(state);
            }
            return 0;
        case 2202: newEnvironment(state); return 0;
        case 2203: deleteEnvironment(state); return 0;
        case 2209: addOrUpdate(state); return 0;
        case 2210: removeSelected(state); return 0;
        case IDOK:
            envLog("IDOK");
            state->done = true;
            DestroyWindow(hwnd);
            return 0;
        case IDCANCEL:
            envLog("IDCANCEL");
            *state->store = state->original;
            state->changed = false;
            state->done = true;
            DestroyWindow(hwnd);
            return 0;
        default:
            break;
        }

        if (notification == EN_SETFOCUS) {
            state->focusedEdit = control;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (notification == EN_KILLFOCUS) {
            if (state->focusedEdit == control) state->focusedEdit = nullptr;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (notification == EN_CHANGE && control == state->keyEdit && !state->error.empty()) {
            state->error.clear();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        return 0;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, theme().text);
        SetBkColor(dc, theme().inputBg);
        return reinterpret_cast<LRESULT>(themeBrush(theme().inputBg));
    }
    case WM_SYSCOMMAND:
        envLog("WM_SYSCOMMAND");
        break;
    case WM_CLOSE:
        envLog("WM_CLOSE");
        if (state) {
            *state->store = state->original;
            state->changed = false;
            state->done = true;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DPICHANGED: {
        themeReloadForDpi(hwnd);
        themeApplyTitleBar(hwnd, theme().dark);
        const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        applyFonts(state);
        layoutDialog(state);
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    case kThemeChanged:
        applyFonts(state);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

bool EnvDialog::show(HWND parent, EnvironmentStore& store) {
    envLog("show enter");
    if (!registerClass(kEnvClass, envProc)) return false;
    if (!registerClass(kEnvListClass, listProc)) return false;
    if (!registerClass(kCheckClass, checkProc)) return false;
    envLog("classes registered");

    EnvState state;
    state.store = &store;
    state.original = store;

    const Theme& t = theme();
    const int width = t.scale(900);
    const int height = t.scale(660);

    RECT ownerRect = {0, 0, 900, 660};
    if (parent) GetWindowRect(parent, &ownerRect);
    const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
    const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;

    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kEnvClass, L"Manage Environments",
                                WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                x, y, width, height, parent, nullptr,
                                GetModuleHandleW(nullptr), &state);
    if (!hwnd) {
        envLog("window create FAILED");
        return false;
    }

    envLog("window created");
    themeApplyTitleBar(hwnd, theme().dark);

    if (parent) EnableWindow(parent, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetFocus(state.dropdown);
    envLog("window shown");

    MSG msg;
    while (!state.done && GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_CLOSE) envLog("loop WM_CLOSE");
        else if (msg.message == WM_SYSCOMMAND) envLog("loop WM_SYSCOMMAND");
        else if (msg.message == WM_COMMAND) envLog("loop WM_COMMAND");
        else if (msg.message == WM_KEYDOWN) envLog("loop WM_KEYDOWN");
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    envLog(state.done ? "loop ended done" : "loop ended quit");

    if (parent) {
        EnableWindow(parent, TRUE);
        SetActiveWindow(parent);
    }
    return state.changed;
}

} // namespace ui
} // namespace lite
