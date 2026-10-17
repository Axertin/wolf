#include "harness.hpp"

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

#include <doctest/doctest.h>

namespace {

using CreateFn = HRESULT( WINAPI * )( HINSTANCE, DWORD, REFIID, void **, IUnknown * );
using ClassObjectFn = HRESULT( WINAPI * )( REFCLSID, REFIID, void ** );
using CanUnloadFn = HRESULT( WINAPI * )();
using DataFormatFn = const DIDATAFORMAT *(WINAPI *)();
using FakeMainCreateFn = CreateFn ( * )();

HMODULE proxy()
{
    return GetModuleHandleW( ( harness::binDir() + L"dinput8.dll" ).c_str() );
}

template <typename Fn>
Fn proxyExport( const char *name )
{
    return reinterpret_cast<Fn>( GetProcAddress( proxy(), name ) );
}

template <typename Fn>
Fn systemExport( const char *name )
{
    HMODULE system = LoadLibraryW( harness::systemDinput8Path().c_str() );
    return reinterpret_cast<Fn>( GetProcAddress( system, name ) );
}

} // namespace

TEST_CASE( "loading main.dll maps the proxy from the game directory and binds its import to it" )
{
    const harness::FakeMain &fake = harness::fakeMain();
    REQUIRE( fake.main != nullptr );
    CHECK_FALSE( fake.proxyLoadedBefore );
    REQUIRE( proxy() != nullptr );

    const auto boundCreate = reinterpret_cast<FakeMainCreateFn>( GetProcAddress( fake.main, "FakeMain_DirectInput8Create" ) );
    REQUIRE( boundCreate != nullptr );
    CHECK( reinterpret_cast<void *>( boundCreate() ) == reinterpret_cast<void *>( GetProcAddress( proxy(), "DirectInput8Create" ) ) );
}

TEST_CASE( "the proxy loads the system dinput8.dll only on the first call" )
{
    const harness::FakeMain &fake = harness::fakeMain();
    REQUIRE( fake.main != nullptr );
    CHECK_FALSE( fake.systemLoadedOnLoad );
}

TEST_CASE( "the first DirectInput8Create calls, made from several threads at once, each forward" )
{
    const harness::FakeMain &fake = harness::fakeMain();
    REQUIRE( fake.main != nullptr );
    CHECK( fake.firstCallsCreated == harness::kFirstCalls );
    CHECK( GetModuleHandleW( harness::systemDinput8Path().c_str() ) != nullptr );
}

TEST_CASE( "the COM exports forward to the system dinput8.dll" )
{
    REQUIRE( harness::fakeMain().main != nullptr );

    const auto getClassObject = proxyExport<ClassObjectFn>( "DllGetClassObject" );
    REQUIRE( getClassObject != nullptr );
    IClassFactory *factory = nullptr;
    REQUIRE( getClassObject( CLSID_DirectInput8, IID_IClassFactory, reinterpret_cast<void **>( &factory ) ) == S_OK );
    REQUIRE( factory != nullptr );
    IDirectInput8W *input = nullptr;
    REQUIRE( factory->CreateInstance( nullptr, IID_IDirectInput8W, reinterpret_cast<void **>( &input ) ) == S_OK );
    REQUIRE( input != nullptr );
    CHECK( input->Initialize( GetModuleHandleW( nullptr ), DIRECTINPUT_VERSION ) == DI_OK );
    input->Release();
    factory->Release();

    const auto canUnload = proxyExport<CanUnloadFn>( "DllCanUnloadNow" );
    REQUIRE( canUnload != nullptr );
    const HRESULT hr = canUnload();
    CHECK( ( hr == S_OK || hr == S_FALSE ) );

    // Registering would write to the registry of the machine running the tests, so these two are only resolved, not called.
    CHECK( GetProcAddress( proxy(), "DllRegisterServer" ) != nullptr );
    CHECK( GetProcAddress( proxy(), "DllUnregisterServer" ) != nullptr );
}

TEST_CASE( "GetdfDIJoystick returns what the system dinput8.dll returns" )
{
    REQUIRE( harness::fakeMain().main != nullptr );
    const auto dataFormat = proxyExport<DataFormatFn>( "GetdfDIJoystick" );
    REQUIRE( dataFormat != nullptr );

    // Windows exports GetdfDIJoystick; Wine's builtin dinput8.dll does not, and then the proxy returns null.
    const auto systemDataFormat = systemExport<DataFormatFn>( "GetdfDIJoystick" );
    if ( systemDataFormat == nullptr )
    {
        CHECK( dataFormat() == nullptr );
        return;
    }
    const DIDATAFORMAT *format = dataFormat();
    REQUIRE( format != nullptr );
    CHECK( format == systemDataFormat() );
    CHECK( format->dwSize == sizeof( DIDATAFORMAT ) );
}
