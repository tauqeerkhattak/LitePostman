#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include "ui/curl_dialogs.h"

#include <commctrl.h>
#include <string>
#include <vector>
#include "ui/util.h"

namespace lite {
namespace ui {

std::string CurlDialogs::importCurl(HWND parent) {
    (void)parent;
    // Try to read from clipboard
    if (!OpenClipboard(parent)) return "";
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (!hData) {
        CloseClipboard();
        return "";
    }
    wchar_t* pszText = static_cast<wchar_t*>(GlobalLock(hData));
    std::string result;
    if (pszText) {
        std::wstring w(pszText);
        result.assign(w.begin(), w.end());
        GlobalUnlock(hData);
    }
    CloseClipboard();
    return result;
}

bool CurlDialogs::exportCurl(HWND parent, const std::string& curlText) {
    if (!OpenClipboard(parent)) return false;
    EmptyClipboard();
    const std::wstring wide = toWide(curlText);
    const size_t lenBytes = (wide.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, lenBytes);
    if (!hMem) {
        CloseClipboard();
        return false;
    }
    wchar_t* buf = static_cast<wchar_t*>(GlobalLock(hMem));
    if (buf) {
        memcpy(buf, wide.c_str(), lenBytes);
        GlobalUnlock(hMem);
        SetClipboardData(CF_UNICODETEXT, hMem);
    }
    CloseClipboard();
    return true;
}

} // namespace ui
} // namespace lite
