#include <doctest/doctest.h>

#include <algorithm>
#include <arena/lifetime.hpp>
#include <atomic>
#include <core/threading.hpp>
#include <cstdint>
#include <frame/boundary.hpp>
#include <map>
#include <tables/registry.hpp>
#include <thread>
#include <vector>

#include "support.hpp"

namespace {

constexpr auto kTestKind = static_cast<wolf::Kind>( 15 ); // unassigned kind

struct TestData
{
    uint32_t value = 0;
};

WolfMod mod{};
std::vector<uint32_t> applied;
std::vector<uint32_t> withdrawn;
std::map<uint32_t, uint32_t> appliedValues; // applies per TestData value, read through registryData as a kind's op does
std::map<uint32_t, uint32_t> withdrawnValues;

uint32_t valueAt( wolf::Registry &registry, uint32_t index )
{
    return static_cast<const TestData *>( wolf::registryData( registry, index ) )->value;
}

void onApply( wolf::Registry &registry, uint32_t index )
{
    applied.push_back( index );
    ++appliedValues[ valueAt( registry, index ) ];
}

void onWithdraw( wolf::Registry &registry, uint32_t index )
{
    withdrawn.push_back( index );
    ++withdrawnValues[ valueAt( registry, index ) ];
}

constinit wolf::Registry registry{
    .kind = kTestKind, .dataSize = sizeof( TestData ), .expected = 4096, .apply = onApply, .withdraw = onWithdraw, .table = { .name = "test" } };
constinit wolf::Registry reuse{ .kind = static_cast<wolf::Kind>( 13 ), .dataSize = sizeof( TestData ), .table = { .name = "reuse" } };
constinit wolf::Registry fifo{ .kind = static_cast<wolf::Kind>( 7 ), .dataSize = sizeof( TestData ), .table = { .name = "fifo" } };
constinit wolf::Registry few{ .kind = static_cast<wolf::Kind>( 12 ), .dataSize = sizeof( TestData ), .expected = 4, .table = { .name = "few" } };
constinit wolf::Registry chunks{ .kind = static_cast<wolf::Kind>( 10 ), .dataSize = sizeof( TestData ), .table = { .name = "chunks" } };
constinit wolf::Registry filling{ .kind = static_cast<wolf::Kind>( 9 ), .dataSize = sizeof( TestData ), .table = { .name = "filling" } };

// Applies what an earlier case queued and forgets what it recorded. Cases that need an empty registry use one no other case touches.
void start()
{
    for ( wolf::Registry *each : { &registry, &reuse, &fifo, &few, &chunks, &filling } )
    {
        wolf::registryInstall( *each );
    }
    wolf::applyPending();
    applied.clear();
    withdrawn.clear();
    appliedValues.clear();
    withdrawnValues.clear();
}

WolfHandle add( wolf::Registry &into, int32_t lifetime = WOLF_LIFETIME_PROCESS )
{
    const TestData data{ 1 };
    return wolf::registryAdd( into, &mod, lifetime, &data );
}

// Appends to into until the next add takes a freed slot.
void fillPastReuse( wolf::Registry &into )
{
    while ( wolf::tableEnd( into.table ) <= 3 * wolf::kChunkEntries / 4 )
    {
        REQUIRE( add( into ) != 0 );
    }
}

} // namespace

TEST_SUITE( "registry" )
{
    TEST_CASE( "handles pack generation, kind and index" )
    {
        start();
        CHECK( wolf::makeHandle( wolf::Kind::Hook, 5, 3 ) == 0x03200005U );
        CHECK( wolf::makeHandle( wolf::Kind::Hook, 0xFFFFF, 0xFF ) == 0xFF2FFFFFU );
        constexpr WolfHandle h = wolf::makeHandle( wolf::Kind::Watch, 77, 9 );
        CHECK( wolf::handleIndex( h ) == 77 );
        CHECK( wolf::handleKind( h ) == wolf::Kind::Watch );
        CHECK( wolf::handleGeneration( h ) == 9 );
        CHECK( wolf::registryResolve( 0 ) == nullptr );
    }

    TEST_CASE( "a registration returns its handle at once and takes effect at applyPending" )
    {
        start();
        const TestData data{ 7 };
        const WolfHandle h = wolf::registryAdd( registry, &mod, WOLF_LIFETIME_PROCESS, &data );
        REQUIRE( h != 0 );
        CHECK( wolf::handleKind( h ) == kTestKind );
        const uint32_t index = wolf::handleIndex( h );
        CHECK( static_cast<TestData *>( wolf::registryResolve( h ) )->value == 7 );
        CHECK( wolf::registryLive( registry, index ) == nullptr );
        CHECK( applied.empty() );
        wolf::applyPending();
        CHECK( applied == std::vector<uint32_t>{ index } );
        CHECK( wolf::registryLive( registry, index ) != nullptr );
    }

    TEST_CASE( "remove takes effect at the next applyPending and the handle goes stale" )
    {
        start();
        const WolfHandle h = add( registry );
        REQUIRE( h != 0 );
        const uint32_t index = wolf::handleIndex( h );
        wolf::applyPending();
        wolf::registryRemove( &mod, h );
        CHECK( wolf::registryLive( registry, index ) != nullptr );
        wolf::applyPending();
        CHECK( withdrawn == std::vector<uint32_t>{ index } );
        CHECK( wolf::registryResolve( h ) == nullptr );
        CHECK( wolf::registryLive( registry, index ) == nullptr );
        wolf::registryRemove( &mod, h );
        wolf::applyPending();
        CHECK( withdrawn.size() == 1 );
    }

    TEST_CASE( "a registration removed before it applies is never applied" )
    {
        start();
        const WolfHandle h = add( registry );
        REQUIRE( h != 0 );
        wolf::registryRemove( &mod, h );
        wolf::applyPending();
        CHECK( applied.empty() );
        CHECK( withdrawn.empty() );
        CHECK( wolf::registryResolve( h ) == nullptr );
    }

    TEST_CASE( "remove ignores zero, stale, foreign and unknown-kind handles, and a second remove" )
    {
        start();
        WolfMod other{};
        const WolfHandle h = add( registry );
        REQUIRE( h != 0 );
        const uint32_t index = wolf::handleIndex( h );
        wolf::applyPending();
        wolf::registryRemove( &mod, 0 );
        wolf::registryRemove( &other, h );
        wolf::registryRemove( &mod, wolf::makeHandle( kTestKind, index, static_cast<uint8_t>( wolf::handleGeneration( h ) + 1 ) ) );
        wolf::registryRemove( &mod, wolf::makeHandle( static_cast<wolf::Kind>( 14 ), index, wolf::handleGeneration( h ) ) );
        wolf::applyPending();
        CHECK( withdrawn.empty() );
        CHECK( wolf::registryLive( registry, index ) != nullptr );
        wolf::registryRemove( &mod, h );
        wolf::registryRemove( &mod, h );
        wolf::applyPending();
        CHECK( withdrawn.size() == 1 );
    }

    TEST_CASE( "freed slots are reused only past three quarters of the committed chunks" )
    {
        start();
        const WolfHandle a = add( reuse );
        REQUIRE( a != 0 );
        wolf::registryRemove( &mod, a );
        wolf::applyPending();
        const WolfHandle b = add( reuse );
        REQUIRE( b != 0 );
        CHECK( wolf::handleIndex( b ) != wolf::handleIndex( a ) );
        fillPastReuse( reuse );
        const WolfHandle c = add( reuse );
        REQUIRE( c != 0 );
        CHECK( wolf::handleIndex( c ) == wolf::handleIndex( a ) );
        CHECK( wolf::handleGeneration( c ) == static_cast<uint8_t>( wolf::handleGeneration( a ) + 1 ) );
        wolf::applyPending();
        CHECK( wolf::registryLive( reuse, wolf::handleIndex( c ) ) != nullptr );
        CHECK( wolf::registryResolve( a ) == nullptr );
    }

    TEST_CASE( "freed slots are reused in the order they were freed" )
    {
        start();
        const WolfHandle a = add( fifo );
        const WolfHandle b = add( fifo );
        REQUIRE( a != 0 );
        REQUIRE( b != 0 );
        wolf::registryRemove( &mod, a );
        wolf::registryRemove( &mod, b );
        wolf::applyPending();
        fillPastReuse( fifo );
        CHECK( wolf::handleIndex( add( fifo ) ) == wolf::handleIndex( a ) );
        CHECK( wolf::handleIndex( add( fifo ) ) == wolf::handleIndex( b ) );
    }

    TEST_CASE( "a registration made before its lifetime ended is dropped at applyPending" )
    {
        start();
        const WolfHandle h = add( registry, WOLF_LIFETIME_ROOM );
        REQUIRE( h != 0 );
        wolf::lifetimeEnd( WOLF_LIFETIME_ROOM );
        wolf::applyPending();
        CHECK( applied.empty() );
        CHECK( wolf::registryResolve( h ) == nullptr );
        drainRuntime();
    }

    TEST_CASE( "a live record stops being live the moment its lifetime ends" )
    {
        start();
        const WolfHandle h = add( registry, WOLF_LIFETIME_ROOM );
        REQUIRE( h != 0 );
        const uint32_t index = wolf::handleIndex( h );
        wolf::applyPending();
        CHECK( wolf::registryLive( registry, index ) != nullptr );
        wolf::lifetimeEnd( WOLF_LIFETIME_ROOM );
        CHECK( wolf::registryLive( registry, index ) == nullptr );
        CHECK( wolf::registryResolve( h ) != nullptr ); // withdrawn at the next frame boundary
        drainRuntime();
    }

    TEST_CASE( "adds past the expected registration count still succeed" )
    {
        start();
        for ( int i = 0; i < 4; ++i )
        {
            REQUIRE( add( few ) != 0 );
        }
        REQUIRE( add( few ) != 0 );
    }

    TEST_CASE( "adds and removes from several threads against boundaries each apply and withdraw once" )
    {
        start();
        drainRuntime();
        appliedValues.clear();
        withdrawnValues.clear();
        constexpr int kThreads = 4;
        constexpr int kEach = 40000;
        constexpr uint32_t kFirstValue = 100; // above the values other cases add
        std::vector<WolfHandle> handles( static_cast<size_t>( kThreads ) * kEach );
        int step = 0;
        raceThreads(
            kThreads,
            [ & ]( int t ) {
                for ( int k = 0; k < kEach; ++k )
                {
                    const size_t index = ( static_cast<size_t>( t ) * kEach ) + k;
                    const TestData data{ kFirstValue + static_cast<uint32_t>( index ) };
                    handles[ index ] = wolf::registryAdd( registry, &mod, k % 2 != 0 ? WOLF_LIFETIME_ROOM : WOLF_LIFETIME_PROCESS, &data );
                    if ( k % 3 == 0 )
                    {
                        wolf::registryRemove( &mod, handles[ index ] );
                    }
                }
            },
            [ & ] {
                if ( ++step % 64 == 0 )
                {
                    wolf::lifetimeEnd( WOLF_LIFETIME_ROOM );
                }
                wolf::frameBoundary();
            } );
        drainRuntime();
        std::vector<WolfHandle> sorted = handles;
        std::ranges::sort( sorted );
        CHECK( std::ranges::count( sorted, 0U ) == 0 );
        CHECK( std::ranges::adjacent_find( sorted ) == sorted.end() );
        bool atMostOnce = true;
        bool endedMatch = true;
        bool keptLive = true;
        for ( size_t index = 0; index < handles.size(); ++index )
        {
            const uint32_t value = kFirstValue + static_cast<uint32_t>( index );
            const uint32_t applies = appliedValues[ value ];
            const uint32_t withdraws = withdrawnValues[ value ];
            const int k = static_cast<int>( index % kEach );
            atMostOnce = atMostOnce && applies <= 1;
            if ( k % 3 == 0 || k % 2 != 0 )
            {
                endedMatch = endedMatch && withdraws == applies && wolf::registryResolve( handles[ index ] ) == nullptr;
            }
            else
            {
                keptLive = keptLive && applies == 1 && withdraws == 0 && wolf::registryLive( registry, wolf::handleIndex( handles[ index ] ) ) != nullptr;
            }
        }
        CHECK( atMostOnce );
        CHECK( endedMatch );
        CHECK( keptLive );
    }

    TEST_CASE( "dispatch reads on another thread stay valid while registrations append across chunks" )
    {
        start();
        constexpr uint32_t kFirst = 100;
        constexpr int kEachWriter = 200000; // two writers append across many chunk boundaries
        const TestData seven{ 7 };
        for ( uint32_t i = 0; i < kFirst; ++i )
        {
            REQUIRE( wolf::registryAdd( chunks, &mod, WOLF_LIFETIME_PROCESS, &seven ) != 0 );
        }
        wolf::applyPending();
        std::atomic<int> writing{ 2 };
        std::atomic<uint64_t> reads{ 0 };
        std::atomic<uint64_t> wrongValues{ 0 };
        raceThreads( 3, [ & ]( int t ) {
            if ( t == 0 )
            {
                while ( writing.load() != 0 )
                {
                    const uint32_t end = wolf::tableEnd( chunks.table );
                    for ( uint32_t i = 1; i < end; ++i )
                    {
                        if ( const auto *live = static_cast<const TestData *>( wolf::registryLive( chunks, i ) ) )
                        {
                            ++reads;
                            wrongValues += live->value != 7 ? 1U : 0U;
                        }
                    }
                }
                return;
            }
            for ( int k = 0; k < kEachWriter; ++k )
            {
                if ( k == kEachWriter / 2 ) // the reader has read before the second half appends, so the two overlap whatever the scheduling
                {
                    while ( reads.load() == 0 )
                    {
                        std::this_thread::yield();
                    }
                }
                wolf::registryAdd( chunks, &mod, WOLF_LIFETIME_PROCESS, &seven );
            }
            --writing;
        } );
        CHECK( wrongValues.load() == 0 );
        CHECK( reads.load() > 0 );
        wolf::applyPending();
        uint32_t live = 0;
        for ( uint32_t i = 1; i < wolf::tableEnd( chunks.table ); ++i )
        {
            live += wolf::registryLive( chunks, i ) != nullptr ? 1U : 0U;
        }
        CHECK( live == kFirst + ( 2 * kEachWriter ) );
    }

    TEST_CASE( "a full pending list refuses an add, and a refused remove can be retried" )
    {
        start();
        WolfMod filler{};
        const TestData data{ 1 };
        const WolfHandle x = wolf::registryAdd( filling, &filler, WOLF_LIFETIME_PROCESS, &data );
        REQUIRE( x != 0 );
        for ( ;; )
        {
            const WolfHandle h = wolf::registryAdd( filling, &filler, WOLF_LIFETIME_PROCESS, &data );
            if ( h == 0 )
            {
                break;
            }
            wolf::registryRemove( &filler, h );
        }
        wolf::registryRemove( &filler, x ); // refused: the list is full
        wolf::applyPending();
        CHECK( wolf::registryLive( filling, wolf::handleIndex( x ) ) != nullptr );
        wolf::registryRemove( &filler, x );
        wolf::applyPending();
        CHECK( wolf::registryResolve( x ) == nullptr );
    }
}
