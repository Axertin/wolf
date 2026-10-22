#include <windows.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <arena/lifetime.hpp>
#include <array>
#include <atomic>
#include <core/threading.hpp>
#include <cstdint>
#include <cstring>
#include <frame/boundary.hpp>
#include <frame/run_queue.hpp>
#include <span>
#include <string>
#include <string_view>
#include <tables/registry.hpp>
#include <thread>
#include <vector>

#include "support.hpp"

namespace {

WolfMod mod{};
std::vector<std::string> events;

struct Data
{
    uint32_t value = 0;
};

const Data recordData{};

void onApply( wolf::Registry & /*registry*/, uint32_t /*index*/ )
{
    events.emplace_back( "apply" );
}

void onWithdraw( wolf::Registry & /*registry*/, uint32_t index )
{
    events.push_back( "withdraw:" + std::to_string( index ) );
}

constinit wolf::Registry registry{
    .kind = static_cast<wolf::Kind>( 11 ), .dataSize = sizeof( Data ), .apply = onApply, .withdraw = onWithdraw, .table = { .name = "boundary" } };

void note( WolfMod * /*mod*/, void *data )
{
    events.emplace_back( static_cast<const char *>( data ) );
}

void noteCall( WolfMod * /*mod*/, void *data, uint32_t /*len*/ )
{
    events.emplace_back( static_cast<const char *>( data ) );
}

unsigned char *roomByte( unsigned char value )
{
    auto *p = static_cast<unsigned char *>( wolf::lifetimeAlloc( &mod, WOLF_LIFETIME_ROOM, 16, 16 ) );
    *p = value;
    return p;
}

WolfHandle add( int32_t lifetime )
{
    return wolf::registryAdd( registry, &mod, lifetime, &recordData );
}

// Room finalizer: ends the room lifetime while the boundary cleans its arena, then pushes into whatever arena is current.
unsigned char *pushedDuringCleanup = nullptr;

void endRoomAndPush( WolfMod * /*mod*/, void * /*data*/ )
{
    wolf::lifetimeEnd( WOLF_LIFETIME_ROOM );
    pushedDuringCleanup = roomByte( 0x5A );
}

// Racing: each iteration of each thread registers one finalizer and queues one call, counted by index.
constexpr int kRaceThreads = 4;
constexpr int kRaceEach = 45000;
std::vector<uint32_t> finalized( static_cast<size_t>( kRaceThreads ) * kRaceEach );
std::vector<uint32_t> ran( static_cast<size_t>( kRaceThreads ) * kRaceEach );

void countFinal( WolfMod * /*mod*/, void *data )
{
    ++*static_cast<uint32_t *>( data );
}

void countRun( WolfMod * /*mod*/, void *data, uint32_t /*len*/ )
{
    uint32_t index = 0;
    std::memcpy( &index, data, sizeof( index ) );
    ++ran.at( index );
}

// Boundary mod code: each calls every entry point that takes the runtime lock.
WolfHandle processHandle = 0;
uint32_t finalizerAdd = 0; // record index the room finalizer registered

// next and run name the room finalizer and the call that callEverything registers, as the events they record.
uint32_t callEverything( const char *next, const char *run )
{
    wolf::lifetimeAlloc( &mod, WOLF_LIFETIME_ROOM, 16, 16 );
    wolf::lifetimeOnReset( &mod, WOLF_LIFETIME_ROOM, note, const_cast<char *>( next ) );
    wolf::runInGame( &mod, noteCall, run, static_cast<uint32_t>( std::strlen( run ) + 1 ) );
    const uint32_t index = wolf::handleIndex( add( WOLF_LIFETIME_ROOM ) );
    wolf::registryRemove( &mod, processHandle );
    return index;
}

void boundaryFinalizer( WolfMod * /*mod*/, void * /*data*/ )
{
    events.emplace_back( "F" );
    finalizerAdd = callEverything( "F-next", "F-run" );
}

void boundaryCall( WolfMod * /*mod*/, void * /*data*/, uint32_t /*len*/ )
{
    events.emplace_back( "Q" );
    callEverything( "Q-next", "Q-run" );
}

// Retires what an earlier case left and leaves every room arena empty, then clears the state the cases share.
void start()
{
    wolf::registryInstall( registry );
    drainRuntime();
    events.clear();
}

} // namespace

TEST_SUITE( "frame boundary" )
{
    TEST_CASE( "the boundary cleans up ended lifetimes, applies pending, resets the frame lifetime and runs the queued calls, in that order" )
    {
        start();
        wolf::lifetimeOnReset( &mod, WOLF_LIFETIME_ROOM, note, const_cast<char *>( "room" ) );
        wolf::lifetimeOnReset( &mod, WOLF_LIFETIME_SESSION, note, const_cast<char *>( "session" ) );
        wolf::lifetimeEnd( WOLF_LIFETIME_SESSION );
        wolf::registryAdd( registry, &mod, WOLF_LIFETIME_PROCESS, &recordData );
        wolf::lifetimeOnReset( &mod, WOLF_LIFETIME_FRAME, note, const_cast<char *>( "frame" ) );
        wolf::runInGame( &mod, noteCall, "call", 5 );
        wolf::frameBoundary();
        CHECK( events == std::vector<std::string>{ "room", "session", "apply", "frame", "call" } );
    }

    TEST_CASE( "an ended lifetime's records are withdrawn at the next boundary, not when it ends" )
    {
        start();
        const WolfHandle h = add( WOLF_LIFETIME_ROOM );
        const uint32_t index = wolf::handleIndex( h );
        wolf::frameBoundary();
        wolf::lifetimeEnd( WOLF_LIFETIME_ROOM );
        CHECK( wolf::registryLive( registry, index ) == nullptr );
        CHECK( events.size() == 1 ); // "apply" only
        wolf::frameBoundary();
        CHECK( events.back() == "withdraw:" + std::to_string( index ) );
        CHECK( wolf::registryResolve( h ) == nullptr );
    }

    TEST_CASE( "a registration made after its lifetime ended survives the boundary" )
    {
        start();
        wolf::lifetimeEnd( WOLF_LIFETIME_ROOM );
        const WolfHandle h = add( WOLF_LIFETIME_ROOM );
        wolf::frameBoundary();
        CHECK( wolf::registryLive( registry, wolf::handleIndex( h ) ) != nullptr );
        CHECK( events == std::vector<std::string>{ "apply" } );
    }

    TEST_CASE( "two room ends before one boundary retire both ended generations and clean up both arenas newest ended first" )
    {
        start();
        const uint32_t index1 = wolf::handleIndex( add( WOLF_LIFETIME_ROOM ) );
        wolf::frameBoundary();
        events.clear();
        roomByte( 0x11 );
        wolf::lifetimeOnReset( &mod, WOLF_LIFETIME_ROOM, note, const_cast<char *>( "first" ) );
        wolf::lifetimeEnd( WOLF_LIFETIME_ROOM );
        const WolfHandle h2 = add( WOLF_LIFETIME_ROOM );
        roomByte( 0x22 );
        wolf::lifetimeOnReset( &mod, WOLF_LIFETIME_ROOM, note, const_cast<char *>( "second" ) );
        wolf::lifetimeEnd( WOLF_LIFETIME_ROOM );
        const WolfHandle h3 = add( WOLF_LIFETIME_ROOM );
        const unsigned char *current = roomByte( 0x33 );
        wolf::frameBoundary();
        CHECK( events == std::vector<std::string>{ "withdraw:" + std::to_string( index1 ), "second", "first", "apply" } );
        CHECK( wolf::registryResolve( h2 ) == nullptr );
        CHECK( wolf::registryLive( registry, wolf::handleIndex( h3 ) ) != nullptr );
        CHECK( *current == 0x33 );
    }

    TEST_CASE( "a third end before a cleanup frees an arena merges into the current arena and logs once" )
    {
        start();
        logs::start();
        const uint32_t index0 = wolf::handleIndex( add( WOLF_LIFETIME_ROOM ) );
        wolf::frameBoundary();
        events.clear();
        wolf::lifetimeOnReset( &mod, WOLF_LIFETIME_ROOM, note, const_cast<char *>( "first" ) );
        wolf::lifetimeEnd( WOLF_LIFETIME_ROOM );
        const WolfHandle h1 = add( WOLF_LIFETIME_ROOM );
        wolf::lifetimeOnReset( &mod, WOLF_LIFETIME_ROOM, note, const_cast<char *>( "second" ) );
        wolf::lifetimeEnd( WOLF_LIFETIME_ROOM );
        const WolfHandle h2 = add( WOLF_LIFETIME_ROOM );
        const unsigned char *kept = roomByte( 0x5A );
        wolf::lifetimeOnReset( &mod, WOLF_LIFETIME_ROOM, note, const_cast<char *>( "third" ) );
        wolf::lifetimeEnd( WOLF_LIFETIME_ROOM );
        const WolfHandle h3 = add( WOLF_LIFETIME_ROOM );
        const uint32_t index3 = wolf::handleIndex( h3 );
        wolf::frameBoundary();
        CHECK( events == std::vector<std::string>{ "withdraw:" + std::to_string( index0 ), "second", "first", "apply" } );
        CHECK( wolf::registryResolve( h1 ) == nullptr );
        CHECK( wolf::registryResolve( h2 ) == nullptr );
        CHECK( wolf::registryLive( registry, index3 ) != nullptr );
        CHECK( *kept == 0x5A ); // the third generation's arena is the current one
        events.clear();
        wolf::lifetimeEnd( WOLF_LIFETIME_ROOM );
        wolf::frameBoundary();
        CHECK( events == std::vector<std::string>{ "withdraw:" + std::to_string( index3 ), "third" } );
        CHECK( logs::count( "room lifetime merged an ended generation into the current arena" ) == 1 );

        // A room finalizer that ends the room lifetime while its own arena and an older one wait to be freed.
        drainRuntime();
        wolf::lifetimeOnReset( &mod, WOLF_LIFETIME_ROOM, note, const_cast<char *>( "A" ) );
        wolf::lifetimeEnd( WOLF_LIFETIME_ROOM );
        wolf::lifetimeOnReset( &mod, WOLF_LIFETIME_ROOM, endRoomAndPush, nullptr );
        wolf::lifetimeEnd( WOLF_LIFETIME_ROOM );
        wolf::frameBoundary();
        REQUIRE( pushedDuringCleanup != nullptr );
        CHECK( *pushedDuringCleanup == 0x5A ); // the end merged into the current arena; the older arena, still being cleaned, was not made current
        CHECK( logs::count( "room lifetime merged an ended generation into the current arena" ) == 1 );
    }

    TEST_CASE( "a room end and then a session end before one boundary clean up both room generations before the session" )
    {
        start();
        roomByte( 0x11 );
        wolf::lifetimeEnd( WOLF_LIFETIME_ROOM );
        roomByte( 0x22 );
        wolf::lifetimeOnReset( &mod, WOLF_LIFETIME_ROOM, note, const_cast<char *>( "room" ) );
        wolf::lifetimeOnReset( &mod, WOLF_LIFETIME_SESSION, note, const_cast<char *>( "session" ) );
        wolf::lifetimeEnd( WOLF_LIFETIME_SESSION );
        const unsigned char *current = roomByte( 0x33 );
        wolf::frameBoundary();
        CHECK( events == std::vector<std::string>{ "room", "session" } );
        CHECK( *current == 0x33 );
    }

    TEST_CASE( "a session end withdraws session and room records and keeps process records" )
    {
        start();
        const WolfHandle hs = add( WOLF_LIFETIME_SESSION );
        const WolfHandle hr = add( WOLF_LIFETIME_ROOM );
        const WolfHandle hp = add( WOLF_LIFETIME_PROCESS );
        wolf::frameBoundary();
        wolf::lifetimeEnd( WOLF_LIFETIME_SESSION );
        wolf::frameBoundary();
        CHECK( wolf::registryResolve( hs ) == nullptr );
        CHECK( wolf::registryResolve( hr ) == nullptr );
        CHECK( wolf::registryLive( registry, wolf::handleIndex( hp ) ) != nullptr );
    }

    TEST_CASE( "pushes, finalizers and queued calls from several threads against frame boundaries each land once" )
    {
        start();
        std::ranges::fill( finalized, 0U );
        std::ranges::fill( ran, 0U );
        std::atomic<int> failures{ 0 };
        constexpr std::array<int32_t, 3> kLifetimes = { WOLF_LIFETIME_ROOM, WOLF_LIFETIME_SESSION, WOLF_LIFETIME_FRAME };
        int step = 0;
        raceThreads(
            kRaceThreads,
            [ & ]( int t ) {
                for ( int k = 0; k < kRaceEach; ++k )
                {
                    const int32_t lifetime = kLifetimes.at( static_cast<size_t>( k % 3 ) );
                    const auto index = static_cast<uint32_t>( ( t * kRaceEach ) + k );
                    if ( wolf::lifetimeAlloc( &mod, lifetime, 16, 16 ) == nullptr ||
                         wolf::lifetimeOnReset( &mod, lifetime, countFinal, &finalized.at( index ) ) != 1 ||
                         wolf::runInGame( &mod, countRun, &index, sizeof( index ) ) != 1 )
                    {
                        ++failures;
                    }
                }
            },
            [ & ] {
                ++step;
                if ( step % 50 == 0 )
                {
                    wolf::lifetimeEnd( step % 100 == 0 ? WOLF_LIFETIME_SESSION : WOLF_LIFETIME_ROOM );
                }
                wolf::frameBoundary();
            } );
        drainRuntime();
        CHECK( failures.load() == 0 );
        CHECK( std::ranges::all_of( finalized, []( uint32_t n ) { return n == 1; } ) );
        CHECK( std::ranges::all_of( ran, []( uint32_t n ) { return n == 1; } ) );
    }

    TEST_CASE( "pushes, adds and queued calls from several threads against warm start and frame boundaries each land once" )
    {
        start();
        constexpr int kThreads = 4;
        constexpr int kEach = 20000;
        std::vector<uint32_t> cells( static_cast<size_t>( kThreads ) * kEach );
        std::ranges::fill( ran, 0U );
        std::atomic<int> failures{ 0 };
        int step = 0;
        raceThreads(
            kThreads,
            [ & ]( int t ) {
                for ( int k = 0; k < kEach; ++k )
                {
                    const auto index = static_cast<uint32_t>( ( t * kEach ) + k );
                    if ( wolf::lifetimeAlloc( &mod, WOLF_LIFETIME_SESSION, 16, 16 ) == nullptr ||
                         wolf::lifetimeOnReset( &mod, WOLF_LIFETIME_SESSION, countFinal, &cells.at( index ) ) != 1 || add( WOLF_LIFETIME_PROCESS ) == 0 ||
                         wolf::runInGame( &mod, countRun, &index, sizeof( index ) ) != 1 )
                    {
                        ++failures;
                    }
                }
            },
            [ & ] {
                // A soft reset ends the session and returns through a warm start before the next frame.
                if ( ++step % 2 == 0 )
                {
                    if ( step % 50 == 0 )
                    {
                        wolf::lifetimeEnd( WOLF_LIFETIME_SESSION );
                    }
                    wolf::warmStartBoundary();
                }
                else
                {
                    wolf::frameBoundary();
                }
            } );
        drainRuntime();
        CHECK( failures.load() == 0 );
        CHECK( std::ranges::count( events, "apply" ) == kThreads * kEach );
        CHECK( std::ranges::all_of( cells, []( uint32_t n ) { return n == 1; } ) );
        CHECK( std::ranges::all_of( std::span( ran ).first( static_cast<size_t>( kThreads ) * kEach ), []( uint32_t n ) { return n == 1; } ) );
    }

    TEST_CASE( "mod code the boundary runs can call every entry point" )
    {
        start();
        processHandle = add( WOLF_LIFETIME_PROCESS );
        wolf::frameBoundary();
        const std::string withdrawn = "withdraw:" + std::to_string( wolf::handleIndex( processHandle ) );
        events.clear();
        wolf::lifetimeOnReset( &mod, WOLF_LIFETIME_ROOM, boundaryFinalizer, nullptr );
        wolf::lifetimeEnd( WOLF_LIFETIME_ROOM );
        wolf::runInGame( &mod, boundaryCall, nullptr, 0 );
        wolf::frameBoundary();
        const auto has = [ & ]( std::string_view event ) { return std::ranges::count( events, event ) == 1; };
        CHECK( has( "F" ) );
        CHECK( has( "Q" ) );
        CHECK( has( "F-run" ) );
        CHECK_FALSE( has( "F-next" ) );
        CHECK_FALSE( has( "Q-run" ) );
        CHECK( wolf::registryLive( registry, finalizerAdd ) != nullptr );
        wolf::lifetimeEnd( WOLF_LIFETIME_ROOM );
        wolf::frameBoundary();
        CHECK( has( "F-next" ) );
        CHECK( has( "Q-next" ) );
        CHECK( has( "Q-run" ) );
        CHECK( has( withdrawn ) );
    }

    TEST_CASE( "a warm start boundary retires, applies and runs the queue and leaves arenas to the next frame boundary" )
    {
        start();
        const uint32_t retiredIndex = wolf::handleIndex( add( WOLF_LIFETIME_SESSION ) );
        wolf::frameBoundary();
        events.clear();
        auto *sessionByte = static_cast<unsigned char *>( wolf::lifetimeAlloc( &mod, WOLF_LIFETIME_SESSION, 16, 16 ) );
        *sessionByte = 0x11; // the first push of a clean arena, in the page a reset re-zeroes
        wolf::lifetimeOnReset( &mod, WOLF_LIFETIME_SESSION, note, const_cast<char *>( "session" ) );
        auto *frameByte = static_cast<unsigned char *>( wolf::lifetimeAlloc( &mod, WOLF_LIFETIME_FRAME, 16, 16 ) );
        *frameByte = 0x22;
        wolf::lifetimeOnReset( &mod, WOLF_LIFETIME_FRAME, note, const_cast<char *>( "frame" ) );
        wolf::lifetimeEnd( WOLF_LIFETIME_SESSION );
        const uint32_t fresh = wolf::handleIndex( add( WOLF_LIFETIME_SESSION ) );
        wolf::runInGame( &mod, noteCall, "queued", 7 );
        wolf::warmStartBoundary();
        CHECK( events == std::vector<std::string>{ "withdraw:" + std::to_string( retiredIndex ), "apply", "queued" } );
        CHECK( wolf::registryLive( registry, fresh ) != nullptr );
        CHECK( *sessionByte == 0x11 );
        CHECK( *frameByte == 0x22 );
        wolf::frameBoundary();
        REQUIRE( events.size() >= 2 );
        CHECK( std::vector<std::string>( events.end() - 2, events.end() ) == std::vector<std::string>{ "session", "frame" } );
        wolf::frameBoundary();
        CHECK( std::ranges::count( events, "session" ) == 1 );
        CHECK( wolf::registryLive( registry, fresh ) != nullptr );
    }

    TEST_CASE( "a remove queued for a record whose lifetime ends first withdraws it once" )
    {
        start();
        const WolfHandle h = add( WOLF_LIFETIME_ROOM );
        wolf::frameBoundary();
        events.clear();
        wolf::registryRemove( &mod, h );
        wolf::lifetimeEnd( WOLF_LIFETIME_ROOM );
        wolf::frameBoundary();
        wolf::frameBoundary();
        CHECK( std::ranges::count( events, "withdraw:" + std::to_string( wolf::handleIndex( h ) ) ) == 1 );
        CHECK( wolf::registryResolve( h ) == nullptr );
    }

    TEST_CASE( "room ends on a worker race pushes and registrations from other threads" )
    {
        start();
        constexpr int kPushers = 4;
        constexpr int kEach = 40000;
        std::vector<uint32_t> cells( static_cast<size_t>( kPushers ) * kEach );
        std::vector<WolfHandle> handles( cells.size() );
        std::atomic<int> failures{ 0 };
        const uint32_t before = wolf::lifetimeGeneration( WOLF_LIFETIME_ROOM );
        // The worker and main take turns, as the map worker and main do: main wakes the worker, waits for its room end, then runs a frame boundary.
        HANDLE go = CreateEventW( nullptr, FALSE, FALSE, nullptr );
        HANDLE done = CreateEventW( nullptr, FALSE, FALSE, nullptr );
        std::atomic<bool> stop{ false };
        uint32_t ends = 0;
        std::thread worker( [ & ] {
            for ( ;; )
            {
                WaitForSingleObject( go, INFINITE );
                if ( stop.load() )
                {
                    return;
                }
                wolf::lifetimeEnd( WOLF_LIFETIME_ROOM );
                ++ends;
                SetEvent( done );
            }
        } );
        raceThreads(
            kPushers,
            [ & ]( int t ) {
                for ( int k = 0; k < kEach; ++k )
                {
                    const size_t index = ( static_cast<size_t>( t ) * kEach ) + k;
                    handles[ index ] = add( WOLF_LIFETIME_ROOM );
                    if ( wolf::lifetimeAlloc( &mod, WOLF_LIFETIME_ROOM, 16, 16 ) == nullptr ||
                         wolf::lifetimeOnReset( &mod, WOLF_LIFETIME_ROOM, countFinal, &cells[ index ] ) != 1 || handles[ index ] == 0 )
                    {
                        ++failures;
                    }
                }
            },
            [ & ] {
                SetEvent( go );
                WaitForSingleObject( done, INFINITE );
                wolf::frameBoundary();
            } );
        stop = true;
        SetEvent( go );
        worker.join();
        CloseHandle( go );
        CloseHandle( done );
        const uint32_t generation = wolf::lifetimeGeneration( WOLF_LIFETIME_ROOM );
        drainRuntime();
        CHECK( failures.load() == 0 );
        CHECK( generation - before == ends );
        CHECK( std::ranges::all_of( cells, []( uint32_t n ) { return n == 1; } ) );
        CHECK( std::ranges::all_of( handles, []( WolfHandle h ) { return wolf::registryResolve( h ) == nullptr; } ) );
    }

    TEST_CASE( "a frame push after the boundary is zeroed where the last frame wrote" )
    {
        start();
        std::memset( wolf::lifetimeAlloc( &mod, WOLF_LIFETIME_FRAME, 64, 16 ), 0xEE, 64 );
        wolf::frameBoundary();
        const auto *p = static_cast<const unsigned char *>( wolf::lifetimeAlloc( &mod, WOLF_LIFETIME_FRAME, 64, 16 ) );
        CHECK( std::all_of( p, p + 64, []( unsigned char b ) { return b == 0; } ) );
    }
}
