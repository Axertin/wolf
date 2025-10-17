#include "core_hooks.h"

#include <okami/bitfield.hpp>

#include "../core/bitfield_monitoring.h"
#include "../core/game_hooks.h"
#include "../core/memory_access.h"
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

// Global flags pointer for lifecycle monitoring
static okami::BitField<86> *g_GlobalFlags = nullptr;

// Check flag 17 directly and trigger lifecycle hooks on transitions
static void checkMainMenuFlag()
{
    static bool previousMainMenuState = true;
    static bool previousLoadingState = false;
    static bool initialized = false;
    static bool gameplayStarted = false;
    static bool InMainMenu = true;

    // Access the bitfield directly via pointer
    if (g_GlobalFlags == nullptr)
        return;

    bool currentMainMenuState = g_GlobalFlags->IsSet(17);
    bool currentLoadingState = g_GlobalFlags->IsSet(16);

    // Initialize on first check
    if (!initialized)
    {
        previousLoadingState = currentLoadingState;
        previousMainMenuState = currentMainMenuState;
        initialized = true;
        return;
    }

    // Detect transitions
    if (previousMainMenuState != currentMainMenuState)
    {
        if (previousMainMenuState && !currentMainMenuState)
        {
            // Transitioned from main menu (true) to gameplay (false)
            logDebug("[WOLF] Main Menu left (flag 17: menu -> gameplay)");
            InMainMenu = false;
        }
        else if (!previousMainMenuState && currentMainMenuState && gameplayStarted)
        {
            // Transitioned from gameplay (false) to main menu (true)
            // Only trigger if gameplay has started
            logDebug("[WOLF] Returned to menu (flag 17: gameplay -> menu)");
            wolf::runtime::internal::callReturnToMenu();
            gameplayStarted = false;
            InMainMenu = true;
        }

        previousMainMenuState = currentMainMenuState;
    }

    if (previousLoadingState != currentLoadingState)
    {
        if (!InMainMenu && previousLoadingState && !currentLoadingState)
        {
            logDebug("[WOLF] Game Loaded and Gameplay Started (flag 16: Loading -> gameplay)");
            wolf::runtime::internal::callPlayStart();
            gameplayStarted = true;
        }

        previousLoadingState = currentLoadingState;
    }
}

// Hook implementations
void __cdecl onReturnToMenu()
{
    logDebug("[WOLF] Return to menu triggered");
    wolf::runtime::internal::callReturnToMenu();
    oReturnToMenu();
}

void __cdecl onGameStop()
{
    logDebug("[WOLF] Game stop triggered");
    wolf::runtime::internal::callGameStop();
    oGameStop();
}

void __cdecl onGameTick()
{
    wolf::runtime::internal::processMemoryWatches();
    wolf::runtime::internal::processBitFieldMonitors();

    // Check flag 17 for gameplay lifecycle transitions
    checkMainMenuFlag();

    wolf::runtime::internal::callGameTick();
    oGameTick();
}

void __cdecl onGameStart()
{
    logDebug("[WOLF] Game start triggered");
    wolf::runtime::internal::callGameStart();
    oGameStart();
}

bool setupCoreHooks(uintptr_t mainBase)
{
    MH_STATUS status;
    logDebug("[WOLF] Setting up core game hooks...");

    // Initialize global flags pointer for lifecycle monitoring
    g_GlobalFlags = reinterpret_cast<okami::BitField<86> *>(mainBase + 0xB6B2AC);

    // Core game flow hooks
    // logInfo("[WOLF] Attempting to create onReturnToMenu hook at address: %p", reinterpret_cast<void *>(mainBase + 0x4B6240));
    // status =
    //     MH_CreateHook(reinterpret_cast<void *>(mainBase + 0x4B6240), reinterpret_cast<LPVOID>(&onReturnToMenu), reinterpret_cast<LPVOID *>(&oReturnToMenu));
    // if (status != MH_OK)
    // {
    //     if (status == MH_ERROR_ALREADY_CREATED)
    //     {
    //         logWarning("[WOLF] onReturnToMenu hook already exists at %p, skipping", reinterpret_cast<void *>(mainBase + 0x4B6240));
    //     }
    //     else
    //     {
    //         logError("[WOLF] onReturnToMenu setup Failed! MinHook error: %d at address: %p", status, reinterpret_cast<void *>(mainBase + 0x4B6240));
    //         return false;
    //     }
    // }
    status = MH_CreateHook(reinterpret_cast<void *>(mainBase + 0x4B6230), reinterpret_cast<LPVOID>(&onGameStop), reinterpret_cast<LPVOID *>(&oGameStop));

    if (status != MH_OK)
    {
        if (status == MH_ERROR_ALREADY_CREATED)
        {
            logWarning("[WOLF] onGameStop hook already exists at %p, skipping", reinterpret_cast<void *>(mainBase + 0x4B6230));
        }
        else
        {
            logError("[WOLF] onGameStop setup Failed! MinHook error: %d", status);
            return false;
        }
    }
    status = MH_CreateHook(reinterpret_cast<void *>(mainBase + 0x4B63B0), reinterpret_cast<LPVOID>(&onGameTick), reinterpret_cast<LPVOID *>(&oGameTick));
    if (status != MH_OK)
    {
        if (status == MH_ERROR_ALREADY_CREATED)
        {
            logWarning("[WOLF] onGameTick hook already exists at %p, skipping", reinterpret_cast<void *>(mainBase + 0x4B63B0));
        }
        else
        {
            logError("[WOLF] onGameTick setup Failed! MinHook error: %d", status);
            return false;
        }
    }

    logDebug("[WOLF] Core game hooks setup complete");
    return true;
}

} // namespace wolf::runtime::hooks
