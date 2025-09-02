/**
 * @file wolf_gui.hpp
 * @brief WOLF Framework GUI System
 */

#pragma once

#ifndef NOIMGUI

#include "wolf_core.hpp"

// Forward declarations for ImGui types and functions
struct ImGuiContext;
typedef void* (*ImGuiMemAllocFunc)(size_t sz, void* user_data);
typedef void (*ImGuiMemFreeFunc)(void* ptr, void* user_data);

namespace ImGui
{
void SetCurrentContext(ImGuiContext *ctx);
void SetAllocatorFunctions(ImGuiMemAllocFunc alloc_func, ImGuiMemFreeFunc free_func, void* user_data = nullptr);
}

//==============================================================================
// GUI SYSTEM
//==============================================================================

namespace wolf
{

/**
 * @brief Setup shared memory allocators with Wolf runtime
 * @return True if allocators were successfully configured
 *
 * This function configures the mod's ImGui instance to use the same memory
 * allocators as Wolf's ImGui instance. This is CRITICAL for preventing heap
 * corruption when sharing ImGui contexts across DLL boundaries on Windows.
 *
 * This function should be called BEFORE any other ImGui operations in the mod.
 * Typically this would be done in the mod's early initialization.
 *
 * Usage:
 * @code
 * // In mod initialization:
 * if (!wolf::setupSharedImGuiAllocators()) {
 *     logError("Failed to setup shared ImGui allocators!");
 *     return false;
 * }
 * 
 * // Now it's safe to use shared ImGui context
 * wolf::registerGuiWindow("My Window", myCallback);
 * @endcode
 */
inline bool setupSharedImGuiAllocators() noexcept
{
    if (!detail::g_runtime)
        return false;

    // Get Wolf's allocator functions
    void* allocFuncPtr = detail::g_runtime->getImGuiAllocFunc();
    void* freeFuncPtr = detail::g_runtime->getImGuiFreeFunc();
    void* userData = detail::g_runtime->getImGuiAllocUserData();

    if (!allocFuncPtr || !freeFuncPtr)
        return false;

    // Cast to proper function pointers
    ImGuiMemAllocFunc allocFunc = reinterpret_cast<ImGuiMemAllocFunc>(allocFuncPtr);
    ImGuiMemFreeFunc freeFunc = reinterpret_cast<ImGuiMemFreeFunc>(freeFuncPtr);

    // Configure this mod's ImGui to use Wolf's allocators
    ImGui::SetAllocatorFunctions(allocFunc, freeFunc, userData);
    
    return true;
}


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
 * This function automatically sets up shared allocators first, then sets the context.
 * Both steps are CRITICAL for safe ImGui usage across DLL boundaries.
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

    // CRITICAL: Set up shared allocators first (required for DLL safety)
    static bool allocatorsSetup = false;
    if (!allocatorsSetup) {
        if (!setupSharedImGuiAllocators()) {
            return false;
        }
        allocatorsSetup = true;
    }

    // Now get and set the shared context
    void *context = detail::g_runtime->getImGuiContext();
    if (!context)
        return false;

    // Set the ImGui context in this DLL's ImGui instance
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
