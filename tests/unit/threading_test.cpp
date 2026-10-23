#include <windows.h>

#include <doctest/doctest.h>

#include <arena/lifetime.hpp>
#include <atomic>
#include <core/threading.hpp>
#include <frame/boundary.hpp>
#include <string>
#include <thread>
#include <vector>

#include "support.hpp"

TEST_SUITE( "threading" )
{
    TEST_CASE( "the runtime lock serialises increments from several threads" )
    {
        int counter = 0; // plain int: only the lock keeps it exact
        raceThreads( 8, [ & ]( int /*index*/ ) {
            for ( int i = 0; i < 200000; ++i )
            {
                const wolf::RuntimeLock lock;
                ++counter;
            }
        } );
        CHECK( counter == 1600000 );
    }

    TEST_CASE( "nested dispatch on one thread restores the outer mod and is not an overlap" )
    {
        WolfMod a{};
        WolfMod b{};
        {
            const wolf::DispatchScope outer( &a );
            {
                const wolf::DispatchScope inner( &b );
                CHECK( wolf::g_dispatchMod == &b );
            }
            CHECK( wolf::g_dispatchMod == &a );
        }
        CHECK( wolf::g_dispatchMod == nullptr );
        CHECK_FALSE( wolf::dispatchOverlapSeen() );
    }

    // No other case in wolf-tests dispatches from two threads at once.
    TEST_CASE( "a lifetime end while another thread dispatches is an overlap, and each thread keeps its own dispatching mod" )
    {
        // The overlap flag is set once per process, so the check below means something only if nothing set it before.
        REQUIRE_FALSE( wolf::dispatchOverlapSeen() );
        drainRuntime();
        WolfMod a{};
        WolfMod b{};
        HANDLE entered = CreateEventW( nullptr, TRUE, FALSE, nullptr );
        HANDLE release = CreateEventW( nullptr, TRUE, FALSE, nullptr );
        WolfMod *seenByA = nullptr;
        std::thread other( [ & ] {
            const wolf::DispatchScope scope( &a );
            SetEvent( entered );
            WaitForSingleObject( release, INFINITE );
            seenByA = wolf::g_dispatchMod;
        } );
        WaitForSingleObject( entered, INFINITE );
        CHECK( wolf::g_dispatchMod == nullptr );
        wolf::lifetimeEnd( WOLF_LIFETIME_ROOM );
        CHECK( wolf::dispatchOverlapSeen() );
        {
            const wolf::DispatchScope scope( &b );
            CHECK( wolf::g_dispatchMod == &b );
        }
        wolf::frameBoundary();
        SetEvent( release );
        other.join();
        CloseHandle( entered );
        CloseHandle( release );
        CHECK( seenByA == &a );
    }

    TEST_CASE( "inDispatch is true only inside a scope, on its own thread" )
    {
        WolfMod mod{};
        CHECK_FALSE( wolf::inDispatch() );
        {
            const wolf::DispatchScope scope( &mod );
            CHECK( wolf::inDispatch() );
            {
                const wolf::DispatchScope inner( nullptr );
                CHECK( wolf::inDispatch() );
            }
            CHECK( wolf::inDispatch() );
            std::atomic<bool> otherInside{ true };
            raceThreads( 1, [ & ]( int /*index*/ ) { otherInside = wolf::inDispatch(); } );
            CHECK_FALSE( otherInside.load() );
        }
        CHECK_FALSE( wolf::inDispatch() );
    }
}
