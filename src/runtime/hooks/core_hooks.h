#pragma once

#include <cstdint>

namespace wolf::runtime::hooks
{
bool setupCoreHooks(uintptr_t mainBase);
}