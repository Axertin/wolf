#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <symbols.hpp>
#include <thread>
#include <vector>

#include "fake_game.hpp"
#include "harness.hpp"

namespace {

// The fake game's exports, from the main.dll the harness loaded.
struct Fake
{
    FakeStartFn start = harness::fakeExport<FakeStartFn>( "FakeMain_Start" );
    FakeTickFn tick = harness::fakeExport<FakeTickFn>( "FakeMain_Tick" );
    FakeRequestRoomFn requestRoom = harness::fakeExport<FakeRequestRoomFn>( "FakeMain_RequestRoom" );
    FakeRequestItemFn requestItem = harness::fakeExport<FakeRequestItemFn>( "FakeMain_RequestItem" );
    FakeRequestSoftResetFn requestSoftReset = harness::fakeExport<FakeRequestSoftResetFn>( "FakeMain_RequestSoftReset" );
    FakeCallsFn calls = harness::fakeExport<FakeCallsFn>( "FakeMain_Calls" );

    [[nodiscard]] bool complete() const
    {
        return start != nullptr && tick != nullptr && requestRoom != nullptr && requestItem != nullptr && requestSoftReset != nullptr && calls != nullptr;
    }

    void ticks( int n ) const
    {
        for ( int i = 0; i < n; ++i )
        {
            tick();
        }
    }

    [[nodiscard]] FakeCalls read() const
    {
        FakeCalls out;
        calls( &out );
        return out;
    }
};

// The play script both runs drive after boot: two rooms with an item and a soft reset between them.
FakeCalls playAfterStart( const Fake &fake )
{
    fake.ticks( 2 );
    fake.requestRoom( 0x122 );
    fake.ticks( 5 );
    fake.requestItem( 0x22, 1 );
    fake.ticks( 1 );
    fake.requestSoftReset();
    fake.ticks( 1 );
    fake.requestRoom( 0xf02 );
    fake.ticks( 5 );
    return fake.read();
}

FakeCalls play( const Fake &fake )
{
    fake.start();
    return playAfterStart( fake );
}

// The events at index from or later that probe reported as what with these values.
size_t probeEvents( const std::vector<FakeEvent> &events, std::string_view what, int64_t a, int64_t b, size_t from )
{
    size_t n = 0;
    for ( size_t i = from; i < events.size(); ++i )
    {
        n += std::string_view( events[ i ].mod ) == "probe" && what == events[ i ].what && events[ i ].a == a && events[ i ].b == b ? 1 : 0;
    }
    return n;
}

// modItems is how many items the loaded mods gave during the play: the probe mod gives one from its init.
void checkPlayed( const FakeCalls &calls, int modItems )
{
    CHECK( calls.coldStarts == 1 );
    CHECK( calls.warmStarts == 1 );
    CHECK( calls.softResets == 1 );
    CHECK( calls.roomChanges == 2 );
    CHECK( calls.itemAdds == 1 + modItems );
    CHECK( calls.lastItemId == 0x22 );
    CHECK( calls.lastItemDelta == 1 );
    CHECK( calls.lastRoomAtEntry == 0xf02 );
    CHECK( calls.workersStarted == 2 );
    CHECK( calls.roomChangeThread != calls.startThread );
}

} // namespace

TEST_SUITE( "unmodded" )
{
    TEST_CASE( "the fake game runs ticks, room changes, items and a soft reset with no runtime" )
    {
        REQUIRE( harness::fakeMain().main != nullptr );
        const Fake fake;
        REQUIRE( fake.complete() );
        checkPlayed( play( fake ), 0 );
    }

    TEST_CASE( "without -MODDED the runtime creates no logs directory" )
    {
        REQUIRE( harness::fakeMain().main != nullptr );
        CHECK_FALSE( std::filesystem::exists( harness::binDir() + L"logs" ) );
    }

    TEST_CASE( "reports reach the event list in order, stamped with their thread" )
    {
        const auto report = harness::fakeExport<FakeReportFn>( "FakeMain_Report" );
        REQUIRE( report != nullptr );
        const size_t mark = harness::events().size();
        report( "t", "first", "words", 1, 2 );
        DWORD otherThread = 0;
        std::thread other( [ & ] {
            otherThread = GetCurrentThreadId();
            report( "t", "second", nullptr, 3, 4 );
        } );
        other.join();
        report( "t", "third", nullptr, 5, 6 );

        const std::vector<FakeEvent> events = harness::events();
        REQUIRE( events.size() == mark + 3 );
        const std::string_view whats[] = { "first", "second", "third" };
        const DWORD threads[] = { GetCurrentThreadId(), otherThread, GetCurrentThreadId() };
        for ( size_t i = 0; i < 3; ++i )
        {
            const FakeEvent &event = events[ mark + i ];
            CHECK( std::string( event.mod ) == "t" );
            CHECK( event.what == whats[ i ] );
            CHECK( event.a == static_cast<int64_t>( ( 2 * i ) + 1 ) );
            CHECK( event.b == static_cast<int64_t>( ( 2 * i ) + 2 ) );
            CHECK( event.thread == threads[ i ] );
        }
        CHECK( std::string( events[ mark ].text ) == "words" );
        CHECK( std::string( events[ mark + 1 ].text ).empty() );
        CHECK( harness::countEvents( events, "t", "second", mark ) == 1 );
    }
}

// These cases share one process and one fake game, and run in this order: each starts from the state the one before leaves.
TEST_SUITE( "modded" )
{
    TEST_CASE( "booting under -MODDED hooks flower_startup from DllMain" )
    {
        REQUIRE( harness::fakeMain().main != nullptr );
        // Unhooked, flower_startup's first bytes are the harness's jump to Fake_flower_startup; hooked, they jump to MinHook's relay instead.
        const auto *entry = reinterpret_cast<const uint8_t *>( harness::fakeMain().main ) + wolf::symbolRva( "flower_startup" );
        REQUIRE( entry[ 0 ] == 0xE9 );
        int32_t rel = 0;
        std::memcpy( &rel, entry + 1, sizeof( rel ) );
        CHECK( reinterpret_cast<const void *>( entry + 5 + rel ) != reinterpret_cast<const void *>( harness::fakeExport<void *>( "Fake_flower_startup" ) ) );
    }

    // Leaves the fake game booted.
    TEST_CASE( "the cold flower_startup installs the sites and calls the game's function once" )
    {
        const Fake fake;
        REQUIRE( fake.complete() );
        fake.start();
        CHECK( fake.read().coldStarts == 1 );
        CHECK( fake.read().startThread == GetCurrentThreadId() );
    }

    // Leaves the fake game after one soft reset, in room 0xf02. The probe mod's reports need every site's symbols: flower_startup for session.start,
    // flower_tick for its ticks, SoftReset_ExecuteAndReload for session.end, Room_Change and g_currentRoomId for room.change, and Item_Add for item.add.
    TEST_CASE( "ticks, room changes, items and a soft reset reach the game's functions with their arguments" )
    {
        const Fake fake;
        const auto probeTicks = harness::modExport<int ( * )()>( L"probe.dll", "Probe_Ticks" );
        const size_t mark = harness::events().size();
        const int ticksBefore = probeTicks();
        const FakeCalls calls = playAfterStart( fake );
        checkPlayed( calls, 1 );
        const std::vector<FakeEvent> events = harness::events();
        CHECK( probeTicks() > ticksBefore );
        CHECK( probeEvents( events, "room.change", 0xFFFF, 0x122, mark ) == 1 );
        CHECK( probeEvents( events, "room.change", 0x122, 0xF02, mark ) == 1 );
        CHECK( probeEvents( events, "item.add", 0x22, 1, mark ) == 1 );
        CHECK( probeEvents( events, "session.end", 0, 0, mark ) == 1 );
        CHECK( probeEvents( events, "session.start", 1, 0, mark ) == 1 );
        for ( size_t i = mark; i < events.size(); ++i )
        {
            if ( std::string_view( events[ i ].what ) == "room.change" && events[ i ].b == 0xF02 )
            {
                CHECK( events[ i ].thread == calls.roomChangeThread ); // the site runs on the worker that runs Room_Change
            }
        }
    }

    TEST_CASE( "across play, room changes and soft resets every session ends and starts again" )
    {
        const Fake fake;
        const size_t eventMark = harness::events().size();
        for ( int reset = 0; reset < 3; ++reset )
        {
            fake.requestSoftReset();
            fake.ticks( 1 );
            fake.requestRoom( static_cast<uint16_t>( 0x200 + reset ) );
            fake.ticks( 5 );
            fake.requestItem( 0x10 + reset, 1 );
            fake.ticks( 10 );
        }
        const FakeCalls calls = fake.read();
        CHECK( calls.softResets == 4 );
        CHECK( calls.roomChanges == 5 );
        // Four from the fake's requests and one from the probe mod's giveItem at init.
        CHECK( calls.itemAdds == 5 );
        const std::vector<FakeEvent> events = harness::events();
        CHECK( harness::countEvents( events, "probe", "session.end", eventMark ) == 3 );
        CHECK( harness::countEvents( events, "probe", "session.start", eventMark ) == 3 );
    }

    TEST_CASE( "the crash filter is WOLF's after the cold flower_startup even though the game replaced it" )
    {
        const auto fakeFilter = harness::fakeExport<FakeFilterFn>( "FakeMain_Filter" );
        REQUIRE( fakeFilter != nullptr );
        const LPTOP_LEVEL_EXCEPTION_FILTER current = SetUnhandledExceptionFilter( nullptr );
        SetUnhandledExceptionFilter( current );
        CHECK( reinterpret_cast<void *>( current ) != fakeFilter() );
        HMODULE owner = nullptr;
        GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>( current ),
                            &owner );
        CHECK( owner == GetModuleHandleW( ( harness::binDir() + L"dinput8.dll" ).c_str() ) );
    }
}
