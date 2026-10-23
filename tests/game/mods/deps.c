/* A test mod that imports deps_helper.dll, which sits beside it in its mod directory rather than beside the game. */
#include <stdint.h>
#include <stdio.h>

#include <wolf/wolf.h>

#include "fake.h"

__declspec( dllimport ) int helper_value( void );

__declspec( dllexport ) int32_t wolf_mod_init( const WolfApi *api, WolfMod *mod )
{
    char line[ 32 ];
    fakeConnect();
    fakeReport( "deps", "init", NULL, helper_value(), 0 );
    snprintf( line, sizeof( line ), "init deps %d", helper_value() );
    api->log( mod, WOLF_LOG_INFO, line );
    return 0;
}
