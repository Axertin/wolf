#include <windows.h>

#include <wolf_version.h>

#include <doctest/doctest.h>
#include <wolf/sites.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <config/config.hpp>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <frame/boundary.hpp>
#include <functional>
#include <hooks/hooks.hpp>
#include <mods/api.hpp>
#include <mods/mod.hpp>
#include <mods/mods.hpp>
#include <sites/sites.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "support.hpp"

#ifdef _MSC_VER
#define TEST_NOINLINE __declspec( noinline )
#else
#define TEST_NOINLINE __attribute__( ( noinline ) )
#endif

namespace {

const WolfApi &api = *wolf::wolfApi();

volatile int sink = 0;

extern "C" TEST_NOINLINE int apiRawTarget( int x )
{
    sink = x + 201;
    return sink - 201 + 1;
}

int ( *rawOriginal )( int ) = nullptr;

int rawDetour( int x )
{
    return rawOriginal( x ) + 100;
}

int ( *unusedOriginal )( int ) = nullptr;

int unusedDetour( int x )
{
    return unusedOriginal( x );
}

void countCall( WolfMod * /*mod*/, void * /*ctx*/, void *user )
{
    ++*static_cast<int *>( user );
}

void countAtomic( WolfMod * /*mod*/, void * /*ctx*/, void *user )
{
    static_cast<std::atomic<int> *>( user )->fetch_add( 1 );
}

int runs = 0;
uint32_t runValue = 0;

void countRun( WolfMod * /*mod*/, void *data, uint32_t len )
{
    ++runs;
    if ( len == sizeof( runValue ) )
    {
        std::memcpy( &runValue, data, len );
    }
}

void countFinal( WolfMod * /*mod*/, void *data )
{
    ++*static_cast<int *>( data );
}

void begin()
{
    wolf::sitesInit();
    wolf::hooksInit();
}

} // namespace

TEST_SUITE( "api" )
{
    TEST_CASE( "the table describes itself" )
    {
        CHECK( api.abiVersion == WOLF_ABI_VERSION );
        CHECK( api.size == sizeof( WolfApi ) );
        CHECK( std::string_view( api.runtimeVersion ) == WOLF_VERSION_STRING );
        // Every member after runtimeVersion is a function pointer.
        std::array<uintptr_t, ( sizeof( WolfApi ) - offsetof( WolfApi, log ) ) / sizeof( void * )> entries{};
        std::memcpy( entries.data(), reinterpret_cast<const uint8_t *>( &api ) + offsetof( WolfApi, log ), sizeof( entries ) );
        CHECK( std::ranges::none_of( entries, []( uintptr_t entry ) { return entry == 0; } ) );
    }

    TEST_CASE( "every check refuses its call" )
    {
        begin();
        struct Row
        {
            std::function<bool( WolfMod * )> call; // true when the entry returned its refusal value
            const char *label;                     // which check the row exercises, for the failure message
        };
        static uint8_t buffer[ 64 ] = {};
        auto **original = reinterpret_cast<void **>( &unusedOriginal );
        void *detour = reinterpret_cast<void *>( &unusedDetour );
        const std::vector<Row> rows = {
            { [ & ]( WolfMod *m ) { return api.alloc( m, 4, 8, 8 ) == nullptr; }, "alloc refused: lifetime 4 is not 0 to 3" },
            { [ & ]( WolfMod *m ) { return api.alloc( m, WOLF_LIFETIME_PROCESS, 8, 3 ) == nullptr; }, "alloc refused: align 3 is not a power of two" },
            { [ & ]( WolfMod *m ) { return api.alloc( m, WOLF_LIFETIME_PROCESS, 8, 0 ) == nullptr; }, "alloc refused: align 0 is not a power of two" },
            { [ & ]( WolfMod *m ) { return api.onReset( m, -1, &countFinal, nullptr ) == 0; }, "onReset refused: lifetime -1 is not 0 to 3" },
            { [ & ]( WolfMod *m ) { return api.onReset( m, WOLF_LIFETIME_SESSION, nullptr, nullptr ) == 0; }, "onReset refused: fn is null" },
            { [ & ]( WolfMod *m ) { return api.on( m, WOLF_SITE_TICK, &countCall, nullptr, WOLF_LIFETIME_FRAME ) == 0; },
              "on refused: the frame lifetime cannot hold a registration" },
            { [ & ]( WolfMod *m ) { return api.on( m, WOLF_SITE_TICK, &countCall, nullptr, 7 ) == 0; }, "on refused: lifetime 7 is not 0 to 3" },
            { [ & ]( WolfMod *m ) { return api.on( m, nullptr, &countCall, nullptr, WOLF_LIFETIME_PROCESS ) == 0; }, "on refused: site is null" },
            { [ & ]( WolfMod *m ) { return api.on( m, WOLF_SITE_TICK, nullptr, nullptr, WOLF_LIFETIME_PROCESS ) == 0; }, "on refused: cb is null" },
            { [ & ]( WolfMod *m ) { return api.on( m, "nope", &countCall, nullptr, WOLF_LIFETIME_PROCESS ) == 0; }, "on refused: no site named nope" },
            { [ & ]( WolfMod *m ) { return api.on( m, "Tick", &countCall, nullptr, WOLF_LIFETIME_PROCESS ) == 0; }, "on refused: no site named Tick" },
            { [ & ]( WolfMod *m ) { return api.hook( m, nullptr, detour, original ) == 0; }, "hook refused: name is null" },
            { [ & ]( WolfMod *m ) { return api.hook( m, "no such", nullptr, original ) == 0; }, "hook refused: detour is null" },
            { [ & ]( WolfMod *m ) { return api.hook( m, "no such", detour, nullptr ) == 0; }, "hook refused: original is null" },
            { [ & ]( WolfMod *m ) { return api.hook( m, "no such", detour, original ) == 0; }, "hook refused: no such did not resolve" },
            { [ & ]( WolfMod *m ) { return api.hookRaw( m, nullptr, 0x10, detour, original ) == 0; }, "hookRaw refused: module is null" },
            { [ & ]( WolfMod *m ) { return api.hookRaw( m, "no-such.dll", 0x10, detour, original ) == 0; },
              "hookRaw refused: no-such.dll+0x10 is not inside a loaded module" },
            { [ & ]( WolfMod *m ) { return api.hookRaw( m, "wolf-tests.exe", 0x7FFFFFFF, detour, original ) == 0; },
              "hookRaw refused: wolf-tests.exe+0x7FFFFFFF is not inside a loaded module" },
            { [ & ]( WolfMod *m ) {
                 api.log( m, WOLF_LOG_INFO, nullptr );
                 return true;
             },
              "log refused: msg is null" },
            { [ & ]( WolfMod *m ) { return api.resolve( m, nullptr ) == nullptr; }, "resolve refused: name is null" },
            { [ & ]( WolfMod *m ) { return api.configInt( m, nullptr, 7 ) == 7; }, "configInt refused: key is null" },
            { [ & ]( WolfMod *m ) { return api.configFloat( m, nullptr, 2.5 ) == 2.5; }, "configFloat refused: key is null" },
            { [ & ]( WolfMod *m ) { return api.configBool( m, nullptr, 1 ) == 1; }, "configBool refused: key is null" },
            { [ & ]( WolfMod *m ) { return std::string_view( api.configStr( m, nullptr, "fb" ) ) == "fb"; }, "configStr refused: key is null" },
            { [ & ]( WolfMod *m ) { return api.runInGame( m, nullptr, nullptr, 0 ) == 0; }, "runInGame refused: fn is null" },
            { [ & ]( WolfMod *m ) { return api.runInGame( m, &countRun, buffer, 65 ) == 0; }, "runInGame refused: len 65 is over 64" },
            { [ & ]( WolfMod *m ) { return api.runInGame( m, &countRun, nullptr, 4 ) == 0; }, "runInGame refused: data is null" },
            { [ & ]( WolfMod *m ) { return api.resourceOverride( m, "a", "b", nullptr, 0, WOLF_LIFETIME_PROCESS ) == 0; },
              "resourceOverride is not built in this runtime" },
            { [ & ]( WolfMod *m ) { return api.gameAlloc( m, 16 ) == nullptr; }, "gameAlloc is not built in this runtime" },
            { [ & ]( WolfMod *m ) { return api.provide( m, "slot", 1, buffer, 4, WOLF_LIFETIME_PROCESS ) == 0; }, "provide is not built in this runtime" },
            { [ & ]( WolfMod *m ) { return api.claimId( m, "space", "name", 3 ) == -1; }, "claimId is not built in this runtime" },
        };
        static std::array<WolfMod, 40> mods;
        REQUIRE( rows.size() <= mods.size() );
        for ( size_t i = 0; i < rows.size(); ++i )
        {
            CAPTURE( rows[ i ].label );
            WolfMod *mod = &mods[ i ];
            CHECK( rows[ i ].call( mod ) );
            CHECK( rows[ i ].call( mod ) );
        }
        drainRuntime();
    }

    TEST_CASE( "a null mod is refused by every entry" )
    {
        begin();
        auto **original = reinterpret_cast<void **>( &unusedOriginal );
        void *detour = reinterpret_cast<void *>( &unusedDetour );
        uint32_t value = 0;
        for ( int round = 0; round < 2; ++round )
        {
            api.log( nullptr, WOLF_LOG_INFO, "x" );
            CHECK( api.alloc( nullptr, WOLF_LIFETIME_FRAME, 8, 8 ) == nullptr );
            CHECK( api.onReset( nullptr, WOLF_LIFETIME_SESSION, &countFinal, nullptr ) == 0 );
            CHECK( api.on( nullptr, WOLF_SITE_TICK, &countCall, nullptr, WOLF_LIFETIME_PROCESS ) == 0 );
            CHECK( api.hook( nullptr, "no such", detour, original ) == 0 );
            CHECK( api.hookRaw( nullptr, "wolf-tests.exe", 0x10, detour, original ) == 0 );
            CHECK( api.watch( nullptr, &value, sizeof( value ), &countCall, nullptr, WOLF_LIFETIME_PROCESS ) == 0 );
            api.remove( nullptr, 1 );
            CHECK( api.write( nullptr, &value, &value, sizeof( value ) ) == 0 );
            CHECK( api.patch( nullptr, &value, &value, sizeof( value ), WOLF_LIFETIME_PROCESS ) == 0 );
            CHECK( api.resolve( nullptr, "no such" ) == nullptr );
            api.flagSet( nullptr, 1, 1 );
            api.flagClear( nullptr, 1, 1 );
            CHECK( api.flagTest( nullptr, 1, 1 ) == 0 );
            CHECK( api.giveItem( nullptr, 1, 1 ) == 0 );
            CHECK( api.configInt( nullptr, "k", 5 ) == 5 );
            CHECK( api.configFloat( nullptr, "k", 1.5 ) == 1.5 );
            CHECK( api.configBool( nullptr, "k", 1 ) == 1 );
            CHECK( std::string_view( api.configStr( nullptr, "k", "fb" ) ) == "fb" );
            CHECK( api.modPath( nullptr ) == nullptr );
            CHECK( api.runInGame( nullptr, &countRun, &value, sizeof( value ) ) == 0 );
            CHECK( api.resourceOverride( nullptr, "a", "b", nullptr, 0, WOLF_LIFETIME_PROCESS ) == 0 );
            CHECK( api.gameAlloc( nullptr, 16 ) == nullptr );
            CHECK( api.provide( nullptr, "slot", 1, &value, 4, WOLF_LIFETIME_PROCESS ) == 0 );
            CHECK( api.claimId( nullptr, "space", "name", 3 ) == -1 );
        }
        for ( const char *entry : { "log",         "alloc",      "onReset",   "on",      "hook",      "hookRaw",          "watch",     "remove",
                                    "write",       "patch",      "resolve",   "flagSet", "flagClear", "flagTest",         "giveItem",  "configInt",
                                    "configFloat", "configBool", "configStr", "modPath", "runInGame", "resourceOverride", "gameAlloc", "provide",
                                    "claimId" } )
        {
            CAPTURE( entry );
        }
        drainRuntime();
    }

    TEST_CASE( "a valid call works through the table" )
    {
        begin();
        static WolfMod mod{ .id = "valid" };
        static int ticks = 0;
        const WolfHandle handle = api.on( &mod, WOLF_SITE_TICK, &countCall, &ticks, WOLF_LIFETIME_PROCESS );
        REQUIRE( handle != 0 );
        wolf::frameBoundary();
        wolf::siteTickEnter();
        CHECK( ticks == 1 );
        api.remove( &mod, handle );
        wolf::siteTickEnter();
        CHECK( ticks == 1 );

        const void *p = api.alloc( &mod, WOLF_LIFETIME_FRAME, 16, 16 );
        CHECK( p != nullptr );
        CHECK( reinterpret_cast<uintptr_t>( p ) % 16 == 0 );

        runs = 0;
        const uint32_t value = 0x1234;
        CHECK( api.runInGame( &mod, &countRun, &value, sizeof( value ) ) == 1 );
        CHECK( runs == 0 );
        wolf::frameBoundary();
        CHECK( runs == 1 );
        CHECK( runValue == 0x1234 );

        wolf::configParse( &mod, "name = Ammy\n", "config.ini" );
        CHECK( std::string_view( api.configStr( &mod, "name", "x" ) ) == "Ammy" );
        drainRuntime();
        runs = 0;
    }

    TEST_CASE( "hook takes a name and hookRaw an RVA" )
    {
        begin();
        static WolfMod mod{ .id = "hooker" };
        void *detour = reinterpret_cast<void *>( &rawDetour );
        auto **original = reinterpret_cast<void **>( &rawOriginal );
        CHECK( api.hook( &mod, "no such", detour, original ) == 0 );

        const uintptr_t rva = reinterpret_cast<uintptr_t>( &apiRawTarget ) - reinterpret_cast<uintptr_t>( GetModuleHandleW( nullptr ) );
        const WolfHandle handle = api.hookRaw( &mod, "wolf-tests.exe", rva, detour, original );
        REQUIRE( handle != 0 );
        CHECK( apiRawTarget( 1 ) == 2 );
        wolf::frameBoundary();
        CHECK( apiRawTarget( 1 ) == 102 );

        alignas( 8 ) static uint8_t buffer[ 16 ] = {};
        auto **misaligned = reinterpret_cast<void **>( buffer + 4 );
        CHECK( api.hookRaw( &mod, "wolf-tests.exe", rva, reinterpret_cast<void *>( &unusedDetour ), misaligned ) == 0 );

        api.remove( &mod, handle );
        drainRuntime();
        CHECK( apiRawTarget( 1 ) == 2 );
    }

    TEST_CASE( "a refused mod's calls do nothing, except modPath and the config fallback" )
    {
        begin();
        static WolfMod mod{ .id = "refusedcalls", .path = R"(C:\game\mods\refusedcalls\)" };
        wolf::configParse( &mod, "k = 4\n", "config.ini" );
        REQUIRE( api.configInt( &mod, "k", 9 ) == 4 );
        wolf::modRefuse( &mod, "test" );
        static int ticks = 0;
        const uint32_t value = 1;
        CHECK( api.on( &mod, WOLF_SITE_TICK, &countCall, &ticks, WOLF_LIFETIME_PROCESS ) == 0 );
        CHECK( api.on( &mod, WOLF_SITE_TICK, &countCall, &ticks, WOLF_LIFETIME_FRAME ) == 0 );
        CHECK( api.alloc( &mod, WOLF_LIFETIME_FRAME, 16, 16 ) == nullptr );
        CHECK( api.alloc( &mod, 9, 16, 3 ) == nullptr );
        CHECK( api.runInGame( &mod, &countRun, &value, sizeof( value ) ) == 0 );
        CHECK( api.runInGame( &mod, &countRun, nullptr, 99 ) == 0 );
        CHECK( api.claimId( &mod, "space", "name", 3 ) == -1 );
        api.log( &mod, WOLF_LOG_INFO, "still logging" );
        CHECK( std::string_view( api.modPath( &mod ) ) == mod.path );
        CHECK( api.configInt( &mod, "k", 9 ) == 9 );
        runs = 0;
        drainRuntime();
        wolf::siteTickEnter();
        CHECK( ticks == 0 );
        CHECK( runs == 0 );
    }

    TEST_CASE( "registrations and queued calls from a mod refused before the apply are dropped" )
    {
        begin();
        static WolfMod mod{ .id = "dropped" };
        static int ticks = 0;
        static int finals = 0;
        runs = 0;
        REQUIRE( api.on( &mod, WOLF_SITE_TICK, &countCall, &ticks, WOLF_LIFETIME_PROCESS ) != 0 );
        REQUIRE( api.runInGame( &mod, &countRun, nullptr, 0 ) == 1 );
        REQUIRE( api.onReset( &mod, WOLF_LIFETIME_SESSION, &countFinal, &finals ) == 1 );
        REQUIRE( api.onReset( &mod, WOLF_LIFETIME_FRAME, &countFinal, &finals ) == 1 );
        wolf::modRefuse( &mod, "test" );
        drainRuntime();
        wolf::siteTickEnter();
        CHECK( ticks == 0 );
        CHECK( runs == 0 );
        CHECK( finals == 0 );
    }

    TEST_CASE( "a refused mod's thread racing the refusal never gets a registration live" )
    {
        begin();
        static WolfMod mod{ .id = "racer" };
        static std::atomic<int> fired{ 0 };
        constexpr int kThreads = 4;
        constexpr int kCalls = 60000;
        std::atomic<int> made{ 0 };
        bool refused = false;
        raceThreads(
            kThreads,
            [ & ]( int /*index*/ ) {
                for ( int i = 0; i < kCalls; ++i )
                {
                    const WolfHandle handle = api.on( &mod, WOLF_SITE_TICK, &countAtomic, &fired, WOLF_LIFETIME_PROCESS );
                    if ( i % 2 == 0 )
                    {
                        api.remove( &mod, handle );
                    }
                    made.fetch_add( 1 );
                }
            },
            // Boot applies nothing until every init has returned, so the boundaries start only once the mod is refused, halfway through the calls.
            [ & ] {
                if ( refused )
                {
                    wolf::frameBoundary();
                    wolf::siteTickEnter();
                }
                else if ( made.load() >= kThreads * kCalls / 2 )
                {
                    wolf::modRefuse( &mod, "race" );
                    refused = true;
                }
            } );
        if ( !refused )
        {
            wolf::modRefuse( &mod, "race" );
        }
        wolf::siteTickEnter();
        wolf::siteTickEnter();
        CHECK( fired.load() == 0 );
        drainRuntime();
    }
}
