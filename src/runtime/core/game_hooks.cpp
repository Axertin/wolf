#include "game_hooks.h"

#include <algorithm>

#include "../utilities/logger.h"
#include "mod_lifecycle.h" // For findMod and g_CurrentModId

#include <MinHook.h>

// Global game hooks storage
std::mutex g_CallbackMutex;
std::mutex g_WndProcMutex;
std::unordered_map<WolfModId, std::unique_ptr<GameEventCallbacks>> g_ModCallbacks;
std::unordered_map<void *, HookInfo> g_ActiveHooks;
std::vector<WndProcHook> g_WndProcHooks;

// C API implementations
extern "C"
{
    void wolfRuntimeRegisterGameTick(WolfModId mod_id, WolfGameEventCallback callback, void *userdata)
    {
        if (!callback)
            return;

        std::lock_guard<std::mutex> lock(g_CallbackMutex);

        if (g_ModCallbacks.find(mod_id) == g_ModCallbacks.end())
        {
            g_ModCallbacks[mod_id] = std::make_unique<GameEventCallbacks>();
        }

        g_ModCallbacks[mod_id]->gameTick.emplace_back(callback, userdata);
    }

    void wolfRuntimeRegisterGameStart(WolfModId mod_id, WolfGameEventCallback callback, void *userdata)
    {
        if (!callback)
            return;

        std::lock_guard<std::mutex> lock(g_CallbackMutex);

        if (g_ModCallbacks.find(mod_id) == g_ModCallbacks.end())
        {
            g_ModCallbacks[mod_id] = std::make_unique<GameEventCallbacks>();
        }

        g_ModCallbacks[mod_id]->gameStart.emplace_back(callback, userdata);
    }

    void wolfRuntimeRegisterGameStop(WolfModId mod_id, WolfGameEventCallback callback, void *userdata)
    {
        if (!callback)
            return;

        std::lock_guard<std::mutex> lock(g_CallbackMutex);

        if (g_ModCallbacks.find(mod_id) == g_ModCallbacks.end())
        {
            g_ModCallbacks[mod_id] = std::make_unique<GameEventCallbacks>();
        }

        g_ModCallbacks[mod_id]->gameStop.emplace_back(callback, userdata);
    }

    void wolfRuntimeRegisterPlayStart(WolfModId mod_id, WolfGameEventCallback callback, void *userdata)
    {
        if (!callback)
            return;

        std::lock_guard<std::mutex> lock(g_CallbackMutex);

        if (g_ModCallbacks.find(mod_id) == g_ModCallbacks.end())
        {
            g_ModCallbacks[mod_id] = std::make_unique<GameEventCallbacks>();
        }

        g_ModCallbacks[mod_id]->playStart.emplace_back(callback, userdata);
    }

    void wolfRuntimeRegisterReturnToMenu(WolfModId mod_id, WolfGameEventCallback callback, void *userdata)
    {
        if (!callback)
            return;

        std::lock_guard<std::mutex> lock(g_CallbackMutex);

        if (g_ModCallbacks.find(mod_id) == g_ModCallbacks.end())
        {
            g_ModCallbacks[mod_id] = std::make_unique<GameEventCallbacks>();
        }

        g_ModCallbacks[mod_id]->returnToMenu.emplace_back(callback, userdata);
    }

    void wolfRuntimeRegisterItemPickup(WolfModId mod_id, WolfItemPickupCallback callback, void *userdata)
    {
        if (!callback)
            return;

        std::lock_guard<std::mutex> lock(g_CallbackMutex);

        if (g_ModCallbacks.find(mod_id) == g_ModCallbacks.end())
        {
            g_ModCallbacks[mod_id] = std::make_unique<GameEventCallbacks>();
        }

        g_ModCallbacks[mod_id]->itemPickup.emplace_back(callback, userdata);
    }

    int wolfRuntimeRegisterItemPickupBlocking(WolfModId mod_id, WolfItemPickupBlockingCallback callback, void *userdata)
    {
        if (!callback)
            return 0;

        std::lock_guard<std::mutex> lock(g_CallbackMutex);

        if (g_ModCallbacks.find(mod_id) == g_ModCallbacks.end())
        {
            g_ModCallbacks[mod_id] = std::make_unique<GameEventCallbacks>();
        }

        // Check if there's already a blocking callback registered - only allow first one
        if (!g_ModCallbacks[mod_id]->itemPickupBlocking.empty())
        {
            ModInfo *mod = findMod(mod_id);
            std::string modName = mod ? mod->name : "Unknown";
            ::logWarning("[WOLF] Mod '" + modName + "' tried to register blocking item pickup callback, but one is already registered");
            return 0;
        }

        g_ModCallbacks[mod_id]->itemPickupBlocking.emplace_back(callback, userdata);
        return 1;
    }

    int wolfRuntimeRegisterBrushEdit(WolfModId mod_id, WolfBrushEditCallback callback, void *userdata)
    {
        if (!callback)
            return 0;

        std::lock_guard<std::mutex> lock(g_CallbackMutex);

        if (g_ModCallbacks.find(mod_id) == g_ModCallbacks.end())
        {
            g_ModCallbacks[mod_id] = std::make_unique<GameEventCallbacks>();
        }

        // Check if there's already a blocking callback registered - only allow first one
        if (!g_ModCallbacks[mod_id]->brushEdit.empty())
        {
            ModInfo *mod = findMod(mod_id);
            std::string modName = mod ? mod->name : "Unknown";
            ::logWarning("[WOLF] Mod '" + modName + "' tried to register brush edit callback, but one is already registered");
            return 0;
        }

        g_ModCallbacks[mod_id]->brushEdit.emplace_back(callback, userdata);
        return 1;
    }

    int wolfRuntimeHookFunction(uintptr_t address, void *detour, void **original)
    {
        if (!address || !detour)
            return 0;

        void *target = reinterpret_cast<void *>(address);

        if (MH_CreateHook(target, detour, original) != MH_OK)
        {
            return 0;
        }

        if (MH_EnableHook(target) != MH_OK)
        {
            MH_RemoveHook(target);
            return 0;
        }

        g_ActiveHooks[target] = {target, detour, original};
        return 1;
    }

    void wolfRuntimeRegisterWndProcHook(WolfModId mod_id, WolfWndProcCallback callback, void *userData)
    {
        if (!callback)
        {
            ::logError("[WOLF] registerWndProcHook: callback is null");
            return;
        }

        std::lock_guard<std::mutex> lock(g_WndProcMutex);

        // Check if mod already has a hook registered
        auto it = std::find_if(g_WndProcHooks.begin(), g_WndProcHooks.end(), [mod_id](const WndProcHook &hook) { return hook.modId == mod_id; });

        if (it != g_WndProcHooks.end())
        {
            // Update existing hook
            it->callback = callback;
            it->userData = userData;
            ::logDebug("[WOLF] Updated WndProc hook for mod %d", mod_id);
        }
        else
        {
            // Add new hook
            g_WndProcHooks.push_back({mod_id, callback, userData});
            ::logDebug("[WOLF] Registered WndProc hook for mod %d", mod_id);
        }
    }

    void wolfRuntimeUnregisterWndProcHook(WolfModId mod_id)
    {
        std::lock_guard<std::mutex> lock(g_WndProcMutex);

        auto it = std::find_if(g_WndProcHooks.begin(), g_WndProcHooks.end(), [mod_id](const WndProcHook &hook) { return hook.modId == mod_id; });

        if (it != g_WndProcHooks.end())
        {
            g_WndProcHooks.erase(it);
            ::logDebug("[WOLF] Unregistered WndProc hook for mod %d", mod_id);
        }
    }

} // extern "C"

// Internal namespace implementation
namespace wolf::runtime::internal
{

namespace
{
// Snapshot every (modId, callback, userdata) entry in a single callback list
// across all registered mods, under the callback mutex. Callers iterate the
// returned vector OUTSIDE the lock so user code can re-enter wolf APIs (which
// also take g_CallbackMutex) without deadlocking on the non-recursive mutex.
template <typename CallbackT>
struct DispatchEntry
{
    WolfModId modId;
    CallbackT callback;
    void *userdata;
};

template <typename CallbackT, typename ListSelector>
std::vector<DispatchEntry<CallbackT>> snapshotDispatch(ListSelector selector)
{
    std::vector<DispatchEntry<CallbackT>> out;
    std::lock_guard<std::mutex> lock(g_CallbackMutex);
    for (const auto &pair : g_ModCallbacks)
    {
        for (const auto &cb : selector(*pair.second))
        {
            out.push_back({pair.first, cb.first, cb.second});
        }
    }
    return out;
}
} // namespace

void callGameTick()
{
    auto snapshot = snapshotDispatch<WolfGameEventCallback>([](GameEventCallbacks &c) -> auto & { return c.gameTick; });
    for (const auto &entry : snapshot)
    {
        g_CurrentModId = entry.modId;
        try
        {
            entry.callback(entry.userdata);
        }
        catch (...)
        {
        }
    }
    g_CurrentModId = 0;
}

void callGameStart()
{
    auto snapshot = snapshotDispatch<WolfGameEventCallback>([](GameEventCallbacks &c) -> auto & { return c.gameStart; });
    for (const auto &entry : snapshot)
    {
        g_CurrentModId = entry.modId;
        try
        {
            entry.callback(entry.userdata);
        }
        catch (...)
        {
        }
    }
    g_CurrentModId = 0;
}

void callGameStop()
{
    auto snapshot = snapshotDispatch<WolfGameEventCallback>([](GameEventCallbacks &c) -> auto & { return c.gameStop; });
    for (const auto &entry : snapshot)
    {
        g_CurrentModId = entry.modId;
        try
        {
            entry.callback(entry.userdata);
        }
        catch (...)
        {
        }
    }
    g_CurrentModId = 0;
}

void callPlayStart()
{
    auto snapshot = snapshotDispatch<WolfGameEventCallback>([](GameEventCallbacks &c) -> auto & { return c.playStart; });
    for (const auto &entry : snapshot)
    {
        g_CurrentModId = entry.modId;
        try
        {
            entry.callback(entry.userdata);
        }
        catch (...)
        {
        }
    }
    g_CurrentModId = 0;
}

void callReturnToMenu()
{
    auto snapshot = snapshotDispatch<WolfGameEventCallback>([](GameEventCallbacks &c) -> auto & { return c.returnToMenu; });
    for (const auto &entry : snapshot)
    {
        g_CurrentModId = entry.modId;
        try
        {
            entry.callback(entry.userdata);
        }
        catch (...)
        {
        }
    }
    g_CurrentModId = 0;
}

void callItemPickup(int itemId, int count)
{
    auto snapshot = snapshotDispatch<WolfItemPickupCallback>([](GameEventCallbacks &c) -> auto & { return c.itemPickup; });
    for (const auto &entry : snapshot)
    {
        g_CurrentModId = entry.modId;
        try
        {
            entry.callback(itemId, count, entry.userdata);
        }
        catch (...)
        {
        }
    }
    g_CurrentModId = 0;
}

bool callItemPickupBlocking(int itemId, int count)
{
    // Non-blocking listeners run unconditionally; the first blocking listener
    // that returns nonzero short-circuits.
    auto nonBlocking = snapshotDispatch<WolfItemPickupCallback>([](GameEventCallbacks &c) -> auto & { return c.itemPickup; });
    auto blocking = snapshotDispatch<WolfItemPickupBlockingCallback>([](GameEventCallbacks &c) -> auto & { return c.itemPickupBlocking; });

    for (const auto &entry : nonBlocking)
    {
        g_CurrentModId = entry.modId;
        try
        {
            entry.callback(itemId, count, entry.userdata);
        }
        catch (...)
        {
        }
    }

    for (const auto &entry : blocking)
    {
        g_CurrentModId = entry.modId;
        try
        {
            int result = entry.callback(itemId, count, entry.userdata);
            g_CurrentModId = 0;
            return result != 0;
        }
        catch (...)
        {
        }
    }

    g_CurrentModId = 0;
    return false;
}

bool callBrushEdit(int bitIndex, int operation)
{
    auto snapshot = snapshotDispatch<WolfBrushEditCallback>([](GameEventCallbacks &c) -> auto & { return c.brushEdit; });
    for (const auto &entry : snapshot)
    {
        g_CurrentModId = entry.modId;
        try
        {
            int result = entry.callback(bitIndex, operation, entry.userdata);
            g_CurrentModId = 0;
            return result != 0;
        }
        catch (...)
        {
        }
    }

    g_CurrentModId = 0;
    return false;
}

bool callModWndProcHooks(void *hwnd, unsigned int msg, uintptr_t wParam, intptr_t lParam)
{
    // Snapshot under the lock; user code may register/unregister hooks during dispatch.
    std::vector<WndProcHook> snapshot;
    {
        std::lock_guard<std::mutex> lock(g_WndProcMutex);
        snapshot = g_WndProcHooks;
    }

    bool handled = false;

    for (const auto &hook : snapshot)
    {
        try
        {
            int result = hook.callback(hwnd, msg, wParam, lParam, hook.userData);
            if (result != 0)
            {
                ::logDebug("[WOLF] Mod %d handled WndProc message 0x%X", hook.modId, msg);
                handled = true;
            }
        }
        catch (const std::exception &e)
        {
            ::logError("[WOLF] Exception in WndProc hook for mod %d: %s", hook.modId, e.what());
        }
        catch (...)
        {
            ::logError("[WOLF] Unknown exception in WndProc hook for mod %d", hook.modId);
        }
    }

    return handled;
}

} // namespace wolf::runtime::internal
