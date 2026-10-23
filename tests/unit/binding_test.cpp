#include <windows.h>

#include <doctest/doctest.h>
#include <wolf/wolf.hpp>

#include <algorithm>
#include <arena/lifetime.hpp>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <frame/boundary.hpp>
#include <hooks/hooks.hpp>
#include <hooks/resolve.hpp>
#include <memory/memory.hpp>
#include <mods/api.hpp>
#include <mods/mod.hpp>
#include <mutex>
#include <sites/sites.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support.hpp"

#ifdef _MSC_VER
#define TEST_NOINLINE __declspec( noinline )
#else
#define TEST_NOINLINE __attribute__( ( noinline ) )
#endif

namespace {

volatile int sink = 0;

// Targets return x + 1, through a volatile and with a constant of their own so the linker never merges two of them.
#define DEFINE_TARGET( name, k )                                                                                                                               \
    extern "C" TEST_NOINLINE int name( int x )                                                                                                                 \
    {                                                                                                                                                          \
        sink = x + ( k );                                                                                                                                      \
        return sink - ( k ) + 1;                                                                                                                               \
    }

DEFINE_TARGET( bindingPlainTarget, 301 )
DEFINE_TARGET( bindingCaptureTarget, 302 )
DEFINE_TARGET( bindingRaceTarget, 303 )

uintptr_t rvaOf( int ( *target )( int ) )
{
    return reinterpret_cast<uintptr_t>( target ) - reinterpret_cast<uintptr_t>( GetModuleHandleW( nullptr ) );
}

void begin( WolfMod &mod )
{
    wolf::sitesInit();
    wolf::hooksInit();
    wolf::memoryInit();
    wolf::bind( wolf::wolfApi(), &mod );
}

int probesDestroyed = 0;

// Counts its own destruction once, however many times it is moved.
struct Probe
{
    int value = 0;
    bool live = true;

    explicit Probe( int v ) : value( v )
    {}
    Probe( const Probe & ) = delete;
    Probe &operator=( const Probe & ) = delete;
    Probe &operator=( Probe && ) = delete;
    Probe( Probe &&other ) noexcept : value( other.value )
    {
        other.live = false;
    }
    ~Probe()
    {
        if ( live )
        {
            ++probesDestroyed;
        }
    }
};

enum class InitMode : uint8_t { Succeed, Throw };
InitMode initMode = InitMode::Succeed;

bool testInit()
{
    if ( initMode == InitMode::Throw )
    {
        throw std::runtime_error( "init failed" );
    }
    return true;
}

constexpr int kRaceThreads = 4;

// Each Round instantiates a lambda type of its own, so every round races for a fresh slot.
template <int Round>
WolfHandle hookRaceRound( int k )
{
    return wolf::hookRaw<int( int )>( "wolf-tests.exe", rvaOf( &bindingRaceTarget ), [ k ]( auto original, int x ) { return original( x ) + k; } );
}

// kRaceThreads threads hook one capturing lambda at once; returns how many got a handle, after removing those.
template <int Round>
int raceHookRound()
{
    std::array<WolfHandle, kRaceThreads> handles{};
    raceThreads( kRaceThreads, [ & ]( int t ) { handles[ static_cast<size_t>( t ) ] = hookRaceRound<Round>( 1 ); } );
    int linked = 0;
    for ( const WolfHandle handle : handles )
    {
        if ( handle != 0 )
        {
            ++linked;
            wolf::remove( handle );
        }
    }
    return linked;
}

template <int... Rounds>
std::vector<int> raceHookRounds( std::integer_sequence<int, Rounds...> /*rounds*/ )
{
    return { raceHookRound<Rounds>()... };
}

} // namespace

namespace {

void *volatile lastExtendedFreed = nullptr;

} // namespace

// Exported from the test exe under cHeapExtended::free's mangled flower_kernel name (wolf-tests.def), so hook can find it by name. Only the by-name hook case
// uses it.
extern "C" TEST_NOINLINE void wolfTestHeapExtendedFree( void *p )
{
    lastExtendedFreed = p;
}

WOLF_MOD_ENTRY( testInit )

TEST_SUITE( "binding" )
{
    TEST_CASE( "a stateful site callback keeps its captures for its lifetime and destroys them at its end" )
    {
        static WolfMod mod{ .id = "bindstate" };
        begin( mod );
        probesDestroyed = 0;
        static int seen = 0;
        static int runs = 0;
        seen = 0;
        runs = 0;
        const WolfHandle handle = wolf::on<wolf::site::Tick>(
            [ probe = Probe( 7 ) ]( WolfTickCtx & /*ctx*/ ) {
                seen = probe.value;
                ++runs;
            },
            wolf::Lifetime::Session );
        REQUIRE( handle != 0 );
        CHECK( probesDestroyed == 0 );
        wolf::siteTickEnter();
        wolf::siteTickEnter();
        CHECK( seen == 7 );
        CHECK( runs == 2 );
        CHECK( probesDestroyed == 0 );

        wolf::lifetimeEnd( WOLF_LIFETIME_SESSION );
        wolf::frameBoundary();
        CHECK( probesDestroyed == 1 );
        wolf::siteTickEnter();
        CHECK( runs == 2 );
        drainRuntime();
    }

    TEST_CASE( "a captureless callback needs no allocation and gets its context" )
    {
        static WolfMod mod{ .id = "bindplain" };
        begin( mod );
        static std::vector<uint64_t> frames;
        frames.clear();
        const WolfHandle handle = wolf::on<wolf::site::Tick>( []( WolfTickCtx &c ) { frames.push_back( c.frame ); } );
        REQUIRE( handle != 0 );
        wolf::siteTickEnter();
        wolf::siteTickEnter();
        REQUIRE( frames.size() == 2 );
        CHECK( frames[ 1 ] == frames[ 0 ] + 1 );
        wolf::remove( handle );
        drainRuntime();
    }

    TEST_CASE( "a watch callback receives the previous bytes and keeps its captures" )
    {
        static WolfMod mod{ .id = "bindwatch" };
        begin( mod );
        static uint32_t word = 0;
        static uint32_t previous = 99;
        static int seen = 0;
        word = 0;
        previous = 99;
        seen = 0;
        const WolfHandle handle = wolf::watch(
            &word, sizeof( word ),
            [ probe = Probe( 5 ) ]( const WolfWatchCtx &ctx ) {
                previous = *static_cast<const uint32_t *>( ctx.previous );
                seen = probe.value;
            },
            wolf::Lifetime::Session );
        REQUIRE( handle != 0 );
        wolf::frameBoundary();
        word = 3;
        wolf::frameBoundary();
        CHECK( previous == 0 );
        CHECK( seen == 5 );
        wolf::remove( handle );
        drainRuntime();
    }

    TEST_CASE( "an exception in a callback is caught and the next callback still runs" )
    {
        static WolfMod mod{ .id = "bindthrow" };
        begin( mod );
        static int after = 0;
        after = 0;
        const WolfHandle first = wolf::on<wolf::site::Tick>( []( WolfTickCtx & /*ctx*/ ) { throw std::runtime_error( "bad" ); } );
        const WolfHandle second = wolf::on<wolf::site::Tick>( []( WolfTickCtx & /*ctx*/ ) { ++after; } );
        REQUIRE( first != 0 );
        REQUIRE( second != 0 );
        wolf::siteTickEnter();
        CHECK( after == 1 );
        wolf::remove( first );
        wolf::remove( second );
        drainRuntime();
    }

    TEST_CASE( "runInGame carries its lambda by copy" )
    {
        static WolfMod mod{ .id = "bindrun" };
        begin( mod );
        static std::array<int, 2> ran{};
        ran = {};
        int a = 3;
        int b = 4;
        CHECK( wolf::runInGame( [ a, b ] { ran = { a, b }; } ) );
        a = 0;
        b = 0;
        CHECK( ran == std::array<int, 2>{ 0, 0 } );
        wolf::frameBoundary();
        CHECK( ran == std::array<int, 2>{ 3, 4 } );
        drainRuntime();
    }

    TEST_CASE( "a captureless hook passes the original and links again after remove" )
    {
        static WolfMod mod{ .id = "bindhook" };
        begin( mod );
        const uintptr_t rva = rvaOf( &bindingPlainTarget );
        auto plusOne = []( auto original, int x ) { return original( x ) + 1; };
        const WolfHandle first = wolf::hookRaw<int( int )>( "wolf-tests.exe", rva, plusOne );
        REQUIRE( first != 0 );
        CHECK( bindingPlainTarget( 1 ) == 2 );
        wolf::frameBoundary();
        CHECK( bindingPlainTarget( 1 ) == 3 );

        wolf::remove( first );
        wolf::frameBoundary();
        CHECK( bindingPlainTarget( 1 ) == 2 );
        const WolfHandle again = wolf::hookRaw<int( int )>( "wolf-tests.exe", rva, plusOne );
        REQUIRE( again != 0 );
        wolf::frameBoundary();
        CHECK( bindingPlainTarget( 1 ) == 3 );

        wolf::remove( again );
        drainRuntime();
        CHECK( bindingPlainTarget( 1 ) == 2 );
    }

    TEST_CASE( "a capturing hook keeps its captures and holds one hook for the process" )
    {
        static WolfMod mod{ .id = "bindcapture" };
        begin( mod );
        const uintptr_t rva = rvaOf( &bindingCaptureTarget );
        // Read through a volatile, so k is not a constant and the lambda really captures it.
        static const volatile int ten = 10;
        const int k = ten;
        auto plusK = [ k ]( auto original, int x ) { return original( x ) + k; };
        const WolfHandle handle = wolf::hookRaw<int( int )>( "wolf-tests.exe", rva, plusK );
        REQUIRE( handle != 0 );
        wolf::frameBoundary();
        CHECK( bindingCaptureTarget( 1 ) == 12 );

        CHECK( wolf::hookRaw<int( int )>( "wolf-tests.exe", rva, plusK ) == 0 );
        wolf::remove( handle );
        drainRuntime();
        CHECK( bindingCaptureTarget( 1 ) == 2 );
        CHECK( wolf::hookRaw<int( int )>( "wolf-tests.exe", rva, plusK ) == 0 );
    }

    TEST_CASE( "hook links the function the symbol table names" )
    {
        static WolfMod mod{ .id = "bindname" };
        begin( mod );
        wolf::symbolsResolve( GetModuleHandleW( nullptr ), GetModuleHandleW( nullptr ) );
        static const void *seen = nullptr;
        seen = nullptr;
        int marker = 0;
        const WolfHandle handle = wolf::hook<void( void * )>( "wk::mem::cHeapExtended::free", []( auto original, void *p ) {
            seen = p;
            original( p );
        } );
        REQUIRE( handle != 0 );
        wolf::frameBoundary();
        wolfTestHeapExtendedFree( &marker );
        CHECK( seen == &marker );

        wolf::remove( handle );
        drainRuntime();
        seen = nullptr;
        wolfTestHeapExtendedFree( &marker );
        CHECK( seen == nullptr );
    }

    TEST_CASE( "alloc hands out zeroed, aligned memory and refuses a count that overflows" )
    {
        static WolfMod mod{ .id = "bindalloc" };
        begin( mod );
        struct alignas( 64 ) Wide
        {
            std::array<uint8_t, 64> bytes;
        };
        const auto *values = wolf::alloc<uint64_t>( wolf::Lifetime::Frame, 8 );
        REQUIRE( values != nullptr );
        CHECK( std::all_of( values, values + 8, []( uint64_t v ) { return v == 0; } ) );
        const Wide *wide = wolf::alloc<Wide>();
        REQUIRE( wide != nullptr );
        CHECK( reinterpret_cast<uintptr_t>( wide ) % 64 == 0 );
        CHECK( std::ranges::all_of( wide->bytes, []( uint8_t byte ) { return byte == 0; } ) );
        CHECK( wolf::alloc<uint64_t>( wolf::Lifetime::Process, SIZE_MAX / 4 ) == nullptr );
        drainRuntime();
    }

    TEST_CASE( "write and patch reach memory through the table" )
    {
        static WolfMod mod{ .id = "bindmemory" };
        begin( mod );
        auto *page = static_cast<uint32_t *>( VirtualAlloc( nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READONLY ) );
        REQUIRE( page != nullptr );
        CHECK( wolf::write( page, 0x12345678U ) );
        CHECK( page[ 0 ] == 0x12345678U );
        const uint32_t patched = 0xCAFEU;
        const WolfHandle handle = wolf::patch( page + 1, &patched, sizeof( patched ) );
        REQUIRE( handle != 0 );
        wolf::frameBoundary();
        CHECK( page[ 1 ] == patched );
        wolf::remove( handle );
        wolf::frameBoundary();
        CHECK( page[ 1 ] == 0 );
        drainRuntime();
    }

    TEST_CASE( "WOLF_MOD_ENTRY refuses another ABI and a short table, and catches a throwing init" )
    {
        static WolfMod mod{ .id = "bindentry" };
        begin( mod );
        initMode = InitMode::Succeed;

        WolfApi otherAbi = *wolf::wolfApi();
        otherAbi.abiVersion = 2;
        CHECK( wolf_mod_init( &otherAbi, &mod ) == 1 );

        WolfApi shortTable = *wolf::wolfApi();
        shortTable.size = 16;
        CHECK( wolf_mod_init( &shortTable, &mod ) == 1 );

        WolfApi olderTable = *wolf::wolfApi();
        olderTable.size = offsetof( WolfApi, claimId );
        CHECK( wolf_mod_init( &olderTable, &mod ) == 1 );

        CHECK( wolf_mod_init( wolf::wolfApi(), &mod ) == 0 );
        initMode = InitMode::Throw;
        CHECK( wolf_mod_init( wolf::wolfApi(), &mod ) == 1 );
        initMode = InitMode::Succeed;
        drainRuntime();
    }

    TEST_CASE( "a stateful process callback registered from another thread is complete before it runs" )
    {
        static WolfMod mod{ .id = "bindrace" };
        begin( mod );
        constexpr int kThreads = 3;
        constexpr int kRounds = 20000;

        struct Filled
        {
            std::array<uint8_t, 64> bytes{};
            Filled()
            {
                bytes.fill( 0x5A );
            }
        };
        static std::atomic<int> torn{ 0 };
        torn = 0;
        std::mutex guard;
        std::vector<WolfHandle> handles;

        raceThreads(
            kThreads,
            [ & ]( int /*index*/ ) {
                for ( int round = 0; round < kRounds; ++round )
                {
                    const WolfHandle handle = wolf::on<wolf::site::Tick>( [ filled = Filled() ]( WolfTickCtx & /*ctx*/ ) {
                        if ( !std::ranges::all_of( filled.bytes, []( uint8_t byte ) { return byte == 0x5A; } ) )
                        {
                            torn.fetch_add( 1 );
                        }
                    } );
                    if ( handle != 0 )
                    {
                        const std::scoped_lock lock( guard );
                        handles.push_back( handle );
                    }
                }
            },
            [ & ] {
                wolf::frameBoundary();
                wolf::siteTickEnter();
                const std::scoped_lock lock( guard );
                for ( const WolfHandle handle : handles )
                {
                    wolf::remove( handle );
                }
                handles.clear();
            } );
        for ( const WolfHandle handle : handles )
        {
            wolf::remove( handle );
        }
        drainRuntime();
        CHECK( torn.load() == 0 );
    }

    TEST_CASE( "threads hooking one capturing lambda get exactly one hook" )
    {
        static WolfMod mod{ .id = "bindhookrace" };
        begin( mod );
        constexpr int kRounds = 64;
        const std::vector<int> linked = raceHookRounds( std::make_integer_sequence<int, kRounds>() );
        CHECK( std::ranges::all_of( linked, []( int n ) { return n == 1; } ) );
        drainRuntime();
        CHECK( bindingRaceTarget( 1 ) == 2 );
    }
}
