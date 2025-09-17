#pragma once

#include "wolf_types.h"

// C API functions for logging
extern "C"
{
    void __cdecl wolfRuntimeLog(WolfModId mod_id, WolfLogLevel level, const char *message);
    void __cdecl wolfRuntimeSetLogPrefix(WolfModId mod_id, const char *prefix);
}