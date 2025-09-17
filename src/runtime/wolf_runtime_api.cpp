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

// Include runtime core components
#include "core/bitfield_monitoring.h"
#include "core/console_system.h"
#include "core/game_hooks.h"
#include "core/gui_system.h"
#include "core/logging.h"
#include "core/memory_access.h"
#include "core/mod_lifecycle.h"
#include "core/resource_system.h"

extern "C"
{
    const char *wolfRuntimeGetVersion(void)
    {
        return WOLF_VERSION_STRING;
    }

    const char *wolfRuntimeGetBuildInfo(void)
    {
        return WOLF_BUILD_TYPE " (" WOLF_COMPILER ")";
    }

} // extern "C"

namespace wolf::runtime
{

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

// Process pending commands now handled by console_system component

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
