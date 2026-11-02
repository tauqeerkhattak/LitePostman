#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <commctrl.h>

#include "ui/main_window.h"
#include "ui/theme.h"

namespace {

void enablePerMonitorDpi() {
    using SetDpiAwarenessContextFn = BOOL(WINAPI*)(HANDLE);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return;
    auto fn = reinterpret_cast<SetDpiAwarenessContextFn>(
        reinterpret_cast<void*>(GetProcAddress(user32, "SetProcessDpiAwarenessContext")));
    if (fn) {
        // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
        fn(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)));
    } else {
        SetProcessDPIAware();
    }
}

} // namespace

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    enablePerMonitorDpi();

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icc.dwICC = ICC_WIN95_CLASSES | ICC_BAR_CLASSES | ICC_TAB_CLASSES | ICC_TREEVIEW_CLASSES |
                ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    lite::ui::MainWindow mainWindow(hInstance);
    if (!mainWindow.create()) {
        return 1;
    }

    mainWindow.show((nCmdShow == SW_HIDE || nCmdShow == 0) ? SW_SHOWNORMAL : nCmdShow);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (mainWindow.preTranslate(&msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    lite::ui::themeShutdown();
    return static_cast<int>(msg.wParam);
}
