#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include "ui/util.h"

#include <cstdio>
#include <vector>

namespace lite {
namespace ui {

std::wstring toWide(const std::string& text) {
    if (text.empty()) return std::wstring();
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) return std::wstring();
    std::wstring out(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length);
    return out;
}

std::string toUtf8(const std::wstring& text) {
    if (text.empty()) return std::string();
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) return std::string();
    std::string out(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length, nullptr, nullptr);
    return out;
}

std::wstring windowTextW(HWND edit) {
    if (!edit) return std::wstring();
    const int length = GetWindowTextLengthW(edit);
    if (length <= 0) return std::wstring();
    std::wstring buffer(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(edit, buffer.data(), length + 1);
    buffer.resize(static_cast<std::size_t>(length));
    return buffer;
}

std::string windowTextUtf8(HWND edit) {
    return toUtf8(windowTextW(edit));
}

void setWindowTextUtf8(HWND edit, const std::string& text) {
    if (edit) SetWindowTextW(edit, toWide(text).c_str());
}

std::wstring normalizeNewlines(const std::wstring& text) {
    std::wstring out;
    out.reserve(text.size() + text.size() / 16);
    for (std::size_t i = 0; i < text.size(); ++i) {
        const wchar_t ch = text[i];
        if (ch == L'\r') {
            out += L"\r\n";
            if (i + 1 < text.size() && text[i + 1] == L'\n') ++i;
        } else if (ch == L'\n') {
            out += L"\r\n";
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

std::string formatBytes(std::size_t bytes) {
    char buffer[64];
    if (bytes < 1024) {
        std::snprintf(buffer, sizeof(buffer), "%zu B", bytes);
    } else if (bytes < 1024 * 1024) {
        std::snprintf(buffer, sizeof(buffer), "%.1f KB", static_cast<double>(bytes) / 1024.0);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.2f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    }
    return buffer;
}

std::string formatElapsed(double milliseconds) {
    char buffer[64];
    if (milliseconds < 1000.0) {
        std::snprintf(buffer, sizeof(buffer), "%.0f ms", milliseconds);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.2f s", milliseconds / 1000.0);
    }
    return buffer;
}

} // namespace ui
} // namespace lite
