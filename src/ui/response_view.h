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

private:
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void layout();
    void updateTab();
    void applyFonts();
    void paint();
    void copyCurrent();

    HWND hwnd_ = nullptr;
    HWND tabs_ = nullptr;
    HWND copyBtn_ = nullptr;
    HWND edit_ = nullptr;

    std::string status_ = "Ready";
    std::string meta_;
    std::string contents_[3];

    int tab_ = 0;
    int code_ = 0;
    bool error_ = false;
    bool loading_ = false;
    bool hasResponse_ = false;
};

} // namespace ui
} // namespace lite
