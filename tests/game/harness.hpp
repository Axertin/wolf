#pragma once

#include <windows.h>

#include <doctest/doctest.h>

#include <string>
#include <string_view>
#include <vector>

#include "fake_game.hpp"

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

// Removes binDir()'s logs\ directory, loads dinput8.dll first when WOLF_GAMETESTS_PROXY_FIRST is set, then writes main.dll (fake_main.dll with the fingerprint
// and a jump to its Fake_ export at each main.dll function RVA of the symbol table) into binDir() and loads it, once per process.
const FakeMain &fakeMain();

// Every event the test mods have reported, in order. Reports lost past the list's capacity fail the case.
std::vector<FakeEvent> events();

// The events at index from or later that mod reported as what.
size_t countEvents( const std::vector<FakeEvent> &events, std::string_view mod, std::string_view what, size_t from = 0 );

// fakeMain()'s export name, or null when it is missing.
template <typename Fn>
Fn fakeExport( const char *name )
{
    return reinterpret_cast<Fn>( GetProcAddress( fakeMain().main, name ) );
}

// The export name of the loaded module dll, such as a mod's DLL; a missing one fails the case.
template <typename Fn>
Fn modExport( const wchar_t *dll, const char *name )
{
    const FARPROC proc = GetProcAddress( GetModuleHandleW( dll ), name );
    REQUIRE( proc != nullptr );
    return reinterpret_cast<Fn>( proc );
}

} // namespace harness
