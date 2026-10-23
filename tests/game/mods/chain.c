/* A test mod built twice, as chain_a with CHAIN_TAG 'A' and as chain_b with 'B'. It links a detour on the fake game's FakeMain_Park that appends its tag to
 * the fake's trace, calls on, and appends the tag in lowercase once that returns, so the trace shows which links a call entered and returned through. */
#include <windows.h>

#include <stdint.h>

#include <wolf/wolf.h>

#include "fake.h"

typedef void ( *ParkFn )( void );

/* The table passes code addresses as void pointers. A union converts them without the cast C leaves undefined. */
typedef union
{
    void *raw;
    ParkFn fn;
} ParkPointer;

static const WolfApi *api;
static WolfMod *self;
static WolfHandle hookLink;
static ParkPointer original;

static void parkDetour( void )
{
    fakeTrace( CHAIN_TAG );
    original.fn();
    fakeTrace( (char)( CHAIN_TAG - 'A' + 'a' ) );
}

__declspec( dllexport ) void Chain_Remove( void )
{
    api->remove( self, hookLink );
}

__declspec( dllexport ) int32_t wolf_mod_init( const WolfApi *table, WolfMod *mod )
{
    fakeConnect();
    const HMODULE fake = GetModuleHandleA( "main.dll" );
    const ParkPointer detour = { .fn = parkDetour };
    api = table;
    self = mod;
    api->log( mod, WOLF_LOG_INFO, "init " CHAIN_ID );
    fakeReport( CHAIN_ID, "init", NULL, 0, 0 );
    const uintptr_t park = (uintptr_t)GetProcAddress( fake, "FakeMain_Park" ) - (uintptr_t)fake;
    hookLink = api->hookRaw( mod, "main.dll", park, detour.raw, &original.raw );
    return 0;
}
