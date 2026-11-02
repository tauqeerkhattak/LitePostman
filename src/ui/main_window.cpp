#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include "ui/main_window.h"

#include <commctrl.h>
#include <commdlg.h>
#include <windowsx.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <memory>
#include <string>
#include <thread>
#include <unordered_set>

#include "core/curl_generator.h"
#include "core/curl_parser.h"
#include "core/models.h"
#include "ui/controls.h"
#include "ui/curl_dialogs.h"
#include "ui/env_dialog.h"
#include "ui/rich_edit.h"
#include "ui/theme.h"
#include "ui/util.h"

namespace lite {
namespace ui {

namespace {

const wchar_t kClassName[] = L"LitePostmanMainWindow";
const wchar_t kPromptClass[] = L"LitePostmanPrompt";
const UINT kRequestFinished = WM_APP + 1;

// --- Save prompt ------------------------------------------------------------

struct PromptState {
    HWND hwnd = nullptr;
    HWND edit = nullptr;
    HWND save = nullptr;
    HWND cancel = nullptr;
    std::wstring label;
    std::wstring value;
    RECT input = {};
    bool done = false;
    bool accepted = false;
};

void paintPrompt(PromptState* state) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(state->hwnd, &ps);
    RECT rc;
    GetClientRect(state->hwnd, &rc);
    const Theme& t = theme();

    fillRect(dc, rc, t.surface);

    RECT label = {t.scale(20), t.scale(18), rc.right - t.scale(20), t.scale(38)};
    drawText(dc, state->label, label, t.fontSmallBold, t.textMuted,
             DT_SINGLELINE | DT_TOP | DT_NOPREFIX);

    fillRound(dc, state->input, t.scale(8), t.inputBg);
    RECT border = state->input;
    InflateRect(&border, -1, -1);
    const bool focused = GetFocus() == state->edit;
    strokeRound(dc, border, t.scale(8), focused ? t.accent : t.border, focused ? 2 : 1);

    EndPaint(state->hwnd, &ps);
}

LRESULT CALLBACK promptProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    PromptState* state = reinterpret_cast<PromptState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = reinterpret_cast<PromptState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        if (state) state->hwnd = hwnd;
        return TRUE;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        if (state) paintPrompt(state);
        return 0;
    case WM_COMMAND:
        if (!state) return 0;
        if (LOWORD(wParam) == IDOK) {
            state->value = windowTextW(state->edit);
            state->accepted = true;
            state->done = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            state->done = true;
            DestroyWindow(hwnd);
            return 0;
        }
        return 0;
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, theme().text);
        SetBkColor(dc, theme().inputBg);
        return reinterpret_cast<LRESULT>(themeBrush(theme().inputBg));
    }
    case WM_CLOSE:
        if (state) state->done = true;
        DestroyWindow(hwnd);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

[[maybe_unused]] bool promptForText(HWND owner, const wchar_t* title, const wchar_t* label, std::wstring& value) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = promptProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = kPromptClass;
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        registered = true;
    }

    const Theme& t = theme();
    const int width = t.scale(470);
    const int height = t.scale(206);

    RECT ownerRect = {0, 0, 800, 600};
    if (owner) GetWindowRect(owner, &ownerRect);
    const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
    const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;

    PromptState state;
    state.label = label;

    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kPromptClass, title,
                                WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                x, y, width, height, owner, nullptr, GetModuleHandleW(nullptr), &state);
    if (!hwnd) return false;

    themeApplyTitleBar(hwnd, theme().dark);

    const int editX = t.scale(20);
    const int editY = t.scale(56);
    const int editW = width - t.scale(40);
    const int editH = t.scale(34);
    state.input = {editX, editY, editX + editW, editY + editH};

    state.edit = CreateWindowExW(0, WC_EDITW, value.c_str(),
                                 WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                 editX + t.scale(10), editY + t.scale(4),
                                 editW - t.scale(20), editH - t.scale(8),
                                 hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(5001)),
                                 GetModuleHandleW(nullptr), nullptr);
    SendMessageW(state.edit, WM_SETFONT, reinterpret_cast<WPARAM>(t.fontUi), TRUE);
    SendMessageW(state.edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELONG(t.scale(2), t.scale(2)));
    attachVariableAutocomplete(state.edit);

    const int buttonW = t.scale(88);
    const int buttonH = t.scale(34);
    const int buttonY = height - t.scale(20) - buttonH;
    state.cancel = createFlatButton(hwnd, IDCANCEL, L"Cancel", kButtonSecondary);
    state.save = createFlatButton(hwnd, IDOK, L"Save", kButtonPrimary);
    MoveWindow(state.cancel, width - t.scale(20) - buttonW, buttonY, buttonW, buttonH, TRUE);
    MoveWindow(state.save, width - t.scale(20) - buttonW * 2 - t.scale(8), buttonY,
               buttonW, buttonH, TRUE);
    setButtonBackdrop(state.cancel, t.surface);
    setButtonBackdrop(state.save, t.surface);

    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    SetFocus(state.edit);
    SendMessageW(state.edit, EM_SETSEL, 0, -1);

    MSG msg;
    while (!state.done && GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
            SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
            continue;
        }
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (owner) {
        EnableWindow(owner, TRUE);
        SetActiveWindow(owner);
    }
    if (state.accepted) {
        value = state.value;
        return !value.empty();
    }
    return false;
}

} // namespace

static MainWindow* g_mainWindowInstance = nullptr;

static std::vector<std::pair<std::wstring, std::wstring>> getActiveVariablesForMenu() {
    if (!g_mainWindowInstance) return {};
    return g_mainWindowInstance->collectVariablesForMenu();
}

MainWindow::MainWindow(HINSTANCE instance) : instance_(instance) {
    storage_.initialize();
    loadState();
}

MainWindow::~MainWindow() {
    if (g_mainWindowInstance == this) {
        g_mainWindowInstance = nullptr;
        richEditSetVariableProvider(nullptr);
    }
    if (busy_ && cancel_) cancel_->cancel();
    joinWorker();
    if (accel_) DestroyAcceleratorTable(accel_);
    if (hwnd_) DestroyWindow(hwnd_);
}

bool MainWindow::darkFromSettings() const {
    switch (settings_.theme) {
    case ThemeMode::Dark: return true;
    case ThemeMode::Light: return false;
    default: return systemPrefersDark();
    }
}

bool MainWindow::create() {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWindow::wndProc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClassName;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);

    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    int x = (settings_.windowX < 0) ? CW_USEDEFAULT : settings_.windowX;
    int y = (settings_.windowY < 0) ? CW_USEDEFAULT : settings_.windowY;
    int w = (settings_.windowWidth > 500) ? settings_.windowWidth : 1280;
    int h = (settings_.windowHeight > 400) ? settings_.windowHeight : 820;

    // Never restore a frame larger than (or outside) the monitor work area.
    POINT anchor = {0, 0};
    if (x != CW_USEDEFAULT && y != CW_USEDEFAULT) {
        anchor.x = x;
        anchor.y = y;
    }
    HMONITOR monitor = MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    if (monitor && GetMonitorInfoW(monitor, &info)) {
        const RECT& work = info.rcWork;
        const int workW = work.right - work.left;
        const int workH = work.bottom - work.top;
        if (w > workW) w = workW;
        if (h > workH) h = workH;
        if (x == CW_USEDEFAULT || y == CW_USEDEFAULT) {
            x = work.left + (workW - w) / 2;
            y = work.top + (workH - h) / 2;
        } else {
            if (x < work.left) x = work.left;
            if (y < work.top) y = work.top;
            if (x + w > work.right) x = work.right - w;
            if (y + h > work.bottom) y = work.bottom - h;
        }
    }

    hwnd_ = CreateWindowExW(0, kClassName, L"LitePostman", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                            x, y, w, h, nullptr, nullptr, instance_, this);
    if (!hwnd_) return false;

    g_mainWindowInstance = this;
    richEditSetVariableProvider(&getActiveVariablesForMenu);

    themeInit(hwnd_);
    themeSetDark(darkFromSettings());
    themeApplyTitleBar(hwnd_, theme().dark);

    sidebar_.create(hwnd_, 1001);
    requestView_.create(hwnd_, 1002);
    responseView_.create(hwnd_, 1003);
    sidebar_.setSelectHandler(&MainWindow::sidebarSelectTrampoline, this);
    sidebar_.setCollections(collections_);

    envChip_ = createFlatButton(hwnd_, 40102, L"\u25CF No environment", kButtonSecondary);
    importBtn_ = createFlatButton(hwnd_, 40002, L"Import", kButtonGhost);
    exportBtn_ = createFlatButton(hwnd_, 40003, L"Export", kButtonGhost);
    themeBtn_ = createFlatButton(hwnd_, 40100, L"\u263E", kButtonGhost);

    applyChrome();

    if (!settings_.tabs.empty()) {
        int index = settings_.activeTab;
        if (index < 0 || index >= static_cast<int>(settings_.tabs.size())) index = 0;
        const TabState& tab = settings_.tabs[static_cast<std::size_t>(index)];

        bool restored = false;
        if (!tab.nodeId.empty()) {
            for (const auto& collection : collections_) {
                if (collection.id != tab.collectionId) continue;
                const CollectionNode* node = findNode(collection, tab.nodeId);
                if (node && node->type == CollectionNode::Type::Request) {
                    currentCollectionId_ = collection.id;
                    currentNodeId_ = node->id;
                    requestView_.setRequest(node->request);
                    sidebar_.selectNode(currentCollectionId_, currentNodeId_);
                    restored = true;
                }
                break;
            }
        }
        if (!restored) {
            requestView_.setRequest(tab.request);
        }
    }

    if (currentNodeId_.empty() && !settings_.activeCollectionId.empty()) {
        for (const auto& collection : collections_) {
            if (collection.id == settings_.activeCollectionId) {
                currentCollectionId_ = collection.id;
                sidebar_.selectNode(currentCollectionId_, std::string());
                break;
            }
        }
    }

    ACCEL accels[] = {
        {FVIRTKEY, VK_F5, 40001},
        {static_cast<BYTE>(FVIRTKEY | FCONTROL), 'N', 40007},
        {static_cast<BYTE>(FVIRTKEY | FCONTROL), 'S', 40006},
        {static_cast<BYTE>(FVIRTKEY | FCONTROL), VK_RETURN, 40001},
        {static_cast<BYTE>(FVIRTKEY | FCONTROL), 'I', 40002},
        {static_cast<BYTE>(FVIRTKEY | FCONTROL), 'E', 40003},
    };
    accel_ = CreateAcceleratorTableW(accels, static_cast<int>(std::size(accels)));

    updateTitle();
    layout();

    if (settings_.maximized) ShowWindow(hwnd_, SW_SHOWMAXIMIZED);
    requestView_.focusUrl();
    return true;
}

void MainWindow::show(int cmdShow) {
    if (!hwnd_) return;
    ShowWindow(hwnd_, cmdShow);
    UpdateWindow(hwnd_);
}

bool MainWindow::preTranslate(MSG* message) {
    return accel_ && TranslateAcceleratorW(hwnd_, accel_, message) != 0;
}

void MainWindow::applyChrome() {
    const Theme& t = theme();
    if (envChip_) setButtonBackdrop(envChip_, t.bg);
    if (importBtn_) setButtonBackdrop(importBtn_, t.bg);
    if (exportBtn_) setButtonBackdrop(exportBtn_, t.bg);
    if (themeBtn_) {
        setButtonBackdrop(themeBtn_, t.bg);
        SetWindowTextW(themeBtn_, t.dark ? L"\u2600" : L"\u263E");
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::broadcastTheme() {
    EnumChildWindows(hwnd_, [](HWND child, LPARAM) -> BOOL {
        SendMessageW(child, kThemeChanged, 0, 0);
        return TRUE;
    }, 0);
}

void MainWindow::toggleTheme() {
    const bool dark = !theme().dark;
    themeSetDark(dark);
    settings_.theme = dark ? ThemeMode::Dark : ThemeMode::Light;
    themeApplyTitleBar(hwnd_, dark);
    applyChrome();
    broadcastTheme();
    layout();
    saveState();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void MainWindow::layout() {
    if (!hwnd_) return;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const int width = rc.right;
    const int height = rc.bottom;
    const Theme& t = theme();

    const int headerH = t.scale(56);
    const int gap = t.scale(12);

    int sidebarWidth = settings_.sidebarWidth > 0 ? settings_.sidebarWidth : t.scale(272);
    sidebarWidth = std::max(t.scale(220), std::min(sidebarWidth, width - t.scale(520)));
    sidebar_.resize(0, headerH, sidebarWidth, height - headerH);

    const int contentX = sidebarWidth + gap;
    const int contentW = width - contentX - gap;
    const int contentY = headerH + gap;

    int responseHeight = settings_.responseHeight > 0 ? settings_.responseHeight : t.scale(290);
    responseHeight = std::max(t.scale(160), std::min(responseHeight, height - contentY - t.scale(300)));
    int requestHeight = height - contentY - responseHeight - gap - gap;
    if (requestHeight < t.scale(220)) {
        requestHeight = t.scale(220);
        responseHeight = std::max(t.scale(120), height - contentY - requestHeight - gap - gap);
    }

    requestView_.resize(contentX, contentY, contentW, requestHeight);
    responseView_.resize(contentX, contentY + requestHeight + gap, contentW, responseHeight);

    const int buttonH = t.scale(32);
    const int buttonY = (headerH - buttonH) / 2;
    const int themeW = t.scale(40);
    const int importW = t.scale(78);
    const int exportW = t.scale(78);
    int right = width - t.scale(16);

    if (themeBtn_) {
        MoveWindow(themeBtn_, right - themeW, buttonY, themeW, buttonH, TRUE);
        right -= themeW + t.scale(4);
    }
    if (exportBtn_) {
        MoveWindow(exportBtn_, right - exportW, buttonY, exportW, buttonH, TRUE);
        right -= exportW + t.scale(4);
    }
    if (importBtn_) {
        MoveWindow(importBtn_, right - importW, buttonY, importW, buttonH, TRUE);
        right -= importW + t.scale(12);
    }
    if (envChip_) {
        const int envW = t.scale(220);
        const int envX = right - envW;
        MoveWindow(envChip_, envX, buttonY, envW, buttonH, TRUE);
    }
}

void MainWindow::paint() {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd_, &ps);
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const Theme& t = theme();

    fillRect(dc, rc, t.bg);

    const int headerH = t.scale(56);
    drawLine(dc, 0, headerH - 1, rc.right, headerH - 1, t.border, 1);

    RECT logo = {t.scale(20), (headerH - t.scale(30)) / 2,
                 t.scale(20) + t.scale(30), (headerH + t.scale(30)) / 2};
    fillRound(dc, logo, t.scale(9), t.accent);
    drawText(dc, L"L", logo, t.fontUiBold, t.onAccent,
             DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);

    RECT title = {logo.right + t.scale(12), 0, logo.right + t.scale(220), headerH};
    drawText(dc, L"LitePostman", title, t.fontTitle, t.text,
             DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

    const SIZE titleSize = measureText(dc, L"LitePostman", t.fontTitle);
    RECT version = {logo.right + t.scale(12) + titleSize.cx + t.scale(8), 0,
                    logo.right + t.scale(12) + titleSize.cx + t.scale(80), headerH};
    drawText(dc, L"v1.0", version, t.fontSmall, t.textFaint,
             DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

    EndPaint(hwnd_, &ps);
}

void MainWindow::updateTitle() {
    const Environment* active = environments_.active();
    std::wstring title = L"LitePostman";
    if (active) title += L" - " + toWide(active->name);
    if (hwnd_) SetWindowTextW(hwnd_, title.c_str());

    std::wstring chip = L"\u25CF ";
    chip += active ? toWide(active->name) : L"No environment";
    if (envChip_) SetWindowTextW(envChip_, chip.c_str());
    sidebar_.setEnvironment(active ? toWide(active->name) : L"No environment", active != nullptr);
}

void MainWindow::onSend() {
    if (busy_) return;

    saveCurrentRequest(false);

    const RequestModel request = requestView_.getRequest();
    if (request.url.empty()) {
        MessageBoxW(hwnd_, L"Enter a request URL first.", L"LitePostman", MB_OK | MB_ICONINFORMATION);
        requestView_.focusUrl();
        return;
    }

    const VariableMap global = variablesToMap(settings_.globalVariables);
    Collection* collection = activeCollection();
    const VariableMap collectionVariables =
        variablesToMap(collection ? collection->variables : std::vector<KeyValue>());
    const VariableContext context = makeVariableContext(global, collectionVariables, environments_);

    PreparedRequest prepared;
    std::string error;
    if (!prepareRequest(request, context, prepared, &error)) {
        responseView_.setError("Request error", error);
        return;
    }

    HttpRequest httpRequest;
    httpRequest.method = prepared.method;
    httpRequest.url = prepared.url;
    httpRequest.body = prepared.body;
    for (const auto& header : prepared.headers) {
        httpRequest.headers.emplace_back(header.key, header.value);
    }
    httpRequest.options = request.options;

    cancel_ = std::make_shared<CancelToken>();
    const std::shared_ptr<CancelToken> token = cancel_;
    busy_ = true;
    requestView_.setBusy(true);
    responseView_.setLoading();

    worker_ = std::thread([this, httpRequest, token]() {
        auto* result = new HttpResponse(HttpClient::execute(httpRequest, *token));
        if (hwnd_) {
            PostMessageW(hwnd_, kRequestFinished, 0, reinterpret_cast<LPARAM>(result));
        } else {
            delete result;
        }
    });
}

void MainWindow::joinWorker() {
    if (worker_.joinable()) worker_.join();
}

void MainWindow::onRequestFinished(HttpResponse* response) {
    std::unique_ptr<HttpResponse> owned(response);
    joinWorker();
    busy_ = false;
    requestView_.setBusy(false);
    if (!owned) return;

    const HttpResponse& resp = *owned;
    if (!resp.error.empty() && resp.statusCode == 0) {
        responseView_.setError(resp.canceled ? "Cancelled" : "Request failed", resp.error);
        return;
    }

    std::string status = std::to_string(resp.statusCode);
    if (!resp.reason.empty()) status += " " + resp.reason;
    const std::string meta = formatElapsed(resp.elapsedMs) + "   \xC2\xB7   " + formatBytes(resp.body.size());

    std::string headers;
    for (const auto& header : resp.headers) {
        headers += header.first + ": " + header.second + "\n";
    }
    if (headers.empty()) headers = "No response headers.";

    std::string cookies;
    for (const auto& cookie : resp.cookies) {
        cookies += cookie.name + " = " + cookie.value + "\n";
        std::string attributes;
        if (!cookie.domain.empty()) attributes += "  domain=" + cookie.domain;
        if (!cookie.path.empty()) attributes += "  path=" + cookie.path;
        if (!cookie.expires.empty()) attributes += "  expires=" + cookie.expires;
        if (cookie.secure) attributes += "  secure";
        if (cookie.httpOnly) attributes += "  httpOnly";
        if (!attributes.empty()) cookies += attributes + "\n";
        cookies += "\n";
    }
    if (cookies.empty()) cookies = "No cookies were set by the server.";

    responseView_.setResponse(status, meta, resp.body, headers, cookies, resp.statusCode);

    RequestModel request = requestView_.getRequest();
    HistoryEntry entry;
    entry.request = request;
    entry.sentAt = static_cast<std::int64_t>(std::time(nullptr));
    entry.statusCode = resp.statusCode;
    entry.elapsedMs = static_cast<int>(resp.elapsedMs);
    history_.push_back(entry);
    if (history_.size() > 100) history_.erase(history_.begin());
    saveState();
}

Collection* MainWindow::activeCollection() {
    const std::string& id = !currentCollectionId_.empty() ? currentCollectionId_
                                                         : settings_.activeCollectionId;
    for (auto& collection : collections_) {
        if (collection.id == id) return &collection;
    }
    if (!collections_.empty()) return &collections_.front();
    return nullptr;
}

bool MainWindow::saveCurrentRequest(bool createIfMissing) {
    RequestModel request = requestView_.getRequest();
    if (request.name.empty()) request.name = "Untitled Request";

    if (!currentNodeId_.empty()) {
        Collection* collection = activeCollection();
        if (collection) {
            CollectionNode* node = findNode(*collection, currentNodeId_);
            if (node && node->type == CollectionNode::Type::Request) {
                node->request = request;
                node->name = request.name;
                storage_.saveCollection(*collection, nullptr);
                refreshSidebar();
                return true;
            }
        }
    }

    if (!createIfMissing) return false;

    Collection* collection = activeCollection();
    if (!collection) {
        Collection created;
        created.id = newId("collection");
        created.name = "My Collection";
        collections_.push_back(std::move(created));
        collection = &collections_.back();
    }

    CollectionNode node;
    node.id = newId("request");
    node.name = request.name;
    node.type = CollectionNode::Type::Request;
    node.request = request;
    const std::string nodeId = node.id;
    insertNode(*collection, std::string(), std::move(node), -1);

    currentCollectionId_ = collection->id;
    currentNodeId_ = nodeId;
    settings_.activeCollectionId = collection->id;

    storage_.saveCollection(*collection, nullptr);
    refreshSidebar();
    sidebar_.selectNode(currentCollectionId_, currentNodeId_);
    return true;
}

void MainWindow::onNewRequest() {
    saveCurrentRequest(false);

    Collection* collection = activeCollection();
    if (!collection) {
        Collection created;
        created.id = newId("collection");
        created.name = "My Collection";
        collections_.push_back(std::move(created));
        collection = &collections_.back();
    }

    RequestModel request;
    request.name = "Untitled Request";

    CollectionNode node;
    node.id = newId("request");
    node.name = request.name;
    node.type = CollectionNode::Type::Request;
    node.request = request;
    const std::string nodeId = node.id;
    insertNode(*collection, std::string(), std::move(node), -1);

    currentCollectionId_ = collection->id;
    currentNodeId_ = nodeId;
    settings_.activeCollectionId = collection->id;

    storage_.saveCollection(*collection, nullptr);
    refreshSidebar();
    sidebar_.selectNode(currentCollectionId_, currentNodeId_);
    requestView_.setRequest(request);
    requestView_.focusUrl();
}

void MainWindow::onSaveRequest() {
    // Ctrl+S is a silent save. Requests are created from the sidebar "+".
    saveCurrentRequest(false);
}

void MainWindow::sidebarSelectTrampoline(void* context, const std::string& collectionId, const std::string& nodeId) {
    static_cast<MainWindow*>(context)->onSidebarSelect(collectionId, nodeId);
}

void MainWindow::onSidebarSelect(const std::string& collectionId, const std::string& nodeId) {
    settings_.activeCollectionId = collectionId;
    if (nodeId.empty()) return; // folder/collection row: keep the open request
    if (collectionId == currentCollectionId_ && nodeId == currentNodeId_) return;

    saveCurrentRequest(false);

    const CollectionNode* selected = nullptr;
    for (const auto& collection : collections_) {
        if (collection.id != collectionId) continue;
        const CollectionNode* node = findNode(collection, nodeId);
        if (node && node->type == CollectionNode::Type::Request) selected = node;
        break;
    }
    if (!selected) return;

    currentCollectionId_ = collectionId;
    currentNodeId_ = nodeId;
    requestView_.setRequest(selected->request);
    requestView_.focusUrl();
}

void MainWindow::refreshSidebar() {
    sidebar_.setCollections(collections_);
}

void MainWindow::onImportCurl() {
    const std::string clip = CurlDialogs::importCurl(hwnd_);
    if (clip.empty()) {
        MessageBoxW(hwnd_, L"The clipboard does not contain text.", L"cURL import", MB_OK | MB_ICONINFORMATION);
        return;
    }
    const CurlImportResult result = parseCurl(clip, CurlShell::Auto);
    if (result.ok) {
        requestView_.setRequest(result.request);
        requestView_.focusUrl();
    } else {
        const std::string message = !result.error.empty() ? result.error
                                  : (!result.warnings.empty() ? result.warnings.front()
                                                              : std::string("The clipboard text is not a cURL command."));
        MessageBoxW(hwnd_, toWide(message).c_str(), L"cURL import", MB_OK | MB_ICONWARNING);
    }
}

void MainWindow::onExportCurl() {
    const RequestModel request = requestView_.getRequest();
    const VariableMap global = variablesToMap(settings_.globalVariables);
    const VariableMap collection = variablesToMap(std::vector<KeyValue>());
    const VariableContext context = makeVariableContext(global, collection, environments_);

    CurlExportOptions options;
    options.target = CurlTarget::Bash;
    const CurlExportResult result = generateCurl(request, context, options);
    if (CurlDialogs::exportCurl(hwnd_, result.command)) {
        MessageBoxW(hwnd_, L"Copied the request as a cURL command.", L"cURL export", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(hwnd_, L"Could not access the clipboard.", L"cURL export", MB_OK | MB_ICONWARNING);
    }
}

void MainWindow::onManageEnvs() {
    if (EnvDialog::show(hwnd_, environments_)) {
        storage_.saveEnvironments(environments_, nullptr);
        updateTitle();
    }
}

void MainWindow::onEnvChipClick() {
    HMENU menu = CreatePopupMenu();
    UINT noEnvFlags = MF_STRING;
    if (environments_.activeId.empty()) noEnvFlags |= MF_CHECKED;
    AppendMenuW(menu, noEnvFlags, 50000, L"No environment");

    for (std::size_t i = 0; i < environments_.environments.size(); ++i) {
        UINT flags = MF_STRING;
        if (environments_.environments[i].id == environments_.activeId) flags |= MF_CHECKED;
        AppendMenuW(menu, flags, 50001 + i, toWide(environments_.environments[i].name).c_str());
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 59999, L"Manage environments...");

    RECT rc;
    GetWindowRect(envChip_, &rc);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, rc.left, rc.bottom, 0, hwnd_, nullptr);
    DestroyMenu(menu);

    if (cmd == 50000) {
        environments_.activeId.clear();
        storage_.saveEnvironments(environments_, nullptr);
        updateTitle();
    } else if (cmd >= 50001 && cmd < 50001 + static_cast<int>(environments_.environments.size())) {
        environments_.activeId = environments_.environments[cmd - 50001].id;
        storage_.saveEnvironments(environments_, nullptr);
        updateTitle();
    } else if (cmd == 59999) {
        onManageEnvs();
    }
}

void MainWindow::onImportClick() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"Import Collection (.json)...");
    AppendMenuW(menu, MF_STRING, 2, L"Import from cURL (Clipboard)");

    RECT rc;
    GetWindowRect(importBtn_, &rc);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, rc.left, rc.bottom, 0, hwnd_, nullptr);
    DestroyMenu(menu);

    if (cmd == 1) onImportCollectionJson();
    else if (cmd == 2) onImportCurl();
}

void MainWindow::onExportClick() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"Export Active Collection (.json)...");
    AppendMenuW(menu, MF_STRING, 2, L"Export Request as cURL (Clipboard)");

    RECT rc;
    GetWindowRect(exportBtn_, &rc);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, rc.left, rc.bottom, 0, hwnd_, nullptr);
    DestroyMenu(menu);

    if (cmd == 1) onExportCollection();
    else if (cmd == 2) onExportCurl();
}

void MainWindow::onImportCollectionJson() {
    wchar_t filename[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(OPENFILENAMEW);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = L"JSON Files (*.json)\0*.json\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (!GetOpenFileNameW(&ofn)) return;

    FILE* fp = _wfopen(filename, L"rb");
    if (!fp) {
        MessageBoxW(hwnd_, L"Failed to open the selected file.", L"Import Collection", MB_OK | MB_ICONWARNING);
        return;
    }
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    std::string content(static_cast<std::size_t>(size), '\0');
    if (size > 0) fread(&content[0], 1, static_cast<std::size_t>(size), fp);
    fclose(fp);

    std::string parseErr;
    Json json = Json::parse(content, &parseErr);
    if (!parseErr.empty() || !json.isObject()) {
        MessageBoxW(hwnd_, L"Invalid JSON file format.", L"Import Collection", MB_OK | MB_ICONWARNING);
        return;
    }

    Collection col = collectionFromJson(json);
    col.id = newId("collection");
    if (col.name.empty()) col.name = "Imported Collection";
    collections_.push_back(col);
    storage_.saveCollection(col, nullptr);
    currentCollectionId_ = col.id;
    refreshSidebar();
    updateTitle();

    std::wstring msg = L"Collection \"" + toWide(col.name) + L"\" imported successfully!";
    MessageBoxW(hwnd_, msg.c_str(), L"Import Collection", MB_OK | MB_ICONINFORMATION);
}

void MainWindow::onExportCollection() {
    std::string targetColId, targetNodeId;
    sidebar_.getContextTarget(targetColId, targetNodeId);
    if (targetColId.empty()) targetColId = currentCollectionId_;

    Collection* target = nullptr;
    for (auto& col : collections_) {
        if (col.id == targetColId) { target = &col; break; }
    }
    if (!target) target = activeCollection();
    if (!target) {
        MessageBoxW(hwnd_, L"No collection available to export.", L"Export Collection", MB_OK | MB_ICONINFORMATION);
        return;
    }

    wchar_t filename[MAX_PATH] = {};
    std::wstring defName = toWide(target->name) + L".json";
    wcsncpy(filename, defName.c_str(), MAX_PATH - 1);

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(OPENFILENAMEW);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = L"JSON Files (*.json)\0*.json\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"json";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

    if (!GetSaveFileNameW(&ofn)) return;

    Json json = collectionToJson(*target);
    std::string serialized = json.dump(2);

    FILE* fp = _wfopen(filename, L"wb");
    if (!fp) {
        MessageBoxW(hwnd_, L"Failed to write to file.", L"Export Collection", MB_OK | MB_ICONWARNING);
        return;
    }
    fwrite(serialized.data(), 1, serialized.size(), fp);
    fclose(fp);

    std::wstring msg = L"Collection \"" + toWide(target->name) + L"\" exported successfully!";
    MessageBoxW(hwnd_, msg.c_str(), L"Export Collection", MB_OK | MB_ICONINFORMATION);
}

void MainWindow::onNewCollection() {
    std::wstring name = L"New Collection";
    if (promptForText(hwnd_, L"New Collection", L"Collection name:", name)) {
        Collection col;
        col.id = newId("collection");
        col.name = toUtf8(name);
        collections_.push_back(col);
        storage_.saveCollection(col, nullptr);
        currentCollectionId_ = col.id;
        refreshSidebar();
        updateTitle();
    }
}

void MainWindow::onRenameCollection() {
    std::string targetColId, targetNodeId;
    sidebar_.getContextTarget(targetColId, targetNodeId);
    if (targetColId.empty()) targetColId = currentCollectionId_;

    Collection* target = nullptr;
    for (auto& col : collections_) {
        if (col.id == targetColId) { target = &col; break; }
    }
    if (!target) return;

    std::wstring name = toWide(target->name);
    if (promptForText(hwnd_, L"Rename Collection", L"Collection name:", name)) {
        target->name = toUtf8(name);
        storage_.saveCollection(*target, nullptr);
        refreshSidebar();
        updateTitle();
    }
}

void MainWindow::onDeleteCollection() {
    std::string targetColId, targetNodeId;
    sidebar_.getContextTarget(targetColId, targetNodeId);
    if (targetColId.empty()) targetColId = currentCollectionId_;

    Collection* target = nullptr;
    auto it = collections_.end();
    for (auto i = collections_.begin(); i != collections_.end(); ++i) {
        if (i->id == targetColId) { target = &(*i); it = i; break; }
    }
    if (!target) return;

    std::wstring prompt = L"Are you sure you want to delete the collection \"" + toWide(target->name) + L"\"?";
    if (MessageBoxW(hwnd_, prompt.c_str(), L"Delete Collection", MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }

    std::string colId = target->id;
    storage_.deleteCollection(colId, nullptr);
    collections_.erase(it);

    if (currentCollectionId_ == colId) {
        currentCollectionId_.clear();
        currentNodeId_.clear();
    }
    refreshSidebar();
    updateTitle();
}

void MainWindow::onRenameRequest() {
    std::string targetColId, targetNodeId;
    sidebar_.getContextTarget(targetColId, targetNodeId);
    if (targetColId.empty()) targetColId = currentCollectionId_;
    if (targetNodeId.empty()) targetNodeId = currentNodeId_;

    for (auto& col : collections_) {
        if (col.id != targetColId) continue;
        CollectionNode* node = findNode(col, targetNodeId);
        if (node && node->type == CollectionNode::Type::Request) {
            std::wstring name = toWide(node->name);
            if (promptForText(hwnd_, L"Rename Request", L"Request name:", name)) {
                node->name = toUtf8(name);
                node->request.name = node->name;
                storage_.saveCollection(col, nullptr);
                refreshSidebar();
                if (col.id == currentCollectionId_ && node->id == currentNodeId_) {
                    requestView_.setRequest(node->request);
                }
            }
        }
        break;
    }
}

void MainWindow::onDuplicateRequest() {
    std::string targetColId, targetNodeId;
    sidebar_.getContextTarget(targetColId, targetNodeId);
    if (targetColId.empty()) targetColId = currentCollectionId_;
    if (targetNodeId.empty()) targetNodeId = currentNodeId_;

    for (auto& col : collections_) {
        if (col.id != targetColId) continue;
        const CollectionNode* node = findNode(col, targetNodeId);
        if (node && node->type == CollectionNode::Type::Request) {
            CollectionNode dup = *node;
            dup.id = newId("request");
            dup.name += " (Copy)";
            dup.request.name = dup.name;
            const std::string newId = dup.id;
            insertNode(col, std::string(), std::move(dup), -1);
            storage_.saveCollection(col, nullptr);
            currentCollectionId_ = col.id;
            currentNodeId_ = newId;
            refreshSidebar();
            sidebar_.selectNode(currentCollectionId_, currentNodeId_);
            const CollectionNode* newNode = findNode(col, newId);
            if (newNode) requestView_.setRequest(newNode->request);
        }
        break;
    }
}

void MainWindow::onDeleteRequest() {
    std::string targetColId, targetNodeId;
    sidebar_.getContextTarget(targetColId, targetNodeId);
    if (targetColId.empty()) targetColId = currentCollectionId_;
    if (targetNodeId.empty()) targetNodeId = currentNodeId_;

    for (auto& col : collections_) {
        if (col.id != targetColId) continue;
        const CollectionNode* node = findNode(col, targetNodeId);
        if (!node) break;

        std::wstring prompt = L"Are you sure you want to delete the request \"" + toWide(node->name) + L"\"?";
        if (MessageBoxW(hwnd_, prompt.c_str(), L"Delete Request", MB_YESNO | MB_ICONQUESTION) != IDYES) {
            return;
        }

        removeNode(col, targetNodeId);
        storage_.saveCollection(col, nullptr);
        if (col.id == currentCollectionId_ && targetNodeId == currentNodeId_) {
            currentNodeId_.clear();
            RequestModel emptyReq;
            requestView_.setRequest(emptyReq);
        }
        refreshSidebar();
        break;
    }
}

std::vector<std::pair<std::wstring, std::wstring>> MainWindow::collectVariablesForMenu() const {
    std::vector<std::pair<std::wstring, std::wstring>> list;
    std::unordered_set<std::wstring> seen;

    auto addVar = [&](const std::wstring& key, const std::wstring& val) {
        if (!key.empty() && seen.insert(key).second) {
            list.emplace_back(key, val);
        }
    };

    // 1. Active environment
    const Environment* active = environments_.active();
    if (active) {
        for (const auto& var : active->variables) {
            if (var.enabled && !var.key.empty()) {
                addVar(toWide(var.key), toWide(var.secret ? "••••••••" : var.value));
            }
        }
    } else {
        for (const auto& env : environments_.environments) {
            for (const auto& var : env.variables) {
                if (var.enabled && !var.key.empty()) {
                    addVar(toWide(var.key), toWide(var.secret ? "••••••••" : var.value));
                }
            }
        }
    }

    // 2. Active collection
    for (const auto& col : collections_) {
        if (col.id == currentCollectionId_) {
            for (const auto& var : col.variables) {
                if (var.enabled && !var.key.empty()) {
                    addVar(toWide(var.key), toWide(var.secret ? "••••••••" : var.value));
                }
            }
            break;
        }
    }

    // 3. Global
    for (const auto& var : settings_.globalVariables) {
        if (var.enabled && !var.key.empty()) {
            addVar(toWide(var.key), toWide(var.secret ? "••••••••" : var.value));
        }
    }
    return list;
}

LRESULT CALLBACK MainWindow::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MainWindow* window = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        window = reinterpret_cast<MainWindow*>(cs->lpCreateParams);
        if (window) {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
            window->hwnd_ = hwnd;
        }
    } else {
        window = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (window) return window->handleMessage(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT MainWindow::handleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case kRequestFinished:
        onRequestFinished(reinterpret_cast<HttpResponse*>(lParam));
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case 40001: onSend(); return 0;
        case 40002: onImportClick(); return 0;
        case 40003: onExportClick(); return 0;
        case 40004: onManageEnvs(); return 0;
        case 40005: PostMessageW(hwnd_, WM_CLOSE, 0, 0); return 0;
        case 40006: onSaveRequest(); return 0;
        case 40007: onNewRequest(); return 0;
        case 40010: onNewCollection(); return 0;
        case 40011: onRenameCollection(); return 0;
        case 40012: onDeleteCollection(); return 0;
        case 40013: onExportCollection(); return 0;
        case 40014: onRenameRequest(); return 0;
        case 40015: onDuplicateRequest(); return 0;
        case 40016: onDeleteRequest(); return 0;
        case 40100: toggleTheme(); return 0;
        case 40102: onEnvChipClick(); return 0;
        default: break;
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        paint();
        return 0;
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED && hwnd_) {
            settings_.windowWidth = LOWORD(lParam);
            settings_.windowHeight = HIWORD(lParam);
            settings_.maximized = (wParam == SIZE_MAXIMIZED);
        }
        layout();
        return 0;
    case WM_MOVE:
        if (hwnd_ && wParam == 0 && !IsZoomed(hwnd_)) {
            RECT rc;
            GetWindowRect(hwnd_, &rc);
            settings_.windowX = rc.left;
            settings_.windowY = rc.top;
        }
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = theme().scale(880);
        info->ptMinTrackSize.y = theme().scale(620);
        return 0;
    }
    case WM_DPICHANGED: {
        themeReloadForDpi(hwnd_);
        themeApplyTitleBar(hwnd_, theme().dark);
        const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        applyChrome();
        broadcastTheme();
        layout();
        InvalidateRect(hwnd_, nullptr, TRUE);
        return 0;
    }
    case WM_CLOSE:
        if (busy_ && cancel_) cancel_->cancel();
        joinWorker();
        saveCurrentRequest(false);
        saveState();
        DestroyWindow(hwnd_);
        return 0;
    case WM_DESTROY:
        hwnd_ = nullptr;
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

void MainWindow::loadState() {
    settings_ = storage_.loadSettings(warnings_);
    environments_ = storage_.loadEnvironments(warnings_);
    collections_ = storage_.loadCollections(warnings_);
    history_ = storage_.loadHistory(warnings_);

    if (!settings_.activeEnvironmentId.empty()) {
        environments_.activeId = settings_.activeEnvironmentId;
    }
}

void MainWindow::saveState() {
    if (hwnd_) {
        if (!IsZoomed(hwnd_)) {
            RECT rc;
            if (GetWindowRect(hwnd_, &rc)) {
                settings_.windowX = rc.left;
                settings_.windowY = rc.top;
                settings_.windowWidth = rc.right - rc.left;
                settings_.windowHeight = rc.bottom - rc.top;
            }
        }
        settings_.maximized = IsZoomed(hwnd_) != FALSE;
    }
    settings_.sidebarWidth = settings_.sidebarWidth > 0 ? settings_.sidebarWidth : theme().scale(272);
    settings_.activeEnvironmentId = environments_.activeId;
    settings_.theme = theme().dark ? ThemeMode::Dark : ThemeMode::Light;

    settings_.tabs.clear();
    TabState tab;
    tab.request = requestView_.getRequest();
    tab.collectionId = currentNodeId_.empty() ? std::string() : currentCollectionId_;
    tab.nodeId = currentNodeId_;
    settings_.tabs.push_back(tab);
    if (!currentCollectionId_.empty()) settings_.activeCollectionId = currentCollectionId_;

    storage_.saveSettings(settings_, nullptr);
    storage_.saveEnvironments(environments_, nullptr);
    for (const auto& collection : collections_) storage_.saveCollection(collection, nullptr);
    storage_.saveHistory(history_, nullptr);
}

} // namespace ui
} // namespace lite
