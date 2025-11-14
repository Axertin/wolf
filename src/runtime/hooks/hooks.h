#pragma once

#include "core_hooks.h"
#include "input_hooks.h"
#include "inventory_hooks.h"
#include "resource_hooks.h"
#include "shop_hooks.h"

namespace wolf::runtime::hooks
{
bool setupAllHooks();
void cleanupAllHooks();
} // namespace wolf::runtime::hooks