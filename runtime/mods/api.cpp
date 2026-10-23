#include "mods/api.hpp"

#include <wolf_version.h>

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>

#include "arena/lifetime.hpp"
#include "config/config.hpp"
#include "frame/run_queue.hpp"
#include "game/game.hpp"
#include "hooks/hooks.hpp"
#include "hooks/resolve.hpp"
#include "log/log.hpp"
#include "memory/memory.hpp"
#include "mods/mod.hpp"
#include "sites/sites.hpp"
#include "tables/registry.hpp"

namespace wolf {

namespace {

// WolfApi's function members in table order; an entry's log-once bit is kOnceEntry plus its ordinal.
enum class Entry : uint8_t {
    Log,
    Alloc,
    OnReset,
    On,
    Hook,
    HookRaw,
    Watch,
    Remove,
    Write,
    Patch,
    Resolve,
    FlagSet,
    FlagClear,
    FlagTest,
    GiveItem,
    ConfigInt,
    ConfigFloat,
    ConfigBool,
    ConfigStr,
    ModPath,
    RunInGame,
    ResourceOverride,
    GameAlloc,
    Provide,
    ClaimId,
};

constexpr const char *kEntryNames[] = { "log",         "alloc",      "onReset",   "on",      "hook",      "hookRaw",          "watch",     "remove",
                                        "write",       "patch",      "resolve",   "flagSet", "flagClear", "flagTest",         "giveItem",  "configInt",
                                        "configFloat", "configBool", "configStr", "modPath", "runInGame", "resourceOverride", "gameAlloc", "provide",
                                        "claimId" };
static_assert( std::size( kEntryNames ) == ( sizeof( WolfApi ) - offsetof( WolfApi, log ) ) / sizeof( void * ) );
static_assert( std::size( kEntryNames ) == static_cast<size_t>( Entry::ClaimId ) + 1 );

// Log-once bits for calls that pass a null mod, shared by the whole process.
constinit WolfMod nullMod;

// One call into the table. Every check returns true when it passes. A failing check logs WARN "<entry> refused: <reason>" once per mod and entry and returns
// false, so an entry runs its checks in order and returns its refusal value at the first that fails.
struct Call
{
    WolfMod *mod;
    Entry entry;

    [[nodiscard]] uint32_t bit() const
    {
        return kOnceEntry + static_cast<uint32_t>( entry );
    }

    [[nodiscard]] const char *name() const
    {
        return kEntryNames[ static_cast<uint8_t>( entry ) ];
    }

    bool refuse( const char *format, ... ) const
    {
        if ( logOnce( mod != nullptr ? mod : &nullMod, bit() ) )
        {
            char reason[ 512 ];
            va_list args;
            va_start( args, format );
            vsnprintf( reason, sizeof( reason ), format, args );
            va_end( args );
            writeLog( mod, WOLF_LOG_WARN, "%s refused: %s", name(), reason );
        }
        return false;
    }

    [[nodiscard]] bool hasMod() const
    {
        return mod != nullptr || refuse( "mod is null" );
    }

    // The mod is not null and not refused. A refused mod's call fails without a line.
    [[nodiscard]] bool live() const
    {
        return hasMod() && !mod->refused.load( std::memory_order_acquire );
    }

    template <typename T>
    [[nodiscard]] bool present( T *pointer, const char *name ) const
    {
        return pointer != nullptr || refuse( "%s is null", name );
    }

    // A data pointer may be null only when its length is 0.
    [[nodiscard]] bool sized( const void *data, size_t n, const char *name ) const
    {
        return n == 0 || present( data, name );
    }

    [[nodiscard]] bool validLifetime( int32_t lifetime ) const
    {
        return ( lifetime >= WOLF_LIFETIME_PROCESS && lifetime <= WOLF_LIFETIME_FRAME ) || refuse( "lifetime %d is not 0 to 3", lifetime );
    }

    // A lifetime a registration can live in: the frame lifetime ends at the boundary that would apply one.
    [[nodiscard]] bool registration( int32_t lifetime ) const
    {
        return validLifetime( lifetime ) && ( lifetime != WOLF_LIFETIME_FRAME || refuse( "the frame lifetime cannot hold a registration" ) );
    }

    [[nodiscard]] bool powerOfTwo( size_t align ) const
    {
        return ( align != 0 && ( align & ( align - 1 ) ) == 0 ) || refuse( "align %zu is not a power of two", align );
    }

    // A hook's detour and original are present, and original is 8-byte aligned for the runtime's atomic store into it.
    [[nodiscard]] bool hookable( void *detour, void **original ) const
    {
        return present( detour, "detour" ) && present( original, "original" ) &&
               ( ( reinterpret_cast<uintptr_t>( original ) & 7U ) == 0 || refuse( "original %p is not 8-byte aligned", static_cast<void *>( original ) ) );
    }

    [[nodiscard]] bool readable( const void *addr, size_t n ) const
    {
        return memoryReadable( addr, n ) || refuse( "%p (%zu bytes) is not readable memory", addr, n );
    }

    [[nodiscard]] bool flag( uint32_t group, uint32_t bit ) const
    {
        return ( group < 80 && bit < 256 ) || refuse( "group %u bit %u is outside groups 0 to 79 and bits 0 to 255", group, bit );
    }

    // For a live mod, logs INFO "<entry> is not built in this runtime" once per mod.
    void unbuilt() const
    {
        if ( live() && logOnce( mod, bit() ) )
        {
            writeLog( mod, WOLF_LOG_INFO, "%s is not built in this runtime", name() );
        }
    }
};

void apiLog( WolfMod *mod, int32_t level, const char *msg )
{
    const Call call{ .mod = mod, .entry = Entry::Log };
    if ( call.hasMod() && call.present( msg, "msg" ) )
    {
        writeLog( mod, level, "%s", msg );
    }
}

void *apiAlloc( WolfMod *mod, int32_t lifetime, size_t n, size_t align )
{
    const Call call{ .mod = mod, .entry = Entry::Alloc };
    if ( !call.live() || !call.validLifetime( lifetime ) || !call.powerOfTwo( align ) )
    {
        return nullptr;
    }
    return lifetimeAlloc( mod, lifetime, n, align );
}

int32_t apiOnReset( WolfMod *mod, int32_t lifetime, WolfFinalizer fn, void *data )
{
    const Call call{ .mod = mod, .entry = Entry::OnReset };
    if ( !call.live() || !call.validLifetime( lifetime ) || !call.present( fn, "fn" ) )
    {
        return 0;
    }
    return lifetimeOnReset( mod, lifetime, fn, data );
}

WolfHandle apiOn( WolfMod *mod, const char *site, WolfCallback cb, void *user, int32_t lifetime )
{
    const Call call{ .mod = mod, .entry = Entry::On };
    if ( !call.live() || !call.present( site, "site" ) || !call.present( cb, "cb" ) || !call.registration( lifetime ) )
    {
        return 0;
    }
    if ( !siteNamed( site ) )
    {
        call.refuse( "no site named %s", site );
        return 0;
    }
    return siteOn( mod, site, cb, user, lifetime );
}

WolfHandle apiHook( WolfMod *mod, const char *name, void *detour, void **original )
{
    const Call call{ .mod = mod, .entry = Entry::Hook };
    if ( !call.live() || !call.present( name, "name" ) || !call.hookable( detour, original ) )
    {
        return 0;
    }
    void *target = symbolAddress( name );
    if ( target == nullptr )
    {
        call.refuse( "%s did not resolve", name );
        return 0;
    }
    return hookAdd( mod, target, detour, original );
}

WolfHandle apiHookRaw( WolfMod *mod, const char *module, uintptr_t rva, void *detour, void **original )
{
    const Call call{ .mod = mod, .entry = Entry::HookRaw };
    if ( !call.live() || !call.present( module, "module" ) || !call.hookable( detour, original ) )
    {
        return 0;
    }
    const auto offset = static_cast<unsigned long long>( rva );
    void *target = rawTarget( module, rva );
    if ( target == nullptr )
    {
        call.refuse( "%s+0x%llX is not inside a loaded module", module, offset );
        return 0;
    }
    const WolfHandle handle = hookAdd( mod, target, detour, original );
    if ( handle != 0 )
    {
        writeLog( mod, WOLF_LOG_WARN, "hookRaw on %s+0x%llX is outside the symbol table and may move in another game build", module, offset );
    }
    return handle;
}

WolfHandle apiWatch( WolfMod *mod, const void *addr, size_t n, WolfCallback cb, void *user, int32_t lifetime )
{
    const Call call{ .mod = mod, .entry = Entry::Watch };
    if ( !call.live() || !call.present( addr, "addr" ) || !call.present( cb, "cb" ) )
    {
        return 0;
    }
    constexpr size_t kMaxWatch = 65536;
    if ( n < 1 || n > kMaxWatch )
    {
        call.refuse( "watch size %zu is not 1 to 65536", n );
        return 0;
    }
    if ( !call.registration( lifetime ) || !call.readable( addr, n ) )
    {
        return 0;
    }
    return watchAdd( mod, addr, n, cb, user, lifetime );
}

void apiRemove( WolfMod *mod, WolfHandle h )
{
    const Call call{ .mod = mod, .entry = Entry::Remove };
    if ( call.live() )
    {
        registryRemove( mod, h );
    }
}

int32_t apiWrite( WolfMod *mod, void *addr, const void *src, size_t n )
{
    const Call call{ .mod = mod, .entry = Entry::Write };
    if ( !call.live() || !call.present( addr, "addr" ) || !call.sized( src, n, "src" ) )
    {
        return 0;
    }
    if ( memoryWrite( addr, src, n ) == 0 )
    {
        call.refuse( "%p (%zu bytes) could not be written", addr, n );
        return 0;
    }
    return 1;
}

WolfHandle apiPatch( WolfMod *mod, void *addr, const void *src, size_t n, int32_t lifetime )
{
    const Call call{ .mod = mod, .entry = Entry::Patch };
    if ( !call.live() || !call.present( addr, "addr" ) || !call.sized( src, n, "src" ) || !call.registration( lifetime ) || !call.readable( addr, n ) )
    {
        return 0;
    }
    return patchAdd( mod, addr, src, n, lifetime );
}

// An unknown name returns null with no line, so a mod can probe for a symbol.
void *apiResolve( WolfMod *mod, const char *name )
{
    const Call call{ .mod = mod, .entry = Entry::Resolve };
    if ( !call.live() || !call.present( name, "name" ) )
    {
        return nullptr;
    }
    return symbolAddress( name );
}

void apiFlagSet( WolfMod *mod, uint32_t group, uint32_t bit )
{
    const Call call{ .mod = mod, .entry = Entry::FlagSet };
    if ( call.live() && call.flag( group, bit ) )
    {
        flagSet( mod, group, bit );
    }
}

void apiFlagClear( WolfMod *mod, uint32_t group, uint32_t bit )
{
    const Call call{ .mod = mod, .entry = Entry::FlagClear };
    if ( call.live() && call.flag( group, bit ) )
    {
        flagClear( mod, group, bit );
    }
}

int32_t apiFlagTest( WolfMod *mod, uint32_t group, uint32_t bit )
{
    const Call call{ .mod = mod, .entry = Entry::FlagTest };
    return call.live() && call.flag( group, bit ) ? flagTest( mod, group, bit ) : 0;
}

int32_t apiGiveItem( WolfMod *mod, int32_t itemId, int32_t delta )
{
    const Call call{ .mod = mod, .entry = Entry::GiveItem };
    if ( !call.live() )
    {
        return 0;
    }
    if ( itemId < 0 || itemId > 255 )
    {
        call.refuse( "item id %d is not 0 to 255", itemId );
        return 0;
    }
    return giveItem( mod, itemId, delta );
}

// The config getters return the fallback for a refused mod, as for any refusal.
int64_t apiConfigInt( WolfMod *mod, const char *key, int64_t fallback )
{
    const Call call{ .mod = mod, .entry = Entry::ConfigInt };
    return call.live() && call.present( key, "key" ) ? configInt( mod, key, fallback ) : fallback;
}

double apiConfigFloat( WolfMod *mod, const char *key, double fallback )
{
    const Call call{ .mod = mod, .entry = Entry::ConfigFloat };
    return call.live() && call.present( key, "key" ) ? configFloat( mod, key, fallback ) : fallback;
}

int32_t apiConfigBool( WolfMod *mod, const char *key, int32_t fallback )
{
    const Call call{ .mod = mod, .entry = Entry::ConfigBool };
    return call.live() && call.present( key, "key" ) ? configBool( mod, key, fallback ) : fallback;
}

const char *apiConfigStr( WolfMod *mod, const char *key, const char *fallback )
{
    const Call call{ .mod = mod, .entry = Entry::ConfigStr };
    return call.live() && call.present( key, "key" ) ? configStr( mod, key, fallback ) : fallback;
}

const char *apiModPath( WolfMod *mod )
{
    const Call call{ .mod = mod, .entry = Entry::ModPath };
    return call.hasMod() ? mod->path : nullptr;
}

int32_t apiRunInGame( WolfMod *mod, WolfRunFn fn, const void *data, uint32_t len )
{
    const Call call{ .mod = mod, .entry = Entry::RunInGame };
    if ( !call.live() || !call.present( fn, "fn" ) || !call.sized( data, len, "data" ) )
    {
        return 0;
    }
    if ( len > kRunMaxPayload )
    {
        call.refuse( "len %u is over 64", len );
        return 0;
    }
    return runInGame( mod, fn, data, len );
}

WolfHandle apiResourceOverride( WolfMod *mod, const char * /*path*/, const char * /*filePath*/, const void * /*bytes*/, size_t /*n*/, int32_t /*lifetime*/ )
{
    Call{ .mod = mod, .entry = Entry::ResourceOverride }.unbuilt();
    return 0;
}

void *apiGameAlloc( WolfMod *mod, size_t /*n*/ )
{
    Call{ .mod = mod, .entry = Entry::GameAlloc }.unbuilt();
    return nullptr;
}

WolfHandle apiProvide( WolfMod *mod, const char * /*slot*/, uint32_t /*key*/, const void * /*buffer*/, size_t /*size*/, int32_t /*lifetime*/ )
{
    Call{ .mod = mod, .entry = Entry::Provide }.unbuilt();
    return 0;
}

int32_t apiClaimId( WolfMod *mod, const char * /*space*/, const char * /*name*/, int32_t /*preferred*/ )
{
    Call{ .mod = mod, .entry = Entry::ClaimId }.unbuilt();
    return -1;
}

constexpr WolfApi kApi{
    .abiVersion = WOLF_ABI_VERSION,
    .size = sizeof( WolfApi ),
    .runtimeVersion = WOLF_VERSION_STRING,
    .log = &apiLog,
    .alloc = &apiAlloc,
    .onReset = &apiOnReset,
    .on = &apiOn,
    .hook = &apiHook,
    .hookRaw = &apiHookRaw,
    .watch = &apiWatch,
    .remove = &apiRemove,
    .write = &apiWrite,
    .patch = &apiPatch,
    .resolve = &apiResolve,
    .flagSet = &apiFlagSet,
    .flagClear = &apiFlagClear,
    .flagTest = &apiFlagTest,
    .giveItem = &apiGiveItem,
    .configInt = &apiConfigInt,
    .configFloat = &apiConfigFloat,
    .configBool = &apiConfigBool,
    .configStr = &apiConfigStr,
    .modPath = &apiModPath,
    .runInGame = &apiRunInGame,
    .resourceOverride = &apiResourceOverride,
    .gameAlloc = &apiGameAlloc,
    .provide = &apiProvide,
    .claimId = &apiClaimId,
};

} // namespace

const WolfApi *wolfApi()
{
    return &kApi;
}

} // namespace wolf
