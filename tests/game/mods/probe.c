/* A test mod that reads its config.ini, links a detour on the site function Item_Add by name and subscribes item.add. The detour appends 'H' and the
 * callback 'S' to the fake game's trace, so the trace shows the order a site's callbacks and a mod's link run in. Its init gives item 17 and sets flag 1:1
 * before the game has started. The item.add callback records, for each item id, how many calls came from this mod's giveItem and the thread they ran on;
 * the exports drive the flag and giveItem entries from the test. */
#include <windows.h>

#include <stdint.h>
#include <stdio.h>

#include <wolf/sites.h>
#include <wolf/wolf.h>

#include "fake.h"

typedef uintptr_t ( *ItemAddFn )( void *itemMgr, int32_t itemId, int32_t delta );

typedef union
{
    void *raw;
    ItemAddFn fn;
} ItemAddPointer;

static ItemAddPointer original;
static const WolfApi *wolf;
static WolfMod *self;

/* Written by the item.add callback, which runs on one game thread at a time, and read by the test between ticks. For the calls for item id that came from
 * this mod's giveItem, fromSelf[ id ] counts them and thread[ id ] is the thread they ran on, or 0 once two of them ran on different threads. */
static volatile LONG fromSelf[ 256 ];
static volatile DWORD thread[ 256 ];

static volatile LONG giveOnTick;
static volatile LONG ticks;
static HANDLE giver;
static HANDLE giverInside; /* set once the giver thread has entered its loop */
static volatile LONG giverDone;
static int giverCount;

static uintptr_t itemAddDetour( void *itemMgr, int32_t itemId, int32_t delta )
{
    fakeTrace( 'H' );
    return original.fn( itemMgr, itemId, delta );
}

static void onItemAdd( WolfMod *mod, void *ctx, void *user )
{
    const WolfItemAddCtx *item = (const WolfItemAddCtx *)ctx;
    const DWORD current = GetCurrentThreadId();
    (void)mod;
    (void)user;
    fakeTrace( 'S' );
    if ( item->fromMod == NULL )
    {
        fakeReport( "probe", "item.add", NULL, item->itemId, item->delta );
    }
    if ( item->itemId < 0 || item->itemId > 255 )
    {
        return;
    }
    if ( item->fromMod != self )
    {
        return;
    }
    if ( fromSelf[ item->itemId ]++ == 0 )
    {
        thread[ item->itemId ] = current;
    }
    else if ( thread[ item->itemId ] != current )
    {
        thread[ item->itemId ] = 0;
    }
}

static void onSessionStart( WolfMod *mod, void *ctx, void *user )
{
    (void)mod;
    (void)user;
    fakeReport( "probe", "session.start", NULL, ( (const WolfSessionStartCtx *)ctx )->warm, 0 );
}

static void onSessionEnd( WolfMod *mod, void *ctx, void *user )
{
    (void)mod;
    (void)ctx;
    (void)user;
    fakeReport( "probe", "session.end", NULL, 0, 0 );
}

static void onRoomChange( WolfMod *mod, void *ctx, void *user )
{
    const WolfRoomChangeCtx *room = (const WolfRoomChangeCtx *)ctx;
    (void)mod;
    (void)user;
    fakeReport( "probe", "room.change", NULL, room->from, room->to );
}

static void onTick( WolfMod *mod, void *ctx, void *user )
{
    (void)ctx;
    (void)user;
    InterlockedIncrement( &ticks );
    if ( InterlockedExchange( &giveOnTick, 0 ) != 0 )
    {
        wolf->giveItem( mod, 21, 1 );
    }
}

/* Sleeps 1 ms after every 50 gifts, so the gifts span many frames and the run step drains the queue while the loop is still appending to it. */
static DWORD WINAPI giveLoop( void *unused )
{
    (void)unused;
    for ( int i = 0; i < giverCount; ++i )
    {
        if ( i == 0 )
        {
            SetEvent( giverInside );
        }
        wolf->giveItem( self, 20, 1 );
        if ( i % 50 == 49 )
        {
            Sleep( 1 );
        }
    }
    InterlockedExchange( &giverDone, 1 );
    return 0;
}

/* How many tick callbacks have run. */
__declspec( dllexport ) int Probe_Ticks( void )
{
    return ticks;
}

/* Sets ('s') or clears ('c') the flag, then returns what flagTest reads back. */
__declspec( dllexport ) int32_t Probe_Flag( char op, uint32_t group, uint32_t bit )
{
    if ( op == 's' )
    {
        wolf->flagSet( self, group, bit );
    }
    else if ( op == 'c' )
    {
        wolf->flagClear( self, group, bit );
    }
    return wolf->flagTest( self, group, bit );
}

__declspec( dllexport ) int32_t Probe_Give( int32_t itemId, int32_t delta )
{
    return wolf->giveItem( self, itemId, delta );
}

/* Starts a thread that gives item 20 count times, and returns once that thread is inside its loop. count is at least 1. */
__declspec( dllexport ) void Probe_GiveFromThread( int count )
{
    giverCount = count;
    InterlockedExchange( &giverDone, 0 );
    giverInside = CreateEventW( NULL, TRUE, FALSE, NULL );
    giver = CreateThread( NULL, 0, giveLoop, NULL, 0, NULL );
    WaitForSingleObject( giverInside, INFINITE );
    CloseHandle( giverInside );
}

/* Nonzero once the giver thread has made all its calls. */
__declspec( dllexport ) int32_t Probe_GiveDone( void )
{
    return InterlockedCompareExchange( &giverDone, 0, 0 );
}

__declspec( dllexport ) void Probe_GiveJoin( void )
{
    WaitForSingleObject( giver, INFINITE );
    CloseHandle( giver );
}

/* The next tick callback gives item 21 once. */
__declspec( dllexport ) void Probe_GiveOnTick( void )
{
    InterlockedExchange( &giveOnTick, 1 );
}

__declspec( dllexport ) int Probe_ItemsFromSelf( int id )
{
    return fromSelf[ id ];
}

/* The thread every item.add call for id from this mod's giveItem ran on; 0 when there was none or they ran on more than one. */
__declspec( dllexport ) DWORD Probe_ItemThread( int id )
{
    return thread[ id ];
}

__declspec( dllexport ) int32_t wolf_mod_init( const WolfApi *api, WolfMod *mod )
{
    fakeConnect();
    const ItemAddPointer detour = { .fn = itemAddDetour };
    char line[ 128 ];
    api->log( mod, WOLF_LOG_INFO, "init probe" );
    fakeReport( "probe", "init", NULL, 0, 0 );
    snprintf( line, sizeof( line ), "greeting %s", api->configStr( mod, "greeting", "none" ) );
    api->log( mod, WOLF_LOG_INFO, line );
    fakeReport( "probe", "greeting", api->configStr( mod, "greeting", "none" ), 0, 0 );
    wolf = api;
    self = mod;
    api->hook( mod, "Item_Add", detour.raw, &original.raw );
    api->on( mod, WOLF_SITE_ITEM_ADD, onItemAdd, NULL, WOLF_LIFETIME_PROCESS );
    api->on( mod, WOLF_SITE_TICK, onTick, NULL, WOLF_LIFETIME_PROCESS );
    api->on( mod, WOLF_SITE_SESSION_START, onSessionStart, NULL, WOLF_LIFETIME_PROCESS );
    api->on( mod, WOLF_SITE_SESSION_END, onSessionEnd, NULL, WOLF_LIFETIME_PROCESS );
    api->on( mod, WOLF_SITE_ROOM_CHANGE, onRoomChange, NULL, WOLF_LIFETIME_PROCESS );
    api->giveItem( mod, 17, 1 );
    api->flagSet( mod, 1, 1 );
    return 0;
}
