// The detours on the game functions the sites follow.

#pragma once

#include <windows.h>

namespace wolf {

// DllMain: makes the flower_startup detour the head of main.dll's flower_startup and enables it; MinHook refusing it logs. The cold flower_startup resolves
// the symbol table and installs the other sites' heads, once the loader lock is released.
void hookFlowerStartup( HMODULE main );

} // namespace wolf
