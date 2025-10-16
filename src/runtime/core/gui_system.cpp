#include "gui_system.h"

#include "../utilities/gui.h"
#include "../utilities/logger.h"
#include "mod_lifecycle.h" // For findMod and g_CurrentModId

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <map>

#include <dxgi.h>
#include <imgui.h>

// Global GUI system storage
std::mutex g_GuiMutex;
std::vector<std::unique_ptr<ModGuiWindow>> g_ModGuiWindows;
std::vector<ImDrawData *> g_ModDrawData;
std::vector<ImGuiContext *> g_ModContexts;

// C API implementations
extern "C"
{
    int wolfRuntimeRegisterGuiWindow(WolfModId mod_id, const char *window_name, WolfGuiWindowCallback callback, void *userdata, int initially_visible)
    {
        if (!window_name || !callback)
            return 0;

        std::lock_guard<std::mutex> lock(g_GuiMutex);

        // Check if window already exists
        for (const auto &window : g_ModGuiWindows)
        {
            if (window->modId == mod_id && window->windowName == window_name)
                return 0; // Window already registered
        }

        auto guiWindow = std::make_unique<ModGuiWindow>();
        guiWindow->modId = mod_id;
        guiWindow->windowName = window_name;
        guiWindow->callback = callback;
        guiWindow->userdata = userdata;
        guiWindow->isVisible = initially_visible != 0;

        g_ModGuiWindows.push_back(std::move(guiWindow));
        return 1;
    }

    int wolfRuntimeUnregisterGuiWindow(WolfModId mod_id, const char *window_name)
    {
        if (!window_name)
            return 0;

        std::lock_guard<std::mutex> lock(g_GuiMutex);

        auto it = std::remove_if(g_ModGuiWindows.begin(), g_ModGuiWindows.end(), [mod_id, window_name](const std::unique_ptr<ModGuiWindow> &window)
                                 { return window->modId == mod_id && window->windowName == window_name; });

        if (it != g_ModGuiWindows.end())
        {
            g_ModGuiWindows.erase(it, g_ModGuiWindows.end());
            return 1;
        }

        return 0;
    }

    int wolfRuntimeToggleGuiWindow(WolfModId mod_id, const char *window_name)
    {
        if (!window_name)
            return 0;

        std::lock_guard<std::mutex> lock(g_GuiMutex);

        for (auto &window : g_ModGuiWindows)
        {
            if (window->modId == mod_id && window->windowName == window_name)
            {
                window->isVisible = !window->isVisible;
                return 1;
            }
        }

        return 0;
    }

    int wolfRuntimeSetGuiWindowVisible(WolfModId mod_id, const char *window_name, int visible)
    {
        if (!window_name)
            return 0;

        std::lock_guard<std::mutex> lock(g_GuiMutex);

        for (auto &window : g_ModGuiWindows)
        {
            if (window->modId == mod_id && window->windowName == window_name)
            {
                window->isVisible = visible != 0;
                return 1;
            }
        }

        return 0;
    }

    int wolfRuntimeExecuteInImGuiContext(WolfModId mod_id, void(__cdecl *renderFunc)(void *userdata), void *userdata)
    {
        if (!renderFunc)
            return 0;

        // Check if ImGui context exists
        if (!ImGui::GetCurrentContext())
        {
            ModInfo *mod = findMod(mod_id);
            std::string modName = mod ? mod->name : "Unknown";
            ::logError("[WOLF] ImGui context not available for mod '" + modName + "'");
            return 0;
        }

        // Set the current mod ID for context
        WolfModId previousModId = g_CurrentModId;
        g_CurrentModId = mod_id;

        try
        {
            renderFunc(userdata);
            g_CurrentModId = previousModId;
            return 1;
        }
        catch (const std::exception &e)
        {
            g_CurrentModId = previousModId;
            ModInfo *mod = findMod(mod_id);
            std::string modName = mod ? mod->name : "Unknown";
            ::logError("[WOLF] Exception in ImGui context function for mod '" + modName + "': " + e.what());
            return 0;
        }
        catch (...)
        {
            g_CurrentModId = previousModId;
            ModInfo *mod = findMod(mod_id);
            std::string modName = mod ? mod->name : "Unknown";
            ::logError("[WOLF] Unknown exception in ImGui context function for mod '" + modName + "'");
            return 0;
        }
    }

    void *wolfRuntimeGetImGuiContext(void)
    {
        return ImGui::GetCurrentContext();
    }

    void *wolfRuntimeGetImGuiAllocFunc(void)
    {
        ImGuiMemAllocFunc allocFunc;
        ImGuiMemFreeFunc freeFunc;
        void *userData;
        ImGui::GetAllocatorFunctions(&allocFunc, &freeFunc, &userData);
        return reinterpret_cast<void *>(allocFunc);
    }

    void *wolfRuntimeGetImGuiFreeFunc(void)
    {
        ImGuiMemAllocFunc allocFunc;
        ImGuiMemFreeFunc freeFunc;
        void *userData;
        ImGui::GetAllocatorFunctions(&allocFunc, &freeFunc, &userData);
        return reinterpret_cast<void *>(freeFunc);
    }

    void *wolfRuntimeGetImGuiAllocUserData(void)
    {
        ImGuiMemAllocFunc allocFunc;
        ImGuiMemFreeFunc freeFunc;
        void *userData;
        ImGui::GetAllocatorFunctions(&allocFunc, &freeFunc, &userData);
        return userData;
    }

    void *wolfRuntimeGetImGuiFontAtlas(void)
    {
        ImGuiContext *context = ImGui::GetCurrentContext();
        if (!context)
            return nullptr;

        ImGuiIO &io = ImGui::GetIO();
        return io.Fonts;
    }

    void *wolfRuntimeGetImGuiIO(void)
    {
        ImGuiContext *context = ImGui::GetCurrentContext();
        if (!context)
            return nullptr;

        ImGuiIO &io = ImGui::GetIO();
        return &io;
    }

    void *wolfRuntimeGetD3D11Device(void)
    {
        return guiGetD3D11Device();
    }

    void *wolfRuntimeGetD3D11DeviceContext(void)
    {
        return guiGetD3D11DeviceContext();
    }

    void wolfRuntimeRegisterModDrawData(WolfModId mod_id, void *draw_data)
    {
        if (!draw_data)
        {
            ::logError("[WOLF] registerModDrawData: draw_data is null");
            return;
        }

        // Note: g_GuiMutex is already held by renderModGuiWindows when this is called
        try
        {
            ImDrawData *modDrawData = static_cast<ImDrawData *>(draw_data);
            if (modDrawData && modDrawData->Valid && modDrawData->CmdListsCount > 0)
            {
                g_ModDrawData.push_back(modDrawData);
            }
        }
        catch (const std::exception &e)
        {
            ::logError("[WOLF] registerModDrawData: Exception - %s", e.what());
        }
        catch (...)
        {
            ::logError("[WOLF] registerModDrawData: Unknown exception");
        }
    }

    void wolfRuntimeRegisterModContext(WolfModId mod_id, void *context)
    {
        if (!context)
        {
            ::logError("[WOLF] registerModContext: context is null");
            return;
        }

        std::lock_guard<std::mutex> lock(g_GuiMutex);
        ImGuiContext *modContext = static_cast<ImGuiContext *>(context);

        // Check if context is already registered
        auto it = std::find(g_ModContexts.begin(), g_ModContexts.end(), modContext);
        if (it == g_ModContexts.end())
        {
            g_ModContexts.push_back(modContext);
        }
    }

    void wolfRuntimeUnregisterModContext(WolfModId mod_id, void *context)
    {
        if (!context)
            return;

        std::lock_guard<std::mutex> lock(g_GuiMutex);
        ImGuiContext *modContext = static_cast<ImGuiContext *>(context);

        auto it = std::find(g_ModContexts.begin(), g_ModContexts.end(), modContext);
        if (it != g_ModContexts.end())
        {
            g_ModContexts.erase(it);
            ::logInfo("[WOLF] Unregistered mod context %p from input forwarding", modContext);
        }
    }

} // extern "C"

namespace wolf::runtime::internal
{

bool hasModContexts()
{
    return !g_ModContexts.empty();
}

bool anyModWantsInput()
{
    // Check all mod contexts to see if any want to capture input
    for (ImGuiContext *context : g_ModContexts)
    {
        if (context)
        {
            ImGuiContext *originalContext = ImGui::GetCurrentContext();
            ImGui::SetCurrentContext(context);

            ImGuiIO &io = ImGui::GetIO();
            bool wantsInput = io.WantCaptureMouse || io.WantCaptureKeyboard;

            ImGui::SetCurrentContext(originalContext);

            if (wantsInput)
            {
                return true;
            }
        }
    }
    return false;
}

void forwardCharacterToModContexts(unsigned short character)
{
    if (g_ModContexts.empty())
        return;

    ImGuiContext *originalContext = ImGui::GetCurrentContext();

    for (ImGuiContext *modContext : g_ModContexts)
    {
        if (modContext && modContext != originalContext)
        {
            ImGui::SetCurrentContext(modContext);
            ImGuiIO &modIO = ImGui::GetIO();

            // Check if mod context has invalid font atlas and skip if so
            if (!modIO.Fonts || !modIO.Fonts->IsBuilt())
            {
                continue;
            }

            modIO.AddInputCharacter(character);
        }
    }

    // Restore original context
    ImGui::SetCurrentContext(originalContext);
}

void forwardInputToModContexts()
{
    if (g_ModContexts.empty())
        return;

    // Get Wolf's ImGui IO as the source of input data
    ImGuiContext *wolfContext = ImGui::GetCurrentContext();
    if (!wolfContext)
        return;

    // Ensure Wolf's context has a valid font atlas before proceeding
    ImGuiIO &wolfIO = ImGui::GetIO();
    if (!wolfIO.Fonts || !wolfIO.Fonts->IsBuilt())
    {
        // Wolf's context isn't fully initialized yet, skip input forwarding this frame
        return;
    }

    // Note: Keyboard capture state is now handled earlier in guiRenderFrame()
    // before Win32 backend processes input messages

    // Copy Wolf's input state to all mod contexts
    for (ImGuiContext *modContext : g_ModContexts)
    {
        if (modContext && modContext != wolfContext)
        {
            ImGui::SetCurrentContext(modContext);
            ImGuiIO &modIO = ImGui::GetIO();

            // Check if mod context has invalid font atlas
            if (!modIO.Fonts || !modIO.Fonts->IsBuilt())
            {
                // The shared font atlas was rebuilt by Wolf, but mod context doesn't know
                // Try to fix this by ensuring the mod uses the same font atlas as Wolf
                if (wolfIO.Fonts && wolfIO.Fonts->IsBuilt())
                {
                    ::logWarning("[WOLF] Mod context font atlas invalid, attempting to sync with Wolf's atlas");

                    // Replace the mod context's font atlas with Wolf's current one
                    modIO.Fonts = wolfIO.Fonts;

                    // This is a bit of a hack, but should restore the mod's font atlas
                    if (modIO.Fonts && modIO.Fonts->IsBuilt())
                    {
                        ::logInfo("[WOLF] Successfully synced mod font atlas with Wolf");
                    }
                    else
                    {
                        ::logError("[WOLF] Failed to sync mod font atlas, skipping this context");
                        continue;
                    }
                }
                else
                {
                    // Still not ready, skip this context
                    continue;
                }
            }

            // Copy mouse state
            modIO.MousePos = wolfIO.MousePos;
            modIO.MouseDown[0] = wolfIO.MouseDown[0]; // Left button
            modIO.MouseDown[1] = wolfIO.MouseDown[1]; // Right button
            modIO.MouseDown[2] = wolfIO.MouseDown[2]; // Middle button
            modIO.AddMouseWheelEvent(wolfIO.MouseWheelH, wolfIO.MouseWheel);

            // Copy mouse click events for widget focus/activation
            // Suppress old-style cast warning from ImGui macro expansion
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // old-style cast
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif
            for (int i = 0; i < IM_ARRAYSIZE(wolfIO.MouseClicked); i++)
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
            {
                if (wolfIO.MouseClicked[i])
                {
                    modIO.AddMouseButtonEvent(i, true);
                }
                if (wolfIO.MouseReleased[i])
                {
                    modIO.AddMouseButtonEvent(i, false);
                }
                // Copy double click events too
                if (wolfIO.MouseDoubleClicked[i])
                {
                    // ImGui doesn't have a direct AddMouseDoubleClickEvent, but we can simulate it
                    // by ensuring the DoubleClicked flag gets set on the mod context
                    modIO.MouseDoubleClicked[i] = wolfIO.MouseDoubleClicked[i];
                }
            }

            // Copy keyboard state
            modIO.KeyCtrl = wolfIO.KeyCtrl;
            modIO.KeyShift = wolfIO.KeyShift;
            modIO.KeyAlt = wolfIO.KeyAlt;
            modIO.KeySuper = wolfIO.KeySuper;

            // Copy character events from Wolf's input queue (fallback for any that weren't directly forwarded)
            for (int i = 0; i < wolfIO.InputQueueCharacters.Size; i++)
            {
                ImWchar c = wolfIO.InputQueueCharacters[i];
                modIO.AddInputCharacter(c);
            }

            // Copy key events from Wolf's input queue
            // Note: We can't easily iterate through ImGui's internal key events,
            // so we'll focus on the most important keys for text input
            // TODO: Handle all key events which can take ImGui::IsKeyDown
            const ImGuiKey textInputKeys[] = {
                ImGuiKey_Backspace, ImGuiKey_Delete,    ImGuiKey_Enter, ImGuiKey_KeypadEnter, ImGuiKey_LeftArrow, ImGuiKey_RightArrow,
                ImGuiKey_UpArrow,   ImGuiKey_DownArrow, ImGuiKey_Home,  ImGuiKey_End,         ImGuiKey_Tab,       ImGuiKey_Escape,
                ImGuiKey_A,         ImGuiKey_C,         ImGuiKey_V,     ImGuiKey_X,           ImGuiKey_Y,         ImGuiKey_Z // Common shortcuts
            };

            for (ImGuiKey key : textInputKeys)
            {
                // Switch to Wolf's context to read its key state
                ImGui::SetCurrentContext(wolfContext);
                bool wolfKeyDown = ImGui::IsKeyDown(key);

                // Switch to mod context to check its key state
                ImGui::SetCurrentContext(modContext);
                bool modKeyDown = ImGui::IsKeyDown(key);

                // If the key state differs, send the appropriate event to mod
                if (wolfKeyDown != modKeyDown)
                {
                    modIO.AddKeyEvent(key, wolfKeyDown);
                }
            }

            // Copy display size to ensure proper coordinate mapping
            modIO.DisplaySize = wolfIO.DisplaySize;
        }
    }

    // Always restore Wolf's context, even if we had issues
    ImGui::SetCurrentContext(wolfContext);
}

void renderModGuiWindows(IDXGISwapChain *pSwapChain)
{
    std::lock_guard<std::mutex> lock(g_GuiMutex);

    if (!pSwapChain)
    {
        ::logError("[WOLF] SwapChain not available for mod GUI rendering");
        return;
    }

    // Calculate window dimensions and UI scale
    RECT rect;
    DXGI_SWAP_CHAIN_DESC desc;
    pSwapChain->GetDesc(&desc);
    HWND hwnd = desc.OutputWindow;
    if (!hwnd || !GetClientRect(hwnd, &rect))
    {
        ::logError("[WOLF] Failed to get window dimensions for mod GUI rendering");
        return;
    }

    int windowWidth = rect.right - rect.left;
    int windowHeight = rect.bottom - rect.top;
    const int BaseWidth = 1920;
    const int BaseHeight = 1080;
    float widthScale = static_cast<float>(windowWidth) / BaseWidth;
    float heightScale = static_cast<float>(windowHeight) / BaseHeight;
    float uiScale = std::min(widthScale, heightScale);

    // Store Wolf's current context to restore later
    ImGuiContext *wolfContext = ImGui::GetCurrentContext();

    // Group windows by mod to manage frame cycles per mod
    std::map<WolfModId, std::vector<ModGuiWindow *>> windowsByMod;
    for (auto &window : g_ModGuiWindows)
    {
        if (window->isVisible && window->callback)
        {
            windowsByMod[window->modId].push_back(window.get());
        }
    }

    // Clear previous frame's draw data
    g_ModDrawData.clear();

    // Execute mod callbacks - they will register their draw data via WOLF_IMGUI_END
    for (auto &[modId, modWindows] : windowsByMod)
    {
        if (modWindows.empty())
            continue;

        g_CurrentModId = modId; // Set context for this mod

        try
        {
            // Execute mod callbacks - they handle their own ImGui frame cycle and register draw data
            for (auto &window : modWindows)
            {
                window->callback(windowWidth, windowHeight, uiScale, window->userdata);
            }
        }
        catch (const std::exception &e)
        {
            ModInfo *mod = findMod(modId);
            std::string modName = mod ? mod->name : "Unknown";
            ::logError("[WOLF] Exception in GUI rendering for mod '" + modName + "': " + e.what());
        }
        catch (...)
        {
            ModInfo *mod = findMod(modId);
            std::string modName = mod ? mod->name : "Unknown";
            ::logError("[WOLF] Unknown exception in GUI rendering for mod '" + modName + "'");
        }
    }

    // Restore Wolf's context
    if (wolfContext)
    {
        ImGui::SetCurrentContext(wolfContext);
    }

    g_CurrentModId = 0; // Clear context
}

void renderCollectedModDrawData()
{
    std::lock_guard<std::mutex> lock(g_GuiMutex);

    if (g_ModDrawData.empty())
        return;

    // Render each mod's draw data with Wolf's D3D11 backend
    for (ImDrawData *drawData : g_ModDrawData)
    {
        if (drawData && drawData->Valid && drawData->CmdListsCount > 0)
        {
            guiRenderDrawData(drawData);
        }
    }
}

} // namespace wolf::runtime::internal
