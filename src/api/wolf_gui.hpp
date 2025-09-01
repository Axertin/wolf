/**
 * @file wolf_gui.hpp
 * @brief WOLF Framework GUI System
 */

#pragma once

#ifndef NOIMGUI

#include "wolf_core.hpp"

// Forward declaration for ImGui types
struct ImGuiContext;
namespace ImGui
{
void SetCurrentContext(ImGuiContext *ctx);
}

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

/**
 * @brief Set the current ImGui context to Wolf's ImGui context
 * @return True if context was successfully set
 *
 * This function sets the mod's ImGui context to match the Wolf runtime's context.
 * This is necessary because each DLL has its own ImGui library instance with
 * separate global state.
 *
 * Usage in GUI callbacks:
 * @code
 * void myGuiCallback(int width, int height, float scale) {
 *     if (!wolf::setImGuiContext()) {
 *         return; // Cannot render without valid context
 *     }
 *
 *     ImGui::Begin("My Window");
 *     // ... ImGui code here ...
 *     ImGui::End();
 * }
 * @endcode
 */
inline bool setImGuiContext() noexcept
{
    if (!detail::g_runtime)
        return false;

    void *context = detail::g_runtime->getImGuiContext();
    if (!context)
        return false;

    // Set the ImGui context in this DLL's ImGui instance
    // Note: This requires ImGui to be linked into the mod
    ImGui::SetCurrentContext(static_cast<ImGuiContext *>(context));
    return true;
}

/**
 * @brief Execute a function within the proper ImGui context
 * @param renderFunc Function to execute with ImGui context active
 * @return True if the function was executed successfully
 *
 * This function ensures that ImGui::GetCurrentContext() returns a valid context
 * when the renderFunc is called, allowing safe use of ImGui functions.
 *
 * Note: This is typically not needed for GUI window callbacks registered with
 * registerGuiWindow(), as they automatically have the correct ImGui context.
 * This function is primarily for advanced use cases where you need to execute
 * ImGui code outside of registered window callbacks.
 */
inline bool executeInImGuiContext(std::function<void()> renderFunc) noexcept
{
    if (!detail::g_runtime || !renderFunc)
        return false;

    return detail::g_runtime->executeInImGuiContext(
               detail::getCurrentModId(),
               [](void *userdata) noexcept
               {
                   auto *func = static_cast<std::function<void()> *>(userdata);
                   (*func)();
               },
               &renderFunc) != 0;
}

} // namespace wolf

#endif // NOIMGUI
