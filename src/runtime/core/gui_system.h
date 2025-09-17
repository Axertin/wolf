#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "wolf_types.h"

// GUI window structure
struct ModGuiWindow
{
    WolfModId modId;
    std::string windowName;
    WolfGuiWindowCallback callback;
    void *userdata;
    bool isVisible;
};

// Forward declaration for ImDrawData
struct ImDrawData;

// Forward declaration for ImGuiContext
struct ImGuiContext;

// Forward declaration for IDXGISwapChain
struct IDXGISwapChain;

// Global GUI system storage (defined in gui_system.cpp)
extern std::mutex g_GuiMutex;
extern std::vector<std::unique_ptr<ModGuiWindow>> g_ModGuiWindows;
extern std::vector<ImDrawData *> g_ModDrawData;
extern std::vector<ImGuiContext *> g_ModContexts;

// Internal functions for GUI system (used by wolf::runtime::internal)
namespace wolf::runtime::internal
{
void forwardCharacterToModContexts(unsigned short character);
bool anyModWantsInput();
bool hasModContexts();
void forwardInputToModContexts();
void renderModGuiWindows(IDXGISwapChain *pSwapChain);
void renderCollectedModDrawData();
} // namespace wolf::runtime::internal

// C API functions for GUI system
extern "C"
{
    int __cdecl wolfRuntimeRegisterGuiWindow(WolfModId mod_id, const char *window_name, WolfGuiWindowCallback callback, void *userdata, int initially_visible);
    int __cdecl wolfRuntimeUnregisterGuiWindow(WolfModId mod_id, const char *window_name);
    int __cdecl wolfRuntimeToggleGuiWindow(WolfModId mod_id, const char *window_name);
    int __cdecl wolfRuntimeSetGuiWindowVisible(WolfModId mod_id, const char *window_name, int visible);
    int __cdecl wolfRuntimeExecuteInImGuiContext(WolfModId mod_id, void(__cdecl *renderFunc)(void *userdata), void *userdata);
    void *__cdecl wolfRuntimeGetImGuiContext(void);
    void *__cdecl wolfRuntimeGetImGuiFreeFunc(void);
    void *__cdecl wolfRuntimeGetImGuiAllocFunc(void);
    void *__cdecl wolfRuntimeGetImGuiAllocUserData(void);
    void *__cdecl wolfRuntimeGetImGuiFontAtlas(void);
    void *__cdecl wolfRuntimeGetImGuiIO(void);
    void *__cdecl wolfRuntimeGetD3D11Device(void);
    void *__cdecl wolfRuntimeGetD3D11DeviceContext(void);
    void __cdecl wolfRuntimeRegisterModDrawData(WolfModId mod_id, void *draw_data);
    void __cdecl wolfRuntimeRegisterModContext(WolfModId mod_id, void *context);
    void __cdecl wolfRuntimeUnregisterModContext(WolfModId mod_id, void *context);
}