#include <windows.h>

#include <doctest/doctest.h>

#include <arena/tier.hpp>
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
        logs::start();
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
        CHECK( logs::count( "dispatch overlap" ) == 0 );
    }

    // The overlap line is logged once per process, and no other case in wolf-tests dispatches from two threads at once.
    TEST_CASE( "a tier end while another thread dispatches logs the overlap once, and each thread keeps its own dispatching mod" )
    {
        logs::start();
        drainRuntime();
        WolfMod a{};
        WolfMod b{};
        HANDLE entered = CreateEventW( nullptr, TRUE, FALSE, nullptr );
        HANDLE release = CreateEventW( nullptr, TRUE, FALSE, nullptr );
        std::atomic<DWORD> threadAId{ 0 };
        WolfMod *seenByA = nullptr;
        std::thread other( [ & ] {
            const wolf::DispatchScope scope( &a );
            threadAId = GetCurrentThreadId();
            SetEvent( entered );
            WaitForSingleObject( release, INFINITE );
            seenByA = wolf::g_dispatchMod;
        } );
        WaitForSingleObject( entered, INFINITE );
        CHECK( wolf::g_dispatchMod == nullptr );
        wolf::tierEnd( WOLF_TIER_ROOM );
        const std::string expected = "dispatch overlap: thread " + std::to_string( GetCurrentThreadId() ) + " entered while thread " +
                                     std::to_string( threadAId.load() ) + " was dispatching";
        CHECK( logs::count( expected ) == 1 );
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
        CHECK( logs::count( "dispatch overlap" ) == 1 );
    }
}
