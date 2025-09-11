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

#if defined(_MSVC_LANG)
#if _MSVC_LANG < 201703L
#error "This header requires C++17 or later"
#endif
#elif __cplusplus < 201703L
#error "This header requires C++17 or later"
#endif

// FRAMEWORK VERSION
// Fallback version information
#define WOLF_VERSION_MAJOR 0
#define WOLF_VERSION_MINOR 1
#define WOLF_VERSION_PATCH 0
#define WOLF_VERSION_STRING "0.1.0-Fallback"

// Semantic version as single integer for comparisons
#define WOLF_VERSION_INT ((0ULL << 32) | (1ULL << 16) | 0ULL)

// Build information
#define WOLF_BUILD_TYPE "Fallback"
#define WOLF_COMPILER "Fallback"
// END FRAMEWORK VERSION

//==============================================================================
// C RUNTIME API DECLARATIONS
// These function pointers are provided by the runtime via function table injection
//==============================================================================

#include "wolf_function_table.h"

// All C types are now defined in wolf_function_table.h and wolf_types.h

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
        WolfModInterface modInterface = {(earlyInit), (lateInit), wolfModShutdownWrapper, (nameFunc), (versionFunc), WOLF_VERSION_INT, IMGUI_VERSION_NUM};     \
        wolf::detail::current_mod_id = runtime->registerMod(&modInterface);                                                                                    \
        return modInterface;                                                                                                                                   \
    }

#define WOLF_MOD_ENTRY_NO_IMGUI(earlyInit, lateInit, shutdownFunc, nameFunc, versionFunc)                                                                      \
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
        WolfModInterface modInterface = {(earlyInit), (lateInit), wolfModShutdownWrapper, (nameFunc), (versionFunc), WOLF_VERSION_INT, 0};                     \
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
        WolfModInterface modInterface = {                                                                                                                      \
            ModClass::earlyGameInit, ModClass::lateGameInit, wolfModClassShutdownWrapper, ModClass::getName, ModClass::getVersion,                             \
            WOLF_VERSION_INT,        IMGUI_VERSION_NUM};                                                                                                       \
        wolf::detail::current_mod_id = runtime->registerMod(&modInterface);                                                                                    \
        return modInterface;                                                                                                                                   \
    }

#define WOLF_MOD_ENTRY_CLASS_NO_IMGUI(ModClass)                                                                                                                \
    static void __cdecl wolfModClassShutdownWrapper(void) noexcept                                                                                             \
    {                                                                                                                                                          \
        wolf::executeModCleanup(); /* Execute automatic cleanup first */                                                                                       \
        ModClass::shutdown();      /* Then call class shutdown */                                                                                              \
    }                                                                                                                                                          \
    extern "C" __declspec(dllexport) WolfModInterface __cdecl wolfGetModInterface(WolfRuntimeAPI *runtime)                                                     \
    {                                                                                                                                                          \
        wolf::detail::initializeRuntime(runtime);                                                                                                              \
        WolfModInterface modInterface = {                                                                                                                      \
            ModClass::earlyGameInit, ModClass::lateGameInit, wolfModClassShutdownWrapper, ModClass::getName, ModClass::getVersion, WOLF_VERSION_INT, 0};       \
        wolf::detail::current_mod_id = runtime->registerMod(&modInterface);                                                                                    \
        return modInterface;                                                                                                                                   \
    }

} // namespace wolf
