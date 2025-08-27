#include "core_hooks.h"

#include "../utilities/logger.h"
#include "../wolf_runtime_api.h"

#include <MinHook.h>

//==============================================================================
// CORE GAME LOOP HOOKS
//==============================================================================

namespace wolf::runtime::hooks
{

// Hook function pointers
static void(__cdecl *oReturnToMenu)() = nullptr;
static void(__cdecl *oGameStop)() = nullptr;
static void(__cdecl *oGameTick)() = nullptr;
static void(__cdecl *oGameStart)() = nullptr;

// Hook implementations
void __cdecl onReturnToMenu()
{
    logInfo("[WOLF] Return to menu triggered");
    wolf::runtime::internal::callReturnToMenu();
    oReturnToMenu();
}

void __cdecl onGameStop()
{
    logInfo("[WOLF] Game stop triggered");
    wolf::runtime::internal::callGameStop();
    oGameStop();
}

void __cdecl onGameTick()
{
    wolf::runtime::internal::callGameTick();
    wolf::runtime::internal::processMemoryWatches();
    oGameTick();
}

void __cdecl onGameStart()
{
    logInfo("[WOLF] Game start triggered");
    wolf::runtime::internal::callGameStart();
    oGameStart();
}

bool setupCoreHooks(uintptr_t mainBase)
{
    logInfo("[WOLF] Setting up core game hooks...");

    // Core game flow hooks
    if (MH_CreateHook(reinterpret_cast<void *>(mainBase + 0x4B6240), reinterpret_cast<LPVOID>(&onReturnToMenu), reinterpret_cast<LPVOID *>(&oReturnToMenu)) !=
        MH_OK)
        return false;
    if (MH_CreateHook(reinterpret_cast<void *>(mainBase + 0x4B6230), reinterpret_cast<LPVOID>(&onGameStop), reinterpret_cast<LPVOID *>(&oGameStop)) != MH_OK)
        return false;
    if (MH_CreateHook(reinterpret_cast<void *>(mainBase + 0x4B63B0), reinterpret_cast<LPVOID>(&onGameTick), reinterpret_cast<LPVOID *>(&oGameTick)) != MH_OK)
        return false;

    logInfo("[WOLF] Core game hooks setup complete");
    return true;
}

} // namespace wolf::runtime::hooks
