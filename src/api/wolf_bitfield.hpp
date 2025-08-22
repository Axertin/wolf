/**
 * @file wolf_bitfield.hpp
 * @brief WOLF Framework Bitfield Monitoring System
 */

#pragma once

#include "wolf_core.hpp"

//==============================================================================
// BITFIELD MONITORING SYSTEM
//==============================================================================

namespace wolf
{

/**
 * @brief Handle for a bitfield monitor instance
 */
using BitfieldMonitorHandle = WolfBitfieldMonitorHandle;

/**
 * @brief Callback function for bitfield changes
 * @param bitIndex Index of the bit that changed (0-based)
 * @param oldValue Previous value of the bit (true/false)
 * @param newValue New value of the bit (true/false)
 */
using BitfieldChangeCallback = std::function<void(unsigned int bitIndex, bool oldValue, bool newValue)>;

namespace detail
{
/**
 * @brief Simple storage for bitfield monitor callbacks
 */
inline std::vector<std::unique_ptr<BitfieldChangeCallback>> bitfield_callbacks;

/**
 * @brief Add a bitfield callback
 * @param callback Callback to store
 * @return Pointer to stored callback for runtime registration
 */
inline BitfieldChangeCallback *addBitfieldCallback(BitfieldChangeCallback &&callback)
{
    auto stored_callback = std::make_unique<BitfieldChangeCallback>(std::move(callback));
    BitfieldChangeCallback *callback_ptr = stored_callback.get();
    bitfield_callbacks.push_back(std::move(stored_callback));
    return callback_ptr;
}

/**
 * @brief Clear all bitfield callbacks during shutdown
 */
inline void clearBitfieldCallbacks()
{
    bitfield_callbacks.clear();
}

} // namespace detail

/**
 * @brief Create a bitfield monitor for a memory location
 * @param address Memory address of the bitfield
 * @param sizeInBytes Size of the bitfield in bytes
 * @param callback Function called when bits change
 * @param description Optional description for debugging
 * @return Handle to bitfield monitor, or nullptr on failure
 */
inline BitfieldMonitorHandle createBitfieldMonitor(uintptr_t address, size_t sizeInBytes, BitfieldChangeCallback callback,
                                                   const char *description = "") noexcept
{
    BitfieldChangeCallback *callback_ptr = detail::addBitfieldCallback(std::move(callback));

    // Register cleanup handler to ensure callback storage is cleaned up
    static bool cleanup_registered = false;
    if (!cleanup_registered)
    {
        registerCleanupHandler([]() { detail::clearBitfieldCallbacks(); });
        cleanup_registered = true;
    }

    if (!detail::g_runtime)
        return nullptr;

    BitfieldMonitorHandle handle = detail::g_runtime->createBitfieldMonitor(
        detail::getCurrentModId(), address, sizeInBytes,
        [](unsigned int bit_index, int old_value, int new_value, void *userdata) noexcept
        {
            auto *cb = static_cast<BitfieldChangeCallback *>(userdata);
            (*cb)(bit_index, old_value != 0, new_value != 0);
        },
        callback_ptr, description);

    return handle;
}

/**
 * @brief Create a bitfield monitor for module + offset
 * @param module Module name (e.g., "main.dll")
 * @param offset Offset from module base
 * @param sizeInBytes Size of the bitfield in bytes
 * @param callback Function called when bits change
 * @param description Optional description for debugging
 * @return Handle to bitfield monitor, or nullptr on failure
 */
inline BitfieldMonitorHandle createBitfieldMonitor(const char *module, uintptr_t offset, size_t sizeInBytes, BitfieldChangeCallback callback,
                                                   const char *description = "") noexcept
{
    BitfieldChangeCallback *callback_ptr = detail::addBitfieldCallback(std::move(callback));

    if (!detail::g_runtime)
        return nullptr;

    BitfieldMonitorHandle handle = detail::g_runtime->createBitfieldMonitorModule(
        detail::getCurrentModId(), module, offset, sizeInBytes,
        [](unsigned int bit_index, int old_value, int new_value, void *userdata) noexcept
        {
            auto *cb = static_cast<BitfieldChangeCallback *>(userdata);
            (*cb)(bit_index, old_value != 0, new_value != 0);
        },
        callback_ptr, description);

    return handle;
}

/**
 * @brief Destroy a bitfield monitor and stop monitoring
 * @param monitor Bitfield monitor handle to destroy
 * @return True if monitor was successfully destroyed
 */
inline bool destroyBitfieldMonitor(BitfieldMonitorHandle monitor) noexcept
{
    if (!monitor)
        return false;

    if (!detail::g_runtime)
        return false;
    detail::g_runtime->destroyBitfieldMonitor(monitor);
    return true;
}

/**
 * @brief Manually update a bitfield monitor (called automatically during game ticks)
 * @param monitor Bitfield monitor handle
 * @return True on success, false on failure
 */
inline bool updateBitfieldMonitor(BitfieldMonitorHandle monitor) noexcept
{
    if (!monitor)
        return false;

    if (detail::g_runtime)
    {
        return detail::g_runtime->updateBitfieldMonitor(monitor) != 0;
    }
    return false;
}

/**
 * @brief Reset a bitfield monitor to reinitialize baseline state
 * @param monitor Bitfield monitor handle
 * @return True on success, false on failure
 */
inline bool resetBitfieldMonitor(BitfieldMonitorHandle monitor) noexcept
{
    if (!monitor)
        return false;

    if (detail::g_runtime)
    {
        return detail::g_runtime->resetBitfieldMonitor(monitor) != 0;
    }
    return false;
}

} // namespace wolf
