#pragma once

#include <windows.h>

namespace wolf::runtime::internal
{
void installCrashHandler();
void uninstallCrashHandler();
} // namespace wolf::runtime::internal
