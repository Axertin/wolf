#pragma once

#include <cstdint>

namespace wolf::runtime::hooks
{
bool setupResourceHooks(uintptr_t mainBase);
}