#include "hooks.h"

#include "../core/memory_access.h"
#include "../utilities/logger.h"
#include "../wolf_runtime_api.h"

#include <MinHook.h>

namespace wolf::runtime::hooks
{

bool setupAllHooks()
{
    logDebug("[WOLF] Initializing MinHook...");

    MH_STATUS result = MH_Initialize();
    if (result != MH_OK && result != MH_ERROR_ALREADY_INITIALIZED)
    {
        logError("[WOLF] Failed to initialize MinHook!");
        return false;
    }

    // Get main module base address
    uintptr_t mainBase = wolfRuntimeGetModuleBase("main.dll");
    if (mainBase == 0)
    {
        logError("[WOLF] Failed to get main module base address!");
        return false;
    }

    // Setup all hook categories
    if (!setupCoreHooks(mainBase))
    {
        logError("[WOLF] Failed to setup core hooks!");
        return false;
    }

    if (!setupInventoryHooks(mainBase))
    {
        logError("[WOLF] Failed to setup inventory hooks!");
        return false;
    }

    if (!setupResourceHooks(mainBase))
    {
        logError("[WOLF] Failed to setup resource hooks!");
        return false;
    }

    if (!setupShopHooks(mainBase))
    {
        logError("[WOLF] Failed to setup shop hooks!");
        return false;
    }

    // Enable all hooks
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK)
    {
        logError("[WOLF] Failed to enable hooks!");
        return false;
    }

    return true;
}

void cleanupAllHooks()
{
    logDebug("[WOLF] Cleaning up game hooks...");

    // Disable and remove all hooks
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();

    logDebug("[WOLF] Game hooks cleanup complete");
}

} // namespace wolf::runtime::hooks
