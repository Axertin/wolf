#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <symbols.hpp>

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

size_t occurrences( const std::string &text, const std::string &needle )
{
    size_t n = 0;
    for ( size_t at = text.find( needle ); at != std::string::npos; at = text.find( needle, at + 1 ) )
    {
        ++n;
    }
    return n;
}

std::string onThisThread()
{
    return " on thread " + std::to_string( GetCurrentThreadId() );
}

void checkPlayed( const FakeCalls &calls )
{
    CHECK( calls.coldStarts == 1 );
    CHECK( calls.warmStarts == 1 );
    CHECK( calls.softResets == 1 );
    CHECK( calls.roomChanges == 2 );
    CHECK( calls.itemAdds == 1 );
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
        checkPlayed( play( fake ) );
    }

    TEST_CASE( "without -MODDED the runtime writes no log" )
    {
        REQUIRE( harness::fakeMain().main != nullptr );
        CHECK( harness::wolfLog().empty() );
        CHECK_FALSE( std::filesystem::exists( harness::binDir() + L"logs" ) );
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
        const std::string log = harness::wolfLog();
        CHECK( occurrences( log, "WOLF " ) >= 1 );
        CHECK( occurrences( log, " attached" + onThisThread() ) == 1 );
        CHECK( occurrences( log, "stays off" ) == 0 );
        CHECK( occurrences( log, "No mods will load" ) == 0 );
    }

    // Leaves the fake game booted.
    TEST_CASE( "the cold flower_startup installs the sites and calls the game's function once" )
    {
        const Fake fake;
        REQUIRE( fake.complete() );
        fake.start();
        CHECK( fake.read().coldStarts == 1 );
        const std::string log = harness::wolfLog();
        CHECK( occurrences( log, "cold flower_startup" + onThisThread() ) == 1 );
        CHECK( occurrences( log, "hook head on" ) == 0 );
        CHECK( occurrences( log, "MinHook" ) == 0 );
        for ( const char *name : { "flower_startup", "flower_tick", "SoftReset_ExecuteAndReload", "Room_Change", "Item_Add", "g_currentRoomId" } )
        {
            CHECK( occurrences( log, std::string( "symbol " ) + name + " did not resolve" ) == 0 );
        }
    }

    // Leaves the fake game after one soft reset, in room 0xf02.
    TEST_CASE( "ticks, room changes, items and a soft reset reach the game's functions with their arguments" )
    {
        const Fake fake;
        checkPlayed( playAfterStart( fake ) );
        const std::string log = harness::wolfLog();
        CHECK( occurrences( log, "room 0xFFFF to 0x0122" ) == 1 );
        CHECK( occurrences( log, "room 0x0122 to 0x0F02" ) == 1 );
        CHECK( occurrences( log, "Item_Add id 34 delta 1" ) == 1 );
        CHECK( occurrences( log, "soft reset: the session ends" ) == 1 );
        CHECK( occurrences( log, "warm flower_startup" ) == 1 );
    }

    TEST_CASE( "across play, room changes and soft resets the overlap and merge lines never log" )
    {
        const Fake fake;
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
        CHECK( calls.itemAdds == 4 );
        const std::string log = harness::wolfLog();
        CHECK( occurrences( log, "soft reset: the session ends" ) == 4 );
        CHECK( occurrences( log, "dispatch overlap" ) == 0 );
        CHECK( occurrences( log, "merged an ended generation" ) == 0 );
    }

    TEST_CASE( "the crash filter is WOLF's after the cold flower_startup even though the game replaced it" )
    {
        const auto fakeFilter = harness::fakeExport<FakeFilterFn>( "FakeMain_Filter" );
        REQUIRE( fakeFilter != nullptr );
        CHECK( occurrences( harness::wolfLog(), "the unhandled exception filter was replaced during flower_startup; WOLF's is back" ) == 1 );
        const LPTOP_LEVEL_EXCEPTION_FILTER current = SetUnhandledExceptionFilter( nullptr );
        SetUnhandledExceptionFilter( current );
        CHECK( reinterpret_cast<void *>( current ) != fakeFilter() );
        HMODULE owner = nullptr;
        GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>( current ),
                            &owner );
        CHECK( owner == GetModuleHandleW( ( harness::binDir() + L"dinput8.dll" ).c_str() ) );
    }
}
