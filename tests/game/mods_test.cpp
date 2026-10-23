#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <symbols.hpp>
#include <thread>
#include <vector>

#include "fake_game.hpp"
#include "harness.hpp"

namespace {

using ChainRemoveFn = void ( * )();
using RefusedTicksFn = int ( * )();
using ProbeFlagFn = int32_t ( * )( char op, uint32_t group, uint32_t bit );
using ProbeGiveFn = int32_t ( * )( int32_t itemId, int32_t delta );
using ProbeGiveFromThreadFn = void ( * )( int count );
using ProbeVoidFn = void ( * )();
using ProbeDoneFn = int32_t ( * )();
using ProbeItemsFn = int ( * )( int id );
using ProbeItemThreadFn = DWORD ( * )( int id );

struct Probe
{
    ProbeFlagFn flag = harness::modExport<ProbeFlagFn>( L"probe.dll", "Probe_Flag" );
    ProbeGiveFn give = harness::modExport<ProbeGiveFn>( L"probe.dll", "Probe_Give" );
    ProbeGiveFromThreadFn giveFromThread = harness::modExport<ProbeGiveFromThreadFn>( L"probe.dll", "Probe_GiveFromThread" );
    ProbeVoidFn giveJoin = harness::modExport<ProbeVoidFn>( L"probe.dll", "Probe_GiveJoin" );
    ProbeDoneFn giveDone = harness::modExport<ProbeDoneFn>( L"probe.dll", "Probe_GiveDone" );
    ProbeVoidFn giveOnTick = harness::modExport<ProbeVoidFn>( L"probe.dll", "Probe_GiveOnTick" );
    ProbeItemsFn itemsFromSelf = harness::modExport<ProbeItemsFn>( L"probe.dll", "Probe_ItemsFromSelf" );
    ProbeItemThreadFn itemThread = harness::modExport<ProbeItemThreadFn>( L"probe.dll", "Probe_ItemThread" );
};

struct Fake
{
    FakeTickFn tick = harness::fakeExport<FakeTickFn>( "FakeMain_Tick" );
    FakeRequestRoomFn requestRoom = harness::fakeExport<FakeRequestRoomFn>( "FakeMain_RequestRoom" );
    FakeRequestItemFn requestItem = harness::fakeExport<FakeRequestItemFn>( "FakeMain_RequestItem" );
    FakeCallsFn calls = harness::fakeExport<FakeCallsFn>( "FakeMain_Calls" );
    FakeParkFn park = harness::fakeExport<FakeParkFn>( "FakeMain_Park" );
    FakeParkFn parkArm = harness::fakeExport<FakeParkFn>( "FakeMain_ParkArm" );
    FakeParkWaitingFn parkWaiting = harness::fakeExport<FakeParkWaitingFn>( "FakeMain_ParkWaiting" );
    FakeParkFn parkRelease = harness::fakeExport<FakeParkFn>( "FakeMain_ParkRelease" );
    FakeTraceTakeFn traceTake = harness::fakeExport<FakeTraceTakeFn>( "FakeMain_TraceTake" );

    Fake()
    {
        REQUIRE( tick != nullptr );
        REQUIRE( requestRoom != nullptr );
        REQUIRE( requestItem != nullptr );
        REQUIRE( calls != nullptr );
        REQUIRE( park != nullptr );
        REQUIRE( parkArm != nullptr );
        REQUIRE( parkWaiting != nullptr );
        REQUIRE( parkRelease != nullptr );
        REQUIRE( traceTake != nullptr );
    }

    void ticks( int n ) const
    {
        for ( int i = 0; i < n; ++i )
        {
            tick();
        }
    }

    [[nodiscard]] std::string trace() const
    {
        char out[ 256 ];
        traceTake( out, sizeof( out ) );
        return out;
    }

    void clearTrace() const
    {
        static_cast<void>( trace() );
    }

    // Calls FakeMain_Park on a thread of its own, so a link that blocks never blocks the test's thread, which is the game's main thread.
    void parkOnThread() const
    {
        std::thread( park ).join();
    }
};

// How many tick callbacks the probe mod has seen.
int probeTickCount()
{
    return harness::modExport<int ( * )()>( L"probe.dll", "Probe_Ticks" )();
}

// The index of mod's first what event, failing the case when there is none.
size_t eventAt( const std::vector<FakeEvent> &events, std::string_view mod, std::string_view what )
{
    for ( size_t i = 0; i < events.size(); ++i )
    {
        if ( mod == events[ i ].mod && what == events[ i ].what )
        {
            return i;
        }
    }
    FAIL_CHECK( "no " << what << " event from " << mod );
    return events.size();
}

} // namespace

// These run after game_test.cpp's modded cases, in the process and fake game those leave booted.
TEST_SUITE( "modded" )
{
    TEST_CASE( "mods load in order and each says so" )
    {
        const std::vector<FakeEvent> events = harness::events();
        for ( const char *mod : { "deps", "refused", "chain_a", "chain_b", "probe" } )
        {
            CHECK( harness::countEvents( events, mod, "init" ) == 1 );
        }
        CHECK( eventAt( events, "deps", "init" ) < eventAt( events, "chain_a", "init" ) );
        CHECK( eventAt( events, "refused", "init" ) < eventAt( events, "chain_a", "init" ) );
        CHECK( eventAt( events, "chain_a", "init" ) < eventAt( events, "chain_b", "init" ) );
        CHECK( eventAt( events, "chain_b", "init" ) < eventAt( events, "probe", "init" ) );
    }

    TEST_CASE( "a mod finds a DLL beside its own" )
    {
        const std::vector<FakeEvent> events = harness::events();
        CHECK( events.at( eventAt( events, "deps", "init" ) ).a == 42 );
    }

    TEST_CASE( "a throwing C++ init refuses its mod" )
    {
        const Fake fake;
        const int probeTicks = probeTickCount();
        fake.ticks( 3 );
        CHECK( probeTickCount() == probeTicks + 3 ); // the tick site reached the mods that were not refused
        const std::vector<FakeEvent> events = harness::events();
        CHECK( harness::countEvents( events, "throwing", "init" ) == 1 );
        CHECK( harness::countEvents( events, "throwing", "tick" ) == 0 );
    }

    TEST_CASE( "a refused mod's registrations never run" )
    {
        const Fake fake;
        fake.clearTrace();
        const int probeTicks = probeTickCount();
        fake.ticks( 5 );
        CHECK( probeTickCount() == probeTicks + 5 ); // the tick site reached the mods that were not refused
        CHECK( harness::modExport<RefusedTicksFn>( L"refused.dll", "Refused_Ticks" )() == 0 );
        fake.parkOnThread();
        CHECK( fake.trace().find( 'R' ) == std::string::npos );
    }

    TEST_CASE( "config reaches the mod" )
    {
        const std::vector<FakeEvent> events = harness::events();
        CHECK( std::string_view( events.at( eventAt( events, "probe", "greeting" ) ).text ) == "hi" );
    }

    TEST_CASE( "a mod hooking a site function at init joins the site's chain" )
    {
        const Fake fake;
        fake.clearTrace();
        FakeCalls before;
        fake.calls( &before );
        fake.requestItem( 3, 1 );
        FakeCalls now = before;
        for ( int i = 0; i < 10 && now.itemAdds == before.itemAdds; ++i )
        {
            fake.tick();
            fake.calls( &now );
        }
        REQUIRE( now.itemAdds == before.itemAdds + 1 );
        // The site detour is the head, so item.add's callbacks run before the mod's link inside it.
        CHECK( fake.trace() == "SH" );
    }

    TEST_CASE( "two mods' links on one function run outermost first, and a removal partway returns through the withdrawn link" )
    {
        const Fake fake;
        const auto chainBRemove = harness::modExport<ChainRemoveFn>( L"chain_b.dll", "Chain_Remove" );
        fake.clearTrace();
        fake.parkOnThread();
        CHECK( fake.trace() == "BAab" );

        fake.parkArm();
        std::thread parked( fake.park );
        while ( !fake.parkWaiting() )
        {
            std::this_thread::yield();
        }
        CHECK( fake.trace() == "BA" );
        chainBRemove();
        fake.ticks( 3 );
        fake.parkRelease();
        parked.join();
        // The parked call entered both links before the removal, so it returns through chain_b's withdrawn one.
        CHECK( fake.trace() == "ab" );

        fake.parkOnThread();
        CHECK( fake.trace() == "Aa" );
    }

    // The game test thread is the game's main thread.
    TEST_CASE( "hello_world loads" )
    {
        CHECK( GetModuleHandleW( L"hello_world.dll" ) != nullptr );
    }

    TEST_CASE( "giveItem during init waits for the game and flagSet is ignored" )
    {
        const Probe probe;
        CHECK( probe.flag( 't', 1, 1 ) == 0 ); // the flagSet( 1, 1 ) at init did nothing
        CHECK( probe.itemsFromSelf( 17 ) == 1 );
        CHECK( probe.itemThread( 17 ) == GetCurrentThreadId() );
    }

    TEST_CASE( "flags set, test and clear through the game's functions" )
    {
        const Probe probe;
        CHECK( probe.flag( 's', 3, 37 ) == 1 );
        CHECK( probe.flag( 't', 3, 37 ) == 1 );
        CHECK( probe.flag( 't', 3, 36 ) == 0 );
        CHECK( probe.flag( 'c', 3, 37 ) == 0 );
        CHECK( probe.flag( 's', 79, 255 ) == 1 );
        CHECK( probe.flag( 'c', 79, 255 ) == 0 );
        CHECK( probe.flag( 's', 80, 0 ) == 0 );
        CHECK( probe.flag( 's', 0, 256 ) == 0 );
    }

    TEST_CASE( "giveItem from a mod's own thread queues, and item.add names the mod" )
    {
        const Fake fake;
        const Probe probe;
        FakeCalls before;
        fake.calls( &before );
        constexpr int kGifts = 5000;
        probe.giveFromThread( kGifts );
        while ( probe.giveDone() == 0 )
        {
            fake.tick();
        }
        probe.giveJoin();
        fake.ticks( 2 );
        FakeCalls now;
        fake.calls( &now );
        CHECK( now.itemAdds >= before.itemAdds + kGifts );
        CHECK( probe.itemsFromSelf( 20 ) == kGifts );
        CHECK( probe.itemThread( 20 ) == GetCurrentThreadId() );
        // giveItem passes the game's item manager, *g_ItemMgrPtr.
        CHECK( now.lastItemMgr == *reinterpret_cast<void **>( reinterpret_cast<uint8_t *>( harness::fakeMain().main ) + wolf::symbolRva( "g_ItemMgrPtr" ) ) );
        CHECK( now.lastItemMgr != nullptr );
    }

    TEST_CASE( "giveItem inside a callback runs at once on that thread" )
    {
        const Fake fake;
        const Probe probe;
        probe.giveOnTick();
        fake.tick();
        CHECK( probe.itemsFromSelf( 21 ) == 1 );
        CHECK( probe.itemThread( 21 ) == GetCurrentThreadId() );
    }

    TEST_CASE( "giveItem refuses an id outside 0 to 255" )
    {
        const Fake fake;
        const Probe probe;
        FakeCalls before;
        fake.calls( &before );
        CHECK( probe.give( 256, 1 ) == 0 );
        CHECK( probe.give( -1, 1 ) == 0 );
        fake.ticks( 2 );
        FakeCalls now;
        fake.calls( &now );
        CHECK( now.itemAdds == before.itemAdds );
    }
}
