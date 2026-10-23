#include <windows.h>

#include <doctest/doctest.h>

#include <atomic>
#include <cstdint>
#include <frame/boundary.hpp>
#include <hooks/hooks.hpp>
#include <mods/mod.hpp>
#include <tables/registry.hpp>
#include <thread>
#include <utility>
#include <vector>

#include "support.hpp"

#ifdef _MSC_VER
#define TEST_NOINLINE __declspec( noinline )
#else
#define TEST_NOINLINE __attribute__( ( noinline ) )
#endif

namespace {

// Written only by the thread making the call, so a case reads the order its own call passed through the chain.
thread_local std::vector<int> trace;
volatile int sink = 0;

// Targets return x + 1, through a volatile and with a constant of their own so the linker never merges two of them.
#define DEFINE_TARGET( name, k )                                                                                                                               \
    extern "C" TEST_NOINLINE int name( int x )                                                                                                                 \
    {                                                                                                                                                          \
        trace.push_back( -1 );                                                                                                                                 \
        sink = x + ( k );                                                                                                                                      \
        return sink - ( k ) + 1;                                                                                                                               \
    }

DEFINE_TARGET( raceTargetPark, 201 )
DEFINE_TARGET( raceTargetChurn, 202 )
DEFINE_TARGET( raceTargetMiddle, 203 )

template <int Id>
int ( *raceOriginal )( int ) = nullptr;

// Sets bit 8 + Id % 16 of the result and records Id.
template <int Id>
int raceDetour( int x )
{
    trace.push_back( Id );
    return raceOriginal<Id>( x ) | ( 1 << ( 8 + ( Id % 16 ) ) );
}

template <int Id>
WolfHandle raceAdd( WolfMod &mod, int ( *target )( int ) )
{
    return wolf::hookAdd( &mod, reinterpret_cast<void *>( target ), reinterpret_cast<void *>( &raceDetour<Id> ),
                          reinterpret_cast<void **>( &raceOriginal<Id> ) );
}

// Ids Base to Base + 15, so a thread can add the one it is told to.
template <int Base, int... I>
WolfHandle raceAddAt( int which, WolfMod &mod, int ( *target )( int ), std::integer_sequence<int, I...> /*ids*/ )
{
    WolfHandle handle = 0;
    (
        [ & ] {
            if ( which == I )
            {
                handle = raceAdd<Base + I>( mod, target );
            }
        }(),
        ... );
    return handle;
}

template <int Base>
WolfHandle raceAddAt( int which, WolfMod &mod, int ( *target )( int ) )
{
    return raceAddAt<Base>( which, mod, target, std::make_integer_sequence<int, 16>{} );
}

HANDLE inside = nullptr;
HANDLE release = nullptr;
int ( *parkOriginal )( int ) = nullptr;

// Holds its caller inside the link until the test releases it.
int parkDetour( int x )
{
    SetEvent( inside );
    WaitForSingleObject( release, INFINITE );
    return parkOriginal( x ) | 0x10000;
}

// The function's own result must survive any chain: the low byte is x + 1, and only known detour bits may be set above it.
bool wellFormed( int result, int x, int knownBits )
{
    return ( result & 0xFF ) == x + 1 && ( result & ~0xFF & ~knownBits ) == 0;
}

} // namespace

TEST_SUITE( "hooks_race" )
{
    TEST_CASE( "a thread parked inside a withdrawn link returns through it while boundaries pass" )
    {
        wolf::hooksInit();
        static WolfMod mod{};
        inside = CreateEventW( nullptr, TRUE, FALSE, nullptr );
        release = CreateEventW( nullptr, TRUE, FALSE, nullptr );
        const WolfHandle handle = wolf::hookAdd( &mod, reinterpret_cast<void *>( &raceTargetPark ), reinterpret_cast<void *>( &parkDetour ),
                                                 reinterpret_cast<void **>( &parkOriginal ) );
        REQUIRE( handle != 0 );
        wolf::frameBoundary();

        int parkedResult = 0;
        std::thread parked( [ & ] { parkedResult = raceTargetPark( 0 ); } );
        REQUIRE( WaitForSingleObject( inside, 10000 ) == WAIT_OBJECT_0 );
        wolf::registryRemove( &mod, handle );
        for ( int i = 0; i < 5; ++i )
        {
            wolf::frameBoundary();
        }
        int passing = 0;
        std::thread other( [ & ] { passing = raceTargetPark( 4 ); } );
        other.join();
        CHECK( passing == 5 );
        SetEvent( release );
        parked.join();
        CHECK( parkedResult == ( 1 | 0x10000 ) );
        CHECK( raceTargetPark( 0 ) == 1 );
        CloseHandle( inside );
        CloseHandle( release );
        drainRuntime();
    }

    TEST_CASE( "hook adds and removes from several threads against boundaries each land once while other threads call the target" )
    {
        wolf::hooksInit();
        constexpr int kAdders = 4;
        constexpr int kCallers = 2;
        constexpr int kRounds = 20000;
        static WolfMod mods[ kAdders ] = { { .loadIndex = 0 }, { .loadIndex = 1 }, { .loadIndex = 2 }, { .loadIndex = 3 } };
        std::atomic<int> adding{ kAdders };
        std::atomic<int> refused{ 0 };
        std::atomic<int> malformed{ 0 };
        raceThreads(
            kAdders + kCallers,
            [ & ]( int t ) {
                if ( t >= kAdders )
                {
                    while ( adding.load() > 0 )
                    {
                        if ( !wellFormed( raceTargetChurn( 0 ), 0, 0xFFFF00 ) )
                        {
                            malformed.fetch_add( 1 );
                        }
                    }
                    return;
                }
                for ( int round = 0; round < kRounds; ++round )
                {
                    for ( int i = 0; i < 4; ++i )
                    {
                        const WolfHandle handle = raceAddAt<0>( ( t * 4 ) + i, mods[ t ], &raceTargetChurn );
                        if ( handle == 0 )
                        {
                            refused.fetch_add( 1 );
                            continue;
                        }
                        wolf::registryRemove( &mods[ t ], handle );
                    }
                }
                for ( int i = 0; i < 4; ++i )
                {
                    if ( raceAddAt<0>( ( t * 4 ) + i, mods[ t ], &raceTargetChurn ) == 0 )
                    {
                        refused.fetch_add( 1 );
                    }
                }
                adding.fetch_sub( 1 );
            },
            [] { wolf::frameBoundary(); } );
        wolf::frameBoundary();
        CHECK( refused.load() == 0 );
        CHECK( malformed.load() == 0 );
        trace.clear();
        CHECK( raceTargetChurn( 0 ) == ( 1 | 0xFFFF00 ) );
        // Each link landed once, at the position its first add gave it: mod 3's four links outermost, the later of each mod's further out.
        CHECK( trace == std::vector{ 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, -1 } );
        drainRuntime();
    }

    TEST_CASE( "calls from several threads while links are inserted mid-chain and withdrawn always reach the function" )
    {
        wolf::hooksInit();
        constexpr int kCallers = 4;
        constexpr int kRounds = 100000;
        static WolfMod mods[ 8 ] = { { .loadIndex = 0 }, { .loadIndex = 1 }, { .loadIndex = 2 }, { .loadIndex = 3 },
                                     { .loadIndex = 4 }, { .loadIndex = 5 }, { .loadIndex = 6 }, { .loadIndex = 7 } };
        WolfHandle handles[ 8 ] = {};
        for ( int i = 0; i < 8; ++i )
        {
            handles[ i ] = raceAddAt<16>( i, mods[ i ], &raceTargetMiddle );
            REQUIRE( handles[ i ] != 0 );
        }
        wolf::frameBoundary();

        std::atomic<bool> stop{ false };
        std::atomic<int> malformed{ 0 };
        int round = 0;
        raceThreads(
            kCallers,
            [ & ]( int /*t*/ ) {
                while ( !stop.load() )
                {
                    // The even links (ids 16, 18, 20, 22) are never withdrawn, so every call passes them whatever happens around them.
                    const int result = raceTargetMiddle( 0 );
                    if ( !wellFormed( result, 0, 0xFF00 ) || ( result & 0x5500 ) != 0x5500 )
                    {
                        malformed.fetch_add( 1 );
                    }
                }
            },
            [ & ] {
                if ( round == kRounds )
                {
                    stop.store( true );
                    return;
                }
                for ( int i = 1; i < 8; i += 2 )
                {
                    if ( ( round & 1 ) == 0 )
                    {
                        wolf::registryRemove( &mods[ i ], handles[ i ] );
                    }
                    else
                    {
                        handles[ i ] = raceAddAt<16>( i, mods[ i ], &raceTargetMiddle );
                    }
                }
                wolf::frameBoundary();
                ++round;
            } );
        CHECK( malformed.load() == 0 );
        trace.clear();
        CHECK( raceTargetMiddle( 0 ) == ( 1 | 0xFF00 ) );
        CHECK( trace == std::vector{ 23, 22, 21, 20, 19, 18, 17, 16, -1 } );
        drainRuntime();
    }
}
