#include "mod_lifecycle.h"

#include <algorithm>

#ifndef WOLF_TESTING_MODE
#include <imgui.h>
#endif
#include <wolf_version.h>

#include "../utilities/logger.h"

// Global mod management storage
std::recursive_mutex g_ModMutex;
std::unordered_map<WolfModId, std::unique_ptr<ModInfo>> g_Mods;
WolfModId g_NextModId = 1;
thread_local WolfModId g_CurrentModId = 0; // 0 = invalid

// Helper function for better exception reporting
void logModException(const std::string &context, const std::string &modName, const std::exception &e)
{
    ::logError("[WOLF] Standard exception in " + context + " for mod '" + modName + "': " + e.what());
}

void logModException(const std::string &context, const std::string &modName)
{
    ::logError("[WOLF] Unknown exception in " + context + " for mod '" + modName + "' - possibly access violation or system exception");
}

ModInfo *findMod(WolfModId modId)
{
    std::lock_guard<std::recursive_mutex> lock(g_ModMutex);
    auto it = g_Mods.find(modId);
    return (it != g_Mods.end()) ? it->second.get() : nullptr;
}

// C API implementations
extern "C"
{
    WolfModId wolfRuntimeGetCurrentModId(void)
    {
        return g_CurrentModId;
    }

    WolfModId wolfRuntimeRegisterMod(const WolfModInterface *modInterface)
    {
        if (!modInterface || !modInterface->getName || !modInterface->shutdown)
            return 0; // Invalid modInterface

        std::lock_guard<std::recursive_mutex> lock(g_ModMutex);

        WolfModId modId = g_NextModId++;
        auto modInfo = std::make_unique<ModInfo>();

        modInfo->id = modId;
        modInfo->modInterface = *modInterface; // Copy struct
        modInfo->name = modInterface->getName();
        modInfo->version = modInterface->getVersion ? modInterface->getVersion() : "0.0.0";
        modInfo->logPrefix = "[" + modInfo->name + "]";

        g_Mods[modId] = std::move(modInfo);

        // Set current mod ID for this thread (mod loading thread)
        g_CurrentModId = modId;

        ::logInfo("[WOLF] Registered mod: " + g_Mods[modId]->name + " v" + g_Mods[modId]->version);

        return modId;
    }

} // extern "C"

// Internal namespace implementation
namespace wolf::runtime::internal
{

bool checkVersionCompatibility(unsigned int modFrameworkVersion, unsigned int modImGuiVersion, const std::string &modName)
{
    unsigned int runtimeVersion = WOLF_VERSION_INT;

    // Extract version components for runtime
    unsigned int runtimeMajor = (runtimeVersion >> 24) & 0xFF;
    unsigned int runtimeMinor = (runtimeVersion >> 16) & 0xFFFF;
    unsigned int runtimePatch = runtimeVersion & 0xFFFF;

    // Extract version components for mod framework
    unsigned int modMajor = (modFrameworkVersion >> 24) & 0xFF;
    unsigned int modMinor = (modFrameworkVersion >> 16) & 0xFFFF;
    unsigned int modPatch = modFrameworkVersion & 0xFFFF;

    // Semantic versioning compatibility rules:
    // - Major versions must match exactly
    // - Runtime minor.patch must be >= mod's required minor.patch

    if (modMajor != runtimeMajor)
    {
        ::logError("[WOLF] Version compatibility error for mod '" + modName + "': Major version mismatch (mod requires " + std::to_string(modMajor) +
                   ".x.x, runtime is " + std::to_string(runtimeMajor) + "." + std::to_string(runtimeMinor) + "." + std::to_string(runtimePatch) + ")");
        return false;
    }

    // Compare minor versions
    if (modMinor > runtimeMinor)
    {
        ::logError("[WOLF] Version compatibility error for mod '" + modName + "': Runtime version too old (mod requires " + std::to_string(modMajor) + "." +
                   std::to_string(modMinor) + "." + std::to_string(modPatch) + "+, runtime is " + std::to_string(runtimeMajor) + "." +
                   std::to_string(runtimeMinor) + "." + std::to_string(runtimePatch) + ")");
        return false;
    }

    // If minor versions match, compare patch versions
    if (modMinor == runtimeMinor && modPatch > runtimePatch)
    {
        ::logError("[WOLF] Version compatibility error for mod '" + modName + "': Runtime version too old (mod requires " + std::to_string(modMajor) + "." +
                   std::to_string(modMinor) + "." + std::to_string(modPatch) + "+, runtime is " + std::to_string(runtimeMajor) + "." +
                   std::to_string(runtimeMinor) + "." + std::to_string(runtimePatch) + ")");
        return false;
    }

    // Check ImGui version compatibility - skip if mod doesn't use ImGui (version 0)
    if (modImGuiVersion != 0)
    {
        unsigned int runtimeImGuiVersion = IMGUI_VERSION_NUM;
        if (modImGuiVersion != runtimeImGuiVersion)
        {
            ::logError("[WOLF] ImGui version compatibility error for mod '" + modName + "': Version mismatch (mod compiled with ImGui " +
                       std::to_string(modImGuiVersion) + ", runtime uses " + std::to_string(runtimeImGuiVersion) + ")");
            return false;
        }
    }

    // Successful and compatible
    return true;
}

void callPreGameInit()
{
    // Call early init for all registered mods
    std::lock_guard<std::recursive_mutex> lock(g_ModMutex);

    for (const auto &pair : g_Mods)
    {
        const auto &mod = pair.second;
        if (mod->modInterface.earlyGameInit)
        {
            g_CurrentModId = mod->id; // Set context for this mod
            try
            {
                mod->modInterface.earlyGameInit();
            }
            catch (const std::exception &e)
            {
                logModException("earlyGameInit", mod->name, e);
            }
            catch (...)
            {
                logModException("earlyGameInit", mod->name);
            }
        }
    }

    g_CurrentModId = 0; // Clear context
}

void callEarlyGameInit()
{
    ::logDebug("[WOLF] Calling EarlyGameInit()");
    // Alias for callPreGameInit for compatibility
    callPreGameInit();
}

void callLateGameInit()
{
    ::logDebug("[WOLF] Calling LateGameInit()");
    // Call late init for all registered mods
    std::lock_guard<std::recursive_mutex> lock(g_ModMutex);

    for (const auto &pair : g_Mods)
    {
        const auto &mod = pair.second;
        if (mod->modInterface.lateGameInit)
        {
            g_CurrentModId = mod->id; // Set context for this mod
            try
            {
                mod->modInterface.lateGameInit();
            }
            catch (...)
            {
                ::logError("[WOLF] Exception in lateGameInit for mod: " + mod->name);
            }
        }
    }

    g_CurrentModId = 0; // Clear context
}

void shutdownMods()
{
    std::lock_guard<std::recursive_mutex> lock(g_ModMutex);

    // Call shutdown for all mods in reverse order
    std::vector<std::pair<WolfModId, ModInfo *>> mods;
    for (const auto &pair : g_Mods)
    {
        mods.emplace_back(pair.first, pair.second.get());
    }

    std::reverse(mods.begin(), mods.end());

    for (const auto &pair : mods)
    {
        WolfModId modId = pair.first;
        ModInfo *mod = pair.second;

        if (mod->modInterface.shutdown)
        {
            g_CurrentModId = modId;
            try
            {
                mod->modInterface.shutdown();
            }
            catch (const std::exception &e)
            {
                logModException("shutdown", mod->name, e);
            }
            catch (...)
            {
                logModException("shutdown", mod->name);
            }
        }
    }

    g_CurrentModId = 0;

    // Clean up all mod data
    g_Mods.clear();
    // Note: Other global cleanups are handled by their respective components
}

} // namespace wolf::runtime::internal