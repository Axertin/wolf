/**
 * @file wolf_gui.hpp
 * @brief WOLF Framework GUI System
 */

#pragma once

#ifndef NOIMGUI

#include "wolf_core.hpp"

#ifndef IMGUI_VERSION
// Forward declarations for ImGui types and functions
struct ImGuiContext;
struct ImFontAtlas;
typedef void *(*ImGuiMemAllocFunc)(size_t sz, void *user_data);
typedef void (*ImGuiMemFreeFunc)(void *ptr, void *user_data);

namespace ImGui
{
void SetCurrentContext(ImGuiContext *ctx);
void SetAllocatorFunctions(ImGuiMemAllocFunc alloc_func, ImGuiMemFreeFunc free_func, void *user_data);
ImGuiContext *CreateContext(ImFontAtlas *shared_font_atlas);
void DestroyContext(ImGuiContext *ctx);
void NewFrame();
void EndFrame();
void Render();
} // namespace ImGui

#endif

//==============================================================================
// GUI SYSTEM
//==============================================================================

namespace wolf
{

// Storage for mod-specific ImGui context and frame state
namespace detail
{
static ImGuiContext *g_modContext = nullptr;
static bool g_modFrameActive = false;
} // namespace detail

/**
 * @brief Setup mod-specific ImGui context with Wolf's shared resources
 * @return True if context was successfully created and configured
 *
 * This function creates a mod-specific ImGui context using Wolf's shared font atlas
 * and allocators. This approach allows each mod to have its own ImGui context while
 * sharing critical resources to prevent crashes and ensure text rendering works.
 *
 * This function should be called BEFORE any other ImGui operations in the mod.
 * Typically this would be done in the mod's early initialization.
 *
 * Usage:
 * @code
 * // In mod initialization:
 * if (!wolf::setupSharedImGuiAllocators()) {
 *     logError("Failed to setup mod ImGui context!");
 *     return false;
 * }
 *
 * // Now it's safe to use ImGui including text rendering
 * wolf::registerGuiWindow("My Window", myCallback);
 * @endcode
 */
inline bool setupSharedImGuiAllocators() noexcept
{
    if (!detail::g_runtime)
    {
        logError("No runtime available");
        return false;
    }

    // Don't create context twice
    if (detail::g_modContext)
    {
        logDebug("Mod context already exists");
        return true;
    }

    // Get Wolf's allocator functions
    void *allocFuncPtr = detail::g_runtime->getImGuiAllocFunc();
    void *freeFuncPtr = detail::g_runtime->getImGuiFreeFunc();
    void *userData = detail::g_runtime->getImGuiAllocUserData();

    if (!allocFuncPtr || !freeFuncPtr)
    {
        logError("Invalid allocator functions");
        return false;
    }

    // Cast to proper function pointers
    ImGuiMemAllocFunc allocFunc = reinterpret_cast<ImGuiMemAllocFunc>(allocFuncPtr);
    ImGuiMemFreeFunc freeFunc = reinterpret_cast<ImGuiMemFreeFunc>(freeFuncPtr);

    // Configure this mod's ImGui to use Wolf's allocators
    ImGui::SetAllocatorFunctions(allocFunc, freeFunc, userData);

    // Get Wolf's shared font atlas
    void *fontAtlasPtr = detail::g_runtime->getImGuiFontAtlas();
    if (!fontAtlasPtr)
    {
        logError("No font atlas available");
        return false;
    }

    ImFontAtlas *sharedFontAtlas = static_cast<ImFontAtlas *>(fontAtlasPtr);

    // Create mod-specific context with Wolf's shared font atlas
    // Each mod gets its own context but shares Wolf's font atlas
    detail::g_modContext = ImGui::CreateContext(sharedFontAtlas);
    if (!detail::g_modContext)
    {
        logError("Failed to create mod context");
        return false;
    }

    // Register this context with Wolf for input event forwarding
    detail::g_runtime->registerModContext(detail::getCurrentModId(), detail::g_modContext);

    return true;
}

/**
 * @brief Cleanup mod's ImGui context
 *
 * This function should be called during mod shutdown to properly cleanup
 * the mod-specific ImGui context. This prevents resource leaks.
 */
inline void cleanupImGuiContext() noexcept
{
    if (detail::g_modContext)
    {
        // Unregister context from Wolf's input forwarding
        if (detail::g_runtime)
        {
            detail::g_runtime->unregisterModContext(detail::getCurrentModId(), detail::g_modContext);
        }

        ImGui::DestroyContext(detail::g_modContext);
        detail::g_modContext = nullptr;
    }
}

/**
 * @brief Get Wolf's shared ImGui font atlas
 * @return Pointer to Wolf's ImFontAtlas, or nullptr if not available
 *
 * This function returns Wolf's shared font atlas that must be used by mods
 * to prevent GPU/rendering conflicts. Without shared font atlas, styled text
 * operations (like ImGui::PushStyleColor() + Text()) can crash due to
 * separate font texture data.
 *
 * Usage:
 * @code
 * // If creating your own context (advanced usage):
 * ImFontAtlas* sharedAtlas = wolf::getSharedFontAtlas();
 * ImGuiContext* context = ImGui::CreateContext(sharedAtlas);
 * @endcode
 */
inline ImFontAtlas *getSharedFontAtlas() noexcept
{
    if (!detail::g_runtime)
        return nullptr;

    void *atlasPtr = detail::g_runtime->getImGuiFontAtlas();
    return static_cast<ImFontAtlas *>(atlasPtr);
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
 * @brief Ensure mod ImGui context is set up and ready
 * @return True if context is available for use
 */
inline bool ensureModContext() noexcept
{
    if (!detail::g_runtime)
    {
        logError("ensureModContext: No runtime");
        return false;
    }

    // CRITICAL: Set up mod context with shared resources first
    static bool contextSetup = false;
    if (!contextSetup)
    {
        if (!setupSharedImGuiAllocators())
        {
            logError("ensureModContext: setupSharedImGuiAllocators failed");
            return false;
        }
        contextSetup = true;
    }

    return detail::g_modContext != nullptr;
}

//==============================================================================
// IMGUI MACROS FOR MODS
//==============================================================================

/**
 * @brief Initialize D3D11 backend for mod's ImGui context
 *
 * Call this once after setupSharedImGuiAllocators() and before using other ImGui macros.
 * Requires imgui_impl_dx11.h to be included.
 *
 * Usage:
 * @code
 * wolf::setupSharedImGuiAllocators();
 * WOLF_IMGUI_INIT_BACKEND();
 * @endcode
 */
#define WOLF_IMGUI_INIT_BACKEND()                                                                                                                              \
    do                                                                                                                                                         \
    {                                                                                                                                                          \
        if (!wolf::detail::g_modContext)                                                                                                                       \
        {                                                                                                                                                      \
            wolf::logError("No mod context available");                                                                                                        \
            break;                                                                                                                                             \
        }                                                                                                                                                      \
        void *d3d11Device = wolf::detail::g_runtime->getD3D11Device();                                                                                         \
        void *d3d11DeviceContext = wolf::detail::g_runtime->getD3D11DeviceContext();                                                                           \
        if (d3d11Device && d3d11DeviceContext)                                                                                                                 \
        {                                                                                                                                                      \
            ImGui::SetCurrentContext(wolf::detail::g_modContext);                                                                                              \
            bool result = ImGui_ImplDX11_Init(static_cast<ID3D11Device *>(d3d11Device), static_cast<ID3D11DeviceContext *>(d3d11DeviceContext));               \
        }                                                                                                                                                      \
        else                                                                                                                                                   \
        {                                                                                                                                                      \
            wolf::logError("Invalid D3D11 device or context");                                                                                                 \
        }                                                                                                                                                      \
    } while (0)

/**
 * @brief Begin ImGui frame with proper context and DisplaySize setup
 * @param width Window width in pixels
 * @param height Window height in pixels
 * @param scale UI scale factor
 *
 * Usage:
 * @code
 * wolf::registerGuiWindow("My Window", [](int w, int h, float scale) {
 *     WOLF_IMGUI_BEGIN(w, h, scale);
 *
 *     ImGui::Begin("My Window");
 *     ImGui::Text("Hello World!");
 *     ImGui::End();
 *
 *     WOLF_IMGUI_END();
 * });
 * @endcode
 */
#define WOLF_IMGUI_BEGIN(width, height, scale)                                                                                                                 \
    do                                                                                                                                                         \
    {                                                                                                                                                          \
        if (!wolf::ensureModContext())                                                                                                                         \
        {                                                                                                                                                      \
            wolf::logError("No valid mod context");                                                                                                            \
            break;                                                                                                                                             \
        }                                                                                                                                                      \
        if (wolf::detail::g_modFrameActive)                                                                                                                    \
        {                                                                                                                                                      \
            wolf::logError("Frame already active");                                                                                                            \
            break;                                                                                                                                             \
        }                                                                                                                                                      \
        ImGui::SetCurrentContext(wolf::detail::g_modContext);                                                                                                  \
        ImGuiIO &io = ImGui::GetIO();                                                                                                                          \
        if (!io.Fonts)                                                                                                                                         \
        {                                                                                                                                                      \
            wolf::logError("No font atlas in IO");                                                                                                             \
            break;                                                                                                                                             \
        }                                                                                                                                                      \
        if (!io.Fonts->IsBuilt())                                                                                                                              \
        {                                                                                                                                                      \
            /* Try to get Wolf's current font atlas and use it */                                                                                              \
            ImFontAtlas *wolfAtlas = wolf::getSharedFontAtlas();                                                                                               \
            if (wolfAtlas && wolfAtlas->IsBuilt())                                                                                                             \
            {                                                                                                                                                  \
                io.Fonts = wolfAtlas;                                                                                                                          \
                wolf::logInfo("Resynced font atlas with Wolf (recovered from invalid state)");                                                                 \
            }                                                                                                                                                  \
            else                                                                                                                                               \
            {                                                                                                                                                  \
                /* Wolf's font atlas is also invalid, skip rendering this frame */                                                                             \
                wolf::logDebug("Font atlas rebuilding in progress, skipping mod render this frame");                                                           \
                break;                                                                                                                                         \
            }                                                                                                                                                  \
        }                                                                                                                                                      \
        io.DisplaySize.x = static_cast<float>(width);                                                                                                          \
        io.DisplaySize.y = static_cast<float>(height);                                                                                                         \
        /* Font atlas is now rasterized at the correct size, no need for FontGlobalScale */                                                                    \
        /* Input events are forwarded globally by Wolf to all registered mod contexts */                                                                       \
        ImGui::NewFrame();                                                                                                                                     \
        wolf::detail::g_modFrameActive = true;

/**
 * @brief End ImGui frame and generate draw data
 *
 * Must be paired with WOLF_IMGUI_BEGIN().
 */
#define WOLF_IMGUI_END()                                                                                                                                       \
    ImGui::EndFrame();                                                                                                                                         \
    ImGui::Render();                                                                                                                                           \
    ImDrawData *drawData = ImGui::GetDrawData();                                                                                                               \
    if (drawData)                                                                                                                                              \
    {                                                                                                                                                          \
        try                                                                                                                                                    \
        {                                                                                                                                                      \
            if (drawData->Valid && drawData->CmdListsCount > 0)                                                                                                \
            {                                                                                                                                                  \
                wolf::detail::g_runtime->registerModDrawData(wolf::detail::getCurrentModId(), drawData);                                                       \
            }                                                                                                                                                  \
        }                                                                                                                                                      \
        catch (...)                                                                                                                                            \
        {                                                                                                                                                      \
            wolf::logError("Exception registering draw data");                                                                                                 \
        }                                                                                                                                                      \
    }                                                                                                                                                          \
    wolf::detail::g_modFrameActive = false;                                                                                                                    \
    }                                                                                                                                                          \
    while (0)

/**
 * @brief Register a Win32 WndProc hook for input handling
 * @param callback Function to handle Win32 messages
 * @param userData User data passed to the callback
 * @return True if hook was successfully registered
 *
 * This allows mods to receive raw Win32 input messages for custom handling,
 * including ImGui input forwarding. The callback should return true if it
 * handled the message and wants to prevent further processing.
 *
 * Usage for ImGui input forwarding:
 * @code
 * wolf::registerWndProcHook([](void* hwnd, unsigned int msg, uintptr_t wParam, intptr_t lParam, void* userData) -> int {
 *     ImGui::SetCurrentContext(myModContext);
 *     return ImGui_ImplWin32_WndProcHandler(static_cast<HWND>(hwnd), msg, wParam, lParam) ? 1 : 0;
 * }, nullptr);
 * @endcode
 */
inline bool registerWndProcHook(std::function<int(void *, unsigned int, uintptr_t, intptr_t, void *)> callback, void *userData = nullptr) noexcept
{
    if (!detail::g_runtime || !callback)
        return false;

    // Store the callback in a way that can be called from C
    static std::vector<std::unique_ptr<std::function<int(void *, unsigned int, uintptr_t, intptr_t, void *)>>> s_callbacks;
    auto stored_callback = std::make_unique<std::function<int(void *, unsigned int, uintptr_t, intptr_t, void *)>>(std::move(callback));
    auto *callback_ptr = stored_callback.get();
    s_callbacks.push_back(std::move(stored_callback));

    // Create C-compatible wrapper
    auto c_wrapper = [](void *hwnd, unsigned int msg, uintptr_t wParam, intptr_t lParam, void *userData) -> int
    {
        auto *cpp_callback = static_cast<std::function<int(void *, unsigned int, uintptr_t, intptr_t, void *)> *>(userData);
        return (*cpp_callback)(hwnd, msg, wParam, lParam, nullptr);
    };

    detail::g_runtime->registerWndProcHook(detail::getCurrentModId(), c_wrapper, callback_ptr);
    return true;
}

/**
 * @brief Unregister the mod's Win32 WndProc hook
 */
inline void unregisterWndProcHook() noexcept
{
    if (detail::g_runtime)
    {
        detail::g_runtime->unregisterWndProcHook(detail::getCurrentModId());
    }
}

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
                   // Execute the mod's GUI callback - mod handles frame lifecycle with macros
                   auto *cb = static_cast<GuiWindowCallback *>(userdata);
                   (*cb)(outer_width, outer_height, ui_scale);
               },
               callback_ptr, initiallyVisible ? 1 : 0) != 0;
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
