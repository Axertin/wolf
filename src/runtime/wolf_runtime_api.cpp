#include "wolf_runtime_api.h"

#include <wolf_version.h>

// Include runtime core components
#include "core/bitfield_monitoring.h"
#include "core/console_system.h"
#include "core/game_hooks.h"
#include "core/gui_system.h"
#include "core/logging.h"
#include "core/memory_access.h"
#include "core/mod_lifecycle.h"
#include "core/resource_system.h"
#include "core/shop_system.h"

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

WolfRuntimeAPI *createRuntimeAPI()
{
    static WolfRuntimeAPI runtimeAPI = {// Version info system
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
                                        .unregisterWndProcHook = wolfRuntimeUnregisterWndProcHook,

                                        // Shop system
                                        .addShopItem = wolfRuntimeAddShopItem,
                                        .addDemonFangItem = wolfRuntimeAddDemonFangItem,
                                        .setSellValue = wolfRuntimeSetSellValue,
                                        .removeModShopItems = wolfRuntimeRemoveModShopItems,
                                        .removeModDemonFangItems = wolfRuntimeRemoveModDemonFangItems,
                                        .cleanupModShops = wolfRuntimeCleanupModShops,
                                        .registerShopPurchase = wolfRuntimeRegisterShopPurchase,
                                        .registerShopInteract = wolfRuntimeRegisterShopInteract};

    return &runtimeAPI;
}

} // namespace wolf::runtime
