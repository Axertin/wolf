#pragma once

#include <windows.h>

#include <string>

namespace harness {

inline constexpr int kFirstCalls = 8;

// The PE fingerprint of the game's real main.dll.
inline constexpr DWORD kMainTimeDateStamp = 0x60E54536;
inline constexpr DWORD kMainSizeOfImage = 0x1150000;

struct FakeMain
{
    HMODULE main = nullptr;          // null if main.dll failed to load
    bool proxyLoadedBefore = false;  // a dinput8.dll was mapped before main.dll
    bool systemLoadedOnLoad = false; // the system dinput8.dll was mapped by the time main.dll finished loading
    int firstCallsCreated = 0;       // of kFirstCalls DirectInput8Create calls made at once, before any test runs, the ones that returned an interface
};

// Directory of the running exe, with a trailing backslash. The build puts dinput8.dll and fake_main.dll there too.
std::wstring binDir();

// Full path of the system dinput8.dll, the one the proxy forwards to.
std::wstring systemDinput8Path();

// Writes main.dll (fake_main.dll with the fingerprint) into binDir() and loads it, once per process.
const FakeMain &fakeMain();

} // namespace harness
