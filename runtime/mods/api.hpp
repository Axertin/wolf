// The C table every code mod receives.

#pragma once

#include <wolf/wolf.h>

namespace wolf {

// The process's one table. Each entry checks the mod's arguments, then calls the runtime. Any thread.
const WolfApi *wolfApi();

} // namespace wolf
