// What the fake main.dll records about the calls that reach its game functions, shared by the fake and the tests.

#pragma once

#include <windows.h>

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
    uint16_t lastRoomAtEntry = 0; // g_currentRoomId when Room_Change was entered
    DWORD startThread = 0;        // the thread of the cold flower_startup
    DWORD roomChangeThread = 0;   // the thread of the last Room_Change
    int workersStarted = 0;
};

// The fake's exports the tests drive it through. Each takes effect through the game function's RVA, so the runtime's heads see every call.
using FakeStartFn = void ( * )();
using FakeTickFn = void ( * )();
using FakeRequestRoomFn = void ( * )( uint16_t room );
using FakeRequestItemFn = void ( * )( int32_t itemId, int32_t delta );
using FakeRequestSoftResetFn = void ( * )();
using FakeCallsFn = void ( * )( FakeCalls *out );
using FakeFilterFn = void *(*)();
