#include "wolf_runtime_api.h"

#define NOMINMAX
#include <Windows.h>

// Include version information
#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <Psapi.h>
#include <wolf_version.h>

#include "utilities/console.h"
#include "utilities/logger.h"

#include <MinHook.h>

//==============================================================================
// INTERNAL DATA STRUCTURES
//==============================================================================

namespace
{

// Helper function for better exception reporting
void logModException(const std::string &context, const std::string &modName, const std::exception &e)
{
    ::logError("[WOLF] Standard exception in " + context + " for mod '" + modName + "': " + e.what());
}

void logModException(const std::string &context, const std::string &modName)
{
    ::logError("[WOLF] Unknown exception in " + context + " for mod '" + modName + "' - possibly access violation or system exception");
}
// Mod management
struct ModInfo
{
    WolfModId id;
    WolfModInterface modInterface;
    std::string name;
    std::string version;
    std::string logPrefix;
};

static std::recursive_mutex g_ModMutex;
static std::unordered_map<WolfModId, std::unique_ptr<ModInfo>> g_Mods;
static WolfModId g_NextModId = 1;
static thread_local WolfModId g_CurrentModId = 0; // 0 = invalid

// Game event callbacks
struct GameEventCallbacks
{
    std::vector<std::pair<WolfGameEventCallback, void *>> gameTick;
    std::vector<std::pair<WolfGameEventCallback, void *>> gameStart;
    std::vector<std::pair<WolfGameEventCallback, void *>> gameStop;
    std::vector<std::pair<WolfGameEventCallback, void *>> playStart;
    std::vector<std::pair<WolfGameEventCallback, void *>> returnToMenu;
    std::vector<std::pair<WolfItemPickupCallback, void *>> itemPickup;
};

static std::mutex g_CallbackMutex;
static std::unordered_map<WolfModId, std::unique_ptr<GameEventCallbacks>> g_ModCallbacks;

// Console commands
struct WolfCommandInfo
{
    WolfModId modId;
    WolfConsoleCommandCallback callback;
    void *userdata;
    std::string description;
};

static std::mutex g_CommandMutex;
static std::unordered_map<std::string, std::unique_ptr<WolfCommandInfo>> g_Commands;
static std::vector<std::string> g_PendingCommands; // Commands waiting for console to be ready

// Memory watching
struct MemoryWatch
{
    WolfModId modId;
    uintptr_t start;
    size_t size;
    WolfMemoryWatchCallback callback;
    void *userdata;
    std::string description;
    std::vector<uint8_t> lastData;
};

static std::mutex g_WatchMutex;
static std::vector<std::unique_ptr<MemoryWatch>> g_MemoryWatches;

// Resource interception
struct ResourceIntercept
{
    WolfModId modId;
    std::string pattern;
    bool isPattern; // true if wildcard pattern, false if exact filename
    WolfResourceProvider provider;
    void *userdata;
};

static std::mutex g_ResourceMutex;
static std::vector<std::unique_ptr<ResourceIntercept>> g_ResourceIntercepts;

// GUI window management
struct ModGuiWindow
{
    WolfModId modId;
    std::string windowName;
    WolfGuiWindowCallback callback;
    void *userdata;
    bool isVisible;
};

static std::mutex g_GuiMutex;
static std::vector<std::unique_ptr<ModGuiWindow>> g_ModGuiWindows;

// Hook information
struct HookInfo
{
    void *target;
    void *detour;
    void **original;
};
static std::unordered_map<void *, HookInfo> g_ActiveHooks;

// Helper functions
inline ModInfo *findMod(WolfModId modId)
{
    std::lock_guard<std::recursive_mutex> lock(g_ModMutex);
    auto it = g_Mods.find(modId);
    return (it != g_Mods.end()) ? it->second.get() : nullptr;
}

inline bool isValidAddress(uintptr_t address)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(reinterpret_cast<void *>(address), &mbi, sizeof(mbi)) == 0)
        return false;

    return (mbi.State == MEM_COMMIT) && (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE));
}

inline bool matchesPattern(const std::string &filename, const std::string &pattern)
{
    // Simple wildcard matching - supports * and ?
    // This is a basic implementation - could be enhanced
    if (pattern.find('*') == std::string::npos && pattern.find('?') == std::string::npos)
        return filename == pattern; // Exact match

    // TODO: Implement proper wildcard matching if needed
    return filename.find(pattern.substr(0, pattern.find('*'))) != std::string::npos;
}
} // namespace

//==============================================================================
// C API IMPLEMENTATIONS
//==============================================================================

extern "C"
{
    //--- MOD IDENTIFICATION & LIFECYCLE ---

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

    //--- LOGGING ---

    void wolfRuntimeLog(WolfModId mod_id, WolfLogLevel level, const char *message)
    {
        if (!message)
            return;

        std::string logMessage;
        ModInfo *mod = findMod(mod_id);

        if (mod)
        {
            logMessage = mod->logPrefix + " " + message;
        }
        else
        {
            logMessage = "[Unknown] ";
            logMessage += message;
        }

        switch (level)
        {
        case WOLF_LOG_INFO:
            ::logInfo(logMessage);
            break;
        case WOLF_LOG_WARNING:
            ::logWarning(logMessage);
            break;
        case WOLF_LOG_ERROR:
            ::logError(logMessage);
            break;
        case WOLF_LOG_DEBUG:
            ::logDebug(logMessage);
            break;
        default:
            ::logInfo(logMessage);
            break;
        }
    }

    void wolfRuntimeSetLogPrefix(WolfModId mod_id, const char *prefix)
    {
        if (!prefix)
            return;

        ModInfo *mod = findMod(mod_id);
        if (mod)
        {
            mod->logPrefix = prefix;
        }
    }

    //--- MEMORY ACCESS ---

    uintptr_t wolfRuntimeGetModuleBase(const char *module_name)
    {
        if (!module_name)
            return 0;

        HMODULE hModule = GetModuleHandleA(module_name);
        return reinterpret_cast<uintptr_t>(hModule);
    }

    int wolfRuntimeIsValidAddress(uintptr_t address)
    {
        return isValidAddress(address) ? 1 : 0;
    }

    int wolfRuntimeReadMemory(uintptr_t address, void *buffer, size_t size)
    {
        if (!buffer || size == 0 || !isValidAddress(address))
            return 0;

        __try
        {
            memcpy(buffer, reinterpret_cast<void *>(address), size);
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    int wolfRuntimeWriteMemory(uintptr_t address, const void *buffer, size_t size)
    {
        if (!buffer || size == 0 || !isValidAddress(address))
            return 0;

        DWORD oldProtect;
        if (!VirtualProtect(reinterpret_cast<void *>(address), size, PAGE_EXECUTE_READWRITE, &oldProtect))
            return 0;

        __try
        {
            memcpy(reinterpret_cast<void *>(address), buffer, size);
            VirtualProtect(reinterpret_cast<void *>(address), size, oldProtect, &oldProtect);
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            VirtualProtect(reinterpret_cast<void *>(address), size, oldProtect, &oldProtect);
            return 0;
        }
    }

    void wolfRuntimeFindPattern(const char *pattern, const char *mask, const char *module_name, WolfPatternCallback callback, void *userdata)
    {
        if (!pattern || !mask || !callback)
            return;

        uintptr_t baseAddress = 0;
        size_t moduleSize = 0;

        if (module_name)
        {
            HMODULE hModule = GetModuleHandleA(module_name);
            if (!hModule)
                return;

            baseAddress = reinterpret_cast<uintptr_t>(hModule);

            MODULEINFO modInfo;
            if (!GetModuleInformation(GetCurrentProcess(), hModule, &modInfo, sizeof(modInfo)))
                return;

            moduleSize = modInfo.SizeOfImage;
        }
        else
        {
            // Search all modules - simplified implementation
            HMODULE hModule = GetModuleHandleA(nullptr); // Main executable
            if (!hModule)
                return;

            baseAddress = reinterpret_cast<uintptr_t>(hModule);

            MODULEINFO modInfo;
            if (!GetModuleInformation(GetCurrentProcess(), hModule, &modInfo, sizeof(modInfo)))
                return;

            moduleSize = modInfo.SizeOfImage;
        }

        size_t patternLength = strlen(mask);
        const uint8_t *searchData = reinterpret_cast<const uint8_t *>(baseAddress);

        for (size_t i = 0; i <= moduleSize - patternLength; ++i)
        {
            bool found = true;
            for (size_t j = 0; j < patternLength; ++j)
            {
                if (mask[j] == 'x' && searchData[i + j] != static_cast<uint8_t>(pattern[j]))
                {
                    found = false;
                    break;
                }
            }

            if (found)
            {
                callback(baseAddress + i, userdata);
            }
        }
    }

    int wolfRuntimeWatchMemory(WolfModId mod_id, uintptr_t start, size_t size, WolfMemoryWatchCallback callback, void *userdata, const char *description)
    {
        if (!callback || size == 0 || !isValidAddress(start))
            return 0;

        std::lock_guard<std::mutex> lock(g_WatchMutex);

        auto watch = std::make_unique<MemoryWatch>();
        watch->modId = mod_id;
        watch->start = start;
        watch->size = size;
        watch->callback = callback;
        watch->userdata = userdata;
        watch->description = description ? description : "";

        // Take initial snapshot
        watch->lastData.resize(size);
        if (!wolfRuntimeReadMemory(start, watch->lastData.data(), size))
        {
            return 0;
        }

        g_MemoryWatches.push_back(std::move(watch));
        return 1;
    }

    int wolfRuntimeUnwatchMemory(WolfModId mod_id, uintptr_t start)
    {
        std::lock_guard<std::mutex> lock(g_WatchMutex);

        auto it = std::remove_if(g_MemoryWatches.begin(), g_MemoryWatches.end(),
                                 [mod_id, start](const std::unique_ptr<MemoryWatch> &watch) { return watch->modId == mod_id && watch->start == start; });

        if (it != g_MemoryWatches.end())
        {
            g_MemoryWatches.erase(it, g_MemoryWatches.end());
            return 1;
        }

        return 0;
    }

    //--- GAME HOOKS & CALLBACKS ---

    void wolfRuntimeRegisterGameTick(WolfModId mod_id, WolfGameEventCallback callback, void *userdata)
    {
        if (!callback)
            return;

        std::lock_guard<std::mutex> lock(g_CallbackMutex);

        if (g_ModCallbacks.find(mod_id) == g_ModCallbacks.end())
        {
            g_ModCallbacks[mod_id] = std::make_unique<GameEventCallbacks>();
        }

        g_ModCallbacks[mod_id]->gameTick.emplace_back(callback, userdata);
    }

    void wolfRuntimeRegisterGameStart(WolfModId mod_id, WolfGameEventCallback callback, void *userdata)
    {
        if (!callback)
            return;

        std::lock_guard<std::mutex> lock(g_CallbackMutex);

        if (g_ModCallbacks.find(mod_id) == g_ModCallbacks.end())
        {
            g_ModCallbacks[mod_id] = std::make_unique<GameEventCallbacks>();
        }

        g_ModCallbacks[mod_id]->gameStart.emplace_back(callback, userdata);
    }

    void wolfRuntimeRegisterGameStop(WolfModId mod_id, WolfGameEventCallback callback, void *userdata)
    {
        if (!callback)
            return;

        std::lock_guard<std::mutex> lock(g_CallbackMutex);

        if (g_ModCallbacks.find(mod_id) == g_ModCallbacks.end())
        {
            g_ModCallbacks[mod_id] = std::make_unique<GameEventCallbacks>();
        }

        g_ModCallbacks[mod_id]->gameStop.emplace_back(callback, userdata);
    }

    void wolfRuntimeRegisterPlayStart(WolfModId mod_id, WolfGameEventCallback callback, void *userdata)
    {
        if (!callback)
            return;

        std::lock_guard<std::mutex> lock(g_CallbackMutex);

        if (g_ModCallbacks.find(mod_id) == g_ModCallbacks.end())
        {
            g_ModCallbacks[mod_id] = std::make_unique<GameEventCallbacks>();
        }

        g_ModCallbacks[mod_id]->playStart.emplace_back(callback, userdata);
    }

    void wolfRuntimeRegisterReturnToMenu(WolfModId mod_id, WolfGameEventCallback callback, void *userdata)
    {
        if (!callback)
            return;

        std::lock_guard<std::mutex> lock(g_CallbackMutex);

        if (g_ModCallbacks.find(mod_id) == g_ModCallbacks.end())
        {
            g_ModCallbacks[mod_id] = std::make_unique<GameEventCallbacks>();
        }

        g_ModCallbacks[mod_id]->returnToMenu.emplace_back(callback, userdata);
    }

    void wolfRuntimeRegisterItemPickup(WolfModId mod_id, WolfItemPickupCallback callback, void *userdata)
    {
        if (!callback)
            return;

        std::lock_guard<std::mutex> lock(g_CallbackMutex);

        if (g_ModCallbacks.find(mod_id) == g_ModCallbacks.end())
        {
            g_ModCallbacks[mod_id] = std::make_unique<GameEventCallbacks>();
        }

        g_ModCallbacks[mod_id]->itemPickup.emplace_back(callback, userdata);
    }

    int wolfRuntimeHookFunction(uintptr_t address, void *detour, void **original)
    {
        if (!address || !detour)
            return 0;

        void *target = reinterpret_cast<void *>(address);

        if (MH_CreateHook(target, detour, original) != MH_OK)
        {
            return 0;
        }

        if (MH_EnableHook(target) != MH_OK)
        {
            MH_RemoveHook(target);
            return 0;
        }

        g_ActiveHooks[target] = {target, detour, original};
        return 1;
    }

    //--- CONSOLE SYSTEM ---

    void wolfRuntimeAddCommand(WolfModId mod_id, const char *name, WolfConsoleCommandCallback callback, void *userdata, const char *description)
    {
        if (!name || !callback)
        {
            ::logError("[WOLF] AddCommand failed: invalid parameters");
            return;
        }

        ::logInfo("[WOLF] Runtime adding command: " + std::string(name));
        std::lock_guard<std::mutex> lock(g_CommandMutex);

        std::string cmdName(name);
        auto cmdInfo = std::make_unique<WolfCommandInfo>();
        cmdInfo->modId = mod_id;
        cmdInfo->callback = callback;
        cmdInfo->userdata = userdata;
        cmdInfo->description = description ? description : "";

        std::string cmdDescription = cmdInfo->description; // Copy before move
        g_Commands[cmdName] = std::move(cmdInfo);

        // Register with actual console system
        if (g_Console)
        {
            ::logInfo("[WOLF] Registering with console system: " + cmdName);
            g_Console->addCommand(
                cmdName,
                [cmdName](const std::vector<std::string> &args)
                {
                    std::lock_guard<std::mutex> lock(g_CommandMutex);
                    auto it = g_Commands.find(cmdName);
                    if (it != g_Commands.end())
                    {
                        // Convert std::vector<std::string> to C-style arrays
                        std::vector<const char *> argv;
                        for (const auto &arg : args)
                        {
                            argv.push_back(arg.c_str());
                        }

                        it->second->callback(static_cast<int>(argv.size()), argv.data(), it->second->userdata);
                    }
                },
                cmdDescription);
            ::logInfo("[WOLF] Console registration completed: " + cmdName);
        }
        else
        {
            ::logWarning("[WOLF] Console not available, deferring command registration: " + cmdName);
            g_PendingCommands.push_back(cmdName);
        }
    }

    void wolfRuntimeRemoveCommand(WolfModId mod_id, const char *name)
    {
        if (!name)
            return;

        std::lock_guard<std::mutex> lock(g_CommandMutex);

        std::string cmdName(name);
        auto it = g_Commands.find(cmdName);
        if (it != g_Commands.end() && it->second->modId == mod_id)
        {
            g_Commands.erase(it);

            // Remove from actual console system
            if (g_Console)
            {
                g_Console->removeCommand(cmdName);
            }
        }
    }

    void wolfRuntimeExecuteCommand(const char *command_line)
    {
        if (!command_line)
            return;

        if (g_Console)
        {
            g_Console->executeCommand(std::string(command_line));
        }
    }

    void wolfRuntimeConsolePrint(const char *message)
    {
        if (!message)
            return;

        if (g_Console)
        {
            g_Console->printToConsole(std::string(message));
        }
        else
        {
            ::logInfo(std::string(message));
        }
    }

    int wolfRuntimeIsConsoleVisible(void)
    {
        return g_Console ? (g_Console->IsVisible ? 1 : 0) : 0;
    }

    //--- RESOURCE SYSTEM ---

    void wolfRuntimeInterceptResource(WolfModId mod_id, const char *filename, WolfResourceProvider provider, void *userdata)
    {
        if (!filename || !provider)
            return;

        std::lock_guard<std::mutex> lock(g_ResourceMutex);

        auto intercept = std::make_unique<ResourceIntercept>();
        intercept->modId = mod_id;
        intercept->pattern = filename;
        intercept->isPattern = false; // Exact filename match
        intercept->provider = provider;
        intercept->userdata = userdata;

        g_ResourceIntercepts.push_back(std::move(intercept));
    }

    void wolfRuntimeRemoveResourceInterception(WolfModId mod_id, const char *filename)
    {
        if (!filename)
            return;

        std::lock_guard<std::mutex> lock(g_ResourceMutex);

        std::string target(filename);
        auto it =
            std::remove_if(g_ResourceIntercepts.begin(), g_ResourceIntercepts.end(), [mod_id, &target](const std::unique_ptr<ResourceIntercept> &intercept)
                           { return intercept->modId == mod_id && intercept->pattern == target && !intercept->isPattern; });

        if (it != g_ResourceIntercepts.end())
        {
            g_ResourceIntercepts.erase(it, g_ResourceIntercepts.end());
        }
    }

    void wolfRuntimeInterceptResourcePattern(WolfModId mod_id, const char *pattern, WolfResourceProvider provider, void *userdata)
    {
        if (!pattern || !provider)
            return;

        std::lock_guard<std::mutex> lock(g_ResourceMutex);

        auto intercept = std::make_unique<ResourceIntercept>();
        intercept->modId = mod_id;
        intercept->pattern = pattern;
        intercept->isPattern = true; // Wildcard pattern match
        intercept->provider = provider;
        intercept->userdata = userdata;

        g_ResourceIntercepts.push_back(std::move(intercept));
    }

    //--- GUI SYSTEM ---

    int wolfRuntimeRegisterGuiWindow(WolfModId mod_id, const char *window_name, WolfGuiWindowCallback callback, void *userdata, int initially_visible)
    {
        if (!window_name || !callback)
            return 0;

        std::lock_guard<std::mutex> lock(g_GuiMutex);

        // Check if window already exists
        for (const auto &window : g_ModGuiWindows)
        {
            if (window->modId == mod_id && window->windowName == window_name)
                return 0; // Window already registered
        }

        auto guiWindow = std::make_unique<ModGuiWindow>();
        guiWindow->modId = mod_id;
        guiWindow->windowName = window_name;
        guiWindow->callback = callback;
        guiWindow->userdata = userdata;
        guiWindow->isVisible = initially_visible != 0;

        g_ModGuiWindows.push_back(std::move(guiWindow));
        return 1;
    }

    int wolfRuntimeUnregisterGuiWindow(WolfModId mod_id, const char *window_name)
    {
        if (!window_name)
            return 0;

        std::lock_guard<std::mutex> lock(g_GuiMutex);

        auto it = std::remove_if(g_ModGuiWindows.begin(), g_ModGuiWindows.end(), [mod_id, window_name](const std::unique_ptr<ModGuiWindow> &window)
                                 { return window->modId == mod_id && window->windowName == window_name; });

        if (it != g_ModGuiWindows.end())
        {
            g_ModGuiWindows.erase(it, g_ModGuiWindows.end());
            return 1;
        }

        return 0;
    }

    int wolfRuntimeToggleGuiWindow(WolfModId mod_id, const char *window_name)
    {
        if (!window_name)
            return 0;

        std::lock_guard<std::mutex> lock(g_GuiMutex);

        for (auto &window : g_ModGuiWindows)
        {
            if (window->modId == mod_id && window->windowName == window_name)
            {
                window->isVisible = !window->isVisible;
                return 1;
            }
        }

        return 0;
    }

    int wolfRuntimeSetGuiWindowVisible(WolfModId mod_id, const char *window_name, int visible)
    {
        if (!window_name)
            return 0;

        std::lock_guard<std::mutex> lock(g_GuiMutex);

        for (auto &window : g_ModGuiWindows)
        {
            if (window->modId == mod_id && window->windowName == window_name)
            {
                window->isVisible = visible != 0;
                return 1;
            }
        }

        return 0;
    }

    //--- VERSION INFORMATION ---

    const char *wolfRuntimeGetVersion(void)
    {
        return WOLF_VERSION_STRING;
    }

    const char *wolfRuntimeGetBuildInfo(void)
    {
        return WOLF_BUILD_TYPE " (" WOLF_COMPILER ")";
    }

} // extern "C"

//==============================================================================
// INTERNAL RUNTIME FUNCTIONS (for runtime use)
//==============================================================================

namespace wolf::runtime
{

namespace internal
{

/**
 * @brief Check if a mod's framework version is compatible with runtime
 * @param modFrameworkVersion Framework version integer from mod
 * @param modName Name of the mod for logging
 * @return true if compatible, false if incompatible
 */
bool checkVersionCompatibility(unsigned int modFrameworkVersion, const std::string &modName)
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
    // Alias for callPreGameInit for compatibility
    callPreGameInit();
}

void callLateGameInit()
{
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

void callGameTick()
{
    std::lock_guard<std::mutex> lock(g_CallbackMutex);

    for (const auto &pair : g_ModCallbacks)
    {
        WolfModId modId = pair.first;
        const auto &callbacks = pair.second;

        g_CurrentModId = modId; // Set context

        for (const auto &callback : callbacks->gameTick)
        {
            try
            {
                callback.first(callback.second);
            }
            catch (...)
            {
                // Log error but continue with other callbacks
            }
        }
    }

    g_CurrentModId = 0; // Clear context
}

void callGameStart()
{
    std::lock_guard<std::mutex> lock(g_CallbackMutex);

    for (const auto &pair : g_ModCallbacks)
    {
        WolfModId modId = pair.first;
        const auto &callbacks = pair.second;

        g_CurrentModId = modId;

        for (const auto &callback : callbacks->gameStart)
        {
            try
            {
                callback.first(callback.second);
            }
            catch (...)
            {
                // Continue with other callbacks
            }
        }
    }

    g_CurrentModId = 0;
}

void callGameStop()
{
    std::lock_guard<std::mutex> lock(g_CallbackMutex);

    for (const auto &pair : g_ModCallbacks)
    {
        WolfModId modId = pair.first;
        const auto &callbacks = pair.second;

        g_CurrentModId = modId;

        for (const auto &callback : callbacks->gameStop)
        {
            try
            {
                callback.first(callback.second);
            }
            catch (...)
            {
                // Continue with other callbacks
            }
        }
    }

    g_CurrentModId = 0;
}

void callPlayStart()
{
    std::lock_guard<std::mutex> lock(g_CallbackMutex);

    for (const auto &pair : g_ModCallbacks)
    {
        WolfModId modId = pair.first;
        const auto &callbacks = pair.second;

        g_CurrentModId = modId;

        for (const auto &callback : callbacks->playStart)
        {
            try
            {
                callback.first(callback.second);
            }
            catch (...)
            {
                // Continue with other callbacks
            }
        }
    }

    g_CurrentModId = 0;
}

void callReturnToMenu()
{
    std::lock_guard<std::mutex> lock(g_CallbackMutex);

    for (const auto &pair : g_ModCallbacks)
    {
        WolfModId modId = pair.first;
        const auto &callbacks = pair.second;

        g_CurrentModId = modId;

        for (const auto &callback : callbacks->returnToMenu)
        {
            try
            {
                callback.first(callback.second);
            }
            catch (...)
            {
                // Continue with other callbacks
            }
        }
    }

    g_CurrentModId = 0;
}

void callItemPickup(int itemId, int count)
{
    std::lock_guard<std::mutex> lock(g_CallbackMutex);

    for (const auto &pair : g_ModCallbacks)
    {
        WolfModId modId = pair.first;
        const auto &callbacks = pair.second;

        g_CurrentModId = modId;

        for (const auto &callback : callbacks->itemPickup)
        {
            try
            {
                callback.first(itemId, count, callback.second);
            }
            catch (...)
            {
                // Continue with other callbacks
            }
        }
    }

    g_CurrentModId = 0;
}

void processMemoryWatches()
{
    std::lock_guard<std::mutex> lock(g_WatchMutex);

    for (const auto &watch : g_MemoryWatches)
    {
        std::vector<uint8_t> currentData(watch->size);
        if (wolfRuntimeReadMemory(watch->start, currentData.data(), watch->size))
        {
            if (currentData != watch->lastData)
            {
                // Memory changed - call callback
                g_CurrentModId = watch->modId;
                try
                {
                    watch->callback(watch->start, watch->lastData.data(), currentData.data(), watch->size, watch->userdata);
                }
                catch (...)
                {
                    // Continue monitoring other watches
                }

                // Update last known data
                watch->lastData = currentData;
            }
        }
    }

    g_CurrentModId = 0;
}

const char *interceptResourceLoad(const char *originalPath)
{
    if (!originalPath)
        return nullptr;

    std::lock_guard<std::mutex> lock(g_ResourceMutex);

    std::string path(originalPath);

    // Check exact filename matches first
    for (const auto &intercept : g_ResourceIntercepts)
    {
        if (!intercept->isPattern && intercept->pattern == path)
        {
            const char *replacement = intercept->provider(originalPath, intercept->userdata);
            if (replacement)
            {
                return replacement;
            }
        }
    }

    // Then check pattern matches
    for (const auto &intercept : g_ResourceIntercepts)
    {
        if (intercept->isPattern && matchesPattern(path, intercept->pattern))
        {
            const char *replacement = intercept->provider(originalPath, intercept->userdata);
            if (replacement)
            {
                return replacement;
            }
        }
    }

    return nullptr; // No interception
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
    g_ModCallbacks.clear();
    g_Commands.clear();
    g_MemoryWatches.clear();
    g_ResourceIntercepts.clear();
    g_ModGuiWindows.clear();
}

// GUI system internal functions
void registerModGuiWindow(WolfModId modId, const std::string &windowName, WolfGuiWindowCallback callback, void *userdata, bool initiallyVisible)
{
    wolfRuntimeRegisterGuiWindow(modId, windowName.c_str(), callback, userdata, initiallyVisible ? 1 : 0);
}

bool unregisterModGuiWindow(WolfModId modId, const std::string &windowName)
{
    return wolfRuntimeUnregisterGuiWindow(modId, windowName.c_str()) != 0;
}

bool toggleModGuiWindow(WolfModId modId, const std::string &windowName)
{
    return wolfRuntimeToggleGuiWindow(modId, windowName.c_str()) != 0;
}

bool setModGuiWindowVisible(WolfModId modId, const std::string &windowName, bool visible)
{
    return wolfRuntimeSetGuiWindowVisible(modId, windowName.c_str(), visible ? 1 : 0) != 0;
}

void renderModGuiWindows(int outerWidth, int outerHeight, float uiScale)
{
    std::lock_guard<std::mutex> lock(g_GuiMutex);

    for (auto &window : g_ModGuiWindows)
    {
        if (window->isVisible && window->callback)
        {
            g_CurrentModId = window->modId; // Set context for this mod
            try
            {
                window->callback(outerWidth, outerHeight, uiScale, window->userdata);
            }
            catch (const std::exception &e)
            {
                ModInfo *mod = findMod(window->modId);
                std::string modName = mod ? mod->name : "Unknown";
                ::logError("[WOLF] Exception in GUI callback for mod '" + modName + "', window '" + window->windowName + "': " + e.what());
            }
            catch (...)
            {
                ModInfo *mod = findMod(window->modId);
                std::string modName = mod ? mod->name : "Unknown";
                ::logError("[WOLF] Unknown exception in GUI callback for mod '" + modName + "', window '" + window->windowName + "'");
            }
        }
    }

    g_CurrentModId = 0; // Clear context
}

} // namespace internal

//==============================================================================
// FUNCTION TABLE DEFINITION & CREATION
//==============================================================================

// Define the runtime API structure here to avoid circular dependencies
typedef struct WolfRuntimeAPI
{
    // Mod lifecycle
    WolfModId(__cdecl *getCurrentModId)(void);
    WolfModId(__cdecl *registerMod)(const WolfModInterface *modInterface);

    // Logging
    void(__cdecl *log)(WolfModId mod_id, WolfLogLevel level, const char *message);
    void(__cdecl *setLogPrefix)(WolfModId mod_id, const char *prefix);

    // Memory access
    uintptr_t(__cdecl *getModuleBase)(const char *module_name);
    int(__cdecl *isValidAddress)(uintptr_t address);
    int(__cdecl *readMemory)(uintptr_t address, void *buffer, size_t size);
    int(__cdecl *writeMemory)(uintptr_t address, const void *buffer, size_t size);
    void(__cdecl *findPattern)(const char *pattern, const char *mask, const char *module_name, WolfPatternCallback callback, void *userdata);
    int(__cdecl *watchMemory)(WolfModId mod_id, uintptr_t start, size_t size, WolfMemoryWatchCallback callback, void *userdata, const char *description);
    int(__cdecl *unwatchMemory)(WolfModId mod_id, uintptr_t start);

    // Game hooks & callbacks
    void(__cdecl *registerGameTick)(WolfModId mod_id, WolfGameEventCallback callback, void *userdata);
    void(__cdecl *registerGameStart)(WolfModId mod_id, WolfGameEventCallback callback, void *userdata);
    void(__cdecl *registerGameStop)(WolfModId mod_id, WolfGameEventCallback callback, void *userdata);
    void(__cdecl *registerPlayStart)(WolfModId mod_id, WolfGameEventCallback callback, void *userdata);
    void(__cdecl *registerReturnToMenu)(WolfModId mod_id, WolfGameEventCallback callback, void *userdata);
    void(__cdecl *registerItemPickup)(WolfModId mod_id, WolfItemPickupCallback callback, void *userdata);
    int(__cdecl *hookFunction)(uintptr_t address, void *detour, void **original);

    // Console system
    void(__cdecl *addCommand)(WolfModId mod_id, const char *name, WolfConsoleCommandCallback callback, void *userdata, const char *description);
    void(__cdecl *removeCommand)(WolfModId mod_id, const char *name);
    void(__cdecl *executeCommand)(const char *command_line);
    void(__cdecl *consolePrint)(const char *message);
    int(__cdecl *isConsoleVisible)(void);

    // Resource system
    void(__cdecl *interceptResource)(WolfModId mod_id, const char *filename, WolfResourceProvider provider, void *userdata);
    void(__cdecl *removeResourceInterception)(WolfModId mod_id, const char *filename);
    void(__cdecl *interceptResourcePattern)(WolfModId mod_id, const char *pattern, WolfResourceProvider provider, void *userdata);

    // GUI system
    int(__cdecl *registerGuiWindow)(WolfModId mod_id, const char *window_name, WolfGuiWindowCallback callback, void *userdata, int initially_visible);
    int(__cdecl *unregisterGuiWindow)(WolfModId mod_id, const char *window_name);
    int(__cdecl *toggleGuiWindow)(WolfModId mod_id, const char *window_name);
    int(__cdecl *setGuiWindowVisible)(WolfModId mod_id, const char *window_name, int visible);

    // Version info system
    const char *(__cdecl *getVersion)(void);
    const char *(__cdecl *getBuildInfo)(void);
} WolfRuntimeAPI;

//==============================================================================
// PUBLIC API FUNCTIONS
//==============================================================================

// Process pending commands when console becomes available
void processPendingCommands()
{
    std::lock_guard<std::mutex> lock(g_CommandMutex);

    if (!g_Console || g_PendingCommands.empty())
        return;

    ::logInfo("[WOLF] Processing " + std::to_string(g_PendingCommands.size()) + " pending commands");

    for (const std::string &cmdName : g_PendingCommands)
    {
        auto it = g_Commands.find(cmdName);
        if (it != g_Commands.end())
        {
            ::logInfo("[WOLF] Registering deferred command: " + cmdName);
            std::string cmdDescription = it->second->description;
            g_Console->addCommand(
                cmdName,
                [cmdName](const std::vector<std::string> &args)
                {
                    std::lock_guard<std::mutex> lock(g_CommandMutex);
                    auto it = g_Commands.find(cmdName);
                    if (it != g_Commands.end())
                    {
                        // Convert std::vector<std::string> to C-style arrays
                        std::vector<const char *> argv;
                        for (const auto &arg : args)
                        {
                            argv.push_back(arg.c_str());
                        }

                        it->second->callback(static_cast<int>(argv.size()), argv.data(), it->second->userdata);
                    }
                },
                cmdDescription);
        }
    }

    g_PendingCommands.clear();
    ::logInfo("[WOLF] All pending commands registered");
}

WolfRuntimeAPI *createRuntimeAPI()
{
    static WolfRuntimeAPI runtimeAPI = {
        // Mod lifecycle
        wolfRuntimeGetCurrentModId, wolfRuntimeRegisterMod,

        // Logging
        wolfRuntimeLog, wolfRuntimeSetLogPrefix,

        // Memory access
        wolfRuntimeGetModuleBase, wolfRuntimeIsValidAddress, wolfRuntimeReadMemory, wolfRuntimeWriteMemory, wolfRuntimeFindPattern, wolfRuntimeWatchMemory,
        wolfRuntimeUnwatchMemory,

        // Game hooks & callbacks
        wolfRuntimeRegisterGameTick, wolfRuntimeRegisterGameStart, wolfRuntimeRegisterGameStop, wolfRuntimeRegisterPlayStart, wolfRuntimeRegisterReturnToMenu,
        wolfRuntimeRegisterItemPickup, wolfRuntimeHookFunction,

        // Console system
        wolfRuntimeAddCommand, wolfRuntimeRemoveCommand, wolfRuntimeExecuteCommand, wolfRuntimeConsolePrint, wolfRuntimeIsConsoleVisible,

        // Resource system
        wolfRuntimeInterceptResource, wolfRuntimeRemoveResourceInterception, wolfRuntimeInterceptResourcePattern,

        // GUI system
        wolfRuntimeRegisterGuiWindow, wolfRuntimeUnregisterGuiWindow, wolfRuntimeToggleGuiWindow, wolfRuntimeSetGuiWindowVisible,

        // Version info system
        wolfRuntimeGetVersion, wolfRuntimeGetBuildInfo};

    return &runtimeAPI;
}

} // namespace wolf::runtime
