#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <string>

namespace lite {
namespace ui {

class ResponseView {
public:
    ResponseView();
    ~ResponseView();

    HWND create(HWND parent, int id);
    void resize(int x, int y, int width, int height);
    void setResponse(const std::string& status, const std::string& meta, const std::string& body,
                     const std::string& headers, const std::string& cookies, int code);
    void setLoading();
    void setError(const std::string& title, const std::string& detail);
    void clear();
    bool hasResponse() const { return hasResponse_; }
    std::string getResponseBody() const { return !prettyBody_.empty() ? prettyBody_ : rawBody_; }
    std::string getResponseStatus() const { return status_; }
    void setExpanded(bool expanded);

private:
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void layout();
    void updateTab();
    void applyFonts();
    void paint();
    void copyCurrent();
    void toggleFind(bool open);
    void doFind(bool forward);
    void setViewMode(int mode);

    HWND hwnd_ = nullptr;
    HWND tabs_ = nullptr;
    HWND copyBtn_ = nullptr;
    HWND edit_ = nullptr;
    HWND prettyBtn_ = nullptr;
    HWND rawBtn_ = nullptr;
    HWND previewBtn_ = nullptr;
    HWND findBtn_ = nullptr;
    HWND expandBtn_ = nullptr;

    HWND findEdit_ = nullptr;
    HWND findPrevBtn_ = nullptr;
    HWND findNextBtn_ = nullptr;
    HWND findCloseBtn_ = nullptr;
    bool findOpen_ = false;
    int findMatchCur_ = 0;
    int findMatchTotal_ = 0;

    int viewMode_ = 0; // 0 = Pretty, 1 = Raw, 2 = Preview
    std::string rawBody_;
    std::string prettyBody_;
    std::string previewBody_;

    bool jsonValid_ = false;
    std::wstring jsonStatusText_;
    int caretLine_ = 1;
    int caretCol_ = 1;

    std::string status_ = "Ready";
    std::string meta_;
    std::string contents_[3];

    int tab_ = 0;
    int code_ = 0;
    bool error_ = false;
    bool loading_ = false;
    bool hasResponse_ = false;
    bool expanded_ = false;
};

} // namespace ui
} // namespace lite
