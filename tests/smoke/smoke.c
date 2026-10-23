/* The smoke mod, for a real-game run by hand. It uses only the C table. On the first room.change into a room other than the title room 0x0C00 it does, once:
 * a round trip on progress flag group 76 bit 255 that tests the flag, sets it, clears it and puts the original value back, logging each step; a giveItem
 * of one Holy Bone S from the room.change callback, which runs at once on the map task worker; and one from a thread it starts, which queues to the frame
 * boundary and runs on the main thread. Its item.add callback logs every item.add with the mod that asked for it.
 *
 * Why these values:
 * - Flag group 76 (0x4c) bit 255. ProgressFlag_Set and ProgressFlag_Clear take group << 16 | bit, and no literal ProgressFlag_Set or ProgressFlag_Clear call in
 *   the room scripts uses group 0x4c. The round trip restores the value it found, so anything else that reads the flag sees it change only for the length of
 *   the callback.
 * - Item 143 (0x8f), Holy Bone S: a consumable that stacks to 99, flags 0x00401611, with bit 26, the key item bit, clear. */
#include <windows.h>

#include <stdint.h>
#include <stdio.h>

#include <wolf/sites.h>
#include <wolf/wolf.h>

#define TITLE_ROOM 0x0C00U
#define FLAG_GROUP 76U
#define FLAG_BIT 255U
#define ITEM 143

static const WolfApi *wolf;
static WolfMod *self;
static int done; /* room.change runs on one thread at a time */

static void say( const char *format, int value )
{
    char line[ 128 ];
    snprintf( line, sizeof( line ), format, value );
    wolf->log( self, WOLF_LOG_INFO, line );
}

static DWORD WINAPI giveFromThread( void *unused )
{
    (void)unused;
    say( "smoke: giveItem 143 from a mod thread returned %d", wolf->giveItem( self, ITEM, 1 ) );
    return 0;
}

static void flagRoundTrip( void )
{
    const int32_t original = wolf->flagTest( self, FLAG_GROUP, FLAG_BIT );
    say( "smoke: flag 76/255 was %d", original );
    wolf->flagSet( self, FLAG_GROUP, FLAG_BIT );
    say( "smoke: flag 76/255 after set is %d", wolf->flagTest( self, FLAG_GROUP, FLAG_BIT ) );
    wolf->flagClear( self, FLAG_GROUP, FLAG_BIT );
    say( "smoke: flag 76/255 after clear is %d", wolf->flagTest( self, FLAG_GROUP, FLAG_BIT ) );
    if ( original != 0 )
    {
        wolf->flagSet( self, FLAG_GROUP, FLAG_BIT );
    }
    say( "smoke: flag 76/255 restored to %d", wolf->flagTest( self, FLAG_GROUP, FLAG_BIT ) );
}

static void onRoomChange( WolfMod *mod, void *ctx, void *user )
{
    const WolfRoomChangeCtx *room = (const WolfRoomChangeCtx *)ctx;
    HANDLE thread;
    (void)mod;
    (void)user;
    if ( done || room->to == TITLE_ROOM )
    {
        return;
    }
    done = 1;
    say( "smoke: first play room %04X", (int)room->to );
    flagRoundTrip();
    say( "smoke: giveItem 143 from room.change returned %d", wolf->giveItem( self, ITEM, 1 ) );
    thread = CreateThread( NULL, 0, giveFromThread, NULL, 0, NULL );
    if ( thread == NULL )
    {
        say( "smoke: the giving thread did not start (error %d)", (int)GetLastError() );
        return;
    }
    CloseHandle( thread );
}

static void onItemAdd( WolfMod *mod, void *ctx, void *user )
{
    const WolfItemAddCtx *item = (const WolfItemAddCtx *)ctx;
    const char *from = "another mod";
    char line[ 128 ];
    (void)mod;
    (void)user;
    if ( item->fromMod == NULL )
    {
        from = "the game";
    }
    else if ( item->fromMod == self )
    {
        from = "smoke";
    }
    snprintf( line, sizeof( line ), "smoke: item.add %d x%d from %s", (int)item->itemId, (int)item->delta, from );
    wolf->log( self, WOLF_LOG_INFO, line );
}

__declspec( dllexport ) int32_t wolf_mod_init( const WolfApi *table, WolfMod *mod )
{
    wolf = table;
    self = mod;
    if ( wolf->on( mod, WOLF_SITE_ROOM_CHANGE, onRoomChange, NULL, WOLF_LIFETIME_PROCESS ) == 0 ||
         wolf->on( mod, WOLF_SITE_ITEM_ADD, onItemAdd, NULL, WOLF_LIFETIME_PROCESS ) == 0 )
    {
        return 1;
    }
    say( "smoke: init, table size %d", (int)table->size );
    return 0;
}
