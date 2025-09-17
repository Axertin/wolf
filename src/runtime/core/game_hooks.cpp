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

void callGameTick()
{
    std::lock_guard<std::mutex> lock(g_CallbackMutex);

    for (const auto &pair : g_ModCallbacks)
    {
        WolfModId modId = pair.first;
        const auto &callbacks = pair.second;

        g_CurrentModId = modId; // Set context

        for (const auto &callback : callbacks->gameTick)
        {
            try
            {
                callback.first(callback.second);
            }
            catch (...)
            {
                // Log error but continue with other callbacks
            }
        }
    }

    g_CurrentModId = 0; // Clear context
}

void callGameStart()
{
    std::lock_guard<std::mutex> lock(g_CallbackMutex);

    for (const auto &pair : g_ModCallbacks)
    {
        WolfModId modId = pair.first;
        const auto &callbacks = pair.second;

        g_CurrentModId = modId;

        for (const auto &callback : callbacks->gameStart)
        {
            try
            {
                callback.first(callback.second);
            }
            catch (...)
            {
                // Continue with other callbacks
            }
        }
    }

    g_CurrentModId = 0;
}

void callGameStop()
{
    std::lock_guard<std::mutex> lock(g_CallbackMutex);

    for (const auto &pair : g_ModCallbacks)
    {
        WolfModId modId = pair.first;
        const auto &callbacks = pair.second;

        g_CurrentModId = modId;

        for (const auto &callback : callbacks->gameStop)
        {
            try
            {
                callback.first(callback.second);
            }
            catch (...)
            {
                // Continue with other callbacks
            }
        }
    }

    g_CurrentModId = 0;
}

void callPlayStart()
{
    std::lock_guard<std::mutex> lock(g_CallbackMutex);

    for (const auto &pair : g_ModCallbacks)
    {
        WolfModId modId = pair.first;
        const auto &callbacks = pair.second;

        g_CurrentModId = modId;

        for (const auto &callback : callbacks->playStart)
        {
            try
            {
                callback.first(callback.second);
            }
            catch (...)
            {
                // Continue with other callbacks
            }
        }
    }

    g_CurrentModId = 0;
}

void callReturnToMenu()
{
    std::lock_guard<std::mutex> lock(g_CallbackMutex);

    for (const auto &pair : g_ModCallbacks)
    {
        WolfModId modId = pair.first;
        const auto &callbacks = pair.second;

        g_CurrentModId = modId;

        for (const auto &callback : callbacks->returnToMenu)
        {
            try
            {
                callback.first(callback.second);
            }
            catch (...)
            {
                // Continue with other callbacks
            }
        }
    }

    g_CurrentModId = 0;
}

void callItemPickup(int itemId, int count)
{
    std::lock_guard<std::mutex> lock(g_CallbackMutex);

    for (const auto &pair : g_ModCallbacks)
    {
        WolfModId modId = pair.first;
        const auto &callbacks = pair.second;

        g_CurrentModId = modId;

        for (const auto &callback : callbacks->itemPickup)
        {
            try
            {
                callback.first(itemId, count, callback.second);
            }
            catch (...)
            {
                // Continue with other callbacks
            }
        }
    }

    g_CurrentModId = 0;
}

bool callItemPickupBlocking(int itemId, int count)
{
    std::lock_guard<std::mutex> lock(g_CallbackMutex);

    // First call all non-blocking callbacks
    for (const auto &pair : g_ModCallbacks)
    {
        WolfModId modId = pair.first;
        const auto &callbacks = pair.second;

        g_CurrentModId = modId;

        for (const auto &callback : callbacks->itemPickup)
        {
            try
            {
                callback.first(itemId, count, callback.second);
            }
            catch (...)
            {
                // Continue with other callbacks
            }
        }
    }

    // Then call blocking callbacks (only first registered)
    for (const auto &pair : g_ModCallbacks)
    {
        WolfModId modId = pair.first;
        const auto &callbacks = pair.second;

        g_CurrentModId = modId;

        for (const auto &callback : callbacks->itemPickupBlocking)
        {
            try
            {
                int result = callback.first(itemId, count, callback.second);
                g_CurrentModId = 0;
                return result != 0; // Return true if callback wants to block
            }
            catch (...)
            {
                // Continue with other callbacks
            }
        }
    }

    g_CurrentModId = 0;
    return false; // No blocking callback found or none blocked
}

bool callBrushEdit(int bitIndex, int operation)
{
    std::lock_guard<std::mutex> lock(g_CallbackMutex);

    // Call brush edit callbacks (only first registered)
    for (const auto &pair : g_ModCallbacks)
    {
        WolfModId modId = pair.first;
        const auto &callbacks = pair.second;

        g_CurrentModId = modId;

        for (const auto &callback : callbacks->brushEdit)
        {
            try
            {
                int result = callback.first(bitIndex, operation, callback.second);
                g_CurrentModId = 0;
                return result != 0; // Return true if callback wants to block
            }
            catch (...)
            {
                // Continue with other callbacks
            }
        }
    }

    g_CurrentModId = 0;
    return false; // No blocking callback found or none blocked
}

bool callModWndProcHooks(void *hwnd, unsigned int msg, uintptr_t wParam, intptr_t lParam)
{
    std::lock_guard<std::mutex> lock(g_WndProcMutex);
    bool handled = false;

    for (const auto &hook : g_WndProcHooks)
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