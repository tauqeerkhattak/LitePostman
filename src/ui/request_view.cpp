#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include "ui/request_view.h"

#include <commctrl.h>
#include <richedit.h>
#include <windowsx.h>

#include <algorithm>
#include <string>
#include <vector>

#include "ui/controls.h"
#include "ui/rich_edit.h"
#include "ui/theme.h"
#include "ui/util.h"
#include "core/json.h"

namespace lite {
namespace ui {

namespace {

const wchar_t kRequestClass[] = L"LitePostmanRequestView";

const char* kMethods[] = {"GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS"};
const int kMethodCount = 7;

int indexOfMethod(const std::string& method) {
    const std::string wanted = methodNormalized(method);
    for (int i = 0; i < kMethodCount; ++i) {
        if (wanted == kMethods[i]) return i;
    }
    return 0;
}

} // namespace

RequestView::RequestView() {}

RequestView::~RequestView() {
    if (hwnd_) DestroyWindow(hwnd_);
}

HWND RequestView::create(HWND parent, int id) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = RequestView::wndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kRequestClass;

    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return nullptr;

    hwnd_ = CreateWindowExW(0, kRequestClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                            0, 0, 700, 400, parent,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                            GetModuleHandleW(nullptr), this);
    if (!hwnd_) return nullptr;

    title_ = createRichEdit(hwnd_, 2006, L"Request title / name", false, false);
    saveBtn_ = createFlatButton(hwnd_, 40006, L"Save", kButtonSecondary);
    curlBtn_ = createFlatButton(hwnd_, 40021, L"Copy cURL", kButtonGhost);

    method_ = createDropdown(hwnd_, 2001);
    for (int i = 0; i < kMethodCount; ++i) {
        dropdownAdd(method_, toWide(kMethods[i]), methodColor(kMethods[i]));
    }
    dropdownSetCurSel(method_, 0);

    url_ = createRichEdit(hwnd_, 2002,
                          L"Enter request URL, e.g. https://httpbin.org/get", false, false);

    sendBtn_ = createFlatButton(hwnd_, 40001, L"Send", kButtonPrimary);

    tabs_ = createTabStrip(hwnd_, 2003);
    tabAdd(tabs_, L"Body");
    tabAdd(tabs_, L"Headers");
    tabAdd(tabs_, L"Auth");
    tabSetCurSel(tabs_, 0);

    beautifyBtn_ = createFlatButton(hwnd_, 2007, L"Beautify", kButtonGhost);
    minifyBtn_ = createFlatButton(hwnd_, 2008, L"Minify", kButtonGhost);
    findBtn_ = createFlatButton(hwnd_, 2009, L"Find", kButtonGhost);

    findEdit_ = CreateWindowExW(0, WC_EDITW, L"", WS_CHILD | ES_AUTOHSCROLL,
                                0, 0, 160, 24, hwnd_,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(2010)),
                                GetModuleHandleW(nullptr), nullptr);
    SendMessageW(findEdit_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Find in body..."));
    findPrevBtn_ = createFlatButton(hwnd_, 2011, L"\u25B2", kButtonGhost);
    findNextBtn_ = createFlatButton(hwnd_, 2012, L"\u25BC", kButtonGhost);
    findCloseBtn_ = createFlatButton(hwnd_, 2013, L"\u2715", kButtonGhost);

    body_ = createRichEdit(hwnd_, 2004,
                           L"Raw request body - JSON, text, XML...", true, true);

    headers_ = createRichEdit(hwnd_, 2005,
                              L"One header per line, e.g. X-Api-Key: 123", true, true);

    auth_ = createDropdown(hwnd_, 2101);
    dropdownAdd(auth_, L"Inherit auth from parent");
    dropdownAdd(auth_, L"No Auth");
    dropdownAdd(auth_, L"Basic Auth");
    dropdownAdd(auth_, L"Bearer Token");
    dropdownSetCurSel(auth_, 0);

    user_ = CreateWindowExW(0, WC_EDITW, L"", WS_CHILD | ES_AUTOHSCROLL,
                            0, 0, 200, 28, hwnd_,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(2102)),
                            GetModuleHandleW(nullptr), nullptr);
    pass_ = CreateWindowExW(0, WC_EDITW, L"", WS_CHILD | ES_AUTOHSCROLL | ES_PASSWORD,
                            0, 0, 200, 28, hwnd_,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(2103)),
                            GetModuleHandleW(nullptr), nullptr);
    token_ = CreateWindowExW(0, WC_EDITW, L"", WS_CHILD | ES_AUTOHSCROLL,
                             0, 0, 200, 28, hwnd_,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(2104)),
                             GetModuleHandleW(nullptr), nullptr);

    attachVariableAutocomplete(user_);
    attachVariableAutocomplete(pass_);
    attachVariableAutocomplete(token_);

    applyFonts();
    updatePage();
    return hwnd_;
}

void RequestView::applyFonts() {
    const Theme& t = theme();
    HWND plain[] = {user_, pass_, token_};
    for (HWND edit : plain) {
        if (edit) SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(t.fontUi), TRUE);
    }

    HWND rich[] = {title_, url_, body_, headers_};
    for (HWND edit : rich) richEditSetTheme(edit);

    if (saveBtn_) setButtonBackdrop(saveBtn_, t.surface);
    if (curlBtn_) setButtonBackdrop(curlBtn_, t.surface);
    if (sendBtn_) setButtonBackdrop(sendBtn_, t.surface);
    if (beautifyBtn_) setButtonBackdrop(beautifyBtn_, t.surface);
    if (minifyBtn_) setButtonBackdrop(minifyBtn_, t.surface);
    if (findBtn_) setButtonBackdrop(findBtn_, t.surface);
    if (findPrevBtn_) setButtonBackdrop(findPrevBtn_, t.surface);
    if (findNextBtn_) setButtonBackdrop(findNextBtn_, t.surface);
    if (findCloseBtn_) setButtonBackdrop(findCloseBtn_, t.surface);
    if (findEdit_) SendMessageW(findEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(t.fontUi), TRUE);
    if (tabs_) tabSetBackdrop(tabs_, t.surface);
    if (method_) dropdownSetBackdrop(method_, t.surface);
    if (auth_) dropdownSetBackdrop(auth_, t.surface);

    HWND plainEdits[] = {user_, pass_, token_};
    for (HWND edit : plainEdits) themeApplyScrollbars(edit, t.dark);
}

void RequestView::resize(int x, int y, int width, int height) {
    if (!hwnd_) return;
    MoveWindow(hwnd_, x, y, width, height, TRUE);
    layout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void RequestView::layout() {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const int width = rc.right;
    const int height = rc.bottom;
    const Theme& t = theme();

    const int pad = t.scale(16);
    const int gap = t.scale(8);
    const int titleH = t.scale(36);
    const int rowH = t.scale(36);
    const int methodW = t.scale(112);
    const int sendW = t.scale(96);
    const int singleEditH = t.scale(20);

    // Row 0: Request Title & Save button & Copy cURL button
    const int titleY = pad;
    const int saveW = t.scale(68);
    const int curlW = t.scale(92);
    const int titleW = std::max(t.scale(80), width - pad * 2 - saveW - curlW - gap * 2);
    titleRect_ = {pad, titleY, pad + titleW, titleY + titleH};
    if (title_) {
        const int titleOffsetY = (titleH - singleEditH) / 2;
        MoveWindow(title_, titleRect_.left + t.scale(10), titleRect_.top + titleOffsetY,
                   titleRect_.right - titleRect_.left - t.scale(20), singleEditH, TRUE);
    }
    if (saveBtn_) {
        MoveWindow(saveBtn_, width - pad - curlW - gap - saveW, titleY, saveW, titleH, TRUE);
    }
    if (curlBtn_) {
        MoveWindow(curlBtn_, width - pad - curlW, titleY, curlW, titleH, TRUE);
    }

    // Row 1: Method, URL, Send button
    const int rowY = titleY + titleH + t.scale(8);
    if (method_) MoveWindow(method_, pad, rowY, methodW, rowH, TRUE);

    const int urlX = pad + methodW + gap;
    const int urlW = std::max(t.scale(80), width - urlX - pad - sendW - gap);
    urlRect_ = {urlX, rowY, urlX + urlW, rowY + rowH};
    if (url_) {
        const int urlOffsetY = (rowH - singleEditH) / 2;
        MoveWindow(url_, urlRect_.left + t.scale(10), urlRect_.top + urlOffsetY,
                   urlRect_.right - urlRect_.left - t.scale(20), singleEditH, TRUE);
    }
    if (sendBtn_) MoveWindow(sendBtn_, width - pad - sendW, rowY, sendW, rowH, TRUE);

    const int tabsY = rowY + rowH + t.scale(12);
    const int beautifyW = t.scale(68);
    const int minifyW = t.scale(56);
    const int findW = t.scale(48);
    const int actionW = beautifyW + minifyW + findW + gap * 2;

    const bool showTabsAndBody = height >= tabsY + t.scale(60);
    if (tabs_) {
        ShowWindow(tabs_, showTabsAndBody ? SW_SHOW : SW_HIDE);
        if (showTabsAndBody) {
            MoveWindow(tabs_, pad, tabsY, width - pad * 2 - actionW - gap, t.scale(34), TRUE);
        }
    }
    if (beautifyBtn_) {
        ShowWindow(beautifyBtn_, (showTabsAndBody && page_ == 0) ? SW_SHOW : SW_HIDE);
        if (showTabsAndBody) {
            MoveWindow(beautifyBtn_, width - pad - actionW, tabsY + t.scale(2), beautifyW, t.scale(30), TRUE);
        }
    }
    if (minifyBtn_) {
        ShowWindow(minifyBtn_, (showTabsAndBody && page_ == 0) ? SW_SHOW : SW_HIDE);
        if (showTabsAndBody) {
            MoveWindow(minifyBtn_, width - pad - minifyW - findW - gap, tabsY + t.scale(2), minifyW, t.scale(30), TRUE);
        }
    }
    if (findBtn_) {
        ShowWindow(findBtn_, (showTabsAndBody && page_ == 0) ? SW_SHOW : SW_HIDE);
        if (showTabsAndBody) {
            MoveWindow(findBtn_, width - pad - findW, tabsY + t.scale(2), findW, t.scale(30), TRUE);
        }
    }

    if (!showTabsAndBody) {
        if (body_) ShowWindow(body_, SW_HIDE);
        if (headers_) ShowWindow(headers_, SW_HIDE);
        if (auth_) ShowWindow(auth_, SW_HIDE);
        if (user_) ShowWindow(user_, SW_HIDE);
        if (pass_) ShowWindow(pass_, SW_HIDE);
        if (token_) ShowWindow(token_, SW_HIDE);
        if (findEdit_) ShowWindow(findEdit_, SW_HIDE);
        if (findPrevBtn_) ShowWindow(findPrevBtn_, SW_HIDE);
        if (findNextBtn_) ShowWindow(findNextBtn_, SW_HIDE);
        if (findCloseBtn_) ShowWindow(findCloseBtn_, SW_HIDE);
        return;
    }

    const int contentTop = tabsY + t.scale(34) + t.scale(8);
    contentRect_ = {pad, contentTop, width - pad, std::max(contentTop + t.scale(80), height - pad)};

    int bodyTop = contentRect_.top + t.scale(8);
    if (findOpen_ && page_ == 0) {
        const int findBarH = t.scale(28);
        const int findY = contentRect_.top + t.scale(6);
        const int findEditW = t.scale(150);
        const int btnSmallW = t.scale(26);
        int curX = contentRect_.left + t.scale(10);
        if (findEdit_) MoveWindow(findEdit_, curX, findY + t.scale(2), findEditW, t.scale(22), TRUE);
        curX += findEditW + t.scale(6);
        if (findPrevBtn_) MoveWindow(findPrevBtn_, curX, findY, btnSmallW, findBarH, TRUE);
        curX += btnSmallW + t.scale(4);
        if (findNextBtn_) MoveWindow(findNextBtn_, curX, findY, btnSmallW, findBarH, TRUE);
        if (findCloseBtn_) MoveWindow(findCloseBtn_, contentRect_.right - t.scale(10) - btnSmallW, findY, btnSmallW, findBarH, TRUE);
        bodyTop += findBarH + t.scale(6);
    }

    const int inset = t.scale(8);
    const int statusH = (page_ == 0) ? t.scale(24) : 0;
    RECT editRect = {contentRect_.left + inset, bodyTop,
                     contentRect_.right - inset, contentRect_.bottom - inset - statusH};
    if (body_) MoveWindow(body_, editRect.left, editRect.top,
                          editRect.right - editRect.left, editRect.bottom - editRect.top, TRUE);
    if (headers_) MoveWindow(headers_, contentRect_.left + inset, contentRect_.top + inset,
                             contentRect_.right - contentRect_.left - inset * 2,
                             contentRect_.bottom - contentRect_.top - inset * 2, TRUE);

    const int authX = contentRect_.left + t.scale(16);
    const int fieldW = std::min(t.scale(420), static_cast<int>(contentRect_.right) - authX - t.scale(16));
    authRect_ = {authX, contentRect_.top + t.scale(34), authX + t.scale(220),
                 contentRect_.top + t.scale(70)};
    userRect_ = {authX, contentRect_.top + t.scale(106), authX + fieldW,
                 contentRect_.top + t.scale(142)};
    passRect_ = {authX, contentRect_.top + t.scale(170), authX + fieldW,
                 contentRect_.top + t.scale(206)};
    tokenRect_ = {authX, contentRect_.top + t.scale(106), authX + fieldW,
                  contentRect_.top + t.scale(142)};

    if (auth_) MoveWindow(auth_, authRect_.left, authRect_.top,
                          authRect_.right - authRect_.left, authRect_.bottom - authRect_.top, TRUE);

    auto placeEdit = [&](HWND edit, const RECT& box) {
        if (!edit) return;
        const int offsetY = (box.bottom - box.top - singleEditH) / 2;
        MoveWindow(edit, box.left + t.scale(10), box.top + offsetY,
                   box.right - box.left - t.scale(20), singleEditH, TRUE);
    };
    placeEdit(user_, userRect_);
    placeEdit(pass_, passRect_);
    placeEdit(token_, tokenRect_);
}

void RequestView::updatePage() {
    page_ = tabs_ ? tabGetCurSel(tabs_) : 0;
    const bool body = page_ == 0;
    const bool headers = page_ == 1;
    const bool auth = page_ == 2;
    if (beautifyBtn_) ShowWindow(beautifyBtn_, body ? SW_SHOW : SW_HIDE);
    if (minifyBtn_) ShowWindow(minifyBtn_, body ? SW_SHOW : SW_HIDE);
    if (findBtn_) ShowWindow(findBtn_, body ? SW_SHOW : SW_HIDE);

    const bool showFind = body && findOpen_;
    if (findEdit_) ShowWindow(findEdit_, showFind ? SW_SHOW : SW_HIDE);
    if (findPrevBtn_) ShowWindow(findPrevBtn_, showFind ? SW_SHOW : SW_HIDE);
    if (findNextBtn_) ShowWindow(findNextBtn_, showFind ? SW_SHOW : SW_HIDE);
    if (findCloseBtn_) ShowWindow(findCloseBtn_, showFind ? SW_SHOW : SW_HIDE);

    if (body_) ShowWindow(body_, body ? SW_SHOW : SW_HIDE);
    if (headers_) ShowWindow(headers_, headers ? SW_SHOW : SW_HIDE);
    if (auth_) ShowWindow(auth_, auth ? SW_SHOW : SW_HIDE);

    const int selection = auth_ ? dropdownGetCurSel(auth_) : 0;
    const bool basic = auth && selection == 2;
    const bool bearer = auth && selection == 3;
    if (user_) ShowWindow(user_, basic ? SW_SHOW : SW_HIDE);
    if (pass_) ShowWindow(pass_, basic ? SW_SHOW : SW_HIDE);
    if (token_) ShowWindow(token_, bearer ? SW_SHOW : SW_HIDE);

    if (body) updateJsonStatus();

    layout();
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void RequestView::drawInput(HDC dc, const RECT& rc, bool focused) const {
    const Theme& t = theme();
    fillRound(dc, rc, t.scale(8), t.inputBg);
    RECT border = rc;
    InflateRect(&border, -1, -1);
    strokeRound(dc, border, t.scale(8), focused ? t.accent : t.border, focused ? 2 : 1);
}

void RequestView::paint() {
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

    drawInput(dc, titleRect_, focusedEdit_ == title_);
    drawInput(dc, urlRect_, focusedEdit_ == url_);

    const int tabsY = t.scale(16) + t.scale(36) + t.scale(8) + t.scale(36) + t.scale(12);
    if (rc.bottom < tabsY + t.scale(60)) {
        EndPaint(hwnd_, &ps);
        return;
    }

    const COLORREF innerBg = page_ == 2 ? t.surface : t.codeBg;
    fillRound(dc, contentRect_, t.scale(8), innerBg);
    RECT contentBorder = contentRect_;
    InflateRect(&contentBorder, -1, -1);
    strokeRound(dc, contentBorder, t.scale(8), t.border, 1);

    if (page_ == 2) {
        auto label = [&](const wchar_t* text, int x, int y) {
            RECT rect = {x, y, x + t.scale(320), y + t.scale(18)};
            drawText(dc, text, rect, t.fontSmallBold, t.textMuted,
                     DT_SINGLELINE | DT_TOP | DT_NOPREFIX);
        };

        const int x = contentRect_.left + t.scale(16);
        label(L"Authentication type", x, contentRect_.top + t.scale(14));

        const int selection = auth_ ? dropdownGetCurSel(auth_) : 0;
        if (selection == 0) {
            RECT hint = {x, contentRect_.top + t.scale(88),
                         contentRect_.right - t.scale(16), contentRect_.top + t.scale(160)};
            std::wstring hintText = L"Using auth from parent: ";
            if (!inheritedAuthHint_.empty()) {
                hintText += inheritedAuthHint_;
            } else {
                hintText += L"No auth configured on parent.";
            }
            drawText(dc, hintText.c_str(), hint, t.fontUi, t.textMuted, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK);
        } else if (selection == 1) {
            RECT hint = {x, contentRect_.top + t.scale(88),
                         contentRect_.right - t.scale(16), contentRect_.top + t.scale(130)};
            drawText(dc, L"This request will be sent without an Authorization header.",
                     hint, t.fontUi, t.textMuted, DT_LEFT | DT_TOP | DT_NOPREFIX);
        } else if (selection == 2) {
            label(L"Username", x, contentRect_.top + t.scale(88));
            label(L"Password", x, contentRect_.top + t.scale(152));
            drawInput(dc, userRect_, focusedEdit_ == user_);
            drawInput(dc, passRect_, focusedEdit_ == pass_);
        } else if (selection == 3) {
            label(L"Token", x, contentRect_.top + t.scale(88));
            drawInput(dc, tokenRect_, focusedEdit_ == token_);
        }
    } else if (page_ == 0) {
        const int statusY = contentRect_.bottom - t.scale(22);
        RECT statusRc = {contentRect_.left + t.scale(10), statusY, contentRect_.right - t.scale(10), contentRect_.bottom - t.scale(2)};
        COLORREF statusCol = jsonValid_ ? t.success : (jsonStatusText_.find(L"empty") != std::string::npos ? t.textMuted : t.warning);
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
            RECT matchRc = {contentRect_.left + t.scale(230), contentRect_.top + t.scale(6),
                            contentRect_.right - t.scale(42), contentRect_.top + t.scale(34)};
            drawText(dc, matchText, matchRc, t.fontSmall, t.textMuted, DT_LEFT | DT_VCENTER | DT_NOPREFIX);
        }
    }

    EndPaint(hwnd_, &ps);
}

void RequestView::setRequest(const RequestModel& req) {
    request_ = req;
    syncing_ = true;

    if (title_) {
        richEditSetText(title_, toWide(req.name.empty() ? "Untitled Request" : req.name));
    }
    richEditSetText(url_, toWide(req.url));
    dropdownSetCurSel(method_, indexOfMethod(req.method));
    if (body_) {
        std::string formatted = Json::format(req.rawBody);
        richEditSetText(body_, normalizeNewlines(toWide(formatted)));
    }
    if (headers_) {
        std::string text;
        for (const auto& header : req.headers) {
            if (!header.enabled) continue;
            if (!text.empty()) text += "\r\n";
            text += header.key + ": " + header.value;
        }
        richEditSetText(headers_, toWide(text));
    }

    int authIndex = 0;
    if (req.authType == AuthType::Inherit) authIndex = 0;
    else if (req.authType == AuthType::None) authIndex = 1;
    else if (req.authType == AuthType::Basic) authIndex = 2;
    else if (req.authType == AuthType::Bearer) authIndex = 3;
    dropdownSetCurSel(auth_, authIndex);
    setWindowTextUtf8(user_, req.username);
    setWindowTextUtf8(pass_, req.password);
    setWindowTextUtf8(token_, req.token);

    syncing_ = false;
    updatePage();
    updateJsonStatus();
}

RequestModel RequestView::getRequest() const {
    RequestModel req = request_;

    if (title_) {
        req.name = windowTextUtf8(title_);
        while (!req.name.empty() && (req.name.back() == '\r' || req.name.back() == '\n' || req.name.back() == ' ' || req.name.back() == '\t')) {
            req.name.pop_back();
        }
    }
    if (req.name.empty()) req.name = "Untitled Request";

    req.url = windowTextUtf8(url_);
    while (!req.url.empty()) {
        const char last = req.url.back();
        if (last != '\r' && last != '\n' && last != ' ' && last != '\t') break;
        req.url.pop_back();
    }

    if (method_) {
        const int sel = dropdownGetCurSel(method_);
        if (sel >= 0 && sel < kMethodCount) req.method = kMethods[sel];
    }

    req.headers.clear();
    const std::string headerText = windowTextUtf8(headers_);
    std::size_t start = 0;
    while (start < headerText.size()) {
        std::size_t end = headerText.find('\n', start);
        if (end == std::string::npos) end = headerText.size();
        std::string line = headerText.substr(start, end - start);
        start = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        if (line.empty()) continue;
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.erase(value.begin());
        if (key.empty()) continue;
        req.headers.push_back(KeyValue{key, value, true, false, false});
    }

    req.rawBody = windowTextUtf8(body_);
    if (!req.rawBody.empty()) {
        req.bodyMode = BodyMode::Raw;
        if (req.bodyLanguage != BodyLanguage::Xml && req.bodyLanguage != BodyLanguage::Text) {
            req.bodyLanguage = BodyLanguage::Json;
        }
    } else if (req.bodyMode == BodyMode::Raw) {
        req.bodyMode = BodyMode::None;
    }

    const int authSel = auth_ ? dropdownGetCurSel(auth_) : 0;
    if (authSel == 0) req.authType = AuthType::Inherit;
    else if (authSel == 1) req.authType = AuthType::None;
    else if (authSel == 2) req.authType = AuthType::Basic;
    else if (authSel == 3) req.authType = AuthType::Bearer;
    req.username = windowTextUtf8(user_);
    req.password = windowTextUtf8(pass_);
    req.token = windowTextUtf8(token_);

    return req;
}

void RequestView::setInheritedAuthHint(const std::wstring& hint) {
    inheritedAuthHint_ = hint;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void RequestView::tryFormatBodyJson() {
    if (!body_ || syncing_) return;
    int selIdx = dropdownGetCurSel(method_);
    std::string method = (selIdx >= 0 && selIdx < kMethodCount) ? kMethods[selIdx] : "";
    if (method != "POST" && method != "PUT" && method != "PATCH") {
        return;
    }
    std::string text = windowTextUtf8(body_);
    if (text.empty()) return;

    std::string err;
    Json j = Json::parse(text, &err);
    if (!err.empty() || (!j.isObject() && !j.isArray())) {
        return;
    }
    std::string formatted = j.dump(2);
    std::wstring wideFormatted = normalizeNewlines(toWide(formatted));

    std::wstring wideCurrent;
    const int currentLen = GetWindowTextLengthW(body_);
    if (currentLen > 0) {
        wideCurrent.resize(static_cast<std::size_t>(currentLen));
        GetWindowTextW(body_, &wideCurrent[0], currentLen + 1);
    }
    if (wideFormatted == wideCurrent) return;

    CHARRANGE cr = {0, 0};
    SendMessageW(body_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&cr));

    syncing_ = true;
    richEditSetText(body_, wideFormatted);
    richEditHighlight(body_);
    syncing_ = false;

    int newLen = GetWindowTextLengthW(body_);
    if (cr.cpMin > newLen) cr.cpMin = newLen;
    if (cr.cpMax > newLen) cr.cpMax = newLen;
    SendMessageW(body_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&cr));
    updateJsonStatus();
}

void RequestView::updateJsonStatus() {
    std::string text = windowTextUtf8(body_);
    if (text.empty()) {
        jsonValid_ = true;
        jsonStatusText_ = L"JSON (empty)";
    } else {
        int lines = 1;
        for (char c : text) if (c == '\n') lines++;
        std::string err;
        int errLine = 1, errCol = 1;
        if (Json::validate(text, &err, &errLine, &errCol)) {
            jsonValid_ = true;
            std::string sizeStr;
            if (text.size() < 1024) sizeStr = std::to_string(text.size()) + " B";
            else sizeStr = std::to_string(text.size() / 1024) + " KB";
            jsonStatusText_ = L"\u2713 Valid JSON \u2022 " + std::to_wstring(lines) + L" lines \u2022 " + toWide(sizeStr);
        } else {
            jsonValid_ = false;
            std::wstring wErr = toWide(err);
            if (wErr.size() > 50) wErr = wErr.substr(0, 47) + L"...";
            jsonStatusText_ = L"\u26A0 " + wErr;
        }
    }

    if (body_) {
        DWORD start = 0, end = 0;
        SendMessageW(body_, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
        LRESULT lineIdx = SendMessageW(body_, EM_LINEFROMCHAR, start, 0);
        LRESULT lineStartChar = SendMessageW(body_, EM_LINEINDEX, lineIdx, 0);
        caretLine_ = static_cast<int>(lineIdx) + 1;
        caretCol_ = static_cast<int>(start - lineStartChar) + 1;
    }

    InvalidateRect(hwnd_, nullptr, FALSE);
}

void RequestView::toggleFind(bool open) {
    findOpen_ = open;
    const bool show = findOpen_ && (page_ == 0);
    if (findEdit_) ShowWindow(findEdit_, show ? SW_SHOW : SW_HIDE);
    if (findPrevBtn_) ShowWindow(findPrevBtn_, show ? SW_SHOW : SW_HIDE);
    if (findNextBtn_) ShowWindow(findNextBtn_, show ? SW_SHOW : SW_HIDE);
    if (findCloseBtn_) ShowWindow(findCloseBtn_, show ? SW_SHOW : SW_HIDE);
    layout();
    if (show && findEdit_) {
        SetFocus(findEdit_);
        SendMessageW(findEdit_, EM_SETSEL, 0, -1);
    } else if (body_) {
        SetFocus(body_);
    }
}

void RequestView::doFind(bool forward) {
    if (!findEdit_ || !body_) return;
    std::wstring q = windowTextW(findEdit_);
    richEditFindText(body_, q, forward, false, &findMatchCur_, &findMatchTotal_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void RequestView::focusUrl(bool selectAll) {
    if (!url_) return;
    SetFocus(url_);
    if (selectAll) {
        SendMessageW(url_, EM_SETSEL, 0, -1);
    } else {
        const int len = GetWindowTextLengthW(url_);
        SendMessageW(url_, EM_SETSEL, len, len);
    }
}

void RequestView::setBusy(bool busy) {
    if (sendBtn_) {
        EnableWindow(sendBtn_, busy ? FALSE : TRUE);
        SetWindowTextW(sendBtn_, busy ? L"Sending..." : L"Send");
    }
}

LRESULT CALLBACK RequestView::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    RequestView* view = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        view = reinterpret_cast<RequestView*>(cs->lpCreateParams);
        if (view) {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(view));
            view->hwnd_ = hwnd;
        }
    } else {
        view = reinterpret_cast<RequestView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (view) return view->handleMessage(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT RequestView::handleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
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
    case WM_SETFOCUS:
        if (lastFocusedEdit_ && IsWindow(lastFocusedEdit_) && IsWindowVisible(lastFocusedEdit_)) {
            SetFocus(lastFocusedEdit_);
        } else if (url_) {
            SetFocus(url_);
        }
        return 0;
    case WM_TIMER:
        if (wParam == 9001) {
            KillTimer(hwnd_, 9001);
            tryFormatBodyJson();
            return 0;
        }
        break;
    case WM_COMMAND: {
        const HWND control = reinterpret_cast<HWND>(lParam);
        const WORD notification = HIWORD(wParam);
        if (control == beautifyBtn_ && notification == BN_CLICKED) {
            std::string text = windowTextUtf8(body_);
            std::string formatted = Json::format(text);
            richEditSetText(body_, normalizeNewlines(toWide(formatted)));
            richEditHighlight(body_);
            updateJsonStatus();
            return 0;
        }
        if (control == minifyBtn_ && notification == BN_CLICKED) {
            std::string text = windowTextUtf8(body_);
            std::string minified = Json::minify(text);
            richEditSetText(body_, normalizeNewlines(toWide(minified)));
            richEditHighlight(body_);
            updateJsonStatus();
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
        if (control == auth_ && notification == CBN_SELCHANGE) {
            updatePage();
            return 0;
        }
        if (control == method_ && notification == CBN_SELCHANGE) {
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        if (notification == EN_SETFOCUS) {
            focusedEdit_ = control;
            lastFocusedEdit_ = control;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        if (notification == EN_KILLFOCUS) {
            if (focusedEdit_ == control) focusedEdit_ = nullptr;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        if (notification == EN_CHANGE) {
            if (control == findEdit_) {
                doFind(true);
                return 0;
            }
            if (!syncing_) {
                if (control == url_ || control == body_ || control == headers_ || control == title_) {
                    richEditHighlight(control);
                }
                if (control == body_) {
                    updateJsonStatus();
                    SetTimer(hwnd_, 9001, 350, nullptr);
                }
            }
            return 0;
        }
        if (control && GetParent(control) == hwnd_) {
            SendMessageW(GetParent(hwnd_), WM_COMMAND, wParam, lParam);
        }
        return 0;
    }
    case WM_NOTIFY: {
        const NMHDR* header = reinterpret_cast<const NMHDR*>(lParam);
        if (header && header->idFrom == 2003 && header->code == TCN_SELCHANGE) {
            updatePage();
            return 0;
        }
        break;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        const HWND control = reinterpret_cast<HWND>(lParam);
        const bool code = control == body_ || control == headers_;
        const COLORREF background = code ? theme().codeBg : theme().inputBg;
        SetTextColor(dc, theme().text);
        SetBkColor(dc, background);
        return reinterpret_cast<LRESULT>(themeBrush(background));
    }
    case kThemeChanged:
        applyFonts();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

} // namespace ui
} // namespace lite
