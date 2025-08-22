#pragma once

#include <cstdint>

#include "wolf_function_table.h"

#ifdef __cplusplus
extern "C"
{
#endif

    //==============================================================================
    // MOD IDENTIFICATION & LIFECYCLE
    //==============================================================================

    // WolfModId is now defined in wolf_types.h

    /**
     * @brief Get the mod ID for the calling mod (automatically managed)
     * @return Mod ID for use in other API calls
     */
    WolfModId __cdecl wolfRuntimeGetCurrentModId(void);

    // WolfModInterface is now defined in wolf_types.h

    /**
     * @brief Register a mod with the runtime
     * @param modInterface Mod interface structure
     * @return Mod ID assigned to this mod
     */
    WolfModId __cdecl wolfRuntimeRegisterMod(const WolfModInterface *modInterface);

    //==============================================================================
    // LOGGING
    //==============================================================================

    // WolfLogLevel is now defined in wolf_types.h

    /**
     * @brief Log a message
     * @param mod_id Calling mod ID
     * @param level Log level
     * @param message Message text
     */
    void __cdecl wolfRuntimeLog(WolfModId mod_id, WolfLogLevel level, const char *message);

    /**
     * @brief Set log prefix for a mod
     * @param mod_id Calling mod ID
     * @param prefix Prefix string (e.g., "[MyMod]")
     */
    void __cdecl wolfRuntimeSetLogPrefix(WolfModId mod_id, const char *prefix);

    //==============================================================================
    // MEMORY ACCESS
    //==============================================================================

    /**
     * @brief Get base address of a loaded module
     * @param module_name Module name (e.g., "main.dll")
     * @return Base address, or 0 if module not found
     */
    uintptr_t __cdecl wolfRuntimeGetModuleBase(const char *module_name);

    /**
     * @brief Check if memory address is valid and accessible
     * @param address Memory address to check
     * @return 1 if valid, 0 if invalid
     */
    int __cdecl wolfRuntimeIsValidAddress(uintptr_t address);

    /**
     * @brief Read raw bytes from memory
     * @param address Source address
     * @param buffer Destination buffer
     * @param size Number of bytes to read
     * @return 1 on success, 0 on failure
     */
    int __cdecl wolfRuntimeReadMemory(uintptr_t address, void *buffer, size_t size);

    /**
     * @brief Write raw bytes to memory
     * @param address Destination address
     * @param buffer Source buffer
     * @param size Number of bytes to write
     * @return 1 on success, 0 on failure
     */
    int __cdecl wolfRuntimeWriteMemory(uintptr_t address, const void *buffer, size_t size);

    /**
     * @brief Pattern search callback
     * @param address Address where pattern was found
     * @param userdata User-provided data
     */
    typedef void(__cdecl *WolfPatternCallback)(uintptr_t address, void *userdata);

    /**
     * @brief Search for byte patterns in memory
     * @param pattern Byte pattern string
     * @param mask Pattern mask ('x' = exact, '?' = wildcard)
     * @param module_name Module name to search in (NULL for all modules)
     * @param callback Function called for each match
     * @param userdata User data passed to callback
     */
    void __cdecl wolfRuntimeFindPattern(const char *pattern, const char *mask, const char *module_name, WolfPatternCallback callback, void *userdata);

    /**
     * @brief Memory change callback
     * @param address Address that changed
     * @param old_data Previous data
     * @param new_data New data
     * @param size Size of changed data
     * @param userdata User-provided data
     */
    typedef void(__cdecl *WolfMemoryWatchCallback)(uintptr_t address, const void *old_data, const void *new_data, size_t size, void *userdata);

    /**
     * @brief Watch memory region for changes
     * @param mod_id Calling mod ID
     * @param start Starting address
     * @param size Size of region to watch
     * @param callback Function called when changes detected
     * @param userdata User data passed to callback
     * @param description Optional description for debugging
     * @return 1 on success, 0 on failure
     */
    int __cdecl wolfRuntimeWatchMemory(WolfModId mod_id, uintptr_t start, size_t size, WolfMemoryWatchCallback callback, void *userdata,
                                       const char *description);

    /**
     * @brief Stop watching memory region
     * @param mod_id Calling mod ID
     * @param start Starting address of region to unwatch
     * @return 1 on success, 0 on failure
     */
    int __cdecl wolfRuntimeUnwatchMemory(WolfModId mod_id, uintptr_t start);

    //==============================================================================
    // GAME HOOKS & CALLBACKS
    //==============================================================================

    /**
     * @brief Game event callback function type
     */
    typedef void(__cdecl *WolfGameEventCallback)(void *userdata);

    /**
     * @brief Item pickup callback function type
     * @param item_id Item type identifier
     * @param count Number of items picked up
     * @param userdata User-provided data
     */
    // WolfItemPickupCallback is now defined in wolf_types.h

    /**
     * @brief Register callback for game tick (called every frame)
     * @param mod_id Calling mod ID
     * @param callback Function to call each frame
     * @param userdata User data passed to callback
     */
    void __cdecl wolfRuntimeRegisterGameTick(WolfModId mod_id, WolfGameEventCallback callback, void *userdata);

    /**
     * @brief Register callback for game start
     * @param mod_id Calling mod ID
     * @param callback Function to call when game starts
     * @param userdata User data passed to callback
     */
    void __cdecl wolfRuntimeRegisterGameStart(WolfModId mod_id, WolfGameEventCallback callback, void *userdata);

    /**
     * @brief Register callback for game stop
     * @param mod_id Calling mod ID
     * @param callback Function to call when game stops
     * @param userdata User data passed to callback
     */
    void __cdecl wolfRuntimeRegisterGameStop(WolfModId mod_id, WolfGameEventCallback callback, void *userdata);

    /**
     * @brief Register callback for gameplay start
     * @param mod_id Calling mod ID
     * @param callback Function to call when gameplay starts
     * @param userdata User data passed to callback
     */
    void __cdecl wolfRuntimeRegisterPlayStart(WolfModId mod_id, WolfGameEventCallback callback, void *userdata);

    /**
     * @brief Register callback for return to menu
     * @param mod_id Calling mod ID
     * @param callback Function to call when returning to menu
     * @param userdata User data passed to callback
     */
    void __cdecl wolfRuntimeRegisterReturnToMenu(WolfModId mod_id, WolfGameEventCallback callback, void *userdata);

    /**
     * @brief Register callback for item pickup events
     * @param mod_id Calling mod ID
     * @param callback Function to call when items are picked up
     * @param userdata User data passed to callback
     */
    void __cdecl wolfRuntimeRegisterItemPickup(WolfModId mod_id, WolfItemPickupCallback callback, void *userdata);

    /**
     * @brief Hook a function
     * @param address Absolute memory address of function
     * @param detour Replacement function pointer
     * @param original Pointer to store original function (can be NULL)
     * @return 1 on success, 0 on failure
     */
    int __cdecl wolfRuntimeHookFunction(uintptr_t address, void *detour, void **original);

    //==============================================================================
    // GUI SYSTEM
    //==============================================================================

    /**
     * @brief GUI window callback function type
     * @param outer_width Window width
     * @param outer_height Window height
     * @param ui_scale UI scaling factor
     * @param userdata User-provided data
     */
    typedef void(__cdecl *WolfGuiWindowCallback)(int outer_width, int outer_height, float ui_scale, void *userdata);

    /**
     * @brief Register a custom GUI window
     * @param mod_id Calling mod ID
     * @param window_name Window name/title
     * @param callback Function called to draw the window
     * @param userdata User data passed to callback
     * @param initially_visible Whether window starts visible
     * @return 1 on success, 0 on failure
     */
    int __cdecl wolfRuntimeRegisterGuiWindow(WolfModId mod_id, const char *window_name, WolfGuiWindowCallback callback, void *userdata, int initially_visible);

    /**
     * @brief Unregister a custom GUI window
     * @param mod_id Calling mod ID
     * @param window_name Window name to remove
     * @return 1 on success, 0 on failure
     */
    int __cdecl wolfRuntimeUnregisterGuiWindow(WolfModId mod_id, const char *window_name);

    /**
     * @brief Toggle visibility of a GUI window
     * @param mod_id Calling mod ID
     * @param window_name Window name to toggle
     * @return 1 on success, 0 on failure (window not found)
     */
    int __cdecl wolfRuntimeToggleGuiWindow(WolfModId mod_id, const char *window_name);

    /**
     * @brief Set visibility of a GUI window
     * @param mod_id Calling mod ID
     * @param window_name Window name
     * @param visible 1 to show, 0 to hide
     * @return 1 on success, 0 on failure (window not found)
     */
    int __cdecl wolfRuntimeSetGuiWindowVisible(WolfModId mod_id, const char *window_name, int visible);

    /**
     * @brief Execute a function within the proper ImGui context
     * @param mod_id Mod ID
     * @param renderFunc Function to execute with ImGui context active
     * @param userdata User data to pass to the function
     * @return 1 on success, 0 on failure
     */
    int __cdecl wolfRuntimeExecuteInImGuiContext(WolfModId mod_id, void(__cdecl *renderFunc)(void *userdata), void *userdata);

    /**
     * @brief Get Wolf runtime's ImGui context pointer
     * @return ImGui context pointer, or NULL if not available
     */
    void *__cdecl wolfRuntimeGetImGuiContext(void);

    //==============================================================================
    // CONSOLE SYSTEM
    //==============================================================================

    /**
     * @brief Console command callback
     * @param argc Argument count
     * @param argv Argument array
     * @param userdata User-provided data
     */
    typedef void(__cdecl *WolfConsoleCommandCallback)(int argc, const char **argv, void *userdata);

    /**
     * @brief Register a console command
     * @param mod_id Calling mod ID
     * @param name Command name
     * @param callback Command handler function
     * @param userdata User data passed to callback
     * @param description Command description (can be NULL)
     */
    void __cdecl wolfRuntimeAddCommand(WolfModId mod_id, const char *name, WolfConsoleCommandCallback callback, void *userdata, const char *description);

    /**
     * @brief Unregister a console command
     * @param mod_id Calling mod ID
     * @param name Command name to remove
     */
    void __cdecl wolfRuntimeRemoveCommand(WolfModId mod_id, const char *name);

    /**
     * @brief Execute a command line
     * @param command_line Full command with arguments
     */
    void __cdecl wolfRuntimeExecuteCommand(const char *command_line);

    /**
     * @brief Print message to console
     * @param message Message to display
     */
    void __cdecl wolfRuntimeConsolePrint(const char *message);

    /**
     * @brief Check if console is visible
     * @return 1 if visible, 0 if not
     */
    int __cdecl wolfRuntimeIsConsoleVisible(void);

    //==============================================================================
    // RESOURCE SYSTEM
    //==============================================================================

    /**
     * @brief Resource provider callback
     * @param original_path Original resource path requested by game
     * @param userdata User-provided data
     * @return Path to replacement resource, or NULL to use original
     *
     * TODO: Pattern matching in interceptResourcePattern uses basic substring matching only
     */
    // WolfResourceProvider is now defined in wolf_types.h

    /**
     * @brief Intercept loading of specific resource file
     * @param mod_id Calling mod ID
     * @param filename Exact filename to intercept
     * @param provider Function that returns replacement path
     * @param userdata User data passed to provider
     */
    void __cdecl wolfRuntimeInterceptResource(WolfModId mod_id, const char *filename, WolfResourceProvider provider, void *userdata);

    /**
     * @brief Remove resource interception
     * @param mod_id Calling mod ID
     * @param filename Filename to stop intercepting
     */
    void __cdecl wolfRuntimeRemoveResourceInterception(WolfModId mod_id, const char *filename);

    /**
     * @brief Intercept resources matching pattern
     * @param mod_id Calling mod ID
     * @param pattern Wildcard pattern
     * @param provider Function that returns replacement path
     * @param userdata User data passed to provider
     */
    void __cdecl wolfRuntimeInterceptResourcePattern(WolfModId mod_id, const char *pattern, WolfResourceProvider provider, void *userdata);

    //==============================================================================
    // BITFIELD MONITORING SYSTEM
    //==============================================================================

    // WolfBitfieldMonitorHandle is now defined in wolf_types.h

    /**
     * @brief Bitfield change callback function type
     * @param bit_index Index of the bit that changed (0-based)
     * @param old_value Previous value of the bit (0 or 1)
     * @param new_value New value of the bit (0 or 1)
     * @param userdata User-provided data
     */
    // WolfBitfieldChangeCallback is now defined in wolf_types.h

    /**
     * @brief Create a bitfield monitor for a memory location
     * @param mod_id Calling mod ID
     * @param address Memory address of the bitfield
     * @param size_in_bytes Size of the bitfield in bytes
     * @param callback Function called when bits change
     * @param userdata User data passed to callback
     * @param description Optional description for debugging
     * @return Handle to bitfield monitor, NULL on failure
     */
    WolfBitfieldMonitorHandle __cdecl wolfRuntimeCreateBitfieldMonitor(WolfModId mod_id, uintptr_t address, size_t size_in_bytes,
                                                                       WolfBitfieldChangeCallback callback, void *userdata, const char *description);

    /**
     * @brief Create a bitfield monitor for module + offset
     * @param mod_id Calling mod ID
     * @param module_name Module name (e.g., "main.dll")
     * @param offset Offset from module base
     * @param size_in_bytes Size of the bitfield in bytes
     * @param callback Function called when bits change
     * @param userdata User data passed to callback
     * @param description Optional description for debugging
     * @return Handle to bitfield monitor, NULL on failure
     */
    WolfBitfieldMonitorHandle __cdecl wolfRuntimeCreateBitfieldMonitorModule(WolfModId mod_id, const char *module_name, uintptr_t offset, size_t size_in_bytes,
                                                                             WolfBitfieldChangeCallback callback, void *userdata, const char *description);

    /**
     * @brief Destroy a bitfield monitor and stop monitoring
     * @param monitor Bitfield monitor handle to destroy
     */
    void __cdecl wolfRuntimeDestroyBitfieldMonitor(WolfBitfieldMonitorHandle monitor);

    /**
     * @brief Manually update a bitfield monitor (called automatically during game ticks)
     * @param monitor Bitfield monitor handle
     * @return 1 on success, 0 on failure
     */
    int __cdecl wolfRuntimeUpdateBitfieldMonitor(WolfBitfieldMonitorHandle monitor);

    /**
     * @brief Reset a bitfield monitor to reinitialize baseline state
     * @param monitor Bitfield monitor handle
     * @return 1 on success, 0 on failure
     */
    int __cdecl wolfRuntimeResetBitfieldMonitor(WolfBitfieldMonitorHandle monitor);

    //==============================================================================
    // VERSION INFORMATION
    //==============================================================================

    /**
     * @brief Get the runtime version string
     * @return Version string (e.g., "0.1.0")
     */
    const char *__cdecl wolfRuntimeGetVersion(void);

    /**
     * @brief Get the runtime build information
     * @return Build info string (e.g., "Debug (Clang 20.1.0)")
     */
    const char *__cdecl wolfRuntimeGetBuildInfo(void);

#ifdef __cplusplus
}

//==============================================================================
// C++ INTERNAL API (for runtime implementation)
//==============================================================================

#include <functional>
#include <memory>
#include <string>

// Forward declarations
struct IDXGISwapChain;
typedef unsigned short ImWchar;

namespace wolf::runtime
{

// WolfRuntimeAPI is now defined in wolf_function_table.h

/**
 * @brief Create runtime API function table for mod injection
 * @return Pointer to static function table
 */
::WolfRuntimeAPI *createRuntimeAPI();

/**
 * @brief Process commands that were deferred due to console not being ready
 */
void processPendingCommands();

// Internal functions for runtime implementation
namespace internal
{
bool checkVersionCompatibility(unsigned int modFrameworkVersion, const std::string &modName);
void callPreGameInit();
void callEarlyGameInit();
void callLateGameInit();
void callGameTick();
void callGameStart();
void callGameStop();
void callPlayStart(); // TODO: Hook implementation needed for gameplay detection
void callReturnToMenu();
void callItemPickup(int itemId, int count);
void processMemoryWatches();
void processBitFieldMonitors();
const char *interceptResourceLoad(const char *originalPath);
void shutdownMods();

// GUI system internal functions
void registerModGuiWindow(WolfModId modId, const std::string &windowName, WolfGuiWindowCallback callback, void *userdata, bool initiallyVisible);
bool unregisterModGuiWindow(WolfModId modId, const std::string &windowName);
bool toggleModGuiWindow(WolfModId modId, const std::string &windowName);
bool setModGuiWindowVisible(WolfModId modId, const std::string &windowName, bool visible);
void renderModGuiWindows(IDXGISwapChain *pSwapChain);
void renderCollectedModDrawData();

// Input forwarding system
void forwardInputToModContexts();
void forwardCharacterToModContexts(ImWchar character);
bool hasModContexts();
bool anyModWantsInput();
bool callModWndProcHooks(void *hwnd, unsigned int msg, uintptr_t wParam, intptr_t lParam);
} // namespace internal

} // namespace wolf::runtime

#endif
