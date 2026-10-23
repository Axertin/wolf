// WOLF C++ binding: typed wrappers over the C table in <wolf/wolf.h>, for C++20 and later, with exceptions on or off.
//
// A mod defines bool init() and writes WOLF_MOD_ENTRY( init ) once in one source file. Every wrapper then acts for that mod; a DLL holds one mod. The
// wrappers work from init on: a static initialiser runs before WOLF_MOD_ENTRY binds the mod, so it calls none of them.
//
//     bool init()
//     {
//         wolf::on<wolf::site::Tick>( []( WolfTickCtx &tick ) { wolf::log( WOLF_LOG_INFO, "frame %llu", tick.frame ); } );
//         return true;
//     }
//     WOLF_MOD_ENTRY( init )
//
// Lifetimes default to process. A callback that captures state has its closure placed in the lifetime it is registered in, so the captures last as long
// as the callback, and a capture with a destructor is destroyed when that lifetime ends. A captureless callback allocates nothing.
//
// Threads: from a thread outside game context, register and allocate only in the process lifetime, or do the work inside runInGame.
//
// Load order: the mod later in load order wins. For hooks the winning mod runs first: its detour sits outermost, decides whether the inner chain runs and
// can rewrite what it returns. For site callbacks the winning mod runs last and has the final word on writable fields.
//
// Exceptions: one that leaves a site callback, a watch callback or a runInGame call is caught and logged, and the next callback still runs. One that leaves a
// hook detour ends the process through WOLF's crash reporter, on whatever thread the detour runs. Destructors are noexcept, so a destructor that throws ends
// the process too, including a capture's destructor at the end of its lifetime. MSVC keeps terminate handlers per thread, so WOLF_MOD_ENTRY installs the one
// that reports to the crash reporter on the thread that runs init, and every callback and finalizer the binding wraps installs it on its own thread the first
// time it runs there. A thread the mod starts itself catches its own exceptions.
#pragma once

#include <wolf/sites.h>
#include <wolf/wolf.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <new>
#include <type_traits>
#include <utility>

#if defined( __cpp_exceptions ) || defined( _CPPUNWIND )
#define WOLF_BINDING_EXCEPTIONS 1
#else
#define WOLF_BINDING_EXCEPTIONS 0
#endif

#if defined( __GNUC__ ) || defined( __clang__ )
#define WOLF_BINDING_PRINTF __attribute__( ( format( printf, 2, 3 ) ) )
#else
#define WOLF_BINDING_PRINTF
#endif

namespace wolf {

// int32_t, as the C table's lifetime arguments are.
// NOLINTNEXTLINE(performance-enum-size)
enum class Lifetime : int32_t { Process = WOLF_LIFETIME_PROCESS, Session = WOLF_LIFETIME_SESSION, Room = WOLF_LIFETIME_ROOM, Frame = WOLF_LIFETIME_FRAME };

namespace detail {

// Set once by bind, before init runs.
inline const WolfApi *boundApi = nullptr;
inline WolfMod *boundMod = nullptr;

} // namespace detail

// Binds the table and mod every wrapper uses. WOLF_MOD_ENTRY calls it.
inline void bind( const WolfApi *table, WolfMod *mod )
{
    detail::boundApi = table;
    detail::boundMod = mod;
}

inline const WolfApi &api()
{
    return *detail::boundApi;
}

inline WolfMod *self()
{
    return detail::boundMod;
}

// printf-style; the line is cut at 1023 bytes.
WOLF_BINDING_PRINTF inline void log( WolfLevel level, const char *format, ... )
{
    char line[ 1024 ];
    va_list args;
    va_start( args, format );
    std::vsnprintf( line, sizeof( line ), format, args );
    va_end( args );
    api().log( self(), level, line );
}

namespace detail {

constexpr DWORD kTerminateCode = 0xE0574F4C;

// Reports an escaped C++ exception to WOLF's crash filter as 0xE0574F4C, then ends the process with that code. It is the mod's terminate handler, and a hook
// detour calls it when an exception leaves the mod's function.
[[noreturn]] inline void reportTerminate()
{
    CONTEXT context{};
    RtlCaptureContext( &context );
    EXCEPTION_RECORD record{};
    record.ExceptionCode = kTerminateCode;
    record.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
    record.ExceptionAddress = reinterpret_cast<void *>( &reportTerminate ); // the crash line names the mod's DLL
    EXCEPTION_POINTERS pointers{ .ExceptionRecord = &record, .ContextRecord = &context };
    UnhandledExceptionFilter( &pointers );
    TerminateProcess( GetCurrentProcess(), kTerminateCode );
    for ( ;; ) // TerminateProcess does not return for the current process
    {
        Sleep( INFINITE );
    }
}

// Installs reportTerminate as this thread's terminate handler the first time a trampoline runs on the thread.
inline void coverThread()
{
#if WOLF_BINDING_EXCEPTIONS
    thread_local bool covered = false;
    if ( !covered )
    {
        covered = true;
        std::set_terminate( &reportTerminate );
    }
#endif
}

// Runs f, catching any exception and logging it as escaping a callback of where.
template <class F>
void guarded( [[maybe_unused]] const char *where, F &&f )
{
    coverThread();
#if WOLF_BINDING_EXCEPTIONS
    try
    {
        f();
    }
    catch ( const std::exception &e )
    {
        log( WOLF_LOG_ERROR, "exception escaped a %s callback: %s", where, e.what() );
    }
    catch ( ... )
    {
        log( WOLF_LOG_ERROR, "exception escaped a %s callback: unknown", where );
    }
#else
    f();
#endif
}

template <class T>
void destroy( WolfMod * /*mod*/, void *data )
{
    coverThread();
    static_cast<T *>( data )->~T();
}

// Zeroed memory is a valid object of these types without a constructor running.
template <class T>
inline constexpr bool kImplicitLifetime =
    std::is_trivially_destructible_v<T> &&
    ( std::is_trivially_default_constructible_v<T> || std::is_trivially_copy_constructible_v<T> || std::is_trivially_move_constructible_v<T> );

// A functor the trampoline default-constructs, so registering it allocates nothing.
template <class F>
inline constexpr bool kStateless = std::is_empty_v<F> && std::is_default_constructible_v<F>;

} // namespace detail

// count zeroed Ts in lifetime, with no constructor run; null when the allocation fails. Freed when the lifetime ends. Outside game context, only the process
// lifetime.
template <class T>
T *alloc( Lifetime lifetime = Lifetime::Process, size_t count = 1 )
{
    static_assert( detail::kImplicitLifetime<T>, "alloc hands out zeroed memory with no constructor run; use make for this type" );
    if ( count > SIZE_MAX / sizeof( T ) )
    {
        return nullptr;
    }
    return static_cast<T *>( api().alloc( self(), static_cast<int32_t>( lifetime ), sizeof( T ) * count, alignof( T ) ) );
}

// A T constructed from args in lifetime; null when the allocation fails. A non-trivial destructor runs when the lifetime ends, which for the process lifetime
// is never, so a process-lifetime T registers no finalizer. Outside game context, only the process lifetime.
template <class T, class... Args>
T *make( Lifetime lifetime, Args &&...args )
{
    // NOLINTNEXTLINE(misc-const-correctness): placement new and the caller both need a T *
    T *object = static_cast<T *>( api().alloc( self(), static_cast<int32_t>( lifetime ), sizeof( T ), alignof( T ) ) );
    if ( object == nullptr )
    {
        return nullptr;
    }
    ::new ( static_cast<void *>( object ) ) T( std::forward<Args>( args )... );
    if constexpr ( !std::is_trivially_destructible_v<T> )
    {
        // Without its finalizer the object would never be destroyed, so it is destroyed now instead.
        if ( lifetime != Lifetime::Process && api().onReset( self(), static_cast<int32_t>( lifetime ), &detail::destroy<T>, object ) == 0 )
        {
            object->~T();
            return nullptr;
        }
    }
    return object;
}

namespace detail {

// The user pointer of a callback f registered in lifetime: null for a stateless f, else f placed with make. false when f cannot be placed.
template <class F>
bool placeUser( F &&f, Lifetime lifetime, void *&user )
{
    using T = std::remove_cvref_t<F>;
    if constexpr ( kStateless<T> )
    {
        user = nullptr;
        return true;
    }
    else
    {
        user = make<T>( lifetime, std::forward<F>( f ) );
        return user != nullptr;
    }
}

} // namespace detail

// The sites in <wolf/sites.h>, each with its name and the context its callbacks receive.
namespace site {

struct SessionStart
{
    static constexpr const char *name = WOLF_SITE_SESSION_START;
    using Ctx = WolfSessionStartCtx;
};

struct SessionEnd
{
    static constexpr const char *name = WOLF_SITE_SESSION_END;
    using Ctx = WolfSessionEndCtx;
};

struct Tick
{
    static constexpr const char *name = WOLF_SITE_TICK;
    using Ctx = WolfTickCtx;
};

struct TickPost
{
    static constexpr const char *name = WOLF_SITE_TICK_POST;
    using Ctx = WolfTickCtx;
};

struct RoomChange
{
    static constexpr const char *name = WOLF_SITE_ROOM_CHANGE;
    using Ctx = WolfRoomChangeCtx;
};

struct ItemAdd
{
    static constexpr const char *name = WOLF_SITE_ITEM_ADD;
    using Ctx = WolfItemAddCtx;
};

} // namespace site

namespace detail {

template <class Site, class F>
void siteCall( WolfMod * /*mod*/, void *ctx, void *user )
{
    auto &context = *static_cast<typename Site::Ctx *>( ctx );
    guarded( Site::name, [ & ] {
        if constexpr ( kStateless<F> )
        {
            F{}( context );
        }
        else
        {
            ( *static_cast<F *>( user ) )( context );
        }
    } );
}

} // namespace detail

// Runs f( typename Site::Ctx & ) on every Site event from the next frame on, until lifetime ends or remove. 0 when refused, which logs. Outside game
// context, only the process lifetime.
template <class Site, class F>
WolfHandle on( F f, Lifetime lifetime = Lifetime::Process )
{
    static_assert( std::is_invocable_v<F &, typename Site::Ctx &>, "an on<Site> callback takes Site::Ctx &, such as WolfTickCtx & for site::Tick" );
    void *user = nullptr;
    if ( !detail::placeUser( std::move( f ), lifetime, user ) )
    {
        return 0;
    }
    return api().on( self(), Site::name, &detail::siteCall<Site, F>, user, static_cast<int32_t>( lifetime ) );
}

namespace detail {

template <class F>
void watchCall( WolfMod * /*mod*/, void *ctx, void *user )
{
    const auto &context = *static_cast<const WolfWatchCtx *>( ctx );
    guarded( "watch", [ & ] {
        if constexpr ( kStateless<F> )
        {
            F{}( context );
        }
        else
        {
            ( *static_cast<F *>( user ) )( context );
        }
    } );
}

} // namespace detail

// Runs f( const WolfWatchCtx & ) at the frame boundary, on the main thread, for each frame in which the n bytes at addr (1 to 65536) differ from the last
// boundary's, with the earlier bytes in previous. Until lifetime ends or remove. 0 when refused, which logs. Outside game context, only the process lifetime.
template <class F>
WolfHandle watch( const void *addr, size_t n, F f, Lifetime lifetime = Lifetime::Process )
{
    static_assert( std::is_invocable_v<F &, const WolfWatchCtx &>, "a watch callback takes const WolfWatchCtx &" );
    void *user = nullptr;
    if ( !detail::placeUser( std::move( f ), lifetime, user ) )
    {
        return 0;
    }
    return api().watch( self(), addr, n, &detail::watchCall<F>, user, static_cast<int32_t>( lifetime ) );
}

namespace detail {

// One hook per ( Fn, F ): the detour is a plain function, so it finds the original and the closure here.
template <class Fn, class F>
struct HookSlot;

template <class R, class... Args, class F>
struct HookSlot<R( Args... ), F>
{
    static inline R ( *original )( Args... ) = nullptr;
    static inline F *closure = nullptr;            // a capturing F's one closure
    static inline std::atomic<bool> held{ false }; // a capturing F's slot is taken

    static R call( Args... args )
    {
        if constexpr ( kStateless<F> )
        {
            return F{}( original, std::forward<Args>( args )... );
        }
        else
        {
            return ( *closure )( original, std::forward<Args>( args )... );
        }
    }

    // The detour may run on a thread whose terminate handler is not the mod's, so it covers the thread and reports an escaped exception itself.
    static R detour( Args... args ) noexcept
    {
        coverThread();
#if WOLF_BINDING_EXCEPTIONS
        try
        {
            return call( std::forward<Args>( args )... );
        }
        catch ( ... )
        {
            reportTerminate();
        }
#else
        return call( std::forward<Args>( args )... );
#endif
    }

    // A captureless f always passes. A capturing f takes the slot for the process and is placed in the process lifetime; false when the slot is taken, which
    // logs naming target, or when f cannot be placed.
    static bool claim( const char *target, F &&f )
    {
        if constexpr ( !kStateless<F> )
        {
            if ( held.exchange( true ) )
            {
                log( WOLF_LOG_ERROR, "hook %s: a lambda with captures holds one hook for the process; keep the hook and test a flag inside it", target );
                return false;
            }
            closure = make<F>( Lifetime::Process, std::move( f ) );
            if ( closure == nullptr )
            {
                held = false;
                return false;
            }
        }
        return true;
    }

    // Frees a capturing F's slot when the table refused its hook.
    static WolfHandle settle( WolfHandle handle )
    {
        if constexpr ( !kStateless<F> )
        {
            if ( handle == 0 )
            {
                held = false;
            }
        }
        return handle;
    }

    static void *detourAddress()
    {
        return reinterpret_cast<void *>( &detour );
    }

    static void **originalAddress()
    {
        return reinterpret_cast<void **>( &original );
    }
};

template <class Fn, class F>
constexpr void checkHook()
{
    static_assert( std::is_function_v<Fn>, "hook<Fn> takes the hooked function's type, such as hook<int( int )>" );
    static_assert( std::is_class_v<F>, "pass a lambda to hook, a separate one for each hook" );
}

} // namespace detail

// Hooks the function the symbol table names, from the next frame on until remove. Fn is its type, such as int( int ), and f( Fn *original, args... )
// replaces it. A lambda with captures holds one hook for the process, so its closure never changes under a live link. A captureless one can hook the same
// target again after remove, and the runtime refuses it on a different target. The detour may run on any thread, so f touches only process-lifetime memory or
// uses runInGame.
template <class Fn, class F>
WolfHandle hook( const char *name, F f )
{
    detail::checkHook<Fn, F>();
    using Slot = detail::HookSlot<Fn, F>;
    if ( !Slot::claim( name, std::move( f ) ) )
    {
        return 0;
    }
    return Slot::settle( api().hook( self(), name, Slot::detourAddress(), Slot::originalAddress() ) );
}

// hook for the function at module base + rva, for one the symbol table does not name. Unstable: an RVA holds for one game build and may point elsewhere in
// another, so the runtime logs every hookRaw with its target.
template <class Fn, class F>
WolfHandle hookRaw( const char *module, uintptr_t rva, F f )
{
    detail::checkHook<Fn, F>();
    using Slot = detail::HookSlot<Fn, F>;
    char target[ MAX_PATH + 32 ]; // "<module>+0x<rva>", for the line a taken slot logs
    std::snprintf( target, sizeof( target ), "%s+0x%llX", module, static_cast<unsigned long long>( rva ) );
    if ( !Slot::claim( target, std::move( f ) ) )
    {
        return 0;
    }
    return Slot::settle( api().hookRaw( self(), module, rva, Slot::detourAddress(), Slot::originalAddress() ) );
}

// Withdraws a registration of any kind at the next frame boundary.
inline void remove( WolfHandle handle )
{
    api().remove( self(), handle );
}

// Copies n bytes from src to addr now, from any thread, through read-only and executable protection. false when it could not, which logs once; a failure
// partway through the range can leave its start written.
inline bool write( void *addr, const void *src, size_t n )
{
    return api().write( self(), addr, src, n ) != 0;
}

// Writes value over *addr now, as write( addr, &value, sizeof( T ) ) does.
template <class T>
bool write( T *addr, const std::type_identity_t<T> &value )
{
    static_assert( std::is_trivially_copyable_v<T>, "write copies value byte for byte, so T must be trivially copyable" );
    return write( static_cast<void *>( addr ), &value, sizeof( T ) );
}

// Copies n bytes of src now and writes them at addr at the next frame boundary; remove puts the original bytes back. When lifetime ends, image memory (code
// and constant data) gets its original bytes back and other memory keeps what the game rebuilt there. A patch overlapping a live one is not applied. 0 when
// refused, which logs. Outside game context, only the process lifetime. To change a patch, remove the old one before adding its replacement, so the two do not
// overlap.
inline WolfHandle patch( void *addr, const void *src, size_t n, Lifetime lifetime = Lifetime::Process )
{
    return api().patch( self(), addr, src, n, static_cast<int32_t>( lifetime ) );
}

// The address the symbol table gives name, as a T *; null when it does not resolve.
template <class T = void>
T *resolve( const char *name )
{
    return reinterpret_cast<T *>( api().resolve( self(), name ) );
}

// The game's progress flags, groups 0 to 79 and bits 0 to 255, from any thread through the game's own interlocked functions. Before the game has started,
// set and clear do nothing, test returns false, and the first such call logs once.
inline void flagSet( uint32_t group, uint32_t bit )
{
    api().flagSet( self(), group, bit );
}

inline void flagClear( uint32_t group, uint32_t bit )
{
    api().flagClear( self(), group, bit );
}

inline bool flagTest( uint32_t group, uint32_t bit )
{
    return api().flagTest( self(), group, bit ) != 0;
}

// Adds delta of item itemId (0 to 255) through the game's Item_Add, so item.add sees it with fromMod set to this mod. Inside a site or watch callback, a
// runInGame call or a finalizer once the game has started, it runs at once on that thread; anywhere else, hook detours included, it runs at the next frame
// boundary. false when refused or the queue cannot grow, which logs.
inline bool giveItem( int32_t itemId, int32_t delta = 1 )
{
    return api().giveItem( self(), itemId, delta ) != 0;
}

// Values from the mod's config.ini, or fallback when key is missing or does not parse.
inline int64_t configInt( const char *key, int64_t fallback )
{
    return api().configInt( self(), key, fallback );
}

inline double configFloat( const char *key, double fallback )
{
    return api().configFloat( self(), key, fallback );
}

inline bool configBool( const char *key, bool fallback )
{
    return api().configBool( self(), key, fallback ? 1 : 0 ) != 0;
}

inline const char *configStr( const char *key, const char *fallback )
{
    return api().configStr( self(), key, fallback );
}

// The mod's directory, absolute UTF-8 with a trailing separator, valid for the process.
inline const char *modPath()
{
    return api().modPath( self() );
}

namespace detail {

template <class F>
void runCall( WolfMod * /*mod*/, void *data, uint32_t /*len*/ )
{
    guarded( "runInGame", [ & ] { ( *static_cast<F *>( data ) )(); } );
}

} // namespace detail

// Runs a copy of f() in game context at the next frame boundary, from any thread. false when the queue cannot take it, which logs.
template <class F>
bool runInGame( F f )
{
    static_assert( std::is_trivially_copyable_v<F>, "runInGame copies its lambda byte for byte, so its captures must be trivially copyable" );
    static_assert( sizeof( F ) <= 64, "runInGame carries at most 64 bytes of captures" );
    static_assert( alignof( F ) <= 16, "runInGame's payload is aligned to 16" );
    return api().runInGame( self(), &detail::runCall<F>, &f, static_cast<uint32_t>( sizeof( F ) ) ) != 0;
}

namespace detail {

inline int32_t modInit( const WolfApi *table, WolfMod *mod, bool ( *init )() )
{
    const bool canLog = WOLF_HAS( table, log );
    if ( table->abiVersion != WOLF_ABI_VERSION )
    {
        if ( canLog )
        {
            char line[ 64 ];
            std::snprintf( line, sizeof( line ), "built for WOLF ABI %u, runtime has %u", static_cast<unsigned>( WOLF_ABI_VERSION ),
                           static_cast<unsigned>( table->abiVersion ) );
            table->log( mod, WOLF_LOG_ERROR, line );
        }
        return 1;
    }
    if ( table->size < sizeof( WolfApi ) )
    {
        if ( canLog )
        {
            table->log( mod, WOLF_LOG_ERROR, "built against a newer runtime table" );
        }
        return 1;
    }
    std::set_terminate( &reportTerminate );
    bind( table, mod );
#if WOLF_BINDING_EXCEPTIONS
    try
    {
        return init() ? 0 : 1;
    }
    catch ( const std::exception &e )
    {
        log( WOLF_LOG_ERROR, "wolf_mod_init threw: %s", e.what() );
    }
    catch ( ... )
    {
        log( WOLF_LOG_ERROR, "wolf_mod_init threw: unknown" );
    }
    return 1;
#else
    return init() ? 0 : 1;
#endif
}

} // namespace detail

} // namespace wolf

// Defines the mod's exported wolf_mod_init around bool initFunction(). It refuses the mod when the runtime's table is another ABI or shorter than the one the
// mod was built against, installs the terminate handler that reports an escaped C++ exception to WOLF's crash reporter, binds the mod and runs initFunction,
// which returns true to load the mod. An exception from initFunction is logged and refuses the mod.
#define WOLF_MOD_ENTRY( initFunction )                                                                                                                         \
    extern "C" __declspec( dllexport ) int32_t wolf_mod_init( const WolfApi *api, WolfMod *mod )                                                               \
    {                                                                                                                                                          \
        return ::wolf::detail::modInit( api, mod, &( initFunction ) );                                                                                         \
    }
