/**
 * @file wolf_hooks.hpp
 * @brief WOLF Framework Game Hooks & Callbacks
 */

#pragma once

#include "wolf_core.hpp"
#include "wolf_memory.hpp"

//==============================================================================
// GAME HOOKS & CALLBACKS
//==============================================================================

namespace wolf
{

namespace detail
{
/**
 * @brief Simple storage for game event callbacks
 */
inline std::vector<std::unique_ptr<std::function<void()>>> game_event_callbacks;

/**
 * @brief Simple storage for item pickup callbacks
 */
inline std::vector<std::unique_ptr<std::function<void(int, int)>>> item_pickup_callbacks;

/**
 * @brief Simple storage for blocking item pickup callbacks
 */
inline std::vector<std::unique_ptr<std::function<bool(int, int)>>> item_pickup_blocking_callbacks;

/**
 * @brief Simple storage for brush edit callbacks
 */
inline std::vector<std::unique_ptr<std::function<bool(int, int)>>> brush_edit_callbacks;

/**
 * @brief Add a game event callback
 * @param callback Callback to store
 * @return Pointer to stored callback for runtime registration
 */
inline std::function<void()> *addGameEventCallback(std::function<void()> &&callback)
{
    auto stored_callback = std::make_unique<std::function<void()>>(std::move(callback));
    std::function<void()> *callback_ptr = stored_callback.get();
    game_event_callbacks.push_back(std::move(stored_callback));
    return callback_ptr;
}

/**
 * @brief Add an item pickup callback
 * @param callback Callback to store
 * @return Pointer to stored callback for runtime registration
 */
inline std::function<void(int, int)> *addItemPickupCallback(std::function<void(int, int)> &&callback)
{
    auto stored_callback = std::make_unique<std::function<void(int, int)>>(std::move(callback));
    std::function<void(int, int)> *callback_ptr = stored_callback.get();
    item_pickup_callbacks.push_back(std::move(stored_callback));
    return callback_ptr;
}

/**
 * @brief Add a blocking item pickup callback
 * @param callback Callback to store
 * @return Pointer to stored callback for runtime registration
 */
inline std::function<bool(int, int)> *addItemPickupBlockingCallback(std::function<bool(int, int)> &&callback)
{
    auto stored_callback = std::make_unique<std::function<bool(int, int)>>(std::move(callback));
    std::function<bool(int, int)> *callback_ptr = stored_callback.get();
    item_pickup_blocking_callbacks.push_back(std::move(stored_callback));
    return callback_ptr;
}

/**
 * @brief Add a brush edit callback
 * @param callback Callback to store
 * @return Pointer to stored callback for runtime registration
 */
inline std::function<bool(int, int)> *addBrushEditCallback(std::function<bool(int, int)> &&callback)
{
    auto stored_callback = std::make_unique<std::function<bool(int, int)>>(std::move(callback));
    std::function<bool(int, int)> *callback_ptr = stored_callback.get();
    brush_edit_callbacks.push_back(std::move(stored_callback));
    return callback_ptr;
}

/**
 * @brief Clear all hook callbacks during shutdown
 */
inline void clearHookCallbacks()
{
    game_event_callbacks.clear();
    item_pickup_callbacks.clear();
    item_pickup_blocking_callbacks.clear();
    brush_edit_callbacks.clear();
}

} // namespace detail

/**
 * @brief Register callback for game tick (called every frame)
 * @param callback Function to call each frame
 * @return True if registration succeeded, false otherwise
 */
inline bool onGameTick(std::function<void()> callback)
{
    auto *callback_ptr = detail::addGameEventCallback(std::move(callback));

    // Register cleanup handler to ensure callback storage is cleaned up
    static bool cleanup_registered = false;
    if (!cleanup_registered)
    {
        registerCleanupHandler([]() { detail::clearHookCallbacks(); });
        cleanup_registered = true;
    }

    if (!detail::g_runtime)
        return false;

    detail::g_runtime->registerGameTick(
        detail::getCurrentModId(),
        [](void *userdata) noexcept
        {
            auto *cb = static_cast<std::function<void()> *>(userdata);
            (*cb)();
        },
        callback_ptr);
    return true;
}

/**
 * @brief Register callback for game start
 * @param callback Function to call when game starts
 * @return True if registration succeeded, false otherwise
 */
inline bool onGameStart(std::function<void()> callback)
{
    auto *callback_ptr = detail::addGameEventCallback(std::move(callback));

    if (!detail::g_runtime)
        return false;

    detail::g_runtime->registerGameStart(
        detail::getCurrentModId(),
        [](void *userdata) noexcept
        {
            auto *cb = static_cast<std::function<void()> *>(userdata);
            (*cb)();
        },
        callback_ptr);
    return true;
}

/**
 * @brief Register callback for game stop
 * @param callback Function to call when game stops
 * @return True if registration succeeded, false otherwise
 */
inline bool onGameStop(std::function<void()> callback)
{
    auto *callback_ptr = detail::addGameEventCallback(std::move(callback));

    if (!detail::g_runtime)
        return false;

    detail::g_runtime->registerGameStop(
        detail::getCurrentModId(),
        [](void *userdata) noexcept
        {
            auto *cb = static_cast<std::function<void()> *>(userdata);
            (*cb)();
        },
        callback_ptr);
    return true;
}

/**
 * @brief Register callback for gameplay start (i.e, not in main menu and after a save file loads)
 * @param callback Function to call when gameplay starts
 * @return True if registration succeeded, false otherwise
 */
inline bool onPlayStart(std::function<void()> callback)
{
    auto *callback_ptr = detail::addGameEventCallback(std::move(callback));

    if (!detail::g_runtime)
        return false;

    detail::g_runtime->registerPlayStart(
        detail::getCurrentModId(),
        [](void *userdata) noexcept
        {
            auto *cb = static_cast<std::function<void()> *>(userdata);
            (*cb)();
        },
        callback_ptr);
    return true;
}

/**
 * @brief Register callback for return to menu
 * @param callback Function to call when returning to menu
 * @return True if registration succeeded, false otherwise
 */
inline bool onReturnToMenu(std::function<void()> callback)
{
    auto *callback_ptr = detail::addGameEventCallback(std::move(callback));

    if (!detail::g_runtime)
        return false;

    detail::g_runtime->registerReturnToMenu(
        detail::getCurrentModId(),
        [](void *userdata) noexcept
        {
            auto *cb = static_cast<std::function<void()> *>(userdata);
            (*cb)();
        },
        callback_ptr);
    return true;
}

/**
 * @brief Register callback for item pickup events
 * @param callback Function to call when player picks up items
 * @return True if registration succeeded, false otherwise
 */
inline bool onItemPickup(std::function<void(int itemId, int count)> callback)
{
    auto *callback_ptr = detail::addItemPickupCallback(std::move(callback));

    if (!detail::g_runtime)
        return false;

    detail::g_runtime->registerItemPickup(
        detail::getCurrentModId(),
        [](int item_id, int count, void *userdata) noexcept
        {
            auto *cb = static_cast<std::function<void(int, int)> *>(userdata);
            (*cb)(item_id, count);
        },
        callback_ptr);
    return true;
}

/**
 * @brief Hook a function at absolute address
 * @tparam FuncSig Function signature type
 * @param address Absolute memory address of function
 * @param detour Replacement function
 * @param original Pointer to store original function (optional)
 * @return True if hook was successfully created
 */
template <typename FuncSig> inline bool hookFunction(uintptr_t address, FuncSig detour, FuncSig *original = nullptr) noexcept
{
    void **orig_ptr = original ? reinterpret_cast<void **>(original) : nullptr;

    if (!detail::g_runtime)
        return false;
    return detail::g_runtime->hookFunction(address, reinterpret_cast<void *>(detour), orig_ptr) != 0;
}

/**
 * @brief Hook a function at module + offset
 * @tparam FuncSig Function signature type
 * @param module Module name (e.g., "main.dll")
 * @param offset Offset from module base
 * @param detour Replacement function
 * @param original Pointer to store original function (optional)
 * @return True if hook was successfully created
 */
template <typename FuncSig> inline bool hookFunction(const char *module, uintptr_t offset, FuncSig detour, FuncSig *original = nullptr) noexcept
{
    uintptr_t module_base = getModuleBase(module);
    if (module_base == 0)
        return false;

    return hookFunction(module_base + offset, detour, original);
}

/**
 * @brief Replace a function completely (no original call)
 * @tparam FuncSig Function signature type
 * @param address Absolute memory address of function
 * @param replacement Replacement function
 * @return True if replacement was successful
 */
template <typename FuncSig> inline bool replaceFunction(uintptr_t address, FuncSig replacement) noexcept
{
    return hookFunction(address, replacement, static_cast<FuncSig *>(nullptr));
}

/**
 * @brief Replace a function at module + offset
 * @tparam FuncSig Function signature type
 * @param module Module name (e.g., "main.dll")
 * @param offset Offset from module base
 * @param replacement Replacement function
 * @return True if replacement was successful
 */
template <typename FuncSig> inline bool replaceFunction(const char *module, uintptr_t offset, FuncSig replacement) noexcept
{
    uintptr_t module_base = getModuleBase(module);
    if (module_base == 0)
        return false;

    return replaceFunction(module_base + offset, replacement);
}

/**
 * @brief Register blocking callback for item pickup events
 * @param callback Function to call when player picks up items (return true to block)
 * @return True if registration succeeded, false otherwise
 */
inline bool onItemPickupBlocking(std::function<bool(int itemId, int count)> callback)
{
    auto *callback_ptr = detail::addItemPickupBlockingCallback(std::move(callback));

    if (!detail::g_runtime)
        return false;

    return detail::g_runtime->registerItemPickupBlocking(
               detail::getCurrentModId(),
               [](int item_id, int count, void *userdata) noexcept -> int
               {
                   auto *cb = static_cast<std::function<bool(int, int)> *>(userdata);
                   try
                   {
                       return (*cb)(item_id, count) ? 1 : 0;
                   }
                   catch (...)
                   {
                       return 0; // Default to not blocking on exception
                   }
               },
               callback_ptr) != 0;
}

/**
 * @brief Register callback for brush edit events
 * @param callback Function to call when brush is edited (return true to block)
 * @return True if registration succeeded, false otherwise
 */
inline bool onBrushEdit(std::function<bool(int bitIndex, int operation)> callback)
{
    auto *callback_ptr = detail::addBrushEditCallback(std::move(callback));

    if (!detail::g_runtime)
        return false;

    return detail::g_runtime->registerBrushEdit(
               detail::getCurrentModId(),
               [](int bit_index, int operation, void *userdata) noexcept -> int
               {
                   auto *cb = static_cast<std::function<bool(int, int)> *>(userdata);
                   try
                   {
                       return (*cb)(bit_index, operation) ? 1 : 0;
                   }
                   catch (...)
                   {
                       return 0; // Default to not blocking on exception
                   }
               },
               callback_ptr) != 0;
}

} // namespace wolf
