#include <windows.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <frame/boundary.hpp>
#include <hooks/hooks.hpp>
#include <mods/mod.hpp>
#include <tables/registry.hpp>
#include <vector>

#include "support.hpp"

#ifdef _MSC_VER
#define TEST_NOINLINE __declspec( noinline )
#else
#define TEST_NOINLINE __attribute__( ( noinline ) )
#endif

namespace {

// Each detour and the target function record themselves here, the target as -1, so a case reads the order a call passed through the chain.
std::vector<int> trace;
volatile int sink = 0;

// Targets return x + 1. Each folds in its own constant through a volatile, so no two have identical code for the linker to merge, and each is long enough for
// MinHook's jump.
#define DEFINE_TARGET( name, k )                                                                                                                               \
    extern "C" TEST_NOINLINE int name( int x )                                                                                                                 \
    {                                                                                                                                                          \
        trace.push_back( -1 );                                                                                                                                 \
        sink = x + ( k );                                                                                                                                      \
        return sink - ( k ) + 1;                                                                                                                               \
    }

DEFINE_TARGET( hookTargetA, 101 )
DEFINE_TARGET( hookTargetOrder, 102 )
DEFINE_TARGET( hookTargetSameMod, 103 )
DEFINE_TARGET( hookTargetInsert, 104 )
DEFINE_TARGET( hookTargetRemove, 105 )
DEFINE_TARGET( hookTargetEmpty, 106 )
DEFINE_TARGET( hookTargetRevive, 107 )
DEFINE_TARGET( hookTargetReAdd, 108 )
DEFINE_TARGET( hookTargetTwice, 109 )
DEFINE_TARGET( hookTargetP, 110 )
DEFINE_TARGET( hookTargetQ, 111 )
DEFINE_TARGET( hookTargetR, 112 )
DEFINE_TARGET( hookTargetS, 113 )
DEFINE_TARGET( hookTargetSite, 114 )
DEFINE_TARGET( hookTargetU, 115 )
DEFINE_TARGET( hookTargetSiteLate, 116 )

// Not code: MinHook refuses to put a head on it.
uint8_t notCode[ 64 ] = {};

template <int Id>
int ( *original )( int ) = nullptr;

// Each detour sets bit 8 + Id of the result (wrapping past bit 30) and records Id.
template <int Id>
int detour( int x )
{
    trace.push_back( Id );
    return original<Id>( x ) | ( 1 << ( ( 8 + Id ) % 31 ) );
}

template <int Id>
WolfHandle add( WolfMod &mod, int ( *target )( int ) )
{
    return wolf::hookAdd( &mod, reinterpret_cast<void *>( target ), reinterpret_cast<void *>( &detour<Id> ), reinterpret_cast<void **>( &original<Id> ) );
}

// The trace of one call of target.
std::vector<int> traceOf( int ( *target )( int ) )
{
    trace.clear();
    target( 1 );
    return trace;
}

int ( *siteChain )( int ) = nullptr;

int siteDetour( int x )
{
    trace.push_back( 100 );
    return siteChain( x );
}

int ( *lateSiteChain )( int ) = nullptr;

int lateSiteDetour( int x )
{
    trace.push_back( 101 );
    return lateSiteChain( x );
}

void begin()
{
    wolf::hooksInit();
}

} // namespace

TEST_SUITE( "hooks" )
{
    TEST_CASE( "a hook links at the next boundary and its original calls the function" )
    {
        begin();
        static WolfMod mod{};
        REQUIRE( add<0>( mod, &hookTargetA ) != 0 );
        CHECK( hookTargetA( 1 ) == 2 );
        wolf::frameBoundary();
        CHECK( hookTargetA( 1 ) == ( 2 | 0x100 ) );
        CHECK( original<0> != nullptr );
        drainRuntime();
    }

    TEST_CASE( "a later mod's link sits further out" )
    {
        begin();
        static WolfMod early{ .loadIndex = 0 };
        static WolfMod late{ .loadIndex = 1 };
        REQUIRE( add<1>( late, &hookTargetOrder ) != 0 );
        REQUIRE( add<2>( early, &hookTargetOrder ) != 0 );
        wolf::frameBoundary();
        CHECK( traceOf( &hookTargetOrder ) == std::vector{ 1, 2, -1 } );
        drainRuntime();
    }

    TEST_CASE( "a later hook of the same mod sits further out" )
    {
        begin();
        static WolfMod mod{};
        REQUIRE( add<3>( mod, &hookTargetSameMod ) != 0 );
        REQUIRE( add<4>( mod, &hookTargetSameMod ) != 0 );
        wolf::frameBoundary();
        CHECK( traceOf( &hookTargetSameMod ) == std::vector{ 4, 3, -1 } );
        drainRuntime();
    }

    TEST_CASE( "a link applied after a later mod's link is inserted at its load-order position" )
    {
        begin();
        static WolfMod mods[ 3 ] = { { .loadIndex = 0 }, { .loadIndex = 1 }, { .loadIndex = 2 } };
        REQUIRE( add<5>( mods[ 2 ], &hookTargetInsert ) != 0 );
        wolf::frameBoundary();
        REQUIRE( add<6>( mods[ 0 ], &hookTargetInsert ) != 0 );
        wolf::frameBoundary();
        REQUIRE( add<7>( mods[ 1 ], &hookTargetInsert ) != 0 );
        wolf::frameBoundary();
        CHECK( traceOf( &hookTargetInsert ) == std::vector{ 5, 7, 6, -1 } );
        drainRuntime();
    }

    TEST_CASE( "removing a link relinks the chain around it at the boundary after" )
    {
        begin();
        static WolfMod mods[ 3 ] = { { .loadIndex = 0 }, { .loadIndex = 1 }, { .loadIndex = 2 } };
        REQUIRE( add<8>( mods[ 0 ], &hookTargetRemove ) != 0 );
        const WolfHandle middle = add<9>( mods[ 1 ], &hookTargetRemove );
        REQUIRE( middle != 0 );
        REQUIRE( add<10>( mods[ 2 ], &hookTargetRemove ) != 0 );
        wolf::frameBoundary();
        wolf::registryRemove( &mods[ 1 ], middle );
        CHECK( traceOf( &hookTargetRemove ) == std::vector{ 10, 9, 8, -1 } );
        wolf::frameBoundary();
        CHECK( traceOf( &hookTargetRemove ) == std::vector{ 10, 8, -1 } );
        drainRuntime();
    }

    TEST_CASE( "removing every link leaves the function behaving as unhooked" )
    {
        begin();
        static WolfMod mods[ 2 ] = { { .loadIndex = 0 }, { .loadIndex = 1 } };
        const WolfHandle inner = add<11>( mods[ 0 ], &hookTargetEmpty );
        const WolfHandle outer = add<12>( mods[ 1 ], &hookTargetEmpty );
        wolf::frameBoundary();
        CHECK( hookTargetEmpty( 1 ) == ( 2 | ( 1 << 19 ) | ( 1 << 20 ) ) );
        wolf::registryRemove( &mods[ 1 ], outer );
        wolf::frameBoundary();
        wolf::registryRemove( &mods[ 0 ], inner );
        wolf::frameBoundary();
        CHECK( hookTargetEmpty( 1 ) == 2 );
        CHECK( traceOf( &hookTargetEmpty ) == std::vector{ -1 } );
        drainRuntime();
    }

    TEST_CASE( "a removed hook added again revives at its position" )
    {
        begin();
        static WolfMod mods[ 3 ] = { { .loadIndex = 0 }, { .loadIndex = 1 }, { .loadIndex = 2 } };
        REQUIRE( add<13>( mods[ 0 ], &hookTargetRevive ) != 0 );
        const WolfHandle first = add<14>( mods[ 1 ], &hookTargetRevive );
        REQUIRE( add<15>( mods[ 2 ], &hookTargetRevive ) != 0 );
        wolf::frameBoundary();
        wolf::registryRemove( &mods[ 1 ], first );
        wolf::frameBoundary();
        CHECK( traceOf( &hookTargetRevive ) == std::vector{ 15, 13, -1 } );
        const WolfHandle second = add<14>( mods[ 1 ], &hookTargetRevive );
        CHECK( second != 0 );
        CHECK( second != first );
        wolf::frameBoundary();
        CHECK( traceOf( &hookTargetRevive ) == std::vector{ 15, 14, 13, -1 } );
        drainRuntime();
    }

    TEST_CASE( "removing and re-adding a hook before the boundary leaves it linked once" )
    {
        begin();
        static WolfMod mod{};
        const WolfHandle first = add<16>( mod, &hookTargetReAdd );
        wolf::frameBoundary();
        wolf::registryRemove( &mod, first );
        CHECK( add<16>( mod, &hookTargetReAdd ) != 0 );
        wolf::frameBoundary();
        CHECK( traceOf( &hookTargetReAdd ) == std::vector{ 16, -1 } );
        drainRuntime();
    }

    TEST_CASE( "a second add of a linked hook is refused" )
    {
        begin();
        static WolfMod mod{};
        REQUIRE( add<17>( mod, &hookTargetTwice ) != 0 );
        CHECK( add<17>( mod, &hookTargetTwice ) == 0 );
        wolf::frameBoundary();
        CHECK( add<17>( mod, &hookTargetTwice ) == 0 );
        CHECK( traceOf( &hookTargetTwice ) == std::vector{ 17, -1 } );
        drainRuntime();
    }

    TEST_CASE( "a detour or original already used on another target is refused" )
    {
        begin();
        static WolfMod mod{};
        REQUIRE( add<18>( mod, &hookTargetP ) != 0 );
        wolf::frameBoundary();
        auto *const p = reinterpret_cast<void *>( &hookTargetP );
        auto *const q = reinterpret_cast<void *>( &hookTargetQ );
        CHECK( wolf::hookAdd( &mod, q, reinterpret_cast<void *>( &detour<18> ), reinterpret_cast<void **>( &original<19> ) ) == 0 );
        CHECK( wolf::hookAdd( &mod, q, reinterpret_cast<void *>( &detour<19> ), reinterpret_cast<void **>( &original<18> ) ) == 0 );
        CHECK( wolf::hookAdd( &mod, p, reinterpret_cast<void *>( &detour<18> ), reinterpret_cast<void **>( &original<19> ) ) == 0 );
        drainRuntime();
    }

    TEST_CASE( "a withdrawn detour stays tied to its target" )
    {
        begin();
        static WolfMod mod{};
        const WolfHandle handle = add<20>( mod, &hookTargetR );
        wolf::frameBoundary();
        wolf::registryRemove( &mod, handle );
        wolf::frameBoundary();
        CHECK( add<20>( mod, &hookTargetS ) == 0 );
        drainRuntime();
    }

    TEST_CASE( "a link on a site target runs inside the site detour" )
    {
        begin();
        static WolfMod mod{};
        siteChain =
            reinterpret_cast<int ( * )( int )>( wolf::hookSite( reinterpret_cast<void *>( &hookTargetSite ), reinterpret_cast<void *>( &siteDetour ) ) );
        REQUIRE( siteChain != nullptr );
        wolf::hooksCommit();
        CHECK( traceOf( &hookTargetSite ) == std::vector{ 100, -1 } );
        const WolfHandle handle = add<21>( mod, &hookTargetSite );
        wolf::frameBoundary();
        CHECK( traceOf( &hookTargetSite ) == std::vector{ 100, 21, -1 } );
        wolf::registryRemove( &mod, handle );
        wolf::frameBoundary();
        CHECK( traceOf( &hookTargetSite ) == std::vector{ 100, -1 } );
        drainRuntime();
    }

    TEST_CASE( "a link queued before its site's head exists runs inside the site detour" )
    {
        // Boot creates the site heads before it applies what the mod inits queued, so a mod's hook on a site function joins the site's chain.
        begin();
        static WolfMod mod{};
        REQUIRE( add<25>( mod, &hookTargetSiteLate ) != 0 );
        lateSiteChain = reinterpret_cast<int ( * )( int )>(
            wolf::hookSite( reinterpret_cast<void *>( &hookTargetSiteLate ), reinterpret_cast<void *>( &lateSiteDetour ) ) );
        REQUIRE( lateSiteChain != nullptr );
        wolf::frameBoundary();
        CHECK( traceOf( &hookTargetSiteLate ) == std::vector{ 101, 25, -1 } );
        drainRuntime();
    }

    TEST_CASE( "a target MinHook refuses leaves the other heads of its batch working" )
    {
        begin();
        static WolfMod mod{};
        CHECK( wolf::hookAdd( &mod, notCode, reinterpret_cast<void *>( &detour<22> ), reinterpret_cast<void **>( &original<22> ) ) != 0 );
        REQUIRE( add<23>( mod, &hookTargetU ) != 0 );
        wolf::frameBoundary();
        CHECK( traceOf( &hookTargetU ) == std::vector{ 23, -1 } );
        CHECK( wolf::hookAdd( &mod, notCode, reinterpret_cast<void *>( &detour<24> ), reinterpret_cast<void **>( &original<24> ) ) != 0 );
        wolf::frameBoundary();
        drainRuntime();
    }

    TEST_CASE( "a raw target is the module base plus the RVA, inside the image only" )
    {
        auto *const base = reinterpret_cast<uint8_t *>( GetModuleHandleW( nullptr ) );
        const auto rva = static_cast<uintptr_t>( reinterpret_cast<uint8_t *>( &hookTargetA ) - base );
        CHECK( wolf::rawTarget( "wolf-tests.exe", rva ) == reinterpret_cast<void *>( &hookTargetA ) );
        CHECK( wolf::rawTarget( "no-such.dll", 0x1000 ) == nullptr );
        const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>( base );
        const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>( base + dos->e_lfanew );
        CHECK( wolf::rawTarget( "wolf-tests.exe", nt->OptionalHeader.SizeOfImage ) == nullptr );
        CHECK( wolf::rawTarget( "wolf-tests.exe", nt->OptionalHeader.SizeOfImage - 1 ) != nullptr );
    }
}
