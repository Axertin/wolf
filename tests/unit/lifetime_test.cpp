#include <doctest/doctest.h>

#include <algorithm>
#include <arena/lifetime.hpp>
#include <core/threading.hpp>
#include <cstdint>
#include <cstring>
#include <frame/boundary.hpp>
#include <string>
#include <vector>

#include "support.hpp"

namespace {

WolfMod mod{};
std::vector<std::string> order;

void record( WolfMod * /*mod*/, void *data )
{
    order.emplace_back( static_cast<const char *>( data ) );
}

void chain( WolfMod *owner, void *data )
{
    record( owner, data );
    wolf::lifetimeOnReset( &mod, WOLF_LIFETIME_FRAME, record, const_cast<char *>( "inner" ) );
}

// Re-registers itself for the next frame, up to kRearmLimit runs.
constexpr int kRearmLimit = 1000;
int rearmRuns = 0;

void rearm( WolfMod *owner, void * /*data*/ )
{
    if ( ++rearmRuns < kRearmLimit )
    {
        wolf::lifetimeOnReset( owner, WOLF_LIFETIME_FRAME, rearm, nullptr );
    }
}

unsigned char *allocatedByFinalizer = nullptr;

void allocateFrame( WolfMod *owner, void * /*data*/ )
{
    allocatedByFinalizer = static_cast<unsigned char *>( wolf::lifetimeAlloc( owner, WOLF_LIFETIME_FRAME, 16, 16 ) );
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

void cleanup( int32_t lifetime )
{
    wolf::WaitingArenas waiting = {};
    {
        const wolf::RuntimeLock lock;
        waiting = wolf::lifetimeWaiting( lifetime );
    }
    wolf::lifetimeCleanup( waiting );
}

// Leaves every session and room arena empty, then forgets the finalizers earlier cases recorded.
void reset()
{
    drainRuntime();
    order.clear();
}

unsigned char *roomByte( unsigned char value )
{
    auto *p = static_cast<unsigned char *>( wolf::lifetimeAlloc( &mod, WOLF_LIFETIME_ROOM, 16, 16 ) );
    *p = value;
    return p;
}

} // namespace

TEST_SUITE( "lifetime" )
{
    TEST_CASE( "lifetimeAlloc returns zeroed memory in every lifetime" )
    {
        reset();
        for ( int32_t lifetime = WOLF_LIFETIME_PROCESS; lifetime <= WOLF_LIFETIME_FRAME; ++lifetime )
        {
            auto *p = static_cast<unsigned char *>( wolf::lifetimeAlloc( &mod, lifetime, 64, 16 ) );
            REQUIRE( p != nullptr );
            CHECK( p[ 0 ] == 0 );
            CHECK( p[ 63 ] == 0 );
        }
    }

    TEST_CASE( "ending the room lifetime keeps its memory until cleanup and runs finalizers newest first" )
    {
        reset();
        const uint32_t before = wolf::lifetimeGeneration( WOLF_LIFETIME_ROOM );
        auto *old = static_cast<unsigned char *>( wolf::lifetimeAlloc( &mod, WOLF_LIFETIME_ROOM, 16, 16 ) );
        *old = 0x11;
        wolf::lifetimeOnReset( &mod, WOLF_LIFETIME_ROOM, record, const_cast<char *>( "a" ) );
        wolf::lifetimeOnReset( &mod, WOLF_LIFETIME_ROOM, record, const_cast<char *>( "b" ) );
        wolf::lifetimeOnReset( &mod, WOLF_LIFETIME_ROOM, record, const_cast<char *>( "c" ) );
        wolf::lifetimeEnd( WOLF_LIFETIME_ROOM );
        CHECK( wolf::lifetimeGeneration( WOLF_LIFETIME_ROOM ) == before + 1 );
        CHECK( *old == 0x11 );
        auto *fresh = static_cast<unsigned char *>( wolf::lifetimeAlloc( &mod, WOLF_LIFETIME_ROOM, 16, 16 ) );
        *fresh = 0x22;
        CHECK( order.empty() );
        cleanup( WOLF_LIFETIME_ROOM );
        CHECK( order == std::vector<std::string>{ "c", "b", "a" } );
        CHECK( *fresh == 0x22 );
        cleanup( WOLF_LIFETIME_ROOM );
        CHECK( order.size() == 3 );
    }

    TEST_CASE( "ending the session ends the room lifetime too" )
    {
        reset();
        const uint32_t session = wolf::lifetimeGeneration( WOLF_LIFETIME_SESSION );
        const uint32_t room = wolf::lifetimeGeneration( WOLF_LIFETIME_ROOM );
        wolf::lifetimeEnd( WOLF_LIFETIME_SESSION );
        CHECK( wolf::lifetimeGeneration( WOLF_LIFETIME_SESSION ) == session + 1 );
        CHECK( wolf::lifetimeGeneration( WOLF_LIFETIME_ROOM ) == room + 1 );
    }

    TEST_CASE( "two ends before cleanup keep both ended generations apart and clean them up newest ended first" )
    {
        reset();
        roomByte( 0x11 );
        wolf::lifetimeOnReset( &mod, WOLF_LIFETIME_ROOM, record, const_cast<char *>( "first" ) );
        wolf::lifetimeEnd( WOLF_LIFETIME_ROOM );
        roomByte( 0x22 );
        wolf::lifetimeOnReset( &mod, WOLF_LIFETIME_ROOM, record, const_cast<char *>( "second" ) );
        wolf::lifetimeEnd( WOLF_LIFETIME_ROOM );
        const unsigned char *current = roomByte( 0x33 );
        wolf::lifetimeOnReset( &mod, WOLF_LIFETIME_ROOM, record, const_cast<char *>( "current" ) );
        cleanup( WOLF_LIFETIME_ROOM );
        CHECK( order == std::vector<std::string>{ "second", "first" } );
        CHECK( *current == 0x33 );
        wolf::lifetimeEnd( WOLF_LIFETIME_ROOM );
        cleanup( WOLF_LIFETIME_ROOM );
        CHECK( order == std::vector<std::string>{ "second", "first", "current" } );
    }

    TEST_CASE( "a frame finalizer registered while frame finalizers run runs at the next boundary" )
    {
        reset();
        wolf::lifetimeOnReset( &mod, WOLF_LIFETIME_FRAME, chain, const_cast<char *>( "outer" ) );
        wolf::lifetimeResetFrame();
        CHECK( order == std::vector<std::string>{ "outer" } );
        wolf::lifetimeResetFrame();
        CHECK( order == std::vector<std::string>{ "outer", "inner" } );
    }

    TEST_CASE( "a frame finalizer that re-arms itself runs once per boundary" )
    {
        reset();
        rearmRuns = 0;
        wolf::lifetimeOnReset( &mod, WOLF_LIFETIME_FRAME, rearm, nullptr );
        wolf::lifetimeResetFrame();
        CHECK( rearmRuns == 1 );
        wolf::lifetimeResetFrame();
        CHECK( rearmRuns == 2 );
        rearmRuns = kRearmLimit; // the next run stops re-arming
        wolf::lifetimeResetFrame();
    }

    TEST_CASE( "frame memory a frame finalizer allocates lives until the next boundary" )
    {
        reset();
        allocatedByFinalizer = nullptr;
        wolf::lifetimeOnReset( &mod, WOLF_LIFETIME_FRAME, allocateFrame, nullptr );
        wolf::lifetimeResetFrame();
        REQUIRE( allocatedByFinalizer != nullptr );
        CHECK( *allocatedByFinalizer == 0x77 );
        wolf::lifetimeResetFrame();
    }

    TEST_CASE( "a finalizer receives its mod and runs with it as the dispatching mod" )
    {
        reset();
        wolf::lifetimeOnReset( &mod, WOLF_LIFETIME_FRAME, check, nullptr );
        wolf::lifetimeResetFrame();
        CHECK( seenArgument == &mod );
        CHECK( seenDispatching == &mod );
    }

    TEST_CASE( "process-lifetime finalizers never run" )
    {
        reset();
        runs = 0;
        wolf::lifetimeOnReset( &mod, WOLF_LIFETIME_PROCESS, count, nullptr );
        drainRuntime();
        wolf::lifetimeResetFrame();
        CHECK( runs == 0 );
    }

    TEST_CASE( "an allocation past the arena's reservation returns null and leaves the arena usable" )
    {
        reset();
        WolfMod first{};
        WolfMod second{};
        CHECK( wolf::lifetimeAlloc( &first, WOLF_LIFETIME_ROOM, size_t{ 1 } << 62, 16 ) == nullptr );
        CHECK( wolf::lifetimeAlloc( &first, WOLF_LIFETIME_ROOM, size_t{ 1 } << 62, 16 ) == nullptr );
        CHECK( wolf::lifetimeAlloc( &second, WOLF_LIFETIME_ROOM, size_t{ 1 } << 62, 16 ) == nullptr );
        CHECK( wolf::lifetimeAlloc( &first, WOLF_LIFETIME_ROOM, 64, 16 ) != nullptr );
        drainRuntime();
    }

    TEST_CASE( "process pushes from several threads never overlap across commit steps" )
    {
        constexpr int kThreads = 8;
        constexpr int kEach = 40000; // 64 bytes each, about 20 MiB, so commit steps happen mid-race
        std::vector<uint32_t *> pushed( static_cast<size_t>( kThreads ) * kEach );
        raceThreads( kThreads, [ & ]( int t ) {
            for ( int k = 0; k < kEach; ++k )
            {
                auto *p = static_cast<uint32_t *>( wolf::lifetimeAlloc( &mod, WOLF_LIFETIME_PROCESS, 64, 16 ) );
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
        auto *p = static_cast<unsigned char *>( wolf::lifetimeAlloc( &mod, WOLF_LIFETIME_PROCESS, 16, 16 ) );
        *p = 0x44;
        wolf::lifetimeEnd( WOLF_LIFETIME_SESSION );
        cleanup( WOLF_LIFETIME_ROOM );
        cleanup( WOLF_LIFETIME_SESSION );
        CHECK( *p == 0x44 );
    }

    TEST_CASE( "processString copies and terminates" )
    {
        const wolf::RuntimeLock lock;
        const std::string_view source( "abcdef", 3 );
        const char *copy = wolf::processString( source );
        REQUIRE( copy != nullptr );
        CHECK( copy != source.data() );
        CHECK( strcmp( copy, "abc" ) == 0 );
        const wchar_t *wide = wolf::processWideString( L"\u00fc" );
        REQUIRE( wide != nullptr );
        CHECK( std::wstring_view( wide ) == L"\u00fc" );
    }
}
