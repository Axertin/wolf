#include <windows.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <hooks/resolve.hpp>
#include <symbols.hpp>

#include "support.hpp"

namespace {

void *volatile lastFreed = nullptr; // the volatile store gives the export a body that can be hooked

} // namespace

// Exported from the test exe under cHeap::free's mangled flower_kernel name (wolf-tests.def).
extern "C" void wolfTestHeapFree( void *p )
{
    lastFreed = p;
}

static_assert( wolf::symbolRva( "Item_Add" ) == 0x4965d0 );
static_assert( wolf::symbolRva( "nope" ) == 0 );
static_assert( wolf::symbolRva( "wk::mem::cHeap::free" ) == 0 );

TEST_SUITE( "resolve" )
{
    TEST_CASE( "an E9 thunk resolves to its jump target" )
    {
        alignas( 16 ) uint8_t code[ 32 ] = { 0xE9, 0x0B, 0x00, 0x00, 0x00 };
        CHECK( wolf::followThunk( code ) == code + 16 );
        const uint8_t back[] = { 0xE9, 0xF6, 0xFF, 0xFF, 0xFF };
        std::ranges::copy( back, code + 16 );
        CHECK( wolf::followThunk( code + 16 ) == code + 11 );
    }

    TEST_CASE( "an address that is not an E9 thunk is its own body" )
    {
        const uint8_t mov[] = { 0x48, 0x89, 0x5C, 0x24, 0x08 };
        const uint8_t shortJump[] = { 0xEB, 0x00 };
        CHECK( wolf::followThunk( mov ) == mov );
        CHECK( wolf::followThunk( shortJump ) == shortJump );
    }

    TEST_CASE( "main.dll names resolve to the module base plus their RVA" )
    {
        auto *const self = reinterpret_cast<uint8_t *>( GetModuleHandleW( nullptr ) );
        wolf::symbolsResolve( GetModuleHandleW( nullptr ), GetModuleHandleW( nullptr ) );
        CHECK( wolf::symbolAddress( "flower_tick" ) == self + 0x4b63b0 );
        CHECK( wolf::symbolAddress( "flower_startup" ) == self + 0x4b6240 );
        CHECK( wolf::symbolAddress( "g_currentRoomId" ) == self + 0xb6b240 );
    }

    TEST_CASE( "kernel names resolve through their export past the thunk, and a name with no export resolves to null" )
    {
        wolf::symbolsResolve( GetModuleHandleW( nullptr ), GetModuleHandleW( nullptr ) );
        // Under MSVC's incremental linking the function's own address is an E9 thunk too.
        CHECK( wolf::symbolAddress( "wk::mem::cHeap::free" ) == wolf::followThunk( reinterpret_cast<const void *>( &wolfTestHeapFree ) ) );
        // wk::mem::cHeapExtended::free resolves too, to an export binding_test.cpp hooks, so its address is not checked here.
        CHECK( wolf::symbolAddress( "m2::render::SwapChain::swap" ) == nullptr );
    }

    TEST_CASE( "with neither module every name resolves to null" )
    {
        wolf::symbolsResolve( nullptr, nullptr );
        for ( const wolf::Symbol &symbol : wolf::kSymbols )
        {
            CHECK( wolf::symbolAddress( symbol.name ) == nullptr );
        }
    }

    TEST_CASE( "a name not in the table resolves to null" )
    {
        wolf::symbolsResolve( GetModuleHandleW( nullptr ), GetModuleHandleW( nullptr ) );
        CHECK( wolf::symbolAddress( "Room_change" ) == nullptr );
        CHECK( wolf::symbolAddress( "" ) == nullptr );
        CHECK( wolf::symbolAddress( "Room_Change" ) != nullptr );
    }
}
