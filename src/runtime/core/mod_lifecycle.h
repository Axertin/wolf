#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "wolf_types.h"

// Forward declare interface struct
struct WolfModInterface;

// Mod info structure
struct ModInfo
{
    WolfModId id;
    WolfModInterface modInterface;
    std::string name;
    std::string version;
    std::string logPrefix;
};

// Global mod management storage (defined in mod_lifecycle.cpp)
extern std::recursive_mutex g_ModMutex;
extern std::unordered_map<WolfModId, std::unique_ptr<ModInfo>> g_Mods;
extern WolfModId g_NextModId;
extern thread_local WolfModId g_CurrentModId;

// Helper functions
ModInfo *findMod(WolfModId modId);
void logModException(const std::string &context, const std::string &modName, const std::exception &e);
void logModException(const std::string &context, const std::string &modName);

// Internal functions for mod lifecycle (used by wolf::runtime::internal)
namespace wolf::runtime::internal
{
bool checkVersionCompatibility(unsigned int modFrameworkVersion, unsigned int modImGuiVersion, const std::string &modName);
void callPreGameInit();
void callEarlyGameInit();
void callLateGameInit();
void shutdownMods();
} // namespace wolf::runtime::internal

// C API functions for mod lifecycle
extern "C"
{
    WolfModId __cdecl wolfRuntimeGetCurrentModId(void);
    WolfModId __cdecl wolfRuntimeRegisterMod(const WolfModInterface *modInterface);
}
