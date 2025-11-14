#include "input_hooks.h"

#include "../utilities/logger.h"
#include "../wolf_runtime_api.h"

#include <MinHook.h>
#include <dinput.h>
#include <imgui.h>
#include <mutex>
#include <unordered_map>

namespace wolf::runtime::hooks
{

// Shared state for mouse release (set by gui.cpp END key handler)
static bool g_MouseIsReleased = false;
static std::mutex g_InputMutex;

// Track hooked devices to avoid double-hooking
static std::unordered_map<IDirectInputDevice8W *, HRESULT(STDMETHODCALLTYPE *)(IDirectInputDevice8W *, DWORD, LPVOID)> g_OriginalGetDeviceState;

// IDirectInput8 interface hooks
using CreateDeviceFunc = HRESULT(STDMETHODCALLTYPE *)(IDirectInput8W *, REFGUID, LPDIRECTINPUTDEVICE8W *, LPUNKNOWN);
static CreateDeviceFunc g_OriginalCreateDevice = nullptr;
static IDirectInput8W *g_HookedDirectInput8 = nullptr;

bool isMouseReleased()
{
    std::lock_guard<std::mutex> lock(g_InputMutex);
    return g_MouseIsReleased;
}

void setMouseReleased(bool released)
{
    std::lock_guard<std::mutex> lock(g_InputMutex);
    g_MouseIsReleased = released;
}

// Hooked GetDeviceState for keyboard/mouse devices
HRESULT STDMETHODCALLTYPE HookedGetDeviceState(IDirectInputDevice8W *pDevice, DWORD cbData, LPVOID lpvData)
{
    // Find the original function for this device
    auto it = g_OriginalGetDeviceState.find(pDevice);
    if (it == g_OriginalGetDeviceState.end())
    {
        logError("[WOLF] GetDeviceState called on unhooked device %p!", pDevice);
        return DIERR_GENERIC;
    }

    auto originalFunc = it->second;

    // Check if original function is valid
    if (!originalFunc)
    {
        logError("[WOLF] Original GetDeviceState function is null for device %p!", pDevice);
        return DIERR_GENERIC;
    }

    // Check if we should block input (simple: just check if mouse is released for ImGui)
    bool shouldBlock = false;
    {
        std::lock_guard<std::mutex> lock(g_InputMutex);
        shouldBlock = g_MouseIsReleased;
    }

    if (shouldBlock)
    {
        // Zero out the input data to block game from receiving input
        ZeroMemory(lpvData, cbData);
        return DI_OK;
    }

    // Allow input to pass through
    return originalFunc(pDevice, cbData, lpvData);
}

// Global storage for the original GetDeviceState function (shared across all devices)
static HRESULT(STDMETHODCALLTYPE *g_GlobalOriginalGetDeviceState)(IDirectInputDevice8W *, DWORD, LPVOID) = nullptr;

// Helper to hook a device's GetDeviceState method
static bool hookDeviceGetDeviceState(IDirectInputDevice8W *pDevice)
{
    if (!pDevice)
        return false;

    // Check if we've already registered this device
    if (g_OriginalGetDeviceState.find(pDevice) != g_OriginalGetDeviceState.end())
        return true;

    // Get the vtable
    void **vtable = *reinterpret_cast<void ***>(pDevice);

    // IDirectInputDevice8::GetDeviceState is at vtable index 9
    void *getDeviceStatePtr = vtable[9];

    // Try to create the hook (might already exist if devices share vtables)
    HRESULT(STDMETHODCALLTYPE * originalFunc)
    (IDirectInputDevice8W *, DWORD, LPVOID) = nullptr;

    MH_STATUS status = MH_CreateHook(getDeviceStatePtr, reinterpret_cast<LPVOID>(&HookedGetDeviceState), reinterpret_cast<LPVOID *>(&originalFunc));

    if (status == MH_OK)
    {
        // Successfully created new hook
        g_GlobalOriginalGetDeviceState = originalFunc;

        // Enable the hook
        if (MH_EnableHook(getDeviceStatePtr) != MH_OK)
        {
            logError("[WOLF] Failed to enable GetDeviceState hook");
            return false;
        }

        logInfo("[WOLF] Successfully hooked GetDeviceState (will block input when mouse is released)");
    }
    else if (status == MH_ERROR_ALREADY_CREATED)
    {
        // Hook already exists (devices share vtables), just register this device silently
    }
    else
    {
        logError("[WOLF] Failed to hook GetDeviceState: %d", status);
        return false;
    }

    // Register this device with the global original function
    g_OriginalGetDeviceState[pDevice] = g_GlobalOriginalGetDeviceState;

    return true;
}

// Hooked CreateDevice to intercept keyboard/mouse device creation
HRESULT STDMETHODCALLTYPE HookedCreateDevice(IDirectInput8W *pThis, REFGUID rguid, LPDIRECTINPUTDEVICE8W *lplpDirectInputDevice, LPUNKNOWN pUnkOuter)
{
    HRESULT result = g_OriginalCreateDevice(pThis, rguid, lplpDirectInputDevice, pUnkOuter);

    if (SUCCEEDED(result) && lplpDirectInputDevice && *lplpDirectInputDevice)
    {
        // Check if this is a keyboard or mouse device
        if (IsEqualGUID(rguid, GUID_SysKeyboard) || IsEqualGUID(rguid, GUID_SysMouse))
        {
            hookDeviceGetDeviceState(*lplpDirectInputDevice);
        }
    }

    return result;
}

// Hook the IDirectInput8 interface's CreateDevice method
// Called from dinput8.cpp when DirectInput8Create succeeds
void hookDirectInput8InterfaceFromProxy(void *pDI)
{
    if (!pDI)
        return;

    IDirectInput8W *pDirectInput = static_cast<IDirectInput8W *>(pDI);

    if (g_HookedDirectInput8 == pDirectInput)
        return;

    logDebug("[WOLF] Hooking IDirectInput8::CreateDevice...");

    // Get vtable
    void **vtable = *reinterpret_cast<void ***>(pDirectInput);

    // IDirectInput8::CreateDevice is at vtable index 3
    void *createDevicePtr = vtable[3];

    MH_STATUS status = MH_CreateHook(createDevicePtr, reinterpret_cast<LPVOID>(&HookedCreateDevice), reinterpret_cast<LPVOID *>(&g_OriginalCreateDevice));

    if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED)
    {
        logError("[WOLF] Failed to hook CreateDevice: %d", status);
        return;
    }

    if (MH_EnableHook(createDevicePtr) != MH_OK)
    {
        logError("[WOLF] Failed to enable CreateDevice hook");
        return;
    }

    g_HookedDirectInput8 = pDirectInput;
    logInfo("[WOLF] Successfully hooked DirectInput8::CreateDevice");
}

bool setupInputHooks()
{
    logDebug("[WOLF] DirectInput8 input hooks ready (devices will be hooked as they're created)");
    return true;
}

void cleanupInputHooks()
{
    logDebug("[WOLF] Cleaning up DirectInput8 hooks...");

    // Unhook CreateDevice
    if (g_OriginalCreateDevice && g_HookedDirectInput8)
    {
        void **vtable = *reinterpret_cast<void ***>(g_HookedDirectInput8);
        void *createDevicePtr = vtable[3];
        MH_DisableHook(createDevicePtr);
        MH_RemoveHook(createDevicePtr);
        g_OriginalCreateDevice = nullptr;
        g_HookedDirectInput8 = nullptr;
    }

    // Unhook all device hooks
    for (auto &[device, original] : g_OriginalGetDeviceState)
    {
        void **vtable = *reinterpret_cast<void ***>(device);
        void *getDeviceStatePtr = vtable[9];
        MH_DisableHook(getDeviceStatePtr);
        MH_RemoveHook(getDeviceStatePtr);
    }

    g_OriginalGetDeviceState.clear();

    logDebug("[WOLF] DirectInput8 hooks cleanup complete");
}

} // namespace wolf::runtime::hooks
