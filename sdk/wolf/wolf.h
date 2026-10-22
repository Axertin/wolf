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
    WolfHandle ( *watch )( WolfMod *mod, const void *addr, size_t n, WolfCallback cb, void *user, int32_t lifetime );
    void ( *remove )( WolfMod *mod, WolfHandle h ); /* any kind */

    int32_t ( *write )( WolfMod *mod, void *addr, const void *src, size_t n );
    WolfHandle ( *patch )( WolfMod *mod, void *addr, const void *src, size_t n, int32_t lifetime );
    void *( *resolve )( WolfMod *mod, const char *name );

    void ( *flagSet )( WolfMod *mod, uint32_t group, uint32_t bit );
    void ( *flagClear )( WolfMod *mod, uint32_t group, uint32_t bit );
    int32_t ( *flagTest )( WolfMod *mod, uint32_t group, uint32_t bit );
    int32_t ( *giveItem )( WolfMod *mod, int32_t itemId, int32_t delta );

    int64_t ( *configInt )( WolfMod *mod, const char *key, int64_t fallback ); /* decimal or 0x hex, optional sign */
    double ( *configFloat )( WolfMod *mod, const char *key, double fallback );
    int32_t ( *configBool )( WolfMod *mod, const char *key, int32_t fallback );
    const char *( *configStr )( WolfMod *mod, const char *key, const char *fallback );
    const char *( *modPath )( WolfMod *mod ); /* absolute UTF-8, trailing separator */

    int32_t ( *runInGame )( WolfMod *mod, WolfRunFn fn, const void *data, uint32_t len ); /* len <= 64, copied; runs at the next frame boundary */

    WolfHandle ( *resourceOverride )( WolfMod *mod, const char *path, const char *filePath, const void *bytes, size_t n, int32_t lifetime );
    void *( *gameAlloc )( WolfMod *mod, size_t n );
    WolfHandle ( *provide )( WolfMod *mod, const char *slot, uint32_t key, const void *buffer, size_t size, int32_t lifetime );
    int32_t ( *claimId )( WolfMod *mod, const char *space, const char *name, int32_t preferred );
} WolfApi;

#define WOLF_HAS( api, entry ) ( ( api )->size >= offsetof( WolfApi, entry ) + sizeof( ( api )->entry ) )

/* Every code mod exports this. Nonzero refuses the mod and drops everything it registered. */
typedef int32_t ( *WolfModInitFn )( const WolfApi *api, WolfMod *mod );
/* export name: wolf_mod_init */

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-use-using,performance-enum-size)

#endif /* WOLF_WOLF_H */
