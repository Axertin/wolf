#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "wolf_types.h"

// Game event callbacks structure
struct GameEventCallbacks
{
    std::vector<std::pair<WolfGameEventCallback, void *>> gameTick;
    std::vector<std::pair<WolfGameEventCallback, void *>> gameStart;
    std::vector<std::pair<WolfGameEventCallback, void *>> gameStop;
    std::vector<std::pair<WolfGameEventCallback, void *>> playStart;
    std::vector<std::pair<WolfGameEventCallback, void *>> returnToMenu;
    std::vector<std::pair<WolfItemPickupCallback, void *>> itemPickup;

    // Blocking callbacks
    std::vector<std::pair<WolfItemPickupBlockingCallback, void *>> itemPickupBlocking;
    std::vector<std::pair<WolfBrushEditCallback, void *>> brushEdit;
};

// Hook information structure
struct HookInfo
{
    void *target;
    void *detour;
    void **original;
};

// Win32 WndProc hook structure
struct WndProcHook
{
    WolfModId modId;
    WolfWndProcCallback callback;
    void *userData;
};

// Global game hooks storage (defined in game_hooks.cpp)
extern std::mutex g_CallbackMutex;
extern std::mutex g_WndProcMutex;
extern std::unordered_map<WolfModId, std::unique_ptr<GameEventCallbacks>> g_ModCallbacks;
extern std::unordered_map<void *, HookInfo> g_ActiveHooks;
extern std::vector<WndProcHook> g_WndProcHooks;

// Internal functions for game hooks (used by wolf::runtime::internal)
namespace wolf::runtime::internal
{
void callGameTick();
void callGameStart();
void callGameStop();
void callPlayStart();
void callReturnToMenu();
void callItemPickup(int itemId, int count);
bool callItemPickupBlocking(int itemId, int count);
bool callBrushEdit(int bitIndex, int operation);
bool callModWndProcHooks(void *hwnd, unsigned int msg, uintptr_t wParam, intptr_t lParam);
} // namespace wolf::runtime::internal

// C API functions for game hooks & callbacks
extern "C"
{
    void __cdecl wolfRuntimeRegisterGameTick(WolfModId mod_id, WolfGameEventCallback callback, void *userdata);
    void __cdecl wolfRuntimeRegisterGameStart(WolfModId mod_id, WolfGameEventCallback callback, void *userdata);
    void __cdecl wolfRuntimeRegisterGameStop(WolfModId mod_id, WolfGameEventCallback callback, void *userdata);
    void __cdecl wolfRuntimeRegisterPlayStart(WolfModId mod_id, WolfGameEventCallback callback, void *userdata);
    void __cdecl wolfRuntimeRegisterReturnToMenu(WolfModId mod_id, WolfGameEventCallback callback, void *userdata);
    void __cdecl wolfRuntimeRegisterItemPickup(WolfModId mod_id, WolfItemPickupCallback callback, void *userdata);
    int __cdecl wolfRuntimeRegisterItemPickupBlocking(WolfModId mod_id, WolfItemPickupBlockingCallback callback, void *userdata);
    int __cdecl wolfRuntimeRegisterBrushEdit(WolfModId mod_id, WolfBrushEditCallback callback, void *userdata);
    int __cdecl wolfRuntimeHookFunction(uintptr_t address, void *detour, void **original);
    void __cdecl wolfRuntimeRegisterWndProcHook(WolfModId mod_id, WolfWndProcCallback callback, void *userData);
    void __cdecl wolfRuntimeUnregisterWndProcHook(WolfModId mod_id);
}