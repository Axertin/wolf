/* Compiles the public headers as C11 and pins the ABI: every WolfApi entry's
 * offset and type, the struct's size on x64, and the layout of the site
 * context structs. A failed pin is a build error. An entry or field may only
 * be appended; moving one or changing its type breaks every built mod. */
#include <wolf/sites.h>
#include <wolf/wolf.h>

#define PIN_OFFSET( entry, offset ) _Static_assert( offsetof( WolfApi, entry ) == ( offset ), "WolfApi." #entry " moved" )

/* Declared and never defined: _Generic only reads its members' types. Not
 * const, so the members carry no qualifier for _Generic to match. */
extern WolfApi pinned;
/* A _Generic association takes a bare type name, which parentheses would break. */
/* NOLINTNEXTLINE(bugprone-macro-parentheses) */
#define PIN_TYPE( entry, type ) _Static_assert( _Generic( pinned.entry, type: 1, default: 0 ), "WolfApi." #entry " changed type" )

_Static_assert( sizeof( void * ) == 8, "WOLF is x64 only" );
_Static_assert( WOLF_ABI_VERSION == 1, "WOLF_ABI_VERSION changed; update the pins below" );
_Static_assert( sizeof( WolfHandle ) == 4, "WolfHandle is a uint32_t" );
_Static_assert( sizeof( WolfLifetime ) == 4, "WolfLifetime is int-sized" );
_Static_assert( sizeof( WolfLevel ) == 4, "WolfLevel is int-sized" );
_Static_assert( WOLF_LIFETIME_PROCESS == 0, "lifetime values" );
_Static_assert( WOLF_LIFETIME_SESSION == 1, "lifetime values" );
_Static_assert( WOLF_LIFETIME_ROOM == 2, "lifetime values" );
_Static_assert( WOLF_LIFETIME_FRAME == 3, "lifetime values" );
_Static_assert( WOLF_LOG_DEBUG == 0, "level values" );
_Static_assert( WOLF_LOG_INFO == 1, "level values" );
_Static_assert( WOLF_LOG_WARN == 2, "level values" );
_Static_assert( WOLF_LOG_ERROR == 3, "level values" );

PIN_OFFSET( abiVersion, 0 );
PIN_OFFSET( size, 4 );
PIN_OFFSET( runtimeVersion, 8 );
PIN_OFFSET( log, 16 );
PIN_OFFSET( alloc, 24 );
PIN_OFFSET( onReset, 32 );
PIN_OFFSET( on, 40 );
PIN_OFFSET( hook, 48 );
PIN_OFFSET( hookRaw, 56 );
PIN_OFFSET( watch, 64 );
PIN_OFFSET( remove, 72 );
PIN_OFFSET( write, 80 );
PIN_OFFSET( patch, 88 );
PIN_OFFSET( resolve, 96 );
PIN_OFFSET( flagSet, 104 );
PIN_OFFSET( flagClear, 112 );
PIN_OFFSET( flagTest, 120 );
PIN_OFFSET( giveItem, 128 );
PIN_OFFSET( configInt, 136 );
PIN_OFFSET( configFloat, 144 );
PIN_OFFSET( configBool, 152 );
PIN_OFFSET( configStr, 160 );
PIN_OFFSET( modPath, 168 );
PIN_OFFSET( runInGame, 176 );
PIN_OFFSET( resourceOverride, 184 );
PIN_OFFSET( gameAlloc, 192 );
PIN_OFFSET( provide, 200 );
PIN_OFFSET( claimId, 208 );
_Static_assert( sizeof( WolfApi ) == 216, "sizeof(WolfApi) changed" );

PIN_TYPE( abiVersion, uint32_t );
PIN_TYPE( size, uint32_t );
PIN_TYPE( runtimeVersion, const char * );
PIN_TYPE( log, void ( * )( WolfMod *, int32_t, const char * ) );
PIN_TYPE( alloc, void *(*)(WolfMod *, int32_t, size_t, size_t));
PIN_TYPE( onReset, int32_t ( * )( WolfMod *, int32_t, void ( * )( WolfMod *, void * ), void * ) );
PIN_TYPE( on, WolfHandle ( * )( WolfMod *, const char *, void ( * )( WolfMod *, void *, void * ), void *, int32_t ) );
PIN_TYPE( hook, WolfHandle ( * )( WolfMod *, const char *, void *, void ** ) );
PIN_TYPE( hookRaw, WolfHandle ( * )( WolfMod *, const char *, uintptr_t, void *, void ** ) );
PIN_TYPE( watch, WolfHandle ( * )( WolfMod *, const void *, size_t, void ( * )( WolfMod *, void *, void * ), void *, int32_t ) );
PIN_TYPE( remove, void ( * )( WolfMod *, WolfHandle ) );
PIN_TYPE( write, int32_t ( * )( WolfMod *, void *, const void *, size_t ) );
PIN_TYPE( patch, WolfHandle ( * )( WolfMod *, void *, const void *, size_t, int32_t ) );
PIN_TYPE( resolve, void *(*)(WolfMod *, const char *));
PIN_TYPE( flagSet, void ( * )( WolfMod *, uint32_t, uint32_t ) );
PIN_TYPE( flagClear, void ( * )( WolfMod *, uint32_t, uint32_t ) );
PIN_TYPE( flagTest, int32_t ( * )( WolfMod *, uint32_t, uint32_t ) );
PIN_TYPE( giveItem, int32_t ( * )( WolfMod *, int32_t, int32_t ) );
PIN_TYPE( configInt, int64_t ( * )( WolfMod *, const char *, int64_t ) );
PIN_TYPE( configFloat, double ( * )( WolfMod *, const char *, double ) );
PIN_TYPE( configBool, int32_t ( * )( WolfMod *, const char *, int32_t ) );
PIN_TYPE( configStr, const char *(*)(WolfMod *, const char *, const char *));
PIN_TYPE( modPath, const char *(*)(WolfMod *));
PIN_TYPE( runInGame, int32_t ( * )( WolfMod *, void ( * )( WolfMod *, void *, uint32_t ), const void *, uint32_t ) );
PIN_TYPE( resourceOverride, WolfHandle ( * )( WolfMod *, const char *, const char *, const void *, size_t, int32_t ) );
PIN_TYPE( gameAlloc, void *(*)(WolfMod *, size_t));
PIN_TYPE( provide, WolfHandle ( * )( WolfMod *, const char *, uint32_t, const void *, size_t, int32_t ) );
PIN_TYPE( claimId, int32_t ( * )( WolfMod *, const char *, const char *, int32_t ) );

/* sites.h */
#define PIN_FIELD( type, field, offset ) _Static_assert( offsetof( type, field ) == ( offset ), #type "." #field " moved" )
_Static_assert( WOLF_ROOM_NONE == 0xFFFF, "WOLF_ROOM_NONE" );
_Static_assert( sizeof( WolfSessionStartCtx ) == 8, "WolfSessionStartCtx size" );
PIN_FIELD( WolfSessionStartCtx, warm, 4 );
_Static_assert( sizeof( WolfSessionEndCtx ) == 4, "WolfSessionEndCtx size" );
_Static_assert( sizeof( WolfTickCtx ) == 16, "WolfTickCtx size" );
PIN_FIELD( WolfTickCtx, frame, 8 );
_Static_assert( sizeof( WolfRoomChangeCtx ) == 12, "WolfRoomChangeCtx size" );
PIN_FIELD( WolfRoomChangeCtx, from, 4 );
PIN_FIELD( WolfRoomChangeCtx, to, 8 );
_Static_assert( sizeof( WolfItemAddCtx ) == 24, "WolfItemAddCtx size" );
PIN_FIELD( WolfItemAddCtx, itemId, 4 );
PIN_FIELD( WolfItemAddCtx, delta, 8 );
PIN_FIELD( WolfItemAddCtx, handled, 12 );
PIN_FIELD( WolfItemAddCtx, fromMod, 16 );
