#include "hooks.h"
#include "../logger.h"
#include "../wolf_runtime_api.h"
#include <MinHook.h>

namespace wolf::runtime::hooks
{

bool setupAllHooks()
{
    logInfo("[WOLF] Initializing MinHook...");
    
    MH_STATUS result = MH_Initialize();
    if (result != MH_OK && result != MH_ERROR_ALREADY_INITIALIZED)
    {
        logError("[WOLF] Failed to initialize MinHook!");
        return false;
    }
    
    // Get main module base address
    uintptr_t mainBase = wolfRuntimeGetModuleBase(nullptr);
    if (mainBase == 0)
    {
        logError("[WOLF] Failed to get main module base address!");
        return false;
    }
    
    logInfo("[WOLF] Setting up game hooks...");
    
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
    
    logInfo("[WOLF] All game hooks initialized successfully");
    return true;
}

void cleanupAllHooks()
{
    logInfo("[WOLF] Cleaning up game hooks...");
    
    // Disable and remove all hooks
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    
    logInfo("[WOLF] Game hooks cleanup complete");
}

} // namespace wolf::runtime::hooks