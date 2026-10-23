// Stand-in for the game's main.dll. Its import of DirectInput8Create is what makes the loader map dinput8.dll while this module is being loaded.
//
// The harness places a jump at each main.dll function RVA of the symbol table to the matching Fake_ export here, so the runtime hooks the same RVAs it hooks
// in the game. Every call the fake makes into a game function goes through that RVA too. The fake models the game's threads: flower_tick resumes the map
// worker and blocks until it sleeps again, as Task_ResumeSlot does; the worker runs Room_Change, which spans several ticks; and a soft reset runs
// SoftReset_ExecuteAndReload and flower_startup( true ) inside flower_tick and replaces the worker with a new thread.
#include <windows.h>

#include <unknwn.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <symbols.hpp>

#include "fake_game.hpp"

using CreateFn = HRESULT( WINAPI * )( HINSTANCE instance, DWORD version, const IID *iid, void **out, IUnknown *outer );

extern "C" __declspec( dllimport ) HRESULT WINAPI DirectInput8Create( HINSTANCE instance, DWORD version, const IID *iid, void **out, IUnknown *outer );

namespace {

constexpr int kRoomChangeYields = 3;

uint8_t *image()
{
    static const HMODULE self = [] {
        HMODULE module = nullptr;
        GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>( &image ),
                            &module );
        return module;
    }();
    return reinterpret_cast<uint8_t *>( self );
}

template <typename Fn>
Fn game( std::string_view name )
{
    return reinterpret_cast<Fn>( image() + wolf::symbolRva( name ) );
}

uint16_t &currentRoom()
{
    return *reinterpret_cast<uint16_t *>( image() + wolf::symbolRva( "g_currentRoomId" ) );
}

void *&gameGlobal( std::string_view name )
{
    return *reinterpret_cast<void **>( image() + wolf::symbolRva( name ) );
}

// What GameState_BootInit points g_progressFlagBlockPtr and g_ItemMgrPtr at during the cold flower_startup.
alignas( 16 ) uint8_t progressBlock[ 0x11b0 ] = {};
uint8_t itemMgr[ 16 ] = {};

// The word and mask ProgressFlag_Set and ProgressFlag_Clear address for ref = group << 16 | bit.
LONG *flagWord( uint32_t ref )
{
    auto *block = static_cast<uint8_t *>( gameGlobal( "g_progressFlagBlockPtr" ) );
    const size_t group = ref >> 16;
    const size_t bit = ref & 0xffffU;
    return reinterpret_cast<LONG *>( block + 0x35c + ( group * 0x20 ) + ( ( bit >> 5 ) * 4 ) );
}

LONG flagMask( uint32_t ref )
{
    return static_cast<LONG>( 0x80000000U >> ( ref & 31U ) );
}

// Written by main and the worker in turns, never at once, and read by the tests between ticks.
FakeCalls calls;

std::atomic<int> requestedRoom{ -1 };
std::atomic<bool> itemRequested{ false };
int32_t requestedItemId = 0;
int32_t requestedItemDelta = 0;
std::atomic<bool> softResetRequested{ false };

HANDLE resume = nullptr; // auto reset: main wakes the worker
HANDLE asleep = nullptr; // auto reset: the worker gives the turn back
HANDLE worker = nullptr;
std::atomic<bool> workerExit{ false };
std::atomic<bool> workerExited{ false };

std::atomic<bool> parkArmed{ false };
std::atomic<bool> parkWaiting{ false };
HANDLE parkReleased = CreateEventW( nullptr, FALSE, FALSE, nullptr ); // auto reset: FakeMain_ParkRelease wakes the parked caller

// Under traceLock. A test takes the trace after each few calls, so 255 characters is room to spare; characters past that are dropped.
SRWLOCK traceLock = SRWLOCK_INIT;
char trace[ 256 ] = {};
size_t traceLength = 0;

// Under eventLock.
SRWLOCK eventLock = SRWLOCK_INIT;
FakeEvent events[ kFakeEvents ] = {};
size_t eventCount = 0;

template <size_t N>
void copyText( char ( &to )[ N ], const char *from )
{
    if ( from != nullptr )
    {
        strncpy_s( to, from, _TRUNCATE );
    }
}

LONG WINAPI fakeFilter( EXCEPTION_POINTERS * /*pointers*/ )
{
    return EXCEPTION_CONTINUE_SEARCH;
}

// On the worker: gives the turn back to main and waits for the next tick.
void yieldFrame()
{
    SignalObjectAndWait( asleep, resume, INFINITE, FALSE );
}

DWORD WINAPI workerMain( void * /*unused*/ )
{
    WaitForSingleObject( resume, INFINITE );
    while ( !workerExit.load() )
    {
        if ( const int room = requestedRoom.exchange( -1 ); room >= 0 )
        {
            currentRoom() = static_cast<uint16_t>( room );
            game<void ( * )( void * )>( "Room_Change" )( nullptr );
        }
        if ( itemRequested.exchange( false ) )
        {
            game<void ( * )( void *, int32_t, int32_t )>( "Item_Add" )( nullptr, requestedItemId, requestedItemDelta );
        }
        yieldFrame();
    }
    workerExited.store( true );
    SetEvent( asleep );
    return 0;
}

// On main: runs the worker until it sleeps again.
void resumeWorker()
{
    SignalObjectAndWait( resume, asleep, INFINITE, FALSE );
}

void startWorker()
{
    workerExit.store( false );
    workerExited.store( false );
    worker = CreateThread( nullptr, 0, &workerMain, nullptr, 0, nullptr );
    ++calls.workersStarted;
}

void stopWorker()
{
    workerExit.store( true );
    // A worker parked inside Room_Change finishes it first, one tick's turn at a time.
    while ( !workerExited.load() )
    {
        resumeWorker();
    }
    WaitForSingleObject( worker, INFINITE );
    CloseHandle( worker );
}

} // namespace

extern "C" {

/* The address the loader bound the dinput8 import to. */
__declspec( dllexport ) CreateFn FakeMain_DirectInput8Create()
{
    return &DirectInput8Create;
}

__declspec( dllexport ) bool Fake_flower_startup( bool warm )
{
    if ( warm )
    {
        ++calls.warmStarts;
        return true;
    }
    ++calls.coldStarts;
    calls.startThread = GetCurrentThreadId();
    gameGlobal( "g_progressFlagBlockPtr" ) = progressBlock;
    gameGlobal( "g_ItemMgrPtr" ) = itemMgr;
    // The game or Steam may replace the unhandled-exception filter during startup.
    SetUnhandledExceptionFilter( &fakeFilter );
    return true;
}

__declspec( dllexport ) bool Fake_flower_tick()
{
    ++calls.ticks;
    if ( softResetRequested.exchange( false ) )
    {
        game<void ( * )()>( "SoftReset_ExecuteAndReload" )();
        game<bool ( * )( bool )>( "flower_startup" )( true );
        stopWorker();
        startWorker();
    }
    resumeWorker();
    return true;
}

__declspec( dllexport ) void Fake_SoftReset_ExecuteAndReload()
{
    ++calls.softResets;
}

__declspec( dllexport ) void Fake_Room_Change( void * /*sceneCtx*/ )
{
    ++calls.roomChanges;
    calls.lastRoomAtEntry = currentRoom();
    calls.roomChangeThread = GetCurrentThreadId();
    for ( int i = 0; i < kRoomChangeYields; ++i )
    {
        yieldFrame();
    }
}

__declspec( dllexport ) void Fake_Item_Add( void *mgr, int32_t itemId, int32_t delta )
{
    ++calls.itemAdds;
    calls.lastItemMgr = mgr;
    calls.lastItemId = itemId;
    calls.lastItemDelta = delta;
}

__declspec( dllexport ) void Fake_ProgressFlag_Set( uint32_t ref )
{
    InterlockedOr( flagWord( ref ), flagMask( ref ) );
}

__declspec( dllexport ) void Fake_ProgressFlag_Clear( uint32_t ref )
{
    InterlockedAnd( flagWord( ref ), ~flagMask( ref ) );
}

__declspec( dllexport ) void FakeMain_Start()
{
    resume = CreateEventW( nullptr, FALSE, FALSE, nullptr );
    asleep = CreateEventW( nullptr, FALSE, FALSE, nullptr );
    game<bool ( * )( bool )>( "flower_startup" )( false );
    startWorker();
}

__declspec( dllexport ) void FakeMain_Tick()
{
    game<bool ( * )()>( "flower_tick" )();
}

__declspec( dllexport ) void FakeMain_RequestRoom( uint16_t room )
{
    requestedRoom.store( room );
}

__declspec( dllexport ) void FakeMain_RequestItem( int32_t itemId, int32_t delta )
{
    requestedItemId = itemId;
    requestedItemDelta = delta;
    itemRequested.store( true );
}

__declspec( dllexport ) void FakeMain_RequestSoftReset()
{
    softResetRequested.store( true );
}

__declspec( dllexport ) void FakeMain_Calls( FakeCalls *out )
{
    *out = calls;
}

__declspec( dllexport ) void *FakeMain_Filter()
{
    return reinterpret_cast<void *>( &fakeFilter );
}

// A function for test mods to hook with hookRaw. While armed it blocks its caller until FakeMain_ParkRelease, so a test can act while a thread is inside the
// mods' links. It is not inlined and its body is well over the 16 bytes MinHook may overwrite.
__declspec( dllexport ) __declspec( noinline ) void FakeMain_Park()
{
    if ( !parkArmed.load() )
    {
        return;
    }
    parkWaiting.store( true );
    WaitForSingleObject( parkReleased, INFINITE );
    parkWaiting.store( false );
}

__declspec( dllexport ) void FakeMain_ParkArm()
{
    ResetEvent( parkReleased );
    parkArmed.store( true );
}

__declspec( dllexport ) bool FakeMain_ParkWaiting()
{
    return parkWaiting.load();
}

__declspec( dllexport ) void FakeMain_ParkRelease()
{
    parkArmed.store( false );
    SetEvent( parkReleased );
}

__declspec( dllexport ) void FakeMain_Trace( char c )
{
    AcquireSRWLockExclusive( &traceLock );
    if ( traceLength < sizeof( trace ) - 1 )
    {
        trace[ traceLength++ ] = c;
    }
    ReleaseSRWLockExclusive( &traceLock );
}

__declspec( dllexport ) void FakeMain_TraceTake( char *out, size_t n )
{
    AcquireSRWLockExclusive( &traceLock );
    if ( n != 0 )
    {
        const size_t copied = std::min( traceLength, n - 1 );
        std::memcpy( out, trace, copied );
        out[ copied ] = '\0';
    }
    traceLength = 0;
    ReleaseSRWLockExclusive( &traceLock );
}

__declspec( dllexport ) void FakeMain_Report( const char *mod, const char *what, const char *text, int64_t a, int64_t b )
{
    AcquireSRWLockExclusive( &eventLock );
    // Past kFakeEvents a report is counted but not kept, so the reader sees that some were lost.
    if ( eventCount++ < kFakeEvents )
    {
        FakeEvent &event = events[ eventCount - 1 ];
        event.thread = GetCurrentThreadId();
        copyText( event.mod, mod );
        copyText( event.what, what );
        copyText( event.text, text );
        event.a = a;
        event.b = b;
    }
    ReleaseSRWLockExclusive( &eventLock );
}

__declspec( dllexport ) size_t FakeMain_Events( FakeEvent *out, size_t cap )
{
    AcquireSRWLockExclusive( &eventLock );
    std::copy_n( events, std::min( { eventCount, cap, kFakeEvents } ), out );
    const size_t count = eventCount;
    ReleaseSRWLockExclusive( &eventLock );
    return count;
}

} // extern "C"
