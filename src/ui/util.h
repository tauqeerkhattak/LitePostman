#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <cstddef>
#include <string>

namespace lite {
namespace ui {

// UTF-8 <-> UTF-16 helpers shared by the UI layer.
std::wstring toWide(const std::string& text);
std::string toUtf8(const std::wstring& text);

std::wstring windowTextW(HWND edit);
std::string windowTextUtf8(HWND edit);
void setWindowTextUtf8(HWND edit, const std::string& text);

// Edit controls need CRLF to render line breaks.
std::wstring normalizeNewlines(const std::wstring& text);

std::string formatBytes(std::size_t bytes);
std::string formatElapsed(double milliseconds);

} // namespace ui
} // namespace lite
