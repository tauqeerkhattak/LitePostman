#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include "ui/sidebar.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <cwctype>

#include "ui/theme.h"
#include "ui/util.h"

namespace lite {
namespace ui {

namespace {
const wchar_t kSidebarClass[] = L"LitePostmanSidebar";
const int kFilterEditId = 1901;
const int kCommandManageEnvironments = 40004;
const int kCommandNewRequest = 40007;
const int kCommandNewFolder = 40008;
const int kCommandNewCollection = 40010;
const int kCommandRenameCollection = 40011;
const int kCommandDeleteCollection = 40012;
const int kCommandExportCollection = 40013;
const int kCommandRenameRequest = 40014;
const int kCommandDuplicateRequest = 40015;
const int kCommandDeleteRequest = 40016;
const int kCommandMoveNode = 40017;
const int kCommandEditAuth = 40018;
const int kCommandRenameFolder = 40019;
const int kCommandDeleteFolder = 40020;
const int kCommandCopyCurl = 40021;

int countRequests(const std::vector<CollectionNode>& nodes) {
    int total = 0;
    for (const auto& node : nodes) {
        if (node.type == CollectionNode::Type::Request) ++total;
        total += countRequests(node.children);
    }
    return total;
}

std::wstring toLowerW(std::wstring text) {
    for (wchar_t& ch : text) ch = static_cast<wchar_t>(std::towlower(ch));
    return text;
}

std::wstring shortMethodTag(const std::string& method) {
    if (method == "DELETE") return L"DEL";
    if (method == "OPTIONS") return L"OPT";
    if (method == "PATCH") return L"PAT";
    return toWide(method);
}
} // namespace

Sidebar::Sidebar() {}

Sidebar::~Sidebar() {
    if (hwnd_) DestroyWindow(hwnd_);
}

HWND Sidebar::create(HWND parent, int id) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = Sidebar::wndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kSidebarClass;

    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return nullptr;

    hwnd_ = CreateWindowExW(0, kSidebarClass, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPCHILDREN,
                            0, 0, 270, 500, parent,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                            GetModuleHandleW(nullptr), this);
    if (hwnd_) {
        filterEdit_ = CreateWindowExW(0, WC_EDITW, L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                      0, 0, 180, 22, hwnd_,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFilterEditId)),
                                      GetModuleHandleW(nullptr), nullptr);
        if (filterEdit_) {
            SendMessageW(filterEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(theme().fontSmall), TRUE);
            SendMessageW(filterEdit_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Filter requests..."));
        }
        layout();
    }
    return hwnd_;
}

void Sidebar::layout() {
    if (!hwnd_ || !filterEdit_) return;
    const Theme& t = theme();
    RECT s = searchRect();
    const int editH = t.scale(18);
    const int editY = s.top + (s.bottom - s.top - editH) / 2;
    MoveWindow(filterEdit_, s.left + t.scale(28), editY,
               std::max(t.scale(40), static_cast<int>(s.right - s.left) - t.scale(36)), editH, TRUE);
}

void Sidebar::resize(int x, int y, int width, int height) {
    if (!hwnd_) return;
    MoveWindow(hwnd_, x, y, width, height, TRUE);
    layout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool Sidebar::isExpanded(const std::string& id, bool defaultValue) const {
    const auto it = expanded_.find(id);
    return it == expanded_.end() ? defaultValue : it->second;
}

void Sidebar::toggle(const std::string& id, bool defaultValue) {
    expanded_[id] = !isExpanded(id, defaultValue);
}

bool Sidebar::nodeMatchesFilter(const CollectionNode& node, const std::wstring& filterLower) const {
    if (filterLower.empty()) return true;
    const std::wstring nameLower = toLowerW(toWide(node.name));
    if (nameLower.find(filterLower) != std::wstring::npos) return true;
    if (node.type == CollectionNode::Type::Request) {
        const std::wstring urlLower = toLowerW(toWide(node.request.url));
        if (urlLower.find(filterLower) != std::wstring::npos) return true;
    }
    for (const auto& child : node.children) {
        if (nodeMatchesFilter(child, filterLower)) return true;
    }
    return false;
}

void Sidebar::flatten(const Collection& collection, const CollectionNode& node, int depth,
                      const std::wstring& filterLower) {
    if (!filterLower.empty() && !nodeMatchesFilter(node, filterLower)) return;

    const bool filtering = !filterLower.empty();
    const bool expanded = filtering ? true : isExpanded(node.id, false);

    Row row;
    row.collectionId = collection.id;
    row.nodeId = node.id;
    row.label = toWide(node.name);
    row.depth = depth;
    row.folder = node.type == CollectionNode::Type::Folder;
    row.method = node.request.method;
    row.expanded = expanded;
    rows_.push_back(std::move(row));

    if (node.type == CollectionNode::Type::Folder && expanded) {
        for (const auto& child : node.children) flatten(collection, child, depth + 1, filterLower);
    }
}

void Sidebar::rebuild() {
    rows_.clear();
    const std::wstring filterLower = toLowerW(filterText_);
    const bool filtering = !filterLower.empty();

    for (const auto& collection : collections_) {
        bool anyChildMatches = false;
        if (filtering) {
            if (toLowerW(toWide(collection.name)).find(filterLower) != std::wstring::npos) {
                anyChildMatches = true;
            } else {
                for (const auto& child : collection.children) {
                    if (nodeMatchesFilter(child, filterLower)) {
                        anyChildMatches = true;
                        break;
                    }
                }
            }
            if (!anyChildMatches) continue;
        }

        const bool expanded = filtering ? true : isExpanded(collection.id, true);
        Row root;
        root.collectionId = collection.id;
        root.label = toWide(collection.name);
        root.depth = 0;
        root.folder = true;
        root.collection = true;
        root.expanded = expanded;
        rows_.push_back(std::move(root));

        if (expanded) {
            for (const auto& child : collection.children) flatten(collection, child, 1, filterLower);
        }
    }

    const int contentHeight = static_cast<int>(rows_.size()) * theme().scale(30);
    const int visible = bodyBottom() - bodyTop();
    scroll_ = std::max(0, std::min(scroll_, std::max(0, contentHeight - visible)));
}

void Sidebar::setCollections(const std::vector<Collection>& collections) {
    collections_ = collections;
    rebuild();
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void Sidebar::setSelectHandler(SelectHandler handler, void* context) {
    handler_ = handler;
    context_ = context;
}

void Sidebar::setEnvironment(const std::wstring& name, bool active) {
    environment_ = name;
    environmentActive_ = active;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

int Sidebar::bodyTop() const {
    return theme().scale(86);
}

int Sidebar::bodyBottom() const {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    return std::max(bodyTop(), static_cast<int>(rc.bottom) - theme().scale(48));
}

RECT Sidebar::plusRect() const {
    const Theme& t = theme();
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const int size = t.scale(24);
    const int right = static_cast<int>(rc.right) - t.scale(12);
    const int top = (t.scale(44) - size) / 2;
    RECT plus = {right - size, top, right, top + size};
    return plus;
}

RECT Sidebar::searchRect() const {
    const Theme& t = theme();
    RECT rc;
    GetClientRect(hwnd_, &rc);
    return {t.scale(12), t.scale(46), std::max(t.scale(120), static_cast<int>(rc.right) - t.scale(12)), t.scale(76)};
}

void Sidebar::selectNode(const std::string& collectionId, const std::string& nodeId) {
    selectedCollection_ = collectionId;
    selectedNode_ = nodeId;
    expanded_[collectionId] = true;
    rebuild();
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
        const Row& row = rows_[static_cast<std::size_t>(i)];
        if (!row.folder && row.nodeId == nodeId && row.collectionId == collectionId) {
            ensureVisible(i);
            break;
        }
    }
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

int Sidebar::rowAt(int y) const {
    const int top = bodyTop();
    if (y < top || y >= bodyBottom()) return -1;
    const int index = (y - top + scroll_) / theme().scale(30);
    if (index < 0 || index >= static_cast<int>(rows_.size())) return -1;
    return index;
}

void Sidebar::ensureVisible(int index) {
    if (index < 0 || index >= static_cast<int>(rows_.size())) return;
    const int rowH = theme().scale(30);
    const int visible = bodyBottom() - bodyTop();
    const int rowTop = index * rowH;
    const int rowBottom = rowTop + rowH;
    if (rowTop < scroll_) scroll_ = rowTop;
    if (rowBottom > scroll_ + visible) scroll_ = rowBottom - visible;
    scroll_ = std::max(0, scroll_);
}

void Sidebar::expand(const std::string& key) {
    if (key.empty()) return;
    expanded_[key] = true;
    rebuild();
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void Sidebar::selectIndex(int index, bool activate) {
    if (index < 0 || index >= static_cast<int>(rows_.size())) return;
    const Row& row = rows_[static_cast<std::size_t>(index)];
    selectedCollection_ = row.collectionId;
    selectedNode_ = row.nodeId;
    if (row.folder) {
        if (activate) {
            toggle(row.collectionId.empty() ? std::string() : (row.nodeId.empty() ? row.collectionId : row.nodeId), true);
            rebuild();
        }
        if (activate && handler_) handler_(context_, row.collectionId, row.nodeId);
    } else {
        if (activate && handler_) handler_(context_, row.collectionId, row.nodeId);
    }
    ensureVisible(index);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Sidebar::paint() {
    DoubleBufferDC db(hwnd_);
    HDC dc = db.dc();
    const RECT rc = db.rect();

    const Theme& t = theme();
    fillRect(dc, rc, t.surface);
    drawLine(dc, rc.right - 1, 0, rc.right - 1, rc.bottom, t.border, 1);

    // Header
    const int headerH = t.scale(44);
    RECT header = {t.scale(14), 0, rc.right - t.scale(14), headerH};
    drawText(dc, L"COLLECTIONS", header, t.fontSmallBold, t.textMuted,
             DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

    int requests = 0;
    for (const auto& collection : collections_) requests += countRequests(collection.children);
    const std::wstring countText = std::to_wstring(requests);
    const SIZE countSize = measureText(dc, countText, t.fontSmallBold);

    RECT plus = plusRect();
    const int pillRight = plus.left - t.scale(8);
    RECT countPill = {pillRight - countSize.cx - t.scale(14),
                      (headerH - t.scale(20)) / 2,
                      pillRight,
                      (headerH + t.scale(20)) / 2};
    drawPill(dc, countPill, countText, t.fontSmallBold, t.textMuted, t.surfaceAlt);

    fillRound(dc, plus, t.scale(6), plusHover_ ? t.accentSoft : t.surfaceAlt);
    drawPlusIcon(dc, (plus.left + plus.right) / 2, (plus.top + plus.bottom) / 2, t.scale(4),
                 plusHover_ ? t.accent : t.textMuted);

    // Search filter box
    RECT s = searchRect();
    fillRound(dc, s, t.scale(7), t.inputBg);
    const bool filterFocused = filterEdit_ && GetFocus() == filterEdit_;
    strokeRound(dc, s, t.scale(7), filterFocused ? t.accent : t.border, 1);
    drawSearchIcon(dc, s.left + t.scale(14), (s.top + s.bottom) / 2, t.scale(12),
                   filterFocused ? t.accent : t.textFaint);

    const int top = bodyTop();
    const int bottom = bodyBottom();
    const int rowH = t.scale(30);

    if (rows_.empty()) {
        RECT empty = {t.scale(20), top + t.scale(16), rc.right - t.scale(20), top + t.scale(44)};
        drawText(dc, filterText_.empty() ? L"Nothing saved yet" : L"No matching requests",
                 empty, t.fontUiMedium, t.textMuted, DT_SINGLELINE | DT_CENTER | DT_TOP | DT_NOPREFIX);
        RECT hint = {t.scale(20), top + t.scale(40), rc.right - t.scale(20), top + t.scale(80)};
        drawText(dc, filterText_.empty() ? L"Click + above to create a request." : L"Try a different filter.",
                 hint, t.fontSmall, t.textFaint, DT_CENTER | DT_TOP | DT_NOPREFIX);
    } else {
        SaveDC(dc);
        IntersectClipRect(dc, 0, top, rc.right, bottom);
        for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
            const Row& row = rows_[static_cast<std::size_t>(i)];
            RECT item = {t.scale(8), top + i * rowH - scroll_, rc.right - t.scale(10),
                         top + i * rowH - scroll_ + rowH - t.scale(2)};
            if (item.bottom < top || item.top > bottom) continue;

            const bool selected = (row.collection && row.collectionId == selectedCollection_ && selectedNode_.empty()) ||
                                  (!row.folder && row.nodeId == selectedNode_ && row.collectionId == selectedCollection_);
            if (selected) {
                fillRound(dc, item, t.scale(6), t.accentSoft);
                RECT bar = {item.left, item.top + t.scale(5), item.left + t.scale(3), item.bottom - t.scale(5)};
                fillRound(dc, bar, t.scale(2), t.accent);
            } else if (i == hover_) {
                fillRound(dc, item, t.scale(6), t.surfaceHover);
            }

            const int baseX = t.scale(12) + row.depth * t.scale(14);
            const int centerY = (item.top + item.bottom) / 2;

            if (row.folder) {
                drawChevron(dc, baseX + t.scale(6), centerY, t.scale(8),
                            selected ? t.accent : t.textMuted, row.expanded);
                RECT iconRc = {baseX + t.scale(16), centerY - t.scale(6),
                               baseX + t.scale(29), centerY + t.scale(6)};
                if (row.collection) {
                    drawCollectionIcon(dc, iconRc, selected ? t.accent : t.accent, selected);
                } else {
                    drawFolderIcon(dc, iconRc, selected ? t.accent : t.textMuted,
                                   selected ? t.accentSoft : t.surfaceAlt, row.expanded);
                }
                RECT textRect = {iconRc.right + t.scale(8), item.top, item.right - t.scale(8), item.bottom};
                HFONT font = row.collection ? t.fontUiBold : t.fontUiMedium;
                COLORREF color = selected ? t.accent : t.text;
                drawText(dc, row.label, textRect, font, color,
                         DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            } else {
                const COLORREF mc = methodColor(row.method);
                const std::wstring tag = shortMethodTag(row.method);
                const int badgeW = t.scale(32);
                const int badgeH = t.scale(16);
                RECT badge = {baseX + t.scale(2), centerY - badgeH / 2,
                              baseX + t.scale(2) + badgeW, centerY + badgeH / 2};
                fillRound(dc, badge, t.scale(4), blend(mc, t.surface, 0.84));
                drawText(dc, tag, badge, t.fontSmallBold, mc,
                         DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);

                RECT textRect = {badge.right + t.scale(8), item.top, item.right - t.scale(8), item.bottom};
                drawText(dc, row.label, textRect, selected ? t.fontUiMedium : t.fontUi,
                         selected ? t.accent : t.text,
                         DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            }
        }
        RestoreDC(dc, -1);

        const int contentHeight = static_cast<int>(rows_.size()) * rowH;
        if (contentHeight > bottom - top) {
            const int trackX = rc.right - t.scale(6);
            const int thumbHeight = std::max(t.scale(24), (bottom - top) * (bottom - top) / contentHeight);
            const int maxScroll = contentHeight - (bottom - top);
            const int thumbY = top + (maxScroll > 0 ? scroll_ * ((bottom - top) - thumbHeight) / maxScroll : 0);
            RECT thumb = {trackX, thumbY, trackX + t.scale(3), thumbY + thumbHeight};
            fillRound(dc, thumb, t.scale(2), t.borderStrong);
        }
    }

    // Footer: active environment
    const int footerTop = bottom;
    drawLine(dc, t.scale(12), footerTop, rc.right - t.scale(12), footerTop, t.border, 1);
    RECT footer = {t.scale(8), footerTop + t.scale(6), rc.right - t.scale(8), rc.bottom - t.scale(6)};
    if (footerHover_) fillRound(dc, footer, t.scale(6), t.surfaceHover);

    const int footerCenter = (footer.top + footer.bottom) / 2;
    drawDot(dc, t.scale(20), footerCenter, t.scale(4),
            environmentActive_ ? t.success : t.textFaint);

    RECT envText = {t.scale(32), footer.top, footer.right - t.scale(24), footer.bottom};
    drawText(dc, environment_, envText, t.fontUiMedium,
             environmentActive_ ? t.text : t.textMuted,
             DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

    drawChevron(dc, footer.right - t.scale(10), footerCenter, t.scale(7), t.textMuted, false);
}

LRESULT CALLBACK Sidebar::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Sidebar* sidebar = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        sidebar = reinterpret_cast<Sidebar*>(cs->lpCreateParams);
        if (sidebar) {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(sidebar));
            sidebar->hwnd_ = hwnd;
        }
    } else {
        sidebar = reinterpret_cast<Sidebar*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (sidebar) return sidebar->handleMessage(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT Sidebar::handleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        paint();
        return 0;
    case WM_SIZE:
        layout();
        rebuild();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_COMMAND: {
        const HWND ctrl = reinterpret_cast<HWND>(lParam);
        const WORD notif = HIWORD(wParam);
        if (ctrl == filterEdit_) {
            if (notif == EN_CHANGE) {
                filterText_ = windowTextW(filterEdit_);
                rebuild();
                InvalidateRect(hwnd_, nullptr, FALSE);
            } else if (notif == EN_SETFOCUS || notif == EN_KILLFOCUS) {
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        }
        break;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, theme().text);
        SetBkColor(dc, theme().inputBg);
        return reinterpret_cast<LRESULT>(themeBrush(theme().inputBg));
    }
    case WM_MOUSEMOVE: {
        if (!tracking_) {
            TRACKMOUSEEVENT track = {sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd_, 0};
            TrackMouseEvent(&track);
            tracking_ = true;
        }
        const int y = GET_Y_LPARAM(lParam);
        POINT point = {GET_X_LPARAM(lParam), y};
        const int index = rowAt(y);
        const bool footer = y >= bodyBottom();
        RECT plusBox = plusRect();
        const bool plus = PtInRect(&plusBox, point) != FALSE;
        if (index != hover_ || footer != footerHover_ || plus != plusHover_) {
            hover_ = index;
            footerHover_ = footer;
            plusHover_ = plus;
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        tracking_ = false;
        hover_ = -1;
        footerHover_ = false;
        plusHover_ = false;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN: {
        SetFocus(hwnd_);
        const int y = GET_Y_LPARAM(lParam);
        POINT point = {GET_X_LPARAM(lParam), y};
        RECT plusBox = plusRect();
        if (PtInRect(&plusBox, point)) {
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, kCommandNewRequest, L"New Request (Ctrl+N)");
            AppendMenuW(menu, MF_STRING, kCommandNewFolder, L"New Folder...");
            AppendMenuW(menu, MF_STRING, kCommandNewCollection, L"New Collection...");
            POINT screenPt = point;
            ClientToScreen(hwnd_, &screenPt);
            int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                     screenPt.x, screenPt.y, 0, hwnd_, nullptr);
            DestroyMenu(menu);
            if (cmd > 0) {
                SendMessageW(GetParent(hwnd_), WM_COMMAND, MAKEWPARAM(cmd, 0), 0);
            }
            return 0;
        }
        if (y >= bodyBottom()) {
            SendMessageW(GetParent(hwnd_), WM_COMMAND, MAKEWPARAM(kCommandManageEnvironments, BN_CLICKED), 0);
            return 0;
        }
        const int index = rowAt(y);
        if (index < 0) return 0;
        selectIndex(index, true);
        return 0;
    }
    case WM_RBUTTONUP: {
        SetFocus(hwnd_);
        const int y = GET_Y_LPARAM(lParam);
        POINT clientPt = {GET_X_LPARAM(lParam), y};
        POINT screenPt = clientPt;
        ClientToScreen(hwnd_, &screenPt);

        const int index = rowAt(y);
        HMENU menu = CreatePopupMenu();
        if (index >= 0) {
            const Row row = rows_[static_cast<std::size_t>(index)];
            contextCollection_ = row.collectionId;
            contextNode_ = row.nodeId;

            if (row.collection) {
                AppendMenuW(menu, MF_STRING, kCommandNewRequest, L"Add Request");
                AppendMenuW(menu, MF_STRING, kCommandNewFolder, L"Add Folder...");
                AppendMenuW(menu, MF_STRING, kCommandEditAuth, L"Edit Collection Auth...");
                AppendMenuW(menu, MF_STRING, kCommandRenameCollection, L"Rename Collection...");
                AppendMenuW(menu, MF_STRING, kCommandExportCollection, L"Export Collection (.json)...");
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(menu, MF_STRING, kCommandDeleteCollection, L"Delete Collection");
            } else if (row.folder) {
                AppendMenuW(menu, MF_STRING, kCommandNewRequest, L"Add Request");
                AppendMenuW(menu, MF_STRING, kCommandNewFolder, L"Add Subfolder...");
                AppendMenuW(menu, MF_STRING, kCommandEditAuth, L"Edit Folder Auth...");
                AppendMenuW(menu, MF_STRING, kCommandRenameFolder, L"Rename Folder...");
                AppendMenuW(menu, MF_STRING, kCommandMoveNode, L"Move Folder...");
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(menu, MF_STRING, kCommandDeleteFolder, L"Delete Folder");
            } else {
                AppendMenuW(menu, MF_STRING, kCommandRenameRequest, L"Rename Request...");
                AppendMenuW(menu, MF_STRING, kCommandMoveNode, L"Move Request...");
                AppendMenuW(menu, MF_STRING, kCommandDuplicateRequest, L"Duplicate Request");
                AppendMenuW(menu, MF_STRING, kCommandCopyCurl, L"Copy as cURL");
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(menu, MF_STRING, kCommandDeleteRequest, L"Delete Request");
            }
        } else {
            contextCollection_.clear();
            contextNode_.clear();
            AppendMenuW(menu, MF_STRING, kCommandNewRequest, L"New Request");
            AppendMenuW(menu, MF_STRING, kCommandNewFolder, L"New Folder...");
            AppendMenuW(menu, MF_STRING, kCommandNewCollection, L"New Collection...");
        }

        int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                 screenPt.x, screenPt.y, 0, hwnd_, nullptr);
        DestroyMenu(menu);
        if (cmd > 0) {
            SendMessageW(GetParent(hwnd_), WM_COMMAND, MAKEWPARAM(cmd, 0), 0);
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        scroll_ -= (delta / WHEEL_DELTA) * theme().scale(60);
        const int contentHeight = static_cast<int>(rows_.size()) * theme().scale(30);
        scroll_ = std::max(0, std::min(scroll_, std::max(0, contentHeight - (bodyBottom() - bodyTop()))));
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    }
    case WM_KEYDOWN: {
        int current = -1;
        for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
            const Row& row = rows_[static_cast<std::size_t>(i)];
            if (!row.folder && row.nodeId == selectedNode_ && row.collectionId == selectedCollection_) {
                current = i;
                break;
            }
        }
        if (wParam == VK_DOWN) {
            selectIndex(std::min(current + 1, static_cast<int>(rows_.size()) - 1), false);
            return 0;
        }
        if (wParam == VK_UP) {
            selectIndex(std::max(current - 1, 0), false);
            return 0;
        }
        if ((wParam == VK_RETURN || wParam == VK_SPACE) && current >= 0) {
            const Row& row = rows_[static_cast<std::size_t>(current)];
            if (row.folder) {
                const std::string key = row.nodeId.empty() ? row.collectionId : row.nodeId;
                expanded_[key] = !isExpanded(key, row.collection);
                rebuild();
                InvalidateRect(hwnd_, nullptr, FALSE);
            } else if (handler_) {
                handler_(context_, row.collectionId, row.nodeId);
            }
            return 0;
        }
        break;
    }
    case kThemeChanged:
        if (filterEdit_) {
            SendMessageW(filterEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(theme().fontSmall), TRUE);
            InvalidateRect(filterEdit_, nullptr, TRUE);
        }
        layout();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

} // namespace ui
} // namespace lite
