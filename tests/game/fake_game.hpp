// What the fake main.dll records about the calls that reach its game functions, shared by the fake and the tests.

#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>

struct FakeCalls
{
    int coldStarts = 0;
    int warmStarts = 0;
    int ticks = 0;
    int softResets = 0;
    int roomChanges = 0;
    int itemAdds = 0;
    int32_t lastItemId = 0;
    int32_t lastItemDelta = 0;
    void *lastItemMgr = nullptr;  // the itemMgr argument of the last Item_Add
    uint16_t lastRoomAtEntry = 0; // g_currentRoomId when Room_Change was entered
    DWORD startThread = 0;        // the thread of the cold flower_startup
    DWORD roomChangeThread = 0;   // the thread of the last Room_Change
    int workersStarted = 0;
};

// What a test mod saw, reported through FakeMain_Report. The list holds kFakeEvents for the process and is never cleared, so a test takes its length before
// acting and reads what follows.
inline constexpr size_t kFakeEvents = 1024;

struct FakeEvent
{
    DWORD thread;   // the reporting thread, stamped by the fake
    char mod[ 24 ]; // the reporting mod's id; strings are cut to fit
    char what[ 24 ];
    char text[ 32 ]; // empty when the report had none
    int64_t a;
    int64_t b;
};

// The fake's exports the tests drive it through. Each takes effect through the game function's RVA, so the runtime's heads see every call.
using FakeStartFn = void ( * )();
using FakeTickFn = void ( * )();
using FakeRequestRoomFn = void ( * )( uint16_t room );
using FakeRequestItemFn = void ( * )( int32_t itemId, int32_t delta );
using FakeRequestSoftResetFn = void ( * )();
using FakeCallsFn = void ( * )( FakeCalls *out );
using FakeFilterFn = void *(*)();
using FakeReportFn = void ( * )( const char *mod, const char *what, const char *text, int64_t a, int64_t b );
using FakeEventsFn = size_t ( * )( FakeEvent *out, size_t cap ); // copies up to cap events and returns how many reports arrived
// FakeMain_Park and its controls, and the trace that test mods' detours append to. Neither is a game function, so mods hook FakeMain_Park with hookRaw.
using FakeParkFn = void ( * )();
using FakeParkWaitingFn = bool ( * )();
using FakeTraceTakeFn = void ( * )( char *out, size_t n );
