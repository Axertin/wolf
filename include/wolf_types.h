/**
 * @file wolf_types.h
 * @brief Core type definitions for the Wolf Framework
 *
 * This file contains all fundamental type definitions, enums, and structures
 * used throughout the Wolf Framework. These types provide the foundation for
 * mod development and runtime interaction.
 */

#ifndef WOLF_TYPES_H
#define WOLF_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //==============================================================================
    // FUNDAMENTAL TYPES
    //==============================================================================

    /**
     * @brief Unique identifier for loaded mods
     *
     * Each mod receives a unique WolfModId when successfully registered with the
     * Wolf runtime. This ID is used to track mod ownership of resources, callbacks,
     * and other runtime objects.
     *
     * @note ID 0 is reserved and indicates an invalid or uninitialized mod ID
     */
    typedef int WolfModId;

    /**
     * @brief Handle for bitfield monitoring objects
     *
     * Opaque handle used to reference bitfield monitors created through the
     * Wolf runtime API. Used for tracking memory changes in game data structures.
     */
    typedef struct WolfBitfieldMonitor *WolfBitfieldMonitorHandle;

    //==============================================================================
    // LOGGING SYSTEM
    //==============================================================================

    /**
     * @brief Log message severity levels
     *
     * Defines the severity levels available for logging messages through the
     * Wolf framework. Higher values indicate more severe messages.
     */
    typedef enum WolfLogLevel
    {
        WOLF_LOG_DEBUG = 3,   /**< Debug information, lowest priority */
        WOLF_LOG_INFO = 0,    /**< General information messages */
        WOLF_LOG_WARNING = 1, /**< Warning messages for potential issues */
        WOLF_LOG_ERROR = 2    /**< Error messages for serious problems */
    } WolfLogLevel;

    //==============================================================================
    // CALLBACK FUNCTION TYPES
    //==============================================================================

    /**
     * @brief Generic game event callback function
     *
     * Called when specific game events occur (game start, stop, tick, etc.).
     *
     * @param userdata User-provided data passed during callback registration
     */
    typedef void(__cdecl *WolfGameEventCallback)(void *userdata);

    /**
     * @brief Item pickup event callback function
     *
     * Called when the player picks up an item in the game.
     *
     * @param item_id Unique identifier of the picked up item
     * @param count Number of items picked up
     * @param userdata User-provided data passed during callback registration
     */
    typedef void(__cdecl *WolfItemPickupCallback)(int item_id, int count, void *userdata);

    /**
     * @brief Memory pattern search callback function
     *
     * Called for each match found during memory pattern searching.
     *
     * @param address Memory address where the pattern was found
     * @param userdata User-provided data passed during pattern search
     */
    typedef void(__cdecl *WolfPatternCallback)(uintptr_t address, void *userdata);

    /**
     * @brief Memory watch callback function
     *
     * Called when monitored memory region changes.
     *
     * @param address Base address of the changed memory region
     * @param old_data Pointer to the old data (size determined by watch region)
     * @param new_data Pointer to the new data (size determined by watch region)
     * @param size Number of bytes that changed
     * @param userdata User-provided data passed during watch registration
     */
    typedef void(__cdecl *WolfMemoryWatchCallback)(uintptr_t address, const void *old_data, const void *new_data, size_t size, void *userdata);

    /**
     * @brief Console command callback function
     *
     * Called when a registered console command is executed.
     *
     * @param argc Number of arguments (including command name)
     * @param argv Array of argument strings
     * @param userdata User-provided data passed during command registration
     */
    typedef void(__cdecl *WolfConsoleCommandCallback)(int argc, const char **argv, void *userdata);

    /**
     * @brief Resource provider callback function
     *
     * Called to provide custom resource data when intercepting game resources.
     *
     * @param original_path Path to the original resource file
     * @param userdata User-provided data passed during interception registration
     * @return Path to replacement resource, or NULL to use original
     */
    typedef const char *(__cdecl *WolfResourceProvider)(const char *original_path, void *userdata);

    /**
     * @brief Bitfield change callback function
     *
     * Called when monitored bitfield data changes.
     *
     * @param bit_index Index of the bit that changed (0-based)
     * @param old_value Previous value of the bit (0 or 1)
     * @param new_value New value of the bit (0 or 1)
     * @param userdata User-provided data passed during monitor creation
     */
    typedef void(__cdecl *WolfBitfieldChangeCallback)(unsigned int bit_index, int old_value, int new_value, void *userdata);

    /**
     * @brief GUI window rendering callback function
     *
     * Called each frame to render a mod's GUI window. ImGui context is automatically
     * available during callback execution.
     *
     * @param outer_width Current window width in pixels
     * @param outer_height Current window height in pixels
     * @param ui_scale UI scaling factor (1.0 = normal, higher = larger)
     * @param userdata User-provided data passed during window registration
     */
    typedef void(__cdecl *WolfGuiWindowCallback)(int outer_width, int outer_height, float ui_scale, void *userdata);

    //==============================================================================
    // MOD INTERFACE STRUCTURE
    //==============================================================================

    /**
     * @brief Mod interface structure (ABI-stable)
     *
     * Structure that mods must implement to be loaded by the Wolf runtime.
     * Field order is critical - matches macro expectations.
     */
    typedef struct WolfModInterface
    {
        /**
         * @brief Early game initialization callback (optional)
         *
         * Called during early mod loading phase. Can be NULL if not needed.
         */
        void(__cdecl *earlyGameInit)(void);

        /**
         * @brief Late game initialization callback (optional)
         *
         * Called after GUI initialization is complete. Useful for mods that
         * need to register GUI windows. Can be NULL if not needed.
         */
        void(__cdecl *lateGameInit)(void);

        /**
         * @brief Mod shutdown callback (required)
         *
         * Called when the mod is being unloaded. Should clean up all resources.
         * Runtime API may not be available during this call.
         */
        void(__cdecl *shutdown)(void);

        /**
         * @brief Get mod name callback (required)
         *
         * Must return a static string containing the mod's name.
         *
         * @return Null-terminated mod name string
         */
        const char *(__cdecl *getName)(void);

        /**
         * @brief Get mod version callback (optional)
         *
         * Returns the mod's version string. Can be NULL if not implemented.
         *
         * @return Null-terminated version string, or NULL
         */
        const char *(__cdecl *getVersion)(void);

        /**
         * @brief Framework version this mod was compiled against
         *
         * Used for compatibility checking. Should match the framework version
         * the mod was developed with (WOLF_VERSION_INT).
         */
        unsigned int frameworkVersionInt;

        /**
         * @brief ImGui version integer
         *
         * Used for ImGui compatibility checking. Should match the ImGui version
         * the mod was compiled with (IMGUI_VERSION_NUM).
         */
        unsigned int imguiVersionInt;
    } WolfModInterface;

    /**
     * @brief Callback function type for Win32 window procedure hooks
     *
     * @param hwnd Window handle
     * @param msg Window message
     * @param wParam Message parameter
     * @param lParam Message parameter
     * @param userData User data provided during registration
     * @return Non-zero if the message was handled and should not be processed further
     */
    typedef int(__cdecl *WolfWndProcCallback)(void *hwnd, unsigned int msg, uintptr_t wParam, intptr_t lParam, void *userData);

#ifdef __cplusplus
}
#endif

#endif // WOLF_TYPES_H
