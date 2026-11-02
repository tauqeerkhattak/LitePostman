#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <string>
#include <vector>

#include "core/models.h"

namespace lite {
namespace ui {

class EnvDialog {
public:
    // Shows a modal environment manager. Returns true when the store changed.
    static bool show(HWND parent, EnvironmentStore& store);
};

} // namespace ui
} // namespace lite
