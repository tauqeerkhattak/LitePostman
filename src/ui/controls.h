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

enum ButtonStyle {
    kButtonPrimary = 0,
    kButtonSecondary,
    kButtonGhost,
    kButtonSubtle
};

enum {
    FBM_SETSTYLE = WM_APP + 40,
    FBM_SETBACKDROP
};

enum {
    DDM_ADDSTRING = WM_APP + 50,
    DDM_RESETCONTENT,
    DDM_SETCURSEL,
    DDM_GETCURSEL,
    DDM_SETITEMCOLOR,
    DDM_SETPLACEHOLDER,
    DDM_SETBACKDROP,
    DDM_GETCOUNT
};

enum {
    TSM_ADDSTRING = WM_APP + 60,
    TSM_RESETCONTENT,
    TSM_SETCURSEL,
    TSM_GETCURSEL,
    TSM_SETBACKDROP
};

HWND createFlatButton(HWND parent, int id, const wchar_t* text, int style);
void setButtonStyle(HWND button, int style);
void setButtonBackdrop(HWND button, COLORREF color);

HWND createDropdown(HWND parent, int id);
void dropdownAdd(HWND dropdown, const std::wstring& text, COLORREF color = CLR_INVALID);
void dropdownReset(HWND dropdown);
void dropdownSetCurSel(HWND dropdown, int index);
int dropdownGetCurSel(HWND dropdown);
void dropdownSetPlaceholder(HWND dropdown, const std::wstring& text);
void dropdownSetBackdrop(HWND dropdown, COLORREF color);

HWND createTabStrip(HWND parent, int id);
void tabAdd(HWND tabs, const std::wstring& text);
void tabReset(HWND tabs);
void tabSetCurSel(HWND tabs, int index);
int tabGetCurSel(HWND tabs);
void tabSetBackdrop(HWND tabs, COLORREF color);

} // namespace ui
} // namespace lite
