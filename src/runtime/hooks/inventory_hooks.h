#pragma once

#include <cstdint>

namespace wolf::runtime::hooks
{
bool setupInventoryHooks(uintptr_t mainBase);
void giveItem(int itemId, int numItems);
} // namespace wolf::runtime::hooks