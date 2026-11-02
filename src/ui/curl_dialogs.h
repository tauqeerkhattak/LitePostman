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

class CurlDialogs {
public:
    static std::string importCurl(HWND parent);
    static bool exportCurl(HWND parent, const std::string& curlText);
};

} // namespace ui
} // namespace lite
