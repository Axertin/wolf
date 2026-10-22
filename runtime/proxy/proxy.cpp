// dinput8.dll exports. Each one forwards to the same export of the system
// dinput8.dll, which is loaded on the first call and never freed. DllMain
// boots the runtime when the game was started with -MODDED.

#include <windows.h>

#include <atomic>
#include <cstring>

#include "boot/boot.hpp"

namespace {

HMODULE thisModule()
{
    HMODULE self = nullptr;
    GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>( &thisModule ),
                        &self );
    return self;
}

// The system copy is loaded by full path: by name, the loader would hand back this module, which is also called dinput8.dll.
HMODULE systemDinput8()
{
    static std::atomic<HMODULE> cached{ nullptr };
    HMODULE module = cached.load( std::memory_order_acquire );
    if ( module != nullptr )
    {
        return module;
    }

    constexpr wchar_t kName[] = L"\\dinput8.dll";
    wchar_t path[ MAX_PATH ];
    const UINT length = GetSystemDirectoryW( path, MAX_PATH );
    if ( length == 0 || length + ( sizeof( kName ) / sizeof( wchar_t ) ) > MAX_PATH )
    {
        return nullptr;
    }
    std::memcpy( path + length, kName, sizeof( kName ) );

    // Two threads can race here. Both LoadLibraryW calls return the same module, and the extra reference is never released.
    module = LoadLibraryW( path );
    // With this DLL copied into the system directory, the full path names this module too, and forwarding would call itself until the stack runs out. Fail
    // every forward instead.
    if ( module != nullptr && module == thisModule() )
    {
        FreeLibrary( module );
        return nullptr;
    }
    cached.store( module, std::memory_order_release );
    return module;
}

template <typename Fn>
Fn systemExport( const char *name )
{
    HMODULE module = systemDinput8();
    if ( module == nullptr )
    {
        return nullptr;
    }
    return reinterpret_cast<Fn>( GetProcAddress( module, name ) );
}

} // namespace

extern "C" {

HRESULT WINAPI proxy_DirectInput8Create( HINSTANCE instance, DWORD version, REFIID iid, void **out, void *outer )
{
    using Fn = HRESULT( WINAPI * )( HINSTANCE, DWORD, REFIID, void **, void * );
    const Fn fn = systemExport<Fn>( "DirectInput8Create" );
    if ( fn == nullptr )
    {
        if ( out != nullptr )
        {
            *out = nullptr;
        }
        return E_FAIL;
    }
    return fn( instance, version, iid, out, outer );
}

HRESULT WINAPI proxy_DllCanUnloadNow()
{
    using Fn = HRESULT( WINAPI * )();
    const Fn fn = systemExport<Fn>( "DllCanUnloadNow" );
    return fn != nullptr ? fn() : S_FALSE;
}

HRESULT WINAPI proxy_DllGetClassObject( REFCLSID clsid, REFIID iid, void **out )
{
    using Fn = HRESULT( WINAPI * )( REFCLSID, REFIID, void ** );
    const Fn fn = systemExport<Fn>( "DllGetClassObject" );
    if ( fn == nullptr )
    {
        if ( out != nullptr )
        {
            *out = nullptr;
        }
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    return fn( clsid, iid, out );
}

HRESULT WINAPI proxy_DllRegisterServer()
{
    using Fn = HRESULT( WINAPI * )();
    const Fn fn = systemExport<Fn>( "DllRegisterServer" );
    return fn != nullptr ? fn() : E_FAIL;
}

HRESULT WINAPI proxy_DllUnregisterServer()
{
    using Fn = HRESULT( WINAPI * )();
    const Fn fn = systemExport<Fn>( "DllUnregisterServer" );
    return fn != nullptr ? fn() : E_FAIL;
}

// Returns the system's c_dfDIJoystick, a const DIDATAFORMAT*.
const void *WINAPI proxy_GetdfDIJoystick()
{
    using Fn = const void *(WINAPI *)();
    const Fn fn = systemExport<Fn>( "GetdfDIJoystick" );
    return fn != nullptr ? fn() : nullptr;
}

BOOL WINAPI DllMain( HINSTANCE self, DWORD reason, LPVOID /*reserved*/ )
{
    if ( reason == DLL_PROCESS_ATTACH )
    {
        wolf::bootAttach( self );
    }
    return TRUE;
}

} // extern "C"
