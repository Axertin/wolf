#include "arena/lifetime.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>

#include "core/threading.hpp"
#include "log/log.hpp"

namespace {

constexpr size_t MiB = size_t{ 1024 } * 1024;
constexpr uint64_t kFirstWarn = 16 * MiB;
constexpr uint32_t kLifetimeRing = 3; // arenas that session and room each rotate through

struct Finalizer // pushed into the arena it finalizes, so it dies with it
{
    Finalizer *prev = nullptr;
    WolfFinalizer fn = nullptr;
    void *data = nullptr;
    WolfMod *mod = nullptr;
};

struct LifetimeArena
{
    wolf::Arena arena = {};
    Finalizer *finalizers = nullptr; // newest first
    uint64_t handedOut = 0;          // bytes pushed since this arena's last reset
};

// Session and room take their arenas in ring order, so the arenas holding ended generations are the ones just behind current, newest nearest. Frame alternates
// between arenas[0] and arenas[1]; process uses arenas[0] only. Everything but generation is read and written under the runtime lock; dispatch reads generation
// unlocked.
struct LifetimeState
{
    LifetimeArena arenas[ kLifetimeRing ] = {};
    std::atomic<uint32_t> generation{ 0 };
    uint32_t current = 0;  // index of the arena taking new allocations
    uint32_t waiting = 0;  // bit i: arenas[i] holds an ended generation
    uint64_t nextWarn = 0; // 0 means 16 MiB; one threshold for the lifetime, kept across resets and ring arenas
};

// Every arena reserves the same range; keep is what a reset leaves committed, sized for the lifetime's usual load.
constexpr size_t kReserve = size_t{ 64 } * 1024 * MiB;

constexpr wolf::Arena arenaOf( size_t keep )
{
    return { .reserve = kReserve, .keep = keep };
}

constinit LifetimeState states[ wolf::kLifetimeCount ] = {
    { .arenas = { { .arena = arenaOf( 4 * MiB ) }, {}, {} } },
    { .arenas = { { .arena = arenaOf( 4 * MiB ) }, { .arena = arenaOf( 4 * MiB ) }, { .arena = arenaOf( 4 * MiB ) } } },
    { .arenas = { { .arena = arenaOf( 4 * MiB ) }, { .arena = arenaOf( 4 * MiB ) }, { .arena = arenaOf( 4 * MiB ) } } },
    { .arenas = { { .arena = arenaOf( 1 * MiB ) }, { .arena = arenaOf( 1 * MiB ) }, {} } },
};

// With the lock held: marks the current arena waiting, moves to the next arena of the ring and starts a new generation. When the next arena still waits as well
// (three ends before one cleanup), the current arena is kept and the generation that ends here is cleaned up together with the new one. So after room, room and
// session ends before one boundary, the third room generation's memory and finalizers outlive the session teardown. The first merge logs once per process.
// Waiting bits clear only in lifetimeCleanup's last hold, so an end while an arena is being cleaned merges instead of reusing that arena.
bool mergeLogged = false; // runtime lock

void endLocked( int32_t lifetime )
{
    wolf::assertLocked();
    LifetimeState &state = states[ lifetime ];
    const uint32_t next = ( state.current + 1 ) % kLifetimeRing;
    if ( ( state.waiting & ( 1U << next ) ) == 0 )
    {
        state.waiting |= 1U << state.current;
        state.current = next;
    }
    else if ( !mergeLogged )
    {
        mergeLogged = true;
        wolf::writeLog( nullptr, WOLF_LOG_WARN, "%s lifetime merged an ended generation into the current arena", wolf::lifetimeName( lifetime ) );
    }
    state.generation.fetch_add( 1 );
}

// Runs a taken finalizer list newest first, each as its mod's dispatch, skipping refused mods. Unlocked.
void runFinalizers( const Finalizer *head )
{
    for ( ; head != nullptr; head = head->prev )
    {
        if ( head->mod->refused.load( std::memory_order_acquire ) )
        {
            continue;
        }
        const wolf::DispatchScope scope( head->mod );
        head->fn( head->mod, head->data );
    }
}

} // namespace

namespace wolf {

// lifetimeAlloc for a caller that holds the runtime lock. Null when the push fails, which logs once per mod. When the arena's bytes since its reset reach
// the lifetime's threshold, logs the highest 16 MiB doubling they crossed, naming mod, and doubles past it.
void *lifetimeAllocLocked( WolfMod *mod, int32_t lifetime, size_t n, size_t align )
{
    assertLocked();
    LifetimeState &state = states[ lifetime ];
    LifetimeArena &arena = state.arenas[ state.current ];
    auto *p = static_cast<unsigned char *>( push( arena.arena, n, align ) );
    if ( p == nullptr )
    {
        if ( arena.arena.full )
        {
            if ( logOnce( mod, kOnceArenaFull ) )
            {
                writeLog( mod, WOLF_LOG_ERROR, "%s arena reached its %llu GiB reservation", lifetimeName( lifetime ),
                          static_cast<unsigned long long>( kReserve / ( 1024 * MiB ) ) );
            }
        }
        else if ( logOnce( mod, kOnceOutOfMemory ) )
        {
            writeLog( mod, WOLF_LOG_ERROR, "out of memory allocating %zu bytes in the %s lifetime", n, lifetimeName( lifetime ) );
        }
        return nullptr;
    }
    arena.handedOut += n;
    const uint64_t threshold = state.nextWarn != 0 ? state.nextWarn : kFirstWarn;
    if ( arena.handedOut >= threshold )
    {
        uint64_t crossed = threshold;
        while ( crossed * 2 <= arena.handedOut )
        {
            crossed *= 2;
        }
        state.nextWarn = crossed * 2;
        writeLog( mod, WOLF_LOG_WARN, "%s arena reached %llu MiB", lifetimeName( lifetime ), static_cast<unsigned long long>( crossed / MiB ) );
    }
    return p;
}

void *lifetimeAlloc( WolfMod *mod, int32_t lifetime, size_t n, size_t align )
{
    assert( lifetime >= WOLF_LIFETIME_PROCESS && lifetime <= WOLF_LIFETIME_FRAME );
    assert( align != 0 && ( align & ( align - 1 ) ) == 0 );
    const RuntimeLock lock;
    return lifetimeAllocLocked( mod, lifetime, n, align );
}

int32_t lifetimeOnReset( WolfMod *mod, int32_t lifetime, WolfFinalizer fn, void *data )
{
    assert( lifetime >= WOLF_LIFETIME_PROCESS && lifetime <= WOLF_LIFETIME_FRAME );
    assert( mod != nullptr && fn != nullptr );
    const RuntimeLock lock;
    auto *entry = static_cast<Finalizer *>( lifetimeAllocLocked( mod, lifetime, sizeof( Finalizer ), alignof( Finalizer ) ) );
    if ( entry == nullptr )
    {
        return 0;
    }
    LifetimeArena &arena = states[ lifetime ].arenas[ states[ lifetime ].current ];
    *entry = { .prev = arena.finalizers, .fn = fn, .data = data, .mod = mod };
    arena.finalizers = entry;
    return 1;
}

uint32_t lifetimeGeneration( int32_t lifetime )
{
    assert( lifetime >= WOLF_LIFETIME_PROCESS && lifetime <= WOLF_LIFETIME_FRAME );
    return states[ lifetime ].generation.load();
}

void lifetimeEnd( int32_t lifetime )
{
    assert( lifetime == WOLF_LIFETIME_SESSION || lifetime == WOLF_LIFETIME_ROOM );
    const DispatchScope scope( nullptr );
    const RuntimeLock lock;
    endLocked( lifetime );
    if ( lifetime == WOLF_LIFETIME_SESSION )
    {
        endLocked( WOLF_LIFETIME_ROOM );
    }
}

void lifetimeCleanup( WaitingArenas waiting )
{
    assertMainThread();
    LifetimeState &state = states[ waiting.lifetime ];
    // Unlocked: a waiting arena is never current, and every push reads current under the lock, so nothing else touches these arenas. Steps back through the
    // ring from the arena that ended last.
    for ( uint32_t back = 0; back < kLifetimeRing - 1; ++back )
    {
        const uint32_t index = ( waiting.newest + kLifetimeRing - back ) % kLifetimeRing;
        if ( ( waiting.mask & ( 1U << index ) ) == 0 )
        {
            continue;
        }
        LifetimeArena &arena = state.arenas[ index ];
        const Finalizer *head = arena.finalizers;
        arena.finalizers = nullptr;
        runFinalizers( head );
        reset( arena.arena );
        arena.handedOut = 0;
    }
    const RuntimeLock lock;
    state.waiting &= ~waiting.mask;
}

WaitingArenas lifetimeWaiting( int32_t lifetime )
{
    assertLocked();
    const LifetimeState &state = states[ lifetime ];
    return { .lifetime = lifetime, .mask = state.waiting, .newest = ( state.current + kLifetimeRing - 1 ) % kLifetimeRing };
}

void lifetimeResetFrame()
{
    assertMainThread();
    LifetimeState &state = states[ WOLF_LIFETIME_FRAME ];
    LifetimeArena *ended = nullptr;
    const Finalizer *head = nullptr;
    {
        const RuntimeLock lock;
        ended = &state.arenas[ state.current ];
        state.current ^= 1U;
        state.generation.fetch_add( 1 );
        head = ended->finalizers;
        ended->finalizers = nullptr;
    }
    // Unlocked: the ended arena stops being current in the hold above and becomes current again only at the next call, after this reset.
    runFinalizers( head );
    reset( ended->arena );
    ended->handedOut = 0;
}

Arena &processArena()
{
    return states[ WOLF_LIFETIME_PROCESS ].arenas[ 0 ].arena;
}

const char *processString( std::string_view text )
{
    auto *copy = static_cast<char *>( lifetimeAllocLocked( nullptr, WOLF_LIFETIME_PROCESS, text.size() + 1, 1 ) );
    if ( copy != nullptr )
    {
        std::ranges::copy( text, copy ); // the arena hands out zeroed memory, so the terminator is already there
    }
    return copy;
}

const wchar_t *processWideString( std::wstring_view text )
{
    auto *copy = static_cast<wchar_t *>( lifetimeAllocLocked( nullptr, WOLF_LIFETIME_PROCESS, ( text.size() + 1 ) * sizeof( wchar_t ), alignof( wchar_t ) ) );
    if ( copy != nullptr )
    {
        std::ranges::copy( text, copy );
    }
    return copy;
}

const char *lifetimeName( int32_t lifetime )
{
    assert( lifetime >= WOLF_LIFETIME_PROCESS && lifetime <= WOLF_LIFETIME_FRAME );
    constexpr const char *names[ kLifetimeCount ] = { "process", "session", "room", "frame" };
    return names[ lifetime ];
}

} // namespace wolf
