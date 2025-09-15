#include "wolf_runtime_api.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// Include version information
#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <dxgi.h>
#include <imgui.h>
#include <psapi.h>
#include <wolf_version.h>

#include "memory/bitfieldmonitor.hpp"
#include "utilities/console.h"
#include "utilities/gui.h"
#include "utilities/logger.h"
#include "wolf_function_table.h"

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

    // Blocking callbacks
    std::vector<std::pair<WolfItemPickupBlockingCallback, void *>> itemPickupBlocking;
    std::vector<std::pair<WolfBrushEditCallback, void *>> brushEdit;
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

// Storage for mod draw data - cleared each frame
static std::vector<ImDrawData *> g_ModDrawData;

// Storage for all mod ImGui contexts for input forwarding
static std::vector<ImGuiContext *> g_ModContexts;

// Storage for Win32 WndProc hooks
struct WndProcHook
{
    WolfModId modId;
    WolfWndProcCallback callback;
    void *userData;
};
static std::vector<WndProcHook> g_WndProcHooks;

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

// Bitfield monitor system
struct WolfBitfieldMonitorImpl
{
    WolfModId modId;                     ///< ID of the mod that owns this monitor
    uintptr_t address;                   ///< Memory address being monitored
    size_t sizeInBytes;                  ///< Size of the bitfield in bytes
    WolfBitfieldChangeCallback callback; ///< Callback to invoke on changes
    void *userdata;                      ///< User data passed to callback
    std::string description;             ///< Human-readable description
    std::vector<uint8_t> previousData;   ///< Previous state for comparison
    bool initialized;                    ///< Whether we have baseline data

    WolfBitfieldMonitorImpl(WolfModId id, uintptr_t addr, size_t size, WolfBitfieldChangeCallback cb, void *ud, const char *desc)
        : modId(id), address(addr), sizeInBytes(size), callback(cb), userdata(ud), description(desc ? desc : ""), previousData(size, 0), initialized(false)
    {
    }
};

// Global storage for bitfield monitors
std::vector<std::unique_ptr<WolfBitfieldMonitorImpl>> g_BitfieldMonitors;
std::mutex g_BitfieldMonitorsMutex;

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

        memcpy(buffer, reinterpret_cast<void *>(address), size);
        return 1;
    }

    int wolfRuntimeWriteMemory(uintptr_t address, const void *buffer, size_t size)
    {
        if (!buffer || size == 0 || !isValidAddress(address))
            return 0;

        DWORD oldProtect;
        if (!VirtualProtect(reinterpret_cast<void *>(address), size, PAGE_EXECUTE_READWRITE, &oldProtect))
            return 0;

        memcpy(reinterpret_cast<void *>(address), buffer, size);
        VirtualProtect(reinterpret_cast<void *>(address), size, oldProtect, &oldProtect);
        return 1;
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

    int wolfRuntimeRegisterItemPickupBlocking(WolfModId mod_id, WolfItemPickupBlockingCallback callback, void *userdata)
    {
        if (!callback)
            return 0;

        std::lock_guard<std::mutex> lock(g_CallbackMutex);

        if (g_ModCallbacks.find(mod_id) == g_ModCallbacks.end())
        {
            g_ModCallbacks[mod_id] = std::make_unique<GameEventCallbacks>();
        }

        // Check if there's already a blocking callback registered - only allow first one
        if (!g_ModCallbacks[mod_id]->itemPickupBlocking.empty())
        {
            ModInfo *mod = findMod(mod_id);
            std::string modName = mod ? mod->name : "Unknown";
            ::logWarning("[WOLF] Mod '" + modName + "' tried to register blocking item pickup callback, but one is already registered");
            return 0;
        }

        g_ModCallbacks[mod_id]->itemPickupBlocking.emplace_back(callback, userdata);
        return 1;
    }

    int wolfRuntimeRegisterBrushEdit(WolfModId mod_id, WolfBrushEditCallback callback, void *userdata)
    {
        if (!callback)
            return 0;

        std::lock_guard<std::mutex> lock(g_CallbackMutex);

        if (g_ModCallbacks.find(mod_id) == g_ModCallbacks.end())
        {
            g_ModCallbacks[mod_id] = std::make_unique<GameEventCallbacks>();
        }

        // Check if there's already a blocking callback registered - only allow first one
        if (!g_ModCallbacks[mod_id]->brushEdit.empty())
        {
            ModInfo *mod = findMod(mod_id);
            std::string modName = mod ? mod->name : "Unknown";
            ::logWarning("[WOLF] Mod '" + modName + "' tried to register brush edit callback, but one is already registered");
            return 0;
        }

        g_ModCallbacks[mod_id]->brushEdit.emplace_back(callback, userdata);
        return 1;
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

        ::logDebug("[WOLF] Runtime adding command: " + std::string(name));
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
                    std::lock_guard<std::mutex> innerLock(g_CommandMutex);
                    auto cmdIt = g_Commands.find(cmdName);
                    if (cmdIt != g_Commands.end())
                    {
                        // Convert std::vector<std::string> to C-style arrays
                        std::vector<const char *> argv;
                        for (const auto &arg : args)
                        {
                            argv.push_back(arg.c_str());
                        }

                        cmdIt->second->callback(static_cast<int>(argv.size()), argv.data(), cmdIt->second->userdata);
                    }
                },
                cmdDescription);
            ::logDebug("[WOLF] Console registration completed: " + cmdName);
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

    int wolfRuntimeExecuteInImGuiContext(WolfModId mod_id, void(__cdecl *renderFunc)(void *userdata), void *userdata)
    {
        if (!renderFunc)
            return 0;

        // Check if ImGui context exists
        if (!ImGui::GetCurrentContext())
        {
            ModInfo *mod = findMod(mod_id);
            std::string modName = mod ? mod->name : "Unknown";
            ::logError("[WOLF] ImGui context not available for mod '" + modName + "'");
            return 0;
        }

        // Set the current mod ID for context
        WolfModId previousModId = g_CurrentModId;
        g_CurrentModId = mod_id;

        try
        {
            renderFunc(userdata);
            g_CurrentModId = previousModId;
            return 1;
        }
        catch (const std::exception &e)
        {
            g_CurrentModId = previousModId;
            ModInfo *mod = findMod(mod_id);
            std::string modName = mod ? mod->name : "Unknown";
            ::logError("[WOLF] Exception in ImGui context function for mod '" + modName + "': " + e.what());
            return 0;
        }
        catch (...)
        {
            g_CurrentModId = previousModId;
            ModInfo *mod = findMod(mod_id);
            std::string modName = mod ? mod->name : "Unknown";
            ::logError("[WOLF] Unknown exception in ImGui context function for mod '" + modName + "'");
            return 0;
        }
    }

    void *wolfRuntimeGetImGuiContext(void)
    {
        return ImGui::GetCurrentContext();
    }

    void *wolfRuntimeGetImGuiAllocFunc(void)
    {
        ImGuiMemAllocFunc allocFunc;
        ImGuiMemFreeFunc freeFunc;
        void *userData;
        ImGui::GetAllocatorFunctions(&allocFunc, &freeFunc, &userData);
        return reinterpret_cast<void *>(allocFunc);
    }

    void *wolfRuntimeGetImGuiFreeFunc(void)
    {
        ImGuiMemAllocFunc allocFunc;
        ImGuiMemFreeFunc freeFunc;
        void *userData;
        ImGui::GetAllocatorFunctions(&allocFunc, &freeFunc, &userData);
        return reinterpret_cast<void *>(freeFunc);
    }

    void *wolfRuntimeGetImGuiAllocUserData(void)
    {
        ImGuiMemAllocFunc allocFunc;
        ImGuiMemFreeFunc freeFunc;
        void *userData;
        ImGui::GetAllocatorFunctions(&allocFunc, &freeFunc, &userData);
        return userData;
    }

    void *wolfRuntimeGetImGuiFontAtlas(void)
    {
        ImGuiContext *context = ImGui::GetCurrentContext();
        if (!context)
            return nullptr;

        ImGuiIO &io = ImGui::GetIO();
        return io.Fonts;
    }

    void *wolfRuntimeGetImGuiIO(void)
    {
        ImGuiContext *context = ImGui::GetCurrentContext();
        if (!context)
            return nullptr;

        ImGuiIO &io = ImGui::GetIO();
        return &io;
    }

    void *wolfRuntimeGetD3D11Device(void)
    {
        return guiGetD3D11Device();
    }

    void *wolfRuntimeGetD3D11DeviceContext(void)
    {
        return guiGetD3D11DeviceContext();
    }

    void wolfRuntimeRegisterModDrawData(WolfModId mod_id, void *draw_data)
    {
        if (!draw_data)
        {
            ::logError("[WOLF] registerModDrawData: draw_data is null");
            return;
        }

        // Note: g_GuiMutex is already held by renderModGuiWindows when this is called
        try
        {
            ImDrawData *modDrawData = static_cast<ImDrawData *>(draw_data);
            if (modDrawData && modDrawData->Valid && modDrawData->CmdListsCount > 0)
            {
                g_ModDrawData.push_back(modDrawData);
            }
        }
        catch (const std::exception &e)
        {
            ::logError("[WOLF] registerModDrawData: Exception - %s", e.what());
        }
        catch (...)
        {
            ::logError("[WOLF] registerModDrawData: Unknown exception");
        }
    }

    void wolfRuntimeRegisterModContext(WolfModId mod_id, void *context)
    {
        if (!context)
        {
            ::logError("[WOLF] registerModContext: context is null");
            return;
        }

        std::lock_guard<std::mutex> lock(g_GuiMutex);
        ImGuiContext *modContext = static_cast<ImGuiContext *>(context);

        // Check if context is already registered
        auto it = std::find(g_ModContexts.begin(), g_ModContexts.end(), modContext);
        if (it == g_ModContexts.end())
        {
            g_ModContexts.push_back(modContext);
        }
    }

    void wolfRuntimeUnregisterModContext(WolfModId mod_id, void *context)
    {
        if (!context)
            return;

        std::lock_guard<std::mutex> lock(g_GuiMutex);
        ImGuiContext *modContext = static_cast<ImGuiContext *>(context);

        auto it = std::find(g_ModContexts.begin(), g_ModContexts.end(), modContext);
        if (it != g_ModContexts.end())
        {
            g_ModContexts.erase(it);
            ::logInfo("[WOLF] Unregistered mod context %p from input forwarding", modContext);
        }
    }

    void wolfRuntimeRegisterWndProcHook(WolfModId mod_id, WolfWndProcCallback callback, void *userData)
    {
        if (!callback)
        {
            ::logError("[WOLF] registerWndProcHook: callback is null");
            return;
        }

        std::lock_guard<std::mutex> lock(g_GuiMutex);

        // Check if mod already has a hook registered
        auto it = std::find_if(g_WndProcHooks.begin(), g_WndProcHooks.end(), [mod_id](const WndProcHook &hook) { return hook.modId == mod_id; });

        if (it != g_WndProcHooks.end())
        {
            // Update existing hook
            it->callback = callback;
            it->userData = userData;
            ::logDebug("[WOLF] Updated WndProc hook for mod %d", mod_id);
        }
        else
        {
            // Add new hook
            g_WndProcHooks.push_back({mod_id, callback, userData});
            ::logDebug("[WOLF] Registered WndProc hook for mod %d", mod_id);
        }
    }

    void wolfRuntimeUnregisterWndProcHook(WolfModId mod_id)
    {
        std::lock_guard<std::mutex> lock(g_GuiMutex);

        auto it = std::find_if(g_WndProcHooks.begin(), g_WndProcHooks.end(), [mod_id](const WndProcHook &hook) { return hook.modId == mod_id; });

        if (it != g_WndProcHooks.end())
        {
            g_WndProcHooks.erase(it);
            ::logDebug("[WOLF] Unregistered WndProc hook for mod %d", mod_id);
        }
    }

    //--- BITFIELD MONITORING SYSTEM ---

    /**
     * @brief Create a bitfield monitor for a specific memory address
     *
     * @param mod_id ID of the mod creating the monitor
     * @param address Memory address to monitor
     * @param size_in_bytes Size of the bitfield in bytes
     * @param callback Function to call when bits change
     * @param userdata User data passed to callback
     * @param description Human-readable description
     * @return Handle to the monitor, or NULL on failure
     */
    WolfBitfieldMonitorHandle wolfRuntimeCreateBitfieldMonitor(WolfModId mod_id, uintptr_t address, size_t size_in_bytes, WolfBitfieldChangeCallback callback,
                                                               void *userdata, const char *description)
    {
        if (!callback)
        {
            ::logError("[WOLF] Cannot create bitfield monitor: callback is NULL");
            return nullptr;
        }

        if (size_in_bytes == 0 || size_in_bytes > 4096) // Reasonable size limit
        {
            ::logError("[WOLF] Cannot create bitfield monitor: invalid size %zu bytes", size_in_bytes);
            return nullptr;
        }

        if (!isValidAddress(address))
        {
            ::logError("[WOLF] Cannot create bitfield monitor: invalid address 0x%p", reinterpret_cast<void *>(address));
            return nullptr;
        }

        try
        {
            std::lock_guard<std::mutex> lock(g_BitfieldMonitorsMutex);

            // Reserve capacity to prevent vector reallocation and potential memory corruption
            if (g_BitfieldMonitors.capacity() == g_BitfieldMonitors.size())
            {
                g_BitfieldMonitors.reserve(g_BitfieldMonitors.size() + 16);
            }

            auto monitor = std::make_unique<WolfBitfieldMonitorImpl>(mod_id, address, size_in_bytes, callback, userdata, description);

            WolfBitfieldMonitorImpl *handle = monitor.get();
            g_BitfieldMonitors.push_back(std::move(monitor));

            // ::logDebug("[WOLF] Created bitfield monitor for mod %d at 0x%p (%zu bytes): %s", mod_id, (void *)address, size_in_bytes,
            //            description ? description : "unnamed");

            return reinterpret_cast<WolfBitfieldMonitorHandle>(handle);
        }
        catch (const std::exception &e)
        {
            ::logError("[WOLF] Failed to create bitfield monitor: %s", e.what());
            return nullptr;
        }
    }

    /**
     * @brief Create a bitfield monitor for a module-relative address
     *
     * @param mod_id ID of the mod creating the monitor
     * @param module_name Name of the module (e.g., "Okami.exe")
     * @param offset Offset within the module
     * @param size_in_bytes Size of the bitfield in bytes
     * @param callback Function to call when bits change
     * @param userdata User data passed to callback
     * @param description Human-readable description
     * @return Handle to the monitor, or NULL on failure
     */
    WolfBitfieldMonitorHandle wolfRuntimeCreateBitfieldMonitorModule(WolfModId mod_id, const char *module_name, uintptr_t offset, size_t size_in_bytes,
                                                                     WolfBitfieldChangeCallback callback, void *userdata, const char *description)
    {
        if (!module_name)
        {
            ::logError("[WOLF] Cannot create bitfield monitor: module_name is NULL");
            return nullptr;
        }

        // Get module base address
        HMODULE module = GetModuleHandleA(module_name);
        if (!module)
        {
            ::logError("[WOLF] Cannot create bitfield monitor: module '%s' not found", module_name);
            return nullptr;
        }

        uintptr_t moduleBase = reinterpret_cast<uintptr_t>(module);
        uintptr_t actualAddress = moduleBase + offset;

        // Use the regular createBitfieldMonitor function with the resolved address
        return wolfRuntimeCreateBitfieldMonitor(mod_id, actualAddress, size_in_bytes, callback, userdata, description);
    }

    /**
     * @brief Destroy a bitfield monitor and free associated resources
     *
     * @param monitor Handle to the monitor to destroy
     */
    void wolfRuntimeDestroyBitfieldMonitor(WolfBitfieldMonitorHandle monitor)
    {
        if (!monitor)
        {
            ::logWarning("[WOLF] Attempted to destroy NULL bitfield monitor handle");
            return;
        }

        try
        {
            std::lock_guard<std::mutex> lock(g_BitfieldMonitorsMutex);

            WolfBitfieldMonitorImpl *targetMonitor = reinterpret_cast<WolfBitfieldMonitorImpl *>(monitor);

            // Find and remove the monitor from our vector
            auto it = std::find_if(g_BitfieldMonitors.begin(), g_BitfieldMonitors.end(),
                                   [targetMonitor](const std::unique_ptr<WolfBitfieldMonitorImpl> &m) { return m.get() == targetMonitor; });

            if (it != g_BitfieldMonitors.end())
            {
                ::logDebug("[WOLF] Destroyed bitfield monitor for mod %d: %s", (*it)->modId, (*it)->description.c_str());
                g_BitfieldMonitors.erase(it);
            }
            else
            {
                ::logWarning("[WOLF] Attempted to destroy invalid bitfield monitor handle");
            }
        }
        catch (const std::exception &e)
        {
            ::logError("[WOLF] Failed to destroy bitfield monitor: %s", e.what());
        }
    }

    /**
     * @brief Manually update a bitfield monitor and check for changes
     *
     * @param monitor Handle to the monitor to update
     * @return 1 if changes were detected, 0 otherwise
     */
    int wolfRuntimeUpdateBitfieldMonitor(WolfBitfieldMonitorHandle monitor)
    {
        if (!monitor)
        {
            ::logWarning("[WOLF] Attempted to update NULL bitfield monitor handle");
            return 0;
        }

        try
        {
            // NOTE: Mutex should already be held by processBitFieldMonitors caller
            // Removed lock to prevent deadlock

            WolfBitfieldMonitorImpl *targetMonitor = reinterpret_cast<WolfBitfieldMonitorImpl *>(monitor);

            // Find the monitor in our vector to validate it
            auto it = std::find_if(g_BitfieldMonitors.begin(), g_BitfieldMonitors.end(),
                                   [targetMonitor](const std::unique_ptr<WolfBitfieldMonitorImpl> &m) { return m.get() == targetMonitor; });

            if (it == g_BitfieldMonitors.end())
            {
                ::logWarning("[WOLF] Attempted to update invalid bitfield monitor handle");
                return 0;
            }

            WolfBitfieldMonitorImpl *m = it->get();

            // Validate the address is still readable
            if (!isValidAddress(m->address))
            {
                ::logWarning("[WOLF] Bitfield monitor address 0x%p is no longer valid", reinterpret_cast<void *>(m->address));
                return 0;
            }

            // Read current data
            std::vector<uint8_t> currentData(m->sizeInBytes);
            memcpy(currentData.data(), reinterpret_cast<void *>(m->address), m->sizeInBytes);

            int changesDetected = 0;

            if (!m->initialized)
            {
                // First update - just capture baseline
                m->previousData = currentData;
                m->initialized = true;
                // ::logDebug("[WOLF] Bitfield monitor '%s' initialized with baseline data (monitor ptr: %p)", m->description.c_str(), m);
            }
            else
            {
                // Compare byte by byte and find changed bits
                for (size_t byteIdx = 0; byteIdx < m->sizeInBytes; ++byteIdx)
                {
                    uint8_t oldByte = m->previousData[byteIdx];
                    uint8_t newByte = currentData[byteIdx];

                    if (oldByte != newByte)
                    {
                        // Find which bits changed in this byte
                        uint8_t changedBits = oldByte ^ newByte;

                        for (int bitIdx = 0; bitIdx < 8; ++bitIdx)
                        {
                            if (changedBits & (1 << bitIdx))
                            {
                                unsigned int globalBitIndex = static_cast<unsigned int>(byteIdx * 8 + bitIdx);
                                int oldValue = (oldByte & (1 << bitIdx)) ? 1 : 0;
                                int newValue = (newByte & (1 << bitIdx)) ? 1 : 0;

                                // Invoke callback for this bit change
                                m->callback(globalBitIndex, oldValue, newValue, m->userdata);
                                changesDetected = 1;
                            }
                        }
                    }
                }

                // Update previous data
                m->previousData = currentData;
            }

            return changesDetected;
        }
        catch (const std::exception &e)
        {
            ::logError("[WOLF] Failed to update bitfield monitor: %s", e.what());
            return 0;
        }
    }

    /**
     * @brief Reset bitfield monitor baseline to current state
     *
     * Updates the monitor's baseline value to the current bitfield state.
     * The next update will only detect changes from this new baseline.
     *
     * @param monitor Handle to the monitor to reset
     * @return 1 on success, 0 on failure
     */
    int wolfRuntimeResetBitfieldMonitor(WolfBitfieldMonitorHandle monitor)
    {
        if (!monitor)
        {
            ::logWarning("[WOLF] Attempted to reset NULL bitfield monitor handle");
            return 0;
        }

        try
        {
            std::lock_guard<std::mutex> lock(g_BitfieldMonitorsMutex);

            WolfBitfieldMonitorImpl *targetMonitor = reinterpret_cast<WolfBitfieldMonitorImpl *>(monitor);

            // Find the monitor in our vector to validate it
            auto it = std::find_if(g_BitfieldMonitors.begin(), g_BitfieldMonitors.end(),
                                   [targetMonitor](const std::unique_ptr<WolfBitfieldMonitorImpl> &m) { return m.get() == targetMonitor; });

            if (it == g_BitfieldMonitors.end())
            {
                ::logWarning("[WOLF] Attempted to reset invalid bitfield monitor handle");
                return 0;
            }

            WolfBitfieldMonitorImpl *m = it->get();

            // Validate the address is still readable
            if (!isValidAddress(m->address))
            {
                ::logWarning("[WOLF] Bitfield monitor address 0x%p is no longer valid", reinterpret_cast<void *>(m->address));
                return 0;
            }

            // Read current data and set as new baseline
            memcpy(m->previousData.data(), reinterpret_cast<void *>(m->address), m->sizeInBytes);
            m->initialized = true;

            ::logDebug("[WOLF] Reset bitfield monitor baseline for %s", m->description.c_str());
            return 1;
        }
        catch (const std::exception &e)
        {
            ::logError("[WOLF] Failed to reset bitfield monitor: %s", e.what());
            return 0;
        }
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

bool callItemPickupBlocking(int itemId, int count)
{
    std::lock_guard<std::mutex> lock(g_CallbackMutex);

    // First call all non-blocking callbacks
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

    // Then call blocking callbacks (only first registered)
    for (const auto &pair : g_ModCallbacks)
    {
        WolfModId modId = pair.first;
        const auto &callbacks = pair.second;

        g_CurrentModId = modId;

        for (const auto &callback : callbacks->itemPickupBlocking)
        {
            try
            {
                int result = callback.first(itemId, count, callback.second);
                g_CurrentModId = 0;
                return result != 0; // Return true if callback wants to block
            }
            catch (...)
            {
                // Continue with other callbacks
            }
        }
    }

    g_CurrentModId = 0;
    return false; // No blocking callback found or none blocked
}

bool callBrushEdit(int bitIndex, int operation)
{
    std::lock_guard<std::mutex> lock(g_CallbackMutex);

    // Call brush edit callbacks (only first registered)
    for (const auto &pair : g_ModCallbacks)
    {
        WolfModId modId = pair.first;
        const auto &callbacks = pair.second;

        g_CurrentModId = modId;

        for (const auto &callback : callbacks->brushEdit)
        {
            try
            {
                int result = callback.first(bitIndex, operation, callback.second);
                g_CurrentModId = 0;
                return result != 0; // Return true if callback wants to block
            }
            catch (...)
            {
                // Continue with other callbacks
            }
        }
    }

    g_CurrentModId = 0;
    return false; // No blocking callback found or none blocked
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

void processBitFieldMonitors()
{
    std::lock_guard<std::mutex> lock(g_BitfieldMonitorsMutex);

    for (const auto &monitor : g_BitfieldMonitors)
    {
        if (monitor)
        {
            g_CurrentModId = monitor->modId;
            try
            {
                int result = wolfRuntimeUpdateBitfieldMonitor(reinterpret_cast<WolfBitfieldMonitorHandle>(monitor.get()));
                // if (result > 0)
                // {
                //     ::logDebug("[WOLF] BitField monitor %s detected changes", monitor->description.c_str());
                // }
            }
            catch (...)
            {
                // Continue monitoring other BitField monitors
                ::logError("[WOLF] Exception in BitField monitor %s", monitor->description.c_str());
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

void renderModGuiWindows(IDXGISwapChain *pSwapChain)
{
    std::lock_guard<std::mutex> lock(g_GuiMutex);

    if (!pSwapChain)
    {
        ::logError("[WOLF] SwapChain not available for mod GUI rendering");
        return;
    }

    // Calculate window dimensions and UI scale
    RECT rect;
    DXGI_SWAP_CHAIN_DESC desc;
    pSwapChain->GetDesc(&desc);
    HWND hwnd = desc.OutputWindow;
    if (!hwnd || !GetClientRect(hwnd, &rect))
    {
        ::logError("[WOLF] Failed to get window dimensions for mod GUI rendering");
        return;
    }

    int windowWidth = rect.right - rect.left;
    int windowHeight = rect.bottom - rect.top;
    const int BaseWidth = 1920;
    const int BaseHeight = 1080;
    float widthScale = static_cast<float>(windowWidth) / BaseWidth;
    float heightScale = static_cast<float>(windowHeight) / BaseHeight;
    float uiScale = std::min(widthScale, heightScale);

    // Store Wolf's current context to restore later
    ImGuiContext *wolfContext = ImGui::GetCurrentContext();

    // Group windows by mod to manage frame cycles per mod
    std::map<WolfModId, std::vector<ModGuiWindow *>> windowsByMod;
    for (auto &window : g_ModGuiWindows)
    {
        if (window->isVisible && window->callback)
        {
            windowsByMod[window->modId].push_back(window.get());
        }
    }

    // Clear previous frame's draw data
    g_ModDrawData.clear();

    // Execute mod callbacks - they will register their draw data via WOLF_IMGUI_END
    for (auto &[modId, modWindows] : windowsByMod)
    {
        if (modWindows.empty())
            continue;

        g_CurrentModId = modId; // Set context for this mod

        try
        {
            // Execute mod callbacks - they handle their own ImGui frame cycle and register draw data
            for (auto &window : modWindows)
            {
                window->callback(windowWidth, windowHeight, uiScale, window->userdata);
            }
        }
        catch (const std::exception &e)
        {
            ModInfo *mod = findMod(modId);
            std::string modName = mod ? mod->name : "Unknown";
            ::logError("[WOLF] Exception in GUI rendering for mod '" + modName + "': " + e.what());
        }
        catch (...)
        {
            ModInfo *mod = findMod(modId);
            std::string modName = mod ? mod->name : "Unknown";
            ::logError("[WOLF] Unknown exception in GUI rendering for mod '" + modName + "'");
        }
    }

    // Restore Wolf's context
    if (wolfContext)
    {
        ImGui::SetCurrentContext(wolfContext);
    }

    g_CurrentModId = 0; // Clear context
}

void renderCollectedModDrawData()
{
    std::lock_guard<std::mutex> lock(g_GuiMutex);

    if (g_ModDrawData.empty())
        return;

    // Render each mod's draw data with Wolf's D3D11 backend
    for (ImDrawData *drawData : g_ModDrawData)
    {
        if (drawData && drawData->Valid && drawData->CmdListsCount > 0)
        {
            guiRenderDrawData(drawData);
        }
    }
}

// Cross-DLL ImGui input forwarding implementation

/**
 * @brief Forwards input state from Wolf's ImGui context to all mod contexts
 *
 * Copies mouse state, keyboard state, and input events from Wolf's main ImGui context
 * to all registered mod contexts. This enables cross-DLL ImGui functionality where
 * each mod has its own context but shares input from Wolf's Win32 backend.
 *
 * @note Called after Wolf's GUI rendering but before mod GUI rendering each frame
 * @see forwardCharacterToModContexts() for direct character event forwarding
 */
void forwardInputToModContexts()
{
    if (g_ModContexts.empty())
        return;

    // Get Wolf's ImGui IO as the source of input data
    ImGuiContext *wolfContext = ImGui::GetCurrentContext();
    if (!wolfContext)
        return;

    // Ensure Wolf's context has a valid font atlas before proceeding
    ImGuiIO &wolfIO = ImGui::GetIO();
    if (!wolfIO.Fonts || !wolfIO.Fonts->IsBuilt())
    {
        // Wolf's context isn't fully initialized yet, skip input forwarding this frame
        return;
    }

    // Note: Keyboard capture state is now handled earlier in guiRenderFrame()
    // before Win32 backend processes input messages

    // Copy Wolf's input state to all mod contexts
    for (ImGuiContext *modContext : g_ModContexts)
    {
        if (modContext && modContext != wolfContext)
        {
            ImGui::SetCurrentContext(modContext);
            ImGuiIO &modIO = ImGui::GetIO();

            // Check if mod context has invalid font atlas
            if (!modIO.Fonts || !modIO.Fonts->IsBuilt())
            {
                // The shared font atlas was rebuilt by Wolf, but mod context doesn't know
                // Try to fix this by ensuring the mod uses the same font atlas as Wolf
                if (wolfIO.Fonts && wolfIO.Fonts->IsBuilt())
                {
                    ::logWarning("[WOLF] Mod context font atlas invalid, attempting to sync with Wolf's atlas");

                    // Replace the mod context's font atlas with Wolf's current one
                    modIO.Fonts = wolfIO.Fonts;

                    // This is a bit of a hack, but should restore the mod's font atlas
                    if (modIO.Fonts && modIO.Fonts->IsBuilt())
                    {
                        ::logInfo("[WOLF] Successfully synced mod font atlas with Wolf");
                    }
                    else
                    {
                        ::logError("[WOLF] Failed to sync mod font atlas, skipping this context");
                        continue;
                    }
                }
                else
                {
                    // Still not ready, skip this context
                    continue;
                }
            }

            // Copy mouse state
            modIO.MousePos = wolfIO.MousePos;
            modIO.MouseDown[0] = wolfIO.MouseDown[0]; // Left button
            modIO.MouseDown[1] = wolfIO.MouseDown[1]; // Right button
            modIO.MouseDown[2] = wolfIO.MouseDown[2]; // Middle button
            modIO.MouseWheel = wolfIO.MouseWheel;
            modIO.MouseWheelH = wolfIO.MouseWheelH;

            // Copy mouse click events for widget focus/activation
            // Suppress old-style cast warning from ImGui macro expansion
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // old-style cast
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif
            for (int i = 0; i < IM_ARRAYSIZE(wolfIO.MouseClicked); i++)
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
            {
                if (wolfIO.MouseClicked[i])
                {
                    modIO.AddMouseButtonEvent(i, true);
                }
                if (wolfIO.MouseReleased[i])
                {
                    modIO.AddMouseButtonEvent(i, false);
                }
                // Copy double click events too
                if (wolfIO.MouseDoubleClicked[i])
                {
                    // ImGui doesn't have a direct AddMouseDoubleClickEvent, but we can simulate it
                    // by ensuring the DoubleClicked flag gets set on the mod context
                    modIO.MouseDoubleClicked[i] = wolfIO.MouseDoubleClicked[i];
                }
            }

            // Copy keyboard state
            modIO.KeyCtrl = wolfIO.KeyCtrl;
            modIO.KeyShift = wolfIO.KeyShift;
            modIO.KeyAlt = wolfIO.KeyAlt;
            modIO.KeySuper = wolfIO.KeySuper;

            // Copy character events from Wolf's input queue (fallback for any that weren't directly forwarded)
            for (int i = 0; i < wolfIO.InputQueueCharacters.Size; i++)
            {
                ImWchar c = wolfIO.InputQueueCharacters[i];
                modIO.AddInputCharacter(c);
            }

            // Copy key events from Wolf's input queue
            // Note: We can't easily iterate through ImGui's internal key events,
            // so we'll focus on the most important keys for text input
            // TODO: Handle all key events which can take ImGui::IsKeyDown
            const ImGuiKey textInputKeys[] = {
                ImGuiKey_Backspace, ImGuiKey_Delete,    ImGuiKey_Enter, ImGuiKey_KeypadEnter, ImGuiKey_LeftArrow, ImGuiKey_RightArrow,
                ImGuiKey_UpArrow,   ImGuiKey_DownArrow, ImGuiKey_Home,  ImGuiKey_End,         ImGuiKey_Tab,       ImGuiKey_Escape,
                ImGuiKey_A,         ImGuiKey_C,         ImGuiKey_V,     ImGuiKey_X,           ImGuiKey_Y,         ImGuiKey_Z // Common shortcuts
            };

            for (ImGuiKey key : textInputKeys)
            {
                bool wolfKeyDown = ImGui::IsKeyDown(key);

                // Temporarily switch to mod context to check its key state
                ImGui::SetCurrentContext(modContext);
                bool modKeyDown = ImGui::IsKeyDown(key);

                // If the key state differs, send the appropriate event to mod
                if (wolfKeyDown != modKeyDown)
                {
                    modIO.AddKeyEvent(key, wolfKeyDown);
                }

                ImGui::SetCurrentContext(wolfContext);
            }

            // Copy display size to ensure proper coordinate mapping
            modIO.DisplaySize = wolfIO.DisplaySize;
        }
    }

    // Always restore Wolf's context, even if we had issues
    ImGui::SetCurrentContext(wolfContext);
}

/**
 * @brief Directly forwards a character input event to all mod contexts
 *
 * This function bypasses Wolf's InputQueueCharacters and immediately adds
 * the character to all mod contexts. Used as a workaround for cross-DLL
 * ImGui issues where the Win32 backend doesn't properly handle WM_CHAR events
 * in mod contexts.
 *
 * @param character The UTF-16 character to forward to mod contexts
 * @note Called directly from WndProc when WM_CHAR events are received
 * @see forwardInputToModContexts() for general input state forwarding
 */
void forwardCharacterToModContexts(ImWchar character)
{
    if (g_ModContexts.empty())
        return;

    ImGuiContext *originalContext = ImGui::GetCurrentContext();

    for (ImGuiContext *modContext : g_ModContexts)
    {
        if (modContext && modContext != originalContext)
        {
            ImGui::SetCurrentContext(modContext);
            ImGuiIO &modIO = ImGui::GetIO();

            // Check if mod context has invalid font atlas and skip if so
            if (!modIO.Fonts || !modIO.Fonts->IsBuilt())
            {
                continue;
            }

            modIO.AddInputCharacter(character);
        }
    }

    // Restore original context
    ImGui::SetCurrentContext(originalContext);
}

bool hasModContexts()
{
    return !g_ModContexts.empty();
}

bool anyModWantsInput()
{
    // Check all mod contexts to see if any want to capture input
    for (ImGuiContext *context : g_ModContexts)
    {
        if (context)
        {
            ImGuiContext *originalContext = ImGui::GetCurrentContext();
            ImGui::SetCurrentContext(context);

            ImGuiIO &io = ImGui::GetIO();
            bool wantsInput = io.WantCaptureMouse || io.WantCaptureKeyboard;

            ImGui::SetCurrentContext(originalContext);

            if (wantsInput)
            {
                return true;
            }
        }
    }
    return false;
}

bool callModWndProcHooks(void *hwnd, unsigned int msg, uintptr_t wParam, intptr_t lParam)
{
    std::lock_guard<std::mutex> lock(g_GuiMutex);

    // Call all registered mod hooks in order until one returns true (handled)
    for (const auto &hook : g_WndProcHooks)
    {
        try
        {
            if (hook.callback && hook.callback(hwnd, msg, wParam, lParam, hook.userData))
            {
                // Hook handled the message, stop processing
                ::logDebug("[WOLF] Mod %d handled WndProc message 0x%X", hook.modId, msg);
                return true;
            }
        }
        catch (const std::exception &e)
        {
            ::logError("[WOLF] Exception in WndProc hook for mod %d: %s", hook.modId, e.what());
        }
        catch (...)
        {
            ::logError("[WOLF] Unknown exception in WndProc hook for mod %d", hook.modId);
        }
    }

    // No hook handled the message
    return false;
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
    int(__cdecl *registerItemPickupBlocking)(WolfModId mod_id, WolfItemPickupBlockingCallback callback, void *userdata);
    int(__cdecl *registerBrushEdit)(WolfModId mod_id, WolfBrushEditCallback callback, void *userdata);
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

    // Bitfield monitoring system
    WolfBitfieldMonitorHandle(__cdecl *createBitfieldMonitor)(WolfModId mod_id, uintptr_t address, size_t size_in_bytes, WolfBitfieldChangeCallback callback,
                                                              void *userdata, const char *description);
    WolfBitfieldMonitorHandle(__cdecl *createBitfieldMonitorModule)(WolfModId mod_id, const char *module_name, uintptr_t offset, size_t size_in_bytes,
                                                                    WolfBitfieldChangeCallback callback, void *userdata, const char *description);
    void(__cdecl *destroyBitfieldMonitor)(WolfBitfieldMonitorHandle monitor);
    int(__cdecl *updateBitfieldMonitor)(WolfBitfieldMonitorHandle monitor);
    int(__cdecl *resetBitfieldMonitor)(WolfBitfieldMonitorHandle monitor);

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
        auto cmdIt = g_Commands.find(cmdName);
        if (cmdIt != g_Commands.end())
        {
            ::logInfo("[WOLF] Registering deferred command: " + cmdName);
            std::string cmdDescription = cmdIt->second->description;
            g_Console->addCommand(
                cmdName,
                [cmdName](const std::vector<std::string> &args)
                {
                    std::lock_guard<std::mutex> innerLock(g_CommandMutex);
                    auto lambdaCmdIt = g_Commands.find(cmdName);
                    if (lambdaCmdIt != g_Commands.end())
                    {
                        // Convert std::vector<std::string> to C-style arrays
                        std::vector<const char *> argv;
                        for (const auto &arg : args)
                        {
                            argv.push_back(arg.c_str());
                        }

                        lambdaCmdIt->second->callback(static_cast<int>(argv.size()), argv.data(), lambdaCmdIt->second->userdata);
                    }
                },
                cmdDescription);
        }
    }

    g_PendingCommands.clear();
    ::logInfo("[WOLF] All pending commands registered");
}

::WolfRuntimeAPI *createRuntimeAPI()
{
    static ::WolfRuntimeAPI runtimeAPI = {// Version info system
                                          .getRuntimeVersion = wolfRuntimeGetVersion,
                                          .getRuntimeBuildInfo = wolfRuntimeGetBuildInfo,

                                          // Mod lifecycle
                                          .getCurrentModId = wolfRuntimeGetCurrentModId,
                                          .registerMod = wolfRuntimeRegisterMod,

                                          // Logging
                                          .log = wolfRuntimeLog,
                                          .setLogPrefix = wolfRuntimeSetLogPrefix,

                                          // Memory access
                                          .getModuleBase = wolfRuntimeGetModuleBase,
                                          .isValidAddress = wolfRuntimeIsValidAddress,
                                          .readMemory = wolfRuntimeReadMemory,
                                          .writeMemory = wolfRuntimeWriteMemory,
                                          .findPattern = wolfRuntimeFindPattern,
                                          .watchMemory = wolfRuntimeWatchMemory,
                                          .unwatchMemory = wolfRuntimeUnwatchMemory,

                                          // Game hooks & callbacks
                                          .registerGameTick = wolfRuntimeRegisterGameTick,
                                          .registerGameStart = wolfRuntimeRegisterGameStart,
                                          .registerGameStop = wolfRuntimeRegisterGameStop,
                                          .registerPlayStart = wolfRuntimeRegisterPlayStart,
                                          .registerReturnToMenu = wolfRuntimeRegisterReturnToMenu,
                                          .registerItemPickup = wolfRuntimeRegisterItemPickup,
                                          .registerItemPickupBlocking = wolfRuntimeRegisterItemPickupBlocking,
                                          .registerBrushEdit = wolfRuntimeRegisterBrushEdit,
                                          .hookFunction = wolfRuntimeHookFunction,

                                          // Console system
                                          .addCommand = wolfRuntimeAddCommand,
                                          .removeCommand = wolfRuntimeRemoveCommand,
                                          .executeCommand = wolfRuntimeExecuteCommand,
                                          .consolePrint = wolfRuntimeConsolePrint,
                                          .isConsoleVisible = wolfRuntimeIsConsoleVisible,

                                          // Resource system
                                          .interceptResource = wolfRuntimeInterceptResource,
                                          .removeResourceInterception = wolfRuntimeRemoveResourceInterception,
                                          .interceptResourcePattern = wolfRuntimeInterceptResourcePattern,

                                          // Bitfield monitoring system
                                          .createBitfieldMonitor = wolfRuntimeCreateBitfieldMonitor,
                                          .createBitfieldMonitorModule = wolfRuntimeCreateBitfieldMonitorModule,
                                          .destroyBitfieldMonitor = wolfRuntimeDestroyBitfieldMonitor,
                                          .updateBitfieldMonitor = wolfRuntimeUpdateBitfieldMonitor,
                                          .resetBitfieldMonitor = wolfRuntimeResetBitfieldMonitor,

                                          // GUI system
                                          .registerGuiWindow = wolfRuntimeRegisterGuiWindow,
                                          .unregisterGuiWindow = wolfRuntimeUnregisterGuiWindow,
                                          .toggleGuiWindow = wolfRuntimeToggleGuiWindow,
                                          .setGuiWindowVisible = wolfRuntimeSetGuiWindowVisible,
                                          .executeInImGuiContext = wolfRuntimeExecuteInImGuiContext,
                                          .getImGuiContext = wolfRuntimeGetImGuiContext,
                                          .getImGuiAllocFunc = wolfRuntimeGetImGuiAllocFunc,
                                          .getImGuiFreeFunc = wolfRuntimeGetImGuiFreeFunc,
                                          .getImGuiAllocUserData = wolfRuntimeGetImGuiAllocUserData,
                                          .getImGuiFontAtlas = wolfRuntimeGetImGuiFontAtlas,
                                          .getImGuiIO = wolfRuntimeGetImGuiIO,
                                          .getD3D11Device = wolfRuntimeGetD3D11Device,
                                          .getD3D11DeviceContext = wolfRuntimeGetD3D11DeviceContext,
                                          .registerModDrawData = wolfRuntimeRegisterModDrawData,
                                          .registerModContext = wolfRuntimeRegisterModContext,
                                          .unregisterModContext = wolfRuntimeUnregisterModContext,
                                          .registerWndProcHook = wolfRuntimeRegisterWndProcHook,
                                          .unregisterWndProcHook = wolfRuntimeUnregisterWndProcHook};

    return &runtimeAPI;
}

} // namespace wolf::runtime
