/**
 * @file wolf_gui.hpp
 * @brief WOLF Framework GUI System
 */

#pragma once

#include "wolf_core.hpp"

//==============================================================================
// GUI SYSTEM
//==============================================================================

namespace wolf
{

/**
 * @brief Function signature for GUI window callbacks
 * @param outerWidth Window width
 * @param outerHeight Window height
 * @param uiScale UI scaling factor
 */
using GuiWindowCallback = std::function<void(int outerWidth, int outerHeight, float uiScale)>;

namespace detail
{
/**
 * @brief Simple storage for GUI window callbacks
 */
inline std::vector<std::unique_ptr<GuiWindowCallback>> gui_window_callbacks;

/**
 * @brief Add a GUI window callback
 * @param callback Callback to store
 * @return Pointer to stored callback for runtime registration
 */
inline GuiWindowCallback *addGuiWindowCallback(GuiWindowCallback &&callback)
{
    auto stored_callback = std::make_unique<GuiWindowCallback>(std::move(callback));
    GuiWindowCallback *callback_ptr = stored_callback.get();
    gui_window_callbacks.push_back(std::move(stored_callback));
    return callback_ptr;
}

/**
 * @brief Clear all GUI window callbacks during shutdown
 */
inline void clearGuiWindowCallbacks()
{
    gui_window_callbacks.clear();
}

} // namespace detail

/**
 * @brief Register a custom GUI window
 * @param windowName Window name/title
 * @param callback Function called to draw the window
 * @param initiallyVisible Whether window starts visible (default: false)
 * @return True if window was successfully registered
 */
inline bool registerGuiWindow(const char *windowName, GuiWindowCallback callback, bool initiallyVisible = false) noexcept
{
    GuiWindowCallback *callback_ptr = detail::addGuiWindowCallback(std::move(callback));

    // Register cleanup handler to ensure callback storage is cleaned up
    static bool cleanup_registered = false;
    if (!cleanup_registered)
    {
        registerCleanupHandler([]() { detail::clearGuiWindowCallbacks(); });
        cleanup_registered = true;
    }

    if (!detail::g_runtime)
        return false;

    return detail::g_runtime->registerGuiWindow(
               detail::getCurrentModId(), windowName,
               [](int outer_width, int outer_height, float ui_scale, void *userdata) noexcept
               {
                   auto *cb = static_cast<GuiWindowCallback *>(userdata);
                   (*cb)(outer_width, outer_height, ui_scale);
               },
               callback_ptr, initiallyVisible ? 1 : 0) != 0;
}

/**
 * @brief Unregister a custom GUI window
 * @param windowName Window name to remove
 * @return True if window was successfully unregistered
 */
inline bool unregisterGuiWindow(const char *windowName) noexcept
{
    if (!detail::g_runtime)
        return false;
    return detail::g_runtime->unregisterGuiWindow(detail::getCurrentModId(), windowName) != 0;
}

/**
 * @brief Toggle visibility of a GUI window
 * @param windowName Window name to toggle
 * @return True if window was found and toggled
 */
inline bool toggleGuiWindow(const char *windowName) noexcept
{
    if (!detail::g_runtime)
        return false;
    return detail::g_runtime->toggleGuiWindow(detail::getCurrentModId(), windowName) != 0;
}

/**
 * @brief Set visibility of a GUI window
 * @param windowName Window name
 * @param visible True to show, false to hide
 * @return True if window was found and visibility was set
 */
inline bool setGuiWindowVisible(const char *windowName, bool visible) noexcept
{
    if (!detail::g_runtime)
        return false;
    return detail::g_runtime->setGuiWindowVisible(detail::getCurrentModId(), windowName, visible ? 1 : 0) != 0;
}

} // namespace wolf
