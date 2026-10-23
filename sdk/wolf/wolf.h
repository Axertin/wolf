/* WOLF mod API, the C table every code mod receives in wolf_mod_init.
 *
 * C11. Keep constexpr, nullptr and typeof out of this file. */
#ifndef WOLF_WOLF_H
#define WOLF_WOLF_H

#include <stddef.h>
#include <stdint.h>

/* This header is C11, and these checks require C++ syntax. */
// NOLINTBEGIN(modernize-use-using,performance-enum-size)

#ifdef __cplusplus
extern "C" {
#endif

#define WOLF_ABI_VERSION 1 /* bumps only on a breaking table change */

typedef struct WolfMod WolfMod; /* opaque, owned by the runtime */
typedef uint32_t WolfHandle;    /* 0 = none */

/* A lifetime is how long what a mod registers or allocates lasts: the process, a session (until a soft reset), a room (until the next room change) or a
 * frame. Code that runs off game context, such as a thread the mod starts, touches only process-lifetime memory or hands its work to runInGame. Three ends of
 * one lifetime between two frame boundaries merge: that logs once, and the merged memory stays valid until the lifetime's next end. */
typedef enum WolfLifetime { WOLF_LIFETIME_PROCESS, WOLF_LIFETIME_SESSION, WOLF_LIFETIME_ROOM, WOLF_LIFETIME_FRAME } WolfLifetime;
typedef enum WolfLevel { WOLF_LOG_DEBUG, WOLF_LOG_INFO, WOLF_LOG_WARN, WOLF_LOG_ERROR } WolfLevel;

typedef void ( *WolfCallback )( WolfMod *mod, void *ctx, void *user );
typedef void ( *WolfRunFn )( WolfMod *mod, void *data, uint32_t len );
typedef void ( *WolfFinalizer )( WolfMod *mod, void *data );

typedef struct WolfApi
{
    uint32_t abiVersion;        /* must equal WOLF_ABI_VERSION */
    uint32_t size;              /* sizeof(WolfApi) in the runtime; grows when entries are appended */
    const char *runtimeVersion; /* e.g. "0.4.0" */

    void ( *log )( WolfMod *mod, int32_t level, const char *msg );

    void *( *alloc )( WolfMod *mod, int32_t lifetime, size_t n, size_t align ); /* zeroed; freed with the lifetime */
    int32_t ( *onReset )( WolfMod *mod, int32_t lifetime, WolfFinalizer fn, void *data );

    WolfHandle ( *on )( WolfMod *mod, const char *site, WolfCallback cb, void *user, int32_t lifetime );
    WolfHandle ( *hook )( WolfMod *mod, const char *name, void *detour, void **original ); /* no lifetime; linked until removed */
    WolfHandle ( *hookRaw )( WolfMod *mod, const char *module, uintptr_t rva, void *detour, void **original );
    /* n is 1 to 65536 and the range readable when registered; compared on the main thread at each frame boundary, cb gets a WolfWatchCtx when it changed */
    WolfHandle ( *watch )( WolfMod *mod, const void *addr, size_t n, WolfCallback cb, void *user, int32_t lifetime );
    void ( *remove )( WolfMod *mod, WolfHandle h ); /* any kind */

    /* copies now, through read-only and executable protection; 0 when a page in the range is uncommitted, no-access, guard or execute-only, or when a
     * page fails partway, which leaves the part before it written */
    int32_t ( *write )( WolfMod *mod, void *addr, const void *src, size_t n );
    /* src copied now; written at the next frame boundary. remove restores; at the lifetime's end image memory is restored, other memory left,
     * so a patch on game heap memory belongs in the lifetime of that memory */
    WolfHandle ( *patch )( WolfMod *mod, void *addr, const void *src, size_t n, int32_t lifetime );
    void *( *resolve )( WolfMod *mod, const char *name );

    /* group 0 to 79, bit 0 to 255; any thread, through the game's own functions. Before the game has started they do nothing and flagTest returns 0 */
    void ( *flagSet )( WolfMod *mod, uint32_t group, uint32_t bit );
    void ( *flagClear )( WolfMod *mod, uint32_t group, uint32_t bit );
    int32_t ( *flagTest )( WolfMod *mod, uint32_t group, uint32_t bit );
    /* itemId 0 to 255. Runs now inside a callback, runInGame call or finalizer once the game has started, else at the next frame boundary; item.add gets
     * fromMod */
    int32_t ( *giveItem )( WolfMod *mod, int32_t itemId, int32_t delta );

    int64_t ( *configInt )( WolfMod *mod, const char *key, int64_t fallback ); /* decimal or 0x hex, optional sign */
    /* decimal, optional sign, fraction and exponent; fallback for hex, inf, nan, trailing text or a value out of range */
    double ( *configFloat )( WolfMod *mod, const char *key, double fallback );
    int32_t ( *configBool )( WolfMod *mod, const char *key, int32_t fallback );
    const char *( *configStr )( WolfMod *mod, const char *key, const char *fallback );
    const char *( *modPath )( WolfMod *mod ); /* absolute UTF-8, trailing separator */

    int32_t ( *runInGame )( WolfMod *mod, WolfRunFn fn, const void *data, uint32_t len ); /* len <= 64, copied; runs at the next frame boundary */

    /* not built in this runtime: returns 0 and logs once per mod */
    WolfHandle ( *resourceOverride )( WolfMod *mod, const char *path, const char *filePath, const void *bytes, size_t n, int32_t lifetime );
    /* not built in this runtime: returns null and logs once per mod */
    void *( *gameAlloc )( WolfMod *mod, size_t n );
    /* not built in this runtime: returns 0 and logs once per mod */
    WolfHandle ( *provide )( WolfMod *mod, const char *slot, uint32_t key, const void *buffer, size_t size, int32_t lifetime );
    /* not built in this runtime: returns -1 and logs once per mod */
    int32_t ( *claimId )( WolfMod *mod, const char *space, const char *name, int32_t preferred );
} WolfApi;

#define WOLF_HAS( api, entry ) ( ( api )->size >= offsetof( WolfApi, entry ) + sizeof( ( api )->entry ) )

/* Every code mod exports this. Nonzero refuses the mod: its registrations, assets and config are dropped, and its DLL stays loaded. */
typedef int32_t ( *WolfModInitFn )( const WolfApi *api, WolfMod *mod );
/* export name: wolf_mod_init */

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-use-using,performance-enum-size)

#endif /* WOLF_WOLF_H */
