#include <doctest/doctest.h>

#include <algorithm>
#include <arena/tier.hpp>
#include <core/threading.hpp>
#include <cstdint>
#include <frame/boundary.hpp>
#include <string>
#include <vector>

#include "support.hpp"

namespace {

constexpr size_t MiB = size_t{ 1024 } * 1024;

WolfMod mod{};
std::vector<std::string> order;

void record( WolfMod * /*mod*/, void *data )
{
    order.emplace_back( static_cast<const char *>( data ) );
}

void chain( WolfMod *owner, void *data )
{
    record( owner, data );
    wolf::tierOnReset( &mod, WOLF_TIER_FRAME, record, const_cast<char *>( "inner" ) );
}

// Re-registers itself for the next frame, up to kRearmLimit runs.
constexpr int kRearmLimit = 1000;
int rearmRuns = 0;

void rearm( WolfMod *owner, void * /*data*/ )
{
    if ( ++rearmRuns < kRearmLimit )
    {
        wolf::tierOnReset( owner, WOLF_TIER_FRAME, rearm, nullptr );
    }
}

unsigned char *allocatedByFinalizer = nullptr;

void allocateFrame( WolfMod *owner, void * /*data*/ )
{
    allocatedByFinalizer = static_cast<unsigned char *>( wolf::tierAlloc( owner, WOLF_TIER_FRAME, 16, 16 ) );
    *allocatedByFinalizer = 0x77;
}

WolfMod *seenArgument = nullptr;
WolfMod *seenDispatching = nullptr;

void check( WolfMod *owner, void * /*data*/ )
{
    seenArgument = owner;
    seenDispatching = wolf::g_dispatchMod;
}

int runs = 0;

void count( WolfMod * /*mod*/, void * /*data*/ )
{
    ++runs;
}

void cleanup( int32_t tier )
{
    wolf::WaitingArenas waiting = {};
    {
        const wolf::RuntimeLock lock;
        waiting = wolf::tierWaiting( tier );
    }
    wolf::tierCleanup( waiting );
}

// Leaves every session and room arena empty, then forgets the finalizers earlier cases recorded.
void reset()
{
    drainRuntime();
    order.clear();
}

unsigned char *roomByte( unsigned char value )
{
    auto *p = static_cast<unsigned char *>( wolf::tierAlloc( &mod, WOLF_TIER_ROOM, 16, 16 ) );
    *p = value;
    return p;
}

} // namespace

TEST_SUITE( "tier" )
{
    TEST_CASE( "tierAlloc returns zeroed memory in every tier" )
    {
        reset();
        for ( int32_t tier = WOLF_TIER_PROCESS; tier <= WOLF_TIER_FRAME; ++tier )
        {
            auto *p = static_cast<unsigned char *>( wolf::tierAlloc( &mod, tier, 64, 16 ) );
            REQUIRE( p != nullptr );
            CHECK( p[ 0 ] == 0 );
            CHECK( p[ 63 ] == 0 );
        }
    }

    TEST_CASE( "ending the room tier keeps its memory until cleanup and runs finalizers newest first" )
    {
        reset();
        const uint32_t before = wolf::tierGeneration( WOLF_TIER_ROOM );
        auto *old = static_cast<unsigned char *>( wolf::tierAlloc( &mod, WOLF_TIER_ROOM, 16, 16 ) );
        *old = 0x11;
        wolf::tierOnReset( &mod, WOLF_TIER_ROOM, record, const_cast<char *>( "a" ) );
        wolf::tierOnReset( &mod, WOLF_TIER_ROOM, record, const_cast<char *>( "b" ) );
        wolf::tierOnReset( &mod, WOLF_TIER_ROOM, record, const_cast<char *>( "c" ) );
        wolf::tierEnd( WOLF_TIER_ROOM );
        CHECK( wolf::tierGeneration( WOLF_TIER_ROOM ) == before + 1 );
        CHECK( *old == 0x11 );
        auto *fresh = static_cast<unsigned char *>( wolf::tierAlloc( &mod, WOLF_TIER_ROOM, 16, 16 ) );
        *fresh = 0x22;
        CHECK( order.empty() );
        cleanup( WOLF_TIER_ROOM );
        CHECK( order == std::vector<std::string>{ "c", "b", "a" } );
        CHECK( *fresh == 0x22 );
        cleanup( WOLF_TIER_ROOM );
        CHECK( order.size() == 3 );
    }

    TEST_CASE( "ending the session ends the room tier too" )
    {
        reset();
        const uint32_t session = wolf::tierGeneration( WOLF_TIER_SESSION );
        const uint32_t room = wolf::tierGeneration( WOLF_TIER_ROOM );
        wolf::tierEnd( WOLF_TIER_SESSION );
        CHECK( wolf::tierGeneration( WOLF_TIER_SESSION ) == session + 1 );
        CHECK( wolf::tierGeneration( WOLF_TIER_ROOM ) == room + 1 );
    }

    TEST_CASE( "two ends before cleanup keep both ended generations apart and clean them up newest ended first" )
    {
        reset();
        roomByte( 0x11 );
        wolf::tierOnReset( &mod, WOLF_TIER_ROOM, record, const_cast<char *>( "first" ) );
        wolf::tierEnd( WOLF_TIER_ROOM );
        roomByte( 0x22 );
        wolf::tierOnReset( &mod, WOLF_TIER_ROOM, record, const_cast<char *>( "second" ) );
        wolf::tierEnd( WOLF_TIER_ROOM );
        const unsigned char *current = roomByte( 0x33 );
        wolf::tierOnReset( &mod, WOLF_TIER_ROOM, record, const_cast<char *>( "current" ) );
        cleanup( WOLF_TIER_ROOM );
        CHECK( order == std::vector<std::string>{ "second", "first" } );
        CHECK( *current == 0x33 );
        wolf::tierEnd( WOLF_TIER_ROOM );
        cleanup( WOLF_TIER_ROOM );
        CHECK( order == std::vector<std::string>{ "second", "first", "current" } );
    }

    TEST_CASE( "a frame finalizer registered while frame finalizers run runs at the next boundary" )
    {
        reset();
        wolf::tierOnReset( &mod, WOLF_TIER_FRAME, chain, const_cast<char *>( "outer" ) );
        wolf::tierResetFrame();
        CHECK( order == std::vector<std::string>{ "outer" } );
        wolf::tierResetFrame();
        CHECK( order == std::vector<std::string>{ "outer", "inner" } );
    }

    TEST_CASE( "a frame finalizer that re-arms itself runs once per boundary" )
    {
        reset();
        rearmRuns = 0;
        wolf::tierOnReset( &mod, WOLF_TIER_FRAME, rearm, nullptr );
        wolf::tierResetFrame();
        CHECK( rearmRuns == 1 );
        wolf::tierResetFrame();
        CHECK( rearmRuns == 2 );
        rearmRuns = kRearmLimit; // the next run stops re-arming
        wolf::tierResetFrame();
    }

    TEST_CASE( "frame memory a frame finalizer allocates lives until the next boundary" )
    {
        reset();
        allocatedByFinalizer = nullptr;
        wolf::tierOnReset( &mod, WOLF_TIER_FRAME, allocateFrame, nullptr );
        wolf::tierResetFrame();
        REQUIRE( allocatedByFinalizer != nullptr );
        CHECK( *allocatedByFinalizer == 0x77 );
        wolf::tierResetFrame();
    }

    TEST_CASE( "a finalizer receives its mod and runs with it as the dispatching mod" )
    {
        reset();
        wolf::tierOnReset( &mod, WOLF_TIER_FRAME, check, nullptr );
        wolf::tierResetFrame();
        CHECK( seenArgument == &mod );
        CHECK( seenDispatching == &mod );
    }

    TEST_CASE( "process-tier finalizers never run" )
    {
        reset();
        runs = 0;
        wolf::tierOnReset( &mod, WOLF_TIER_PROCESS, count, nullptr );
        drainRuntime();
        wolf::tierResetFrame();
        CHECK( runs == 0 );
    }

    TEST_CASE( "a tier warns at 16 MiB and at each doubling, naming the mod whose push crossed, once across its ring arenas" )
    {
        drainRuntime();
        logs::start();
        WolfMod a{};
        WolfMod b{};
        wolf::tierAlloc( &a, WOLF_TIER_ROOM, 15 * MiB, 16 );
        CHECK( logs::count( "tier arena reached" ) == 0 );
        wolf::tierAlloc( &b, WOLF_TIER_ROOM, 2 * MiB, 16 );
        REQUIRE( logs::count( "room tier arena reached 16 MiB" ) == 1 );
        CHECK( logs::lines().back().mod == &b );
        CHECK( logs::lines().back().level == WOLF_LOG_WARN );
        wolf::tierAlloc( &a, WOLF_TIER_ROOM, 14 * MiB, 16 );
        CHECK( logs::count( "room tier arena reached" ) == 1 );
        wolf::tierAlloc( &a, WOLF_TIER_ROOM, 2 * MiB, 16 );
        REQUIRE( logs::count( "room tier arena reached 32 MiB" ) == 1 );
        CHECK( logs::lines().back().mod == &a );
        wolf::tierEnd( WOLF_TIER_ROOM );
        wolf::frameBoundary();
        wolf::tierAlloc( &a, WOLF_TIER_ROOM, 33 * MiB, 16 );    // the next ring arena
        CHECK( logs::count( "room tier arena reached" ) == 2 ); // the tier's threshold is 64 MiB now
        drainRuntime();
    }

    TEST_CASE( "a tier warns again only at a new high after a reset" )
    {
        drainRuntime();
        logs::start();
        wolf::tierAlloc( &mod, WOLF_TIER_FRAME, 17 * MiB, 16 );
        CHECK( logs::count( "frame tier arena reached 16 MiB" ) == 1 );
        wolf::tierResetFrame();
        wolf::tierAlloc( &mod, WOLF_TIER_FRAME, 17 * MiB, 16 );
        CHECK( logs::count( "frame tier arena reached" ) == 1 );
        wolf::tierAlloc( &mod, WOLF_TIER_FRAME, 16 * MiB, 16 );
        CHECK( logs::count( "frame tier arena reached 32 MiB" ) == 1 );
        wolf::tierResetFrame();
    }

    TEST_CASE( "one push that crosses two thresholds warns once, at the higher" )
    {
        drainRuntime();
        logs::start();
        WolfMod a{};
        wolf::tierAlloc( &a, WOLF_TIER_SESSION, 40 * MiB, 16 );
        CHECK( logs::count( "session tier arena reached" ) == 1 );
        CHECK( logs::count( "session tier arena reached 32 MiB" ) == 1 );
        drainRuntime();
    }

    TEST_CASE( "an allocation that cannot be mapped returns null and logs once per mod" )
    {
        reset();
        logs::start();
        WolfMod first{};
        WolfMod second{};
        CHECK( wolf::tierAlloc( &first, WOLF_TIER_ROOM, size_t{ 1 } << 62, 16 ) == nullptr );
        CHECK( wolf::tierAlloc( &first, WOLF_TIER_ROOM, size_t{ 1 } << 62, 16 ) == nullptr );
        CHECK( logs::count( "out of memory allocating" ) == 1 );
        CHECK( wolf::tierAlloc( &second, WOLF_TIER_ROOM, size_t{ 1 } << 62, 16 ) == nullptr );
        CHECK( logs::count( "out of memory allocating" ) == 2 );
    }

    TEST_CASE( "process pushes from several threads never overlap across block ends" )
    {
        constexpr int kThreads = 8;
        constexpr int kEach = 40000; // 64 bytes each, about 20 MiB, so 4 MiB blocks are mapped mid-race
        std::vector<uint32_t *> pushed( static_cast<size_t>( kThreads ) * kEach );
        raceThreads( kThreads, [ & ]( int t ) {
            for ( int k = 0; k < kEach; ++k )
            {
                auto *p = static_cast<uint32_t *>( wolf::tierAlloc( &mod, WOLF_TIER_PROCESS, 64, 16 ) );
                if ( p != nullptr )
                {
                    p[ 0 ] = static_cast<uint32_t>( t );
                    p[ 1 ] = static_cast<uint32_t>( k );
                }
                pushed.at( ( static_cast<size_t>( t ) * kEach ) + k ) = p;
            }
        } );
        REQUIRE( std::ranges::none_of( pushed, []( const uint32_t *p ) { return p == nullptr; } ) );
        bool ownValues = true;
        for ( size_t i = 0; i < pushed.size(); ++i )
        {
            ownValues = ownValues && pushed[ i ][ 0 ] == i / kEach && pushed[ i ][ 1 ] == i % kEach;
        }
        CHECK( ownValues );
        std::vector<uintptr_t> sorted;
        sorted.reserve( pushed.size() );
        for ( const uint32_t *p : pushed )
        {
            sorted.push_back( reinterpret_cast<uintptr_t>( p ) );
        }
        std::ranges::sort( sorted );
        CHECK( std::ranges::adjacent_find( sorted, []( uintptr_t a, uintptr_t b ) { return b - a < 64; } ) == sorted.end() );
    }

    TEST_CASE( "process memory survives a session end" )
    {
        reset();
        auto *p = static_cast<unsigned char *>( wolf::tierAlloc( &mod, WOLF_TIER_PROCESS, 16, 16 ) );
        *p = 0x44;
        wolf::tierEnd( WOLF_TIER_SESSION );
        cleanup( WOLF_TIER_ROOM );
        cleanup( WOLF_TIER_SESSION );
        CHECK( *p == 0x44 );
    }
}
