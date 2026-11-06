#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <string>
#include <utility>
#include <vector>

namespace lite {
namespace ui {

using VariableListProvider = std::vector<std::pair<std::wstring, std::wstring>> (*)();

// RichEdit-backed text fields. RichEdit is used so that {{variables}} can be
// painted in the accent colour while the user types. If the RichEdit library
// is unavailable the helpers fall back to a plain single-line edit control.
bool richEditInit();
HWND createRichEdit(HWND parent, int id, const wchar_t* cue, bool multiline, bool mono);
void richEditSetText(HWND edit, const std::wstring& text);
void richEditSetTheme(HWND edit);
void richEditHighlight(HWND edit);
void richEditSetVariableProvider(VariableListProvider provider);
void attachVariableAutocomplete(HWND edit);

const int kCommandToggleFind = 7700;
bool richEditFindText(HWND edit, const std::wstring& query, bool forward, bool matchCase,
                      int* outCurIndex = nullptr, int* outTotalCount = nullptr);

} // namespace ui
} // namespace lite
