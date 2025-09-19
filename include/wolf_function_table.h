/**
 * @file wolf_function_table.h
 * @brief Wolf Runtime API Function Table Definition
 *
 * This file defines the complete function table structure that provides the
 * interface between mods and the Wolf runtime. The WolfRuntimeAPI structure
 * contains function pointers for all available runtime services including
 * logging, memory access, game hooks, console commands, resource interception,
 * bitfield monitoring, GUI management, and version information.
 *
 * This is the single source of truth for the runtime API structure, ensuring
 * synchronization between the implementation and all consumers.
 */

#ifndef WOLF_FUNCTION_TABLE_H
#define WOLF_FUNCTION_TABLE_H

#include "wolf_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    //==============================================================================
    // WOLF RUNTIME API STRUCTURE
    //==============================================================================

    /**
     * @brief Complete Wolf Runtime API Function Table
     *
     * This structure contains function pointers to all services provided by the
     * Wolf runtime. It is passed to mods during initialization and provides the
     * complete interface for mod-runtime interaction.
     *
     * @attention Insertions to or reordering of this struct constitutes a BREAKING CHANGE. Unless this is your intention, always append to this struct.
     *
     * @note Function pointers should never be NULL when the API is properly initialized
     * @note All functions use __cdecl calling convention for cross-compiler compatibility
     */
    typedef struct WolfRuntimeAPI
    {
        //==========================================================================
        // VERSION INFORMATION SYSTEM
        //==========================================================================

        /**
         * @brief Get Wolf runtime version string
         *
         * Returns the version of the Wolf runtime currently loaded.
         *
         * @return Null-terminated version string (e.g., "1.0.0")
         */
        const char *(__cdecl *getRuntimeVersion)(void);

        /**
         * @brief Get Wolf runtime build information
         *
         * Returns detailed build information including commit hash, build date, etc.
         *
         * @return Null-terminated build information string
         */
        const char *(__cdecl *getRuntimeBuildInfo)(void);

        //==========================================================================
        // MOD LIFECYCLE MANAGEMENT
        //==========================================================================

        /**
         * @brief Get current mod ID for the calling thread
         *
         * Returns the WolfModId of the mod that is currently executing on this thread.
         * Useful for debugging and context tracking.
         *
         * @return Current mod's unique identifier, or 0 if no mod context
         */
        WolfModId(__cdecl *getCurrentModId)(void);

        /**
         * @brief Register a new mod with the runtime
         *
         * Called during mod loading to register the mod interface and obtain a unique ID.
         * This function is typically called automatically during mod loading.
         *
         * @param modInterface Pointer to the mod's interface structure
         * @return Unique mod ID on success, 0 on failure
         */
        WolfModId(__cdecl *registerMod)(const WolfModInterface *modInterface);

        //==========================================================================
        // LOGGING SYSTEM
        //==========================================================================

        /**
         * @brief Log a message with specified severity level
         *
         * Outputs a message to the Wolf logging system with the specified severity.
         * Messages are automatically prefixed with the mod's identifier.
         *
         * @param mod_id ID of the mod sending the message
         * @param level Severity level of the message
         * @param message Null-terminated message string
         */
        void(__cdecl *log)(WolfModId mod_id, WolfLogLevel level, const char *message);

        /**
         * @brief Set custom log prefix for a mod
         *
         * Allows mods to customize their log message prefix instead of using
         * the default "[ModName]" format.
         *
         * @param mod_id ID of the mod setting the prefix
         * @param prefix New prefix string to use for log messages
         */
        void(__cdecl *setLogPrefix)(WolfModId mod_id, const char *prefix);

        //==========================================================================
        // MEMORY ACCESS AND PATTERN SEARCHING
        //==========================================================================

        /**
         * @brief Get base address of a loaded module
         *
         * Retrieves the base memory address where the specified module is loaded.
         * Useful for calculating offsets and hooking functions.
         *
         * @param module_name Name of the module (e.g., "game.exe")
         * @return Base address of the module, or 0 if not found
         */
        uintptr_t(__cdecl *getModuleBase)(const char *module_name);

        /**
         * @brief Check if a memory address is valid for access
         *
         * Verifies that the given address can be safely read from or written to.
         *
         * @param address Memory address to check
         * @return 1 if address is valid, 0 otherwise
         */
        int(__cdecl *isValidAddress)(uintptr_t address);

        /**
         * @brief Read memory from the target process
         *
         * Safely reads memory from the specified address into the provided buffer.
         *
         * @param address Source memory address
         * @param buffer Destination buffer to store read data
         * @param size Number of bytes to read
         * @return 1 on success, 0 on failure
         */
        int(__cdecl *readMemory)(uintptr_t address, void *buffer, size_t size);

        /**
         * @brief Write memory to the target process
         *
         * Safely writes data from the provided buffer to the specified memory address.
         *
         * @param address Destination memory address
         * @param buffer Source buffer containing data to write
         * @param size Number of bytes to write
         * @return 1 on success, 0 on failure
         */
        int(__cdecl *writeMemory)(uintptr_t address, const void *buffer, size_t size);

        /**
         * @brief Search for byte patterns in memory
         *
         * Searches for the specified byte pattern within a module's memory space.
         * The callback is invoked for each match found.
         *
         * @param pattern Byte pattern to search for (hex string format)
         * @param mask Mask string specifying which bytes to match ('x' = match, '?' = wildcard)
         * @param module_name Name of module to search in, or NULL for all modules
         * @param callback Function called for each pattern match
         * @param userdata User data passed to the callback
         */
        void(__cdecl *findPattern)(const char *pattern, const char *mask, const char *module_name, WolfPatternCallback callback, void *userdata);

        /**
         * @brief Monitor memory region for changes
         *
         * Sets up monitoring for a memory region, calling the callback whenever
         * the memory content changes.
         *
         * @param mod_id ID of the mod setting up the watch
         * @param start Starting address of the memory region
         * @param size Size of the memory region in bytes
         * @param callback Function called when memory changes
         * @param userdata User data passed to the callback
         * @param description Human-readable description of what's being watched
         * @return 1 on success, 0 on failure
         */
        int(__cdecl *watchMemory)(WolfModId mod_id, uintptr_t start, size_t size, WolfMemoryWatchCallback callback, void *userdata, const char *description);

        /**
         * @brief Stop monitoring a memory region
         *
         * Removes memory monitoring set up by a previous watchMemory call.
         *
         * @param mod_id ID of the mod that set up the watch
         * @param start Starting address of the watched region
         * @return 1 on success, 0 if no watch was found
         */
        int(__cdecl *unwatchMemory)(WolfModId mod_id, uintptr_t start);

        //==========================================================================
        // GAME HOOKS AND EVENT CALLBACKS
        //==========================================================================

        /**
         * @brief Register callback for game tick events
         *
         * Called every game frame/tick. Useful for continuous monitoring or updates.
         *
         * @param mod_id ID of the mod registering the callback
         * @param callback Function to call on each game tick
         * @param userdata User data passed to the callback
         */
        void(__cdecl *registerGameTick)(WolfModId mod_id, WolfGameEventCallback callback, void *userdata);

        /**
         * @brief Register callback for game start events
         *
         * Called when the game begins (e.g., new game, load game).
         *
         * @param mod_id ID of the mod registering the callback
         * @param callback Function to call when game starts
         * @param userdata User data passed to the callback
         */
        void(__cdecl *registerGameStart)(WolfModId mod_id, WolfGameEventCallback callback, void *userdata);

        /**
         * @brief Register callback for game stop events
         *
         * Called when the game ends or is paused.
         *
         * @param mod_id ID of the mod registering the callback
         * @param callback Function to call when game stops
         * @param userdata User data passed to the callback
         */
        void(__cdecl *registerGameStop)(WolfModId mod_id, WolfGameEventCallback callback, void *userdata);

        /**
         * @brief Register callback for play start events
         *
         * Called when active gameplay begins.
         *
         * @param mod_id ID of the mod registering the callback
         * @param callback Function to call when play starts
         * @param userdata User data passed to the callback
         */
        void(__cdecl *registerPlayStart)(WolfModId mod_id, WolfGameEventCallback callback, void *userdata);

        /**
         * @brief Register callback for return to menu events
         *
         * Called when the player returns to the main menu.
         *
         * @param mod_id ID of the mod registering the callback
         * @param callback Function to call when returning to menu
         * @param userdata User data passed to the callback
         */
        void(__cdecl *registerReturnToMenu)(WolfModId mod_id, WolfGameEventCallback callback, void *userdata);

        /**
         * @brief Register callback for item pickup events
         *
         * Called when the player picks up an item in the game.
         *
         * @param mod_id ID of the mod registering the callback
         * @param callback Function to call when item is picked up
         * @param userdata User data passed to the callback
         */
        void(__cdecl *registerItemPickup)(WolfModId mod_id, WolfItemPickupCallback callback, void *userdata);

        /**
         * @brief Register blocking callback for item pickup events
         *
         * Called when the player picks up an item in the game. Return 1 to block the pickup.
         *
         * @param mod_id ID of the mod registering the callback
         * @param callback Function to call when item is picked up (return 1 to block)
         * @param userdata User data passed to the callback
         * @return 1 on success, 0 on failure
         */
        int(__cdecl *registerItemPickupBlocking)(WolfModId mod_id, WolfItemPickupBlockingCallback callback, void *userdata);

        /**
         * @brief Register callback for brush edit events
         *
         * Called when a brush is edited. Return 1 to block the edit.
         *
         * @param mod_id ID of the mod registering the callback
         * @param callback Function to call when brush is edited (return 1 to block)
         * @param userdata User data passed to the callback
         * @return 1 on success, 0 on failure
         */
        int(__cdecl *registerBrushEdit)(WolfModId mod_id, WolfBrushEditCallback callback, void *userdata);

        /**
         * @brief Install function hook
         *
         * Installs a hook at the specified address, redirecting calls to the detour function.
         *
         * @param address Address of the function to hook
         * @param detour Address of the replacement function
         * @param original Pointer to store the original function address
         * @return 1 on success, 0 on failure
         */
        int(__cdecl *hookFunction)(uintptr_t address, void *detour, void **original);

        //==========================================================================
        // CONSOLE COMMAND SYSTEM
        //==========================================================================

        /**
         * @brief Register a new console command
         *
         * Adds a new command to the developer console that mods can implement.
         *
         * @param mod_id ID of the mod registering the command
         * @param name Command name (without prefix)
         * @param callback Function to call when command is executed
         * @param userdata User data passed to the callback
         * @param description Help text describing the command
         */
        void(__cdecl *addCommand)(WolfModId mod_id, const char *name, WolfConsoleCommandCallback callback, void *userdata, const char *description);

        /**
         * @brief Remove a console command
         *
         * Removes a previously registered console command.
         *
         * @param mod_id ID of the mod that registered the command
         * @param name Name of the command to remove
         */
        void(__cdecl *removeCommand)(WolfModId mod_id, const char *name);

        /**
         * @brief Execute a console command
         *
         * Programmatically executes a console command as if it was typed by the user.
         *
         * @param command_line Complete command line to execute
         */
        void(__cdecl *executeCommand)(const char *command_line);

        /**
         * @brief Print message to console
         *
         * Displays a message in the developer console.
         *
         * @param message Message to display in the console
         */
        void(__cdecl *consolePrint)(const char *message);

        /**
         * @brief Check if console is currently visible
         *
         * Determines whether the developer console is currently open and visible.
         *
         * @return 1 if console is visible, 0 otherwise
         */
        int(__cdecl *isConsoleVisible)(void);

        //==========================================================================
        // RESOURCE INTERCEPTION SYSTEM
        //==========================================================================

        /**
         * @brief Intercept resource file loading
         *
         * Registers a provider to supply custom data when the game requests a specific resource file.
         *
         * @param mod_id ID of the mod registering the interception
         * @param filename Name of the resource file to intercept
         * @param provider Function that provides the resource data
         * @param userdata User data passed to the provider function
         */
        void(__cdecl *interceptResource)(WolfModId mod_id, const char *filename, WolfResourceProvider provider, void *userdata);

        /**
         * @brief Remove resource interception
         *
         * Removes a previously registered resource interception.
         *
         * @param mod_id ID of the mod that registered the interception
         * @param filename Name of the resource file to stop intercepting
         */
        void(__cdecl *removeResourceInterception)(WolfModId mod_id, const char *filename);

        /**
         * @brief Intercept resource files matching a pattern
         *
         * Registers a provider for multiple resource files matching a wildcard pattern.
         *
         * @param mod_id ID of the mod registering the interception
         * @param pattern Wildcard pattern for filenames to intercept
         * @param provider Function that provides the resource data
         * @param userdata User data passed to the provider function
         */
        void(__cdecl *interceptResourcePattern)(WolfModId mod_id, const char *pattern, WolfResourceProvider provider, void *userdata);

        //==========================================================================
        // BITFIELD MONITORING SYSTEM
        //==========================================================================

        /**
         * @brief Create bitfield monitor for absolute address
         *
         * Sets up monitoring of bitfield data at a specific memory address.
         *
         * @param mod_id ID of the mod creating the monitor
         * @param address Absolute memory address to monitor
         * @param size_in_bytes Size of the bitfield data in bytes
         * @param callback Function called when bitfield changes
         * @param userdata User data passed to the callback
         * @param description Human-readable description of the bitfield
         * @return Handle to the monitor, or NULL on failure
         */
        WolfBitfieldMonitorHandle(__cdecl *createBitfieldMonitor)(WolfModId mod_id, uintptr_t address, size_t size_in_bytes,
                                                                  WolfBitfieldChangeCallback callback, void *userdata, const char *description);

        /**
         * @brief Create bitfield monitor for module-relative address
         *
         * Sets up monitoring of bitfield data at an offset within a specific module.
         *
         * @param mod_id ID of the mod creating the monitor
         * @param module_name Name of the module containing the bitfield
         * @param offset Offset from the module base address
         * @param size_in_bytes Size of the bitfield data in bytes
         * @param callback Function called when bitfield changes
         * @param userdata User data passed to the callback
         * @param description Human-readable description of the bitfield
         * @return Handle to the monitor, or NULL on failure
         */
        WolfBitfieldMonitorHandle(__cdecl *createBitfieldMonitorModule)(WolfModId mod_id, const char *module_name, uintptr_t offset, size_t size_in_bytes,
                                                                        WolfBitfieldChangeCallback callback, void *userdata, const char *description);

        /**
         * @brief Destroy a bitfield monitor
         *
         * Removes monitoring and frees resources associated with the monitor handle.
         *
         * @param monitor Handle to the monitor to destroy
         */
        void(__cdecl *destroyBitfieldMonitor)(WolfBitfieldMonitorHandle monitor);

        /**
         * @brief Manually update a bitfield monitor
         *
         * Forces a check for changes in the monitored bitfield data.
         *
         * @param monitor Handle to the monitor to update
         * @return 1 if changes were detected, 0 otherwise
         */
        int(__cdecl *updateBitfieldMonitor)(WolfBitfieldMonitorHandle monitor);

        /**
         * @brief Reset bitfield monitor baseline
         *
         * Updates the monitor's baseline value to the current bitfield state.
         *
         * @param monitor Handle to the monitor to reset
         * @return 1 on success, 0 on failure
         */
        int(__cdecl *resetBitfieldMonitor)(WolfBitfieldMonitorHandle monitor);

        //==========================================================================
        // GUI WINDOW MANAGEMENT
        //==========================================================================

        /**
         * @brief Register a GUI window for rendering
         *
         * Creates a new ImGui window that will be rendered each frame. The callback
         * is executed with proper ImGui context automatically available.
         *
         * @param mod_id ID of the mod registering the window
         * @param window_name Unique name for the window
         * @param callback Function called each frame to render the window
         * @param userdata User data passed to the callback
         * @param initially_visible Whether the window should start visible (1) or hidden (0)
         * @return 1 on success, 0 on failure (e.g., name already registered)
         */
        int(__cdecl *registerGuiWindow)(WolfModId mod_id, const char *window_name, WolfGuiWindowCallback callback, void *userdata, int initially_visible);

        /**
         * @brief Unregister a GUI window
         *
         * Removes a previously registered GUI window and stops rendering it.
         *
         * @param mod_id ID of the mod that registered the window
         * @param window_name Name of the window to unregister
         * @return 1 on success, 0 if window not found
         */
        int(__cdecl *unregisterGuiWindow)(WolfModId mod_id, const char *window_name);

        /**
         * @brief Toggle GUI window visibility
         *
         * Toggles the visibility state of a registered GUI window.
         *
         * @param mod_id ID of the mod that owns the window
         * @param window_name Name of the window to toggle
         * @return 1 on success, 0 if window not found
         */
        int(__cdecl *toggleGuiWindow)(WolfModId mod_id, const char *window_name);

        /**
         * @brief Set GUI window visibility
         *
         * Sets the visibility state of a registered GUI window.
         *
         * @param mod_id ID of the mod that owns the window
         * @param window_name Name of the window to modify
         * @param visible 1 to show the window, 0 to hide it
         * @return 1 on success, 0 if window not found
         */
        int(__cdecl *setGuiWindowVisible)(WolfModId mod_id, const char *window_name, int visible);

        /**
         * @brief Execute function within ImGui context
         *
         * Executes the provided function with the Wolf runtime's ImGui context active.
         * This ensures that ImGui functions called within the callback have access to
         * a valid ImGui context. Primarily useful for advanced scenarios where ImGui
         * code needs to run outside of registered window callbacks.
         *
         * @param mod_id ID of the calling mod
         * @param renderFunc Function to execute with ImGui context active
         * @param userdata User data passed to the render function
         * @return 1 on success, 0 on failure (invalid context or function)
         */
        int(__cdecl *executeInImGuiContext)(WolfModId mod_id, void(__cdecl *renderFunc)(void *userdata), void *userdata);

        /**
         * @brief Get Wolf runtime's ImGui context pointer
         *
         * Returns the Wolf runtime's ImGui context pointer that mods can use to set
         * their own ImGui context. This is needed because each DLL has its own ImGui
         * library instance with separate global state.
         *
         * @return ImGui context pointer, or NULL if ImGui is not initialized
         *
         * @note Mods should call ImGui::SetCurrentContext() with this pointer in their
         *       GUI callbacks to access the Wolf runtime's ImGui context
         */
        void *(__cdecl *getImGuiContext)(void);

        /**
         * @brief Get Wolf's ImGui memory allocator function
         *
         * Returns the memory allocation function used by Wolf's ImGui instance.
         * Mods must call ImGui::SetAllocatorFunctions() with this allocator to avoid
         * heap corruption when sharing ImGui contexts across DLL boundaries.
         *
         * @return Memory allocator function pointer
         */
        void *(__cdecl *getImGuiAllocFunc)(void);

        /**
         * @brief Get Wolf's ImGui memory free function
         *
         * Returns the memory free function used by Wolf's ImGui instance.
         * Mods must call ImGui::SetAllocatorFunctions() with this free function to avoid
         * heap corruption when sharing ImGui contexts across DLL boundaries.
         *
         * @return Memory free function pointer
         */
        void *(__cdecl *getImGuiFreeFunc)(void);

        /**
         * @brief Get Wolf's ImGui allocator user data
         *
         * Returns the user data pointer used by Wolf's ImGui allocator functions.
         *
         * @return User data pointer for allocator functions
         */
        void *(__cdecl *getImGuiAllocUserData)(void);

        /**
         * @brief Get Wolf's ImGui font atlas
         *
         * Returns Wolf's shared ImFontAtlas instance. Mods must use this shared
         * font atlas to prevent GPU/rendering conflicts during styled text rendering.
         * Without shared font atlas, styled text operations (like ImGui::PushStyleColor() + Text())
         * can cause crashes due to separate font texture data.
         *
         * @return Pointer to Wolf's ImFontAtlas, or NULL if not available
         */
        void *(__cdecl *getImGuiFontAtlas)(void);

        /**
         * @brief Get Wolf runtime's ImGui IO configuration
         *
         * Returns Wolf's ImGuiIO configuration that mods should copy when creating their own
         * ImGui contexts. This is critical for preventing assertion failures in ImGui.
         * Mods must copy essential fields like DisplaySize, DeltaTime, etc. to their own context.
         *
         * @return Pointer to Wolf's ImGuiIO, or NULL if not available
         */
        void *(__cdecl *getImGuiIO)(void);

        /**
         * @brief Get Wolf runtime's D3D11 device for mod backend initialization
         *
         * Returns Wolf's ID3D11Device that mods should use when initializing their own
         * ImGui D3D11 backends. This allows multiple backends to share the same device.
         *
         * @return Pointer to Wolf's ID3D11Device, or NULL if not available
         */
        void *(__cdecl *getD3D11Device)(void);

        /**
         * @brief Get Wolf runtime's D3D11 device context for mod backend initialization
         *
         * Returns Wolf's ID3D11DeviceContext that mods should use when initializing their own
         * ImGui D3D11 backends. This allows multiple backends to share the same device context.
         *
         * @return Pointer to Wolf's ID3D11DeviceContext, or NULL if not available
         */
        void *(__cdecl *getD3D11DeviceContext)(void);

        /**
         * @brief Register mod's ImGui draw data for centralized rendering
         *
         * Mods call this after ImGui::Render() to register their draw data with Wolf.
         * Wolf will collect all mod draw data and render it centrally.
         *
         * @param mod_id Mod ID
         * @param draw_data Pointer to ImDrawData from ImGui::GetDrawData()
         */
        void(__cdecl *registerModDrawData)(WolfModId mod_id, void *draw_data);

        /**
         * @brief Register mod's ImGui context for input event forwarding
         *
         * Called by mods to register their ImGui context for input forwarding.
         * Wolf will forward all input events to registered mod contexts.
         *
         * @param mod_id Mod ID
         * @param context Pointer to ImGuiContext
         */
        void(__cdecl *registerModContext)(WolfModId mod_id, void *context);

        /**
         * @brief Unregister mod's ImGui context from input event forwarding
         *
         * Called by mods during cleanup to unregister their ImGui context.
         *
         * @param mod_id Mod ID
         * @param context Pointer to ImGuiContext
         */
        void(__cdecl *unregisterModContext)(WolfModId mod_id, void *context);

        //==========================================================================
        // WIN32 INPUT SYSTEM
        //==========================================================================

        /**
         * @brief Register a Win32 window procedure hook for input handling
         *
         * Allows mods to receive Win32 messages for custom input handling.
         * Callbacks are called in registration order until one returns true.
         *
         * @param mod_id Mod ID
         * @param callback Callback function to handle Win32 messages
         * @param userData User data passed to the callback
         */
        void(__cdecl *registerWndProcHook)(WolfModId mod_id, WolfWndProcCallback callback, void *userData);

        /**
         * @brief Unregister a Win32 window procedure hook
         *
         * @param mod_id Mod ID
         */
        void(__cdecl *unregisterWndProcHook)(WolfModId mod_id);

        //==========================================================================
        // SHOP SYSTEM
        //==========================================================================

        /**
         * @brief Add an item to a shop on a specific map
         *
         * @param mod_id Mod ID that owns this shop item
         * @param map_id Map ID where the shop is located
         * @param item_type Type/ID of the item to add
         * @param cost Cost of the item in yen
         */
        void(__cdecl *addShopItem)(WolfModId mod_id, uint32_t map_id, uint32_t shop_idx, int32_t item_type, int32_t cost);

        /**
         * @brief Add an item to a demon fang shop on a specific map
         *
         * @param mod_id Mod ID that owns this shop item
         * @param map_id Map ID where the demon fang shop is located
         * @param item_type Type/ID of the item to add
         * @param cost Cost of the item in demon fangs
         */
        void(__cdecl *addDemonFangItem)(WolfModId mod_id, uint32_t map_id, uint32_t shop_idx, int32_t item_type, int32_t cost);

        /**
         * @brief Set custom sell value for an item type on a specific map
         *
         * @param mod_id Mod ID making this change
         * @param map_id Map ID where the sell value applies
         * @param item_type Type/ID of the item
         * @param sell_value Custom sell value in yen
         */
        void(__cdecl *setSellValue)(WolfModId mod_id, uint32_t map_id, uint32_t shop_idx, int32_t item_type, int32_t sell_value);

        /**
         * @brief Remove all shop items added by a mod from a specific map
         *
         * @param mod_id Mod ID whose items to remove
         * @param map_id Map ID to clean up
         */
        void(__cdecl *removeModShopItems)(WolfModId mod_id, uint32_t map_id, uint32_t shop_idx);

        /**
         * @brief Remove all demon fang shop items added by a mod from a specific map
         *
         * @param mod_id Mod ID whose items to remove
         * @param map_id Map ID to clean up
         */
        void(__cdecl *removeModDemonFangItems)(WolfModId mod_id, uint32_t map_id, uint32_t shop_idx);

        /**
         * @brief Remove all shop items added by a mod from all maps
         *
         * @param mod_id Mod ID to clean up
         */
        void(__cdecl *cleanupModShops)(WolfModId mod_id);

        /**
         * @brief Register callback for shop purchase events
         *
         * @param mod_id Mod ID registering the callback
         * @param callback Function to call when shop purchases occur
         * @param userdata User data passed to the callback
         */
        void(__cdecl *registerShopPurchase)(WolfModId mod_id, WolfShopPurchaseCallback callback, void *userdata);

        /**
         * @brief Register callback for shop interaction events
         *
         * @param mod_id Mod ID registering the callback
         * @param callback Function to call when shop interactions occur
         * @param userdata User data passed to the callback
         */
        void(__cdecl *registerShopInteract)(WolfModId mod_id, WolfShopInteractCallback callback, void *userdata);

    } WolfRuntimeAPI;

#ifdef __cplusplus
}
#endif

#endif // WOLF_FUNCTION_TABLE_H
