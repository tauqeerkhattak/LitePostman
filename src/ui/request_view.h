#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <string>

#include "core/models.h"

namespace lite {
namespace ui {

class RequestView {
public:
    RequestView();
    ~RequestView();

    HWND create(HWND parent, int id);
    void resize(int x, int y, int width, int height);
    void setRequest(const RequestModel& req);
    RequestModel getRequest() const;
    void focusUrl(bool selectAll = false);
    void setBusy(bool busy);
    void setInheritedAuthHint(const std::wstring& hint);
    HWND getSendButton() const { return sendBtn_; }

private:
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void layout();
    void updatePage();
    void applyFonts();
    void paint();
    void drawInput(HDC dc, const RECT& rc, bool focused) const;
    void tryFormatBodyJson();
    void updateJsonStatus();
    void toggleFind(bool open);
    void doFind(bool forward);

    HWND hwnd_ = nullptr;
    HWND title_ = nullptr;
    HWND saveBtn_ = nullptr;
    HWND curlBtn_ = nullptr;
    HWND method_ = nullptr;
    HWND url_ = nullptr;
    HWND sendBtn_ = nullptr;
    HWND tabs_ = nullptr;
    HWND beautifyBtn_ = nullptr;
    HWND minifyBtn_ = nullptr;
    HWND findBtn_ = nullptr;
    HWND body_ = nullptr;
    HWND headers_ = nullptr;
    HWND auth_ = nullptr;
    HWND user_ = nullptr;
    HWND pass_ = nullptr;
    HWND token_ = nullptr;

    HWND findEdit_ = nullptr;
    HWND findPrevBtn_ = nullptr;
    HWND findNextBtn_ = nullptr;
    HWND findCloseBtn_ = nullptr;
    bool findOpen_ = false;
    int findMatchCur_ = 0;
    int findMatchTotal_ = 0;

    bool jsonValid_ = true;
    std::wstring jsonStatusText_;
    int caretLine_ = 1;
    int caretCol_ = 1;

    RECT titleRect_ = {};
    RECT urlRect_ = {};
    RECT contentRect_ = {};
    RECT authRect_ = {};
    RECT userRect_ = {};
    RECT passRect_ = {};
    RECT tokenRect_ = {};

    HWND focusedEdit_ = nullptr;
    HWND lastFocusedEdit_ = nullptr;
    int page_ = 0;
    RequestModel request_;
    bool syncing_ = false;
    std::wstring inheritedAuthHint_;
};

} // namespace ui
} // namespace lite
