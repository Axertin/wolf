/*
Generated using [dll-proxy-generator](https://github.com/maluramichael/dll-proxy-generator).
The generated code is microsoft-specific, so modified to work with other compilers.

Intercepts `dinput8.dll` to load mods.
*/
#include <windows.h>

#include <iostream>
#include <string>

#include <dinput.h>

#include "hooks/hooks.h"
#include "modloader.h"
#include "utilities/console.h"
#include "utilities/gui.h"
#include "utilities/logger.h"
#include "wolf_runtime_api.h"

#include <MinHook.h>

static struct dinput8_dll
{
    HMODULE dll;
    decltype(DirectInput8Create) *OriginalDirectInput8Create;
    FARPROC OriginalDllCanUnloadNow;
    FARPROC OriginalDllGetClassObject;
    FARPROC OriginalDllRegisterServer;
    FARPROC OriginalDllUnregisterServer;
    FARPROC OriginalGetdfDIJoystick;
} dinput8;

/*
IMPORTANT:
  Originally used __asm tags and jmp, which is not cross-compiler compatible.
  Currently relies on whatever the compiler does, if it doesn't only generate a jmp instruction it can break.
*/
extern "C"
{
    HRESULT __stdcall FakeDirectInput8Create(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID *ppvOut, LPUNKNOWN punkOuter)
    {
        return dinput8.OriginalDirectInput8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter);
    }
    HRESULT __stdcall FakeDllCanUnloadNow()
    {
        SUPPRESS_FARPROC_CAST_START
        return reinterpret_cast<HRESULT (*)()>(dinput8.OriginalDllCanUnloadNow)();
        SUPPRESS_FARPROC_CAST_END
    }
    HRESULT __stdcall FakeDllGetClassObject()
    {
        SUPPRESS_FARPROC_CAST_START
        return reinterpret_cast<HRESULT (*)()>(dinput8.OriginalDllGetClassObject)();
        SUPPRESS_FARPROC_CAST_END
    }
    HRESULT __stdcall FakeDllRegisterServer()
    {
        SUPPRESS_FARPROC_CAST_START
        return reinterpret_cast<HRESULT (*)()>(dinput8.OriginalDllRegisterServer)();
        SUPPRESS_FARPROC_CAST_END
    }
    HRESULT __stdcall FakeDllUnregisterServer()
    {
        SUPPRESS_FARPROC_CAST_START
        return reinterpret_cast<HRESULT (*)()>(dinput8.OriginalDllUnregisterServer)();
        SUPPRESS_FARPROC_CAST_END
    }
    void __stdcall FakeGetdfDIJoystick()
    {
        reinterpret_cast<void (*)()>(dinput8.OriginalGetdfDIJoystick)();
    }
}

void error(const std::string &msg)
{
    MessageBox(nullptr, msg.c_str(), "Error", MB_ICONERROR);
    std::cerr << msg << std::endl;
}

static bool(__fastcall *pOriginalFlowerStartup)(bool started);
bool __fastcall OverrideFlowerStartup(bool started)
{
    logDebug("[WOLF] flower_startup called, game performing init...");
    // Call original flower_startup first
    bool result = pOriginalFlowerStartup(started);

    if (!started)
    {
        // Call late game initialization after flower_startup completes
        // wolf::runtime::internal::callLateGameInit();
    }

    return result;
}

static decltype(CreateWindowExW) *pOriginalCreateWindowExW;
HWND WINAPI OverrideCreateWindowExW(DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth, int nHeight,
                                    HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
{
    static bool initialized = false;
    if (!initialized)
    {
        initialized = true;

        if (MH_OK != MH_CreateHookApi(L"main.dll", "?flower_startup@@YA_N_N@Z", reinterpret_cast<LPVOID>(&OverrideFlowerStartup),
                                      reinterpret_cast<LPVOID *>(&pOriginalFlowerStartup)))
        {
            error("[WOLF] Failed to hook flower_startup");
        }
        MH_EnableHook(MH_ALL_HOOKS);

        if (!wolf::runtime::hooks::setupAllHooks())
        {
            logError("[WOLF] Failed to setup game hooks!");
        }

        // Load mod DLLs immediately after enabling hooks
        logDebug("[WOLF] Game startup detected, loading mods...");
        LoadMods();

        // Call early game initialization (function hooks and memory setup)
        wolf::runtime::internal::callEarlyGameInit();
    }

    return pOriginalCreateWindowExW(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
}

void InitiateMainHook()
{
    if (!IsModded())
        return;

    // Initialize logger first
    initializeLogger();
    logInfo("[WOLF] Runtime initializing...");

    // Initialize MinHook
    MH_Initialize();

    // dinput8 gets loaded before main.dll so we hook something else and wait for that to be called
    if (MH_OK != MH_CreateHookApi(L"User32.dll", "CreateWindowExW", reinterpret_cast<LPVOID>(&OverrideCreateWindowExW),
                                  reinterpret_cast<LPVOID *>(&pOriginalCreateWindowExW)))
    {
        error("Failed to hook CreateWindowExW");
    }
    MH_EnableHook(MH_ALL_HOOKS);

    // Initialize GUI system
    guiInitHooks();
}

void DestroyHooks()
{
    // Shutdown all mods before cleaning up
    wolf::runtime::internal::shutdownMods();

    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();

    // Cleanup GUI
    guiCleanup();

    // Cleanup logger
    shutdownLogger();
}

void LoadOriginalLibrary()
{
    char path[MAX_PATH * 2];
    GetSystemDirectoryA(path, MAX_PATH);
    strcat_s(path, sizeof(path), "\\dinput8.dll");

    dinput8.dll = LoadLibraryA(path);

    if (dinput8.dll == nullptr)
    {
        MessageBoxA(0, "Cannot load original dinput8.dll library", "Proxy", MB_ICONERROR);
        ExitProcess(0);
    }

    reinterpret_cast<FARPROC &>(dinput8.OriginalDirectInput8Create) = GetProcAddress(dinput8.dll, "DirectInput8Create");
    dinput8.OriginalDllCanUnloadNow = GetProcAddress(dinput8.dll, "DllCanUnloadNow");
    dinput8.OriginalDllGetClassObject = GetProcAddress(dinput8.dll, "DllGetClassObject");
    dinput8.OriginalDllRegisterServer = GetProcAddress(dinput8.dll, "DllRegisterServer");
    dinput8.OriginalDllUnregisterServer = GetProcAddress(dinput8.dll, "DllUnregisterServer");
    dinput8.OriginalGetdfDIJoystick = GetProcAddress(dinput8.dll, "GetdfDIJoystick");
}

// This module is loaded while flower_kernel is loading, but before main.dll gets loaded, so not all hooking can happen immediately
BOOL APIENTRY DllMain([[maybe_unused]] HMODULE hModule, DWORD ul_reason_for_call, [[maybe_unused]] LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        LoadOriginalLibrary();
        InitiateMainHook();
        break;
    case DLL_PROCESS_DETACH:
        UnloadMods();
        DestroyHooks();
        FreeLibrary(dinput8.dll);
        break;
    }
    return TRUE;
}
