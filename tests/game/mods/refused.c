/* A test mod whose wolf_mod_init registers a tick callback and a link on FakeMain_Park, then returns 1, so WOLF refuses it and drops both. */
#include <windows.h>

#include <stdint.h>

#include <wolf/sites.h>
#include <wolf/wolf.h>

#include "fake.h"

typedef void ( *ParkFn )( void );

typedef union
{
    void *raw;
    ParkFn fn;
} ParkPointer;

static int ticks;
static ParkPointer original;

static void onTick( WolfMod *mod, void *ctx, void *user )
{
    (void)mod;
    (void)ctx;
    (void)user;
    ++ticks;
}

static void parkDetour( void )
{
    fakeTrace( 'R' );
    original.fn();
}

/* How many tick callbacks ran: 0 while WOLF keeps a refused mod's registrations from running. */
__declspec( dllexport ) int Refused_Ticks( void )
{
    return ticks;
}

__declspec( dllexport ) int32_t wolf_mod_init( const WolfApi *api, WolfMod *mod )
{
    fakeConnect();
    const HMODULE fake = GetModuleHandleA( "main.dll" );
    const ParkPointer detour = { .fn = parkDetour };
    api->log( mod, WOLF_LOG_INFO, "init refused" );
    fakeReport( "refused", "init", NULL, 0, 0 );
    api->on( mod, WOLF_SITE_TICK, onTick, NULL, WOLF_LIFETIME_PROCESS );
    const uintptr_t park = (uintptr_t)GetProcAddress( fake, "FakeMain_Park" ) - (uintptr_t)fake;
    api->hookRaw( mod, "main.dll", park, detour.raw, &original.raw );
    return 1;
}
