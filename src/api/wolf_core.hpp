/**
 * @file wolf_core.hpp
 * @brief WOLF Framework Core - Mod Registration and Basic Infrastructure
 */

#pragma once

#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// FRAMEWORK VERSION
// Fallback version information
#define WOLF_VERSION_MAJOR 0
#define WOLF_VERSION_MINOR 1
#define WOLF_VERSION_PATCH 0
#define WOLF_VERSION_STRING "0.1.0-Fallback"

// Semantic version as single integer for comparisons
#define WOLF_VERSION_INT ((0 << 16) | (1 << 8) | 0)

// Build information
#define WOLF_BUILD_TYPE "Fallback"
#define WOLF_COMPILER "Fallback"
// END FRAMEWORK VERSION

//==============================================================================
// C RUNTIME API DECLARATIONS
// These function pointers are provided by the runtime via function table injection
//==============================================================================

extern "C"
{
    //--- MOD IDENTIFICATION & LIFECYCLE ---

    /**
     * @brief Opaque handle representing a loaded mod
     */
    typedef int WolfModId;

    /**
     * @brief Mod interface structure (ABI-stable)
     */
    typedef void(__cdecl *WolfModInitFunc)(void);
    typedef void(__cdecl *WolfModShutdownFunc)(void);
    typedef const char *(__cdecl *WolfModStringFunc)(void);

    typedef struct WolfModInterface
    {
        WolfModInitFunc earlyGameInit; ///< Early initialization callback (can be NULL)
        WolfModInitFunc lateGameInit;  ///< Late initialization callback (can be NULL)
        WolfModShutdownFunc shutdown;  ///< Shutdown callback (required)
        WolfModStringFunc getName;     ///< Get mod name (required)
        WolfModStringFunc getVersion;  ///< Get mod version (can be NULL for default)
    } WolfModInterface;

    //--- LOGGING ---

    /**
     * @brief Log levels
     */
    typedef enum WolfLogLevel
    {
        WOLF_LOG_INFO = 0,
        WOLF_LOG_WARNING = 1,
        WOLF_LOG_ERROR = 2,
        WOLF_LOG_DEBUG = 3
    } WolfLogLevel;

    //--- CALLBACK TYPES ---

    /**
     * @brief Pattern search callback
     */
    typedef void(__cdecl *WolfPatternCallback)(uintptr_t address, void *userdata);

    /**
     * @brief Memory change callback
     */
    typedef void(__cdecl *WolfMemoryWatchCallback)(uintptr_t address, const void *old_data, const void *new_data, size_t size, void *userdata);

    /**
     * @brief Game event callback function type
     */
    typedef void(__cdecl *WolfGameEventCallback)(void *userdata);

    /**
     * @brief Item pickup callback function type
     */
    typedef void(__cdecl *WolfItemPickupCallback)(int item_id, int count, void *userdata);

    /**
     * @brief Console command callback
     */
    typedef void(__cdecl *WolfConsoleCommandCallback)(int argc, const char **argv, void *userdata);

    /**
     * @brief Resource provider callback
     */
    typedef const char *(__cdecl *WolfResourceProvider)(const char *original_path, void *userdata);

    /**
     * @brief GUI window callback
     */
    typedef void(__cdecl *WolfGuiWindowCallback)(int outer_width, int outer_height, float ui_scale, void *userdata);

    /**
     * @brief Bitfield change callback
     */
    typedef void(__cdecl *WolfBitfieldChangeCallback)(unsigned int bit_index, int old_value, int new_value, void *userdata);

    /**
     * @brief Bitfield monitor handle
     */
    typedef struct WolfBitfieldMonitor *WolfBitfieldMonitorHandle;

    //--- RUNTIME API FUNCTION TABLE ---

    /**
     * @brief Function table provided by runtime to mods
     * All runtime functionality is accessed through this table
     */
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

        // Bitfield monitoring system
        WolfBitfieldMonitorHandle(__cdecl *createBitfieldMonitor)(WolfModId mod_id, uintptr_t address, size_t size_in_bytes,
                                                                  WolfBitfieldChangeCallback callback, void *userdata, const char *description);
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
        const char *(__cdecl *getRuntimeVersion)(void);
        const char *(__cdecl *getRuntimeBuildInfo)(void);
    } WolfRuntimeAPI;

} // extern "C"

//==============================================================================
// C++ FRAMEWORK IMPLEMENTATION
//==============================================================================

namespace wolf
{

//==============================================================================
// MOD INTERFACE & REGISTRATION
//==============================================================================

namespace detail
{
/**
 * @brief Simple global runtime pointer - direct access for game modding
 */
inline WolfRuntimeAPI *g_runtime = nullptr;

/**
 * @brief Initialize runtime pointer
 * @param runtime Runtime API pointer to store
 */
inline void initializeRuntime(WolfRuntimeAPI *runtime) noexcept
{
    g_runtime = runtime;
}

/**
 * @brief Get runtime API pointer
 * @return Runtime API pointer or nullptr if not initialized
 */
inline WolfRuntimeAPI *getRuntime() noexcept
{
    return g_runtime;
}

// Simple mod ID storage for automatic mod identification
inline WolfModId current_mod_id = -1;

/**
 * @brief Get current mod ID
 * @return Current mod ID or -1 if not available
 */
inline WolfModId getCurrentModId() noexcept
{
    if (current_mod_id == -1 && g_runtime)
    {
        current_mod_id = g_runtime->getCurrentModId();
    }
    return current_mod_id;
}

/**
 * @brief Simple cleanup handler storage
 */
inline std::vector<std::function<void()>> cleanup_handlers;

/**
 * @brief Execute all cleanup handlers during mod shutdown
 */
inline void executeCleanup() noexcept
{
    // Execute cleanup handlers in reverse order (LIFO)
    for (auto it = cleanup_handlers.rbegin(); it != cleanup_handlers.rend(); ++it)
    {
        (*it)();
    }
    cleanup_handlers.clear();
}

} // namespace detail

/**
 * @brief Register a cleanup handler for automatic resource management
 * @param handler Function to call during mod shutdown
 *
 * Handlers are executed in LIFO order (last registered, first executed).
 */
inline void registerCleanupHandler(std::function<void()> handler)
{
    detail::cleanup_handlers.push_back(std::move(handler));
}

/**
 * @brief Execute all registered cleanup handlers (internal use only)
 * @warning Should only be called during mod shutdown
 */
inline void executeModCleanup() noexcept
{
    detail::executeCleanup();
}

//==============================================================================
// RUNTIME VERSION INFORMATION
//==============================================================================

/**
 * @brief Get the runtime version string
 * @return Version string (e.g., "0.1.0") or "Unknown" if runtime not available
 */
inline const char *getRuntimeVersion() noexcept
{
    if (detail::g_runtime)
    {
        return detail::g_runtime->getRuntimeVersion();
    }
    return "Unknown";
}

/**
 * @brief Get the runtime build information
 * @return Build info string (e.g., "Debug (Clang 20.1.0)") or "Unknown" if runtime not available
 */
inline const char *getRuntimeBuildInfo() noexcept
{
    if (detail::g_runtime)
    {
        return detail::g_runtime->getRuntimeBuildInfo();
    }
    return "Unknown";
}

//==============================================================================
// ENTRY MACROS
//==============================================================================

/**
 * @brief Register a C-style mod interface with the WOLF framework (recommended)
 * @param earlyInit Function for early initialization (or nullptr)
 * @param lateInit Function for late initialization (or nullptr)
 * @param shutdownFunc Function for cleanup (required)
 * @param nameFunc Function returning mod name (required)
 * @param versionFunc Function returning version (or nullptr for default)
 *
 * Usage:
 * @code
 * void myEarlyInit() { ... }
 * void myLateInit() { ... }
 * void myShutdown() { ... }
 * const char* myGetName() { return "MyMod"; }
 * const char* myGetVersion() { return "1.0.0"; }
 *
 * WOLF_MOD_ENTRY(myEarlyInit, myLateInit, myShutdown, myGetName, myGetVersion)
 * @endcode
 */
#define WOLF_MOD_ENTRY(earlyInit, lateInit, shutdownFunc, nameFunc, versionFunc)                                                                               \
    static void __cdecl wolfModShutdownWrapper(void) noexcept                                                                                                  \
    {                                                                                                                                                          \
        wolf::executeModCleanup(); /* Execute automatic cleanup first */                                                                                       \
        if ((shutdownFunc))                                                                                                                                    \
        {                                                                                                                                                      \
            (shutdownFunc)();                                                                                                                                  \
        } /* Then call user shutdown */                                                                                                                        \
    }                                                                                                                                                          \
    extern "C" __declspec(dllexport) WolfModInterface __cdecl wolfGetModInterface(WolfRuntimeAPI *runtime)                                                     \
    {                                                                                                                                                          \
        wolf::detail::initializeRuntime(runtime);                                                                                                              \
        WolfModInterface modInterface = {(earlyInit), (lateInit), wolfModShutdownWrapper, (nameFunc), (versionFunc)};                                          \
        wolf::detail::current_mod_id = runtime->registerMod(&modInterface);                                                                                    \
        return modInterface;                                                                                                                                   \
    }

/**
 * @brief Simplified macro for common mod pattern
 * @param ModClass A class with static methods: earlyGameInit(), lateGameInit(), shutdown(), getName(), getVersion()
 *
 * Usage:
 * @code
 * class MyMod {
 * public:
 *     static void earlyGameInit() { ... }
 *     static void lateGameInit() { ... }
 *     static void shutdown() { ... }
 *     static const char* getName() { return "MyMod"; }
 *     static const char* getVersion() { return "1.0.0"; }
 * };
 * WOLF_MOD_ENTRY_CLASS(MyMod)
 * @endcode
 */
#define WOLF_MOD_ENTRY_CLASS(ModClass)                                                                                                                         \
    static void __cdecl wolfModClassShutdownWrapper(void) noexcept                                                                                             \
    {                                                                                                                                                          \
        wolf::executeModCleanup(); /* Execute automatic cleanup first */                                                                                       \
        ModClass::shutdown();      /* Then call class shutdown */                                                                                              \
    }                                                                                                                                                          \
    extern "C" __declspec(dllexport) WolfModInterface __cdecl wolfGetModInterface(WolfRuntimeAPI *runtime)                                                     \
    {                                                                                                                                                          \
        wolf::detail::initializeRuntime(runtime);                                                                                                              \
        WolfModInterface modInterface = {ModClass::earlyGameInit, ModClass::lateGameInit, wolfModClassShutdownWrapper, ModClass::getName,                      \
                                         ModClass::getVersion};                                                                                                \
        wolf::detail::current_mod_id = runtime->registerMod(&modInterface);                                                                                    \
        return modInterface;                                                                                                                                   \
    }

} // namespace wolf
