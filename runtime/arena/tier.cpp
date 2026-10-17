#include "arena/tier.hpp"

#include <atomic>
#include <cassert>

#include "core/threading.hpp"
#include "log/log.hpp"

namespace {

constexpr size_t MiB = size_t{ 1024 } * 1024;
constexpr uint64_t kFirstWarn = 16 * MiB;
constexpr uint32_t kTierRing = 3; // arenas that session and room each rotate through

struct Finalizer // pushed into the arena it finalizes, so it dies with it
{
    Finalizer *prev = nullptr;
    WolfFinalizer fn = nullptr;
    void *data = nullptr;
    WolfMod *mod = nullptr;
};

struct TierArena
{
    wolf::Arena arena = {};
    Finalizer *finalizers = nullptr; // newest first
    uint64_t handedOut = 0;          // bytes pushed since this arena's last reset
};

// Session and room take their arenas in ring order, so the arenas holding ended generations are the ones just behind current, newest nearest. Frame alternates
// between arenas[0] and arenas[1]; process uses arenas[0] only. Everything but generation is read and written under the runtime lock; dispatch reads generation
// unlocked.
struct TierState
{
    TierArena arenas[ kTierRing ] = {};
    std::atomic<uint32_t> generation{ 0 };
    uint32_t current = 0;  // index of the arena taking new allocations
    uint32_t waiting = 0;  // bit i: arenas[i] holds an ended generation
    uint64_t nextWarn = 0; // 0 means 16 MiB; one threshold for the tier, kept across resets and ring arenas
};

constinit TierState states[ wolf::kTierCount ] = {
    { .arenas = { { .arena = { .blockSize = 4 * MiB } }, {}, {} } },
    { .arenas = { { .arena = { .blockSize = 4 * MiB } }, { .arena = { .blockSize = 4 * MiB } }, { .arena = { .blockSize = 4 * MiB } } } },
    { .arenas = { { .arena = { .blockSize = 4 * MiB } }, { .arena = { .blockSize = 4 * MiB } }, { .arena = { .blockSize = 4 * MiB } } } },
    { .arenas = { { .arena = { .blockSize = 1 * MiB } }, { .arena = { .blockSize = 1 * MiB } }, {} } },
};

// With the lock held: pushes into the tier's current arena. Null when the push fails, which logs once per mod. When the arena's bytes since its reset reach the
// tier's threshold, logs the highest 16 MiB doubling they crossed, naming mod, and doubles past it.
void *pushLocked( WolfMod *mod, int32_t tier, size_t n, size_t align )
{
    wolf::assertLocked();
    TierState &state = states[ tier ];
    TierArena &arena = state.arenas[ state.current ];
    auto *p = static_cast<unsigned char *>( wolf::push( arena.arena, n, align ) );
    if ( p == nullptr )
    {
        if ( wolf::logOnce( mod, wolf::kOnceOutOfMemory ) )
        {
            wolf::writeLog( mod, WOLF_LOG_ERROR, "out of memory allocating %zu bytes in the %s tier", n, wolf::tierName( tier ) );
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
        wolf::writeLog( mod, WOLF_LOG_WARN, "%s tier arena reached %llu MiB", wolf::tierName( tier ), static_cast<unsigned long long>( crossed / MiB ) );
    }
    return p;
}

// With the lock held: marks the current arena waiting, moves to the next arena of the ring and starts a new generation. When the next arena still waits as well
// (three ends before one cleanup), the current arena is kept and the generation that ends here is cleaned up together with the new one. So after room, room and
// session ends before one boundary, the third room generation's memory and finalizers outlive the session teardown. The first merge logs once per process.
// Waiting bits clear only in tierCleanup's last hold, so an end while an arena is being cleaned merges instead of reusing that arena.
bool mergeLogged = false; // runtime lock

void endLocked( int32_t tier )
{
    wolf::assertLocked();
    TierState &state = states[ tier ];
    const uint32_t next = ( state.current + 1 ) % kTierRing;
    if ( ( state.waiting & ( 1U << next ) ) == 0 )
    {
        state.waiting |= 1U << state.current;
        state.current = next;
    }
    else if ( !mergeLogged )
    {
        mergeLogged = true;
        wolf::writeLog( nullptr, WOLF_LOG_WARN, "%s tier merged an ended generation into the current arena", wolf::tierName( tier ) );
    }
    state.generation.fetch_add( 1 );
}

// Runs a taken finalizer list newest first, each as its mod's dispatch. Unlocked.
void runFinalizers( const Finalizer *head )
{
    for ( ; head != nullptr; head = head->prev )
    {
        const wolf::DispatchScope scope( head->mod );
        head->fn( head->mod, head->data );
    }
}

} // namespace

namespace wolf {

void *tierAlloc( WolfMod *mod, int32_t tier, size_t n, size_t align )
{
    assert( tier >= WOLF_TIER_PROCESS && tier <= WOLF_TIER_FRAME );
    assert( align != 0 && ( align & ( align - 1 ) ) == 0 );
    const RuntimeLock lock;
    return pushLocked( mod, tier, n, align );
}

int32_t tierOnReset( WolfMod *mod, int32_t tier, WolfFinalizer fn, void *data )
{
    assert( tier >= WOLF_TIER_PROCESS && tier <= WOLF_TIER_FRAME );
    assert( fn != nullptr );
    const RuntimeLock lock;
    auto *entry = static_cast<Finalizer *>( pushLocked( mod, tier, sizeof( Finalizer ), alignof( Finalizer ) ) );
    if ( entry == nullptr )
    {
        return 0;
    }
    TierArena &arena = states[ tier ].arenas[ states[ tier ].current ];
    *entry = { .prev = arena.finalizers, .fn = fn, .data = data, .mod = mod };
    arena.finalizers = entry;
    return 1;
}

uint32_t tierGeneration( int32_t tier )
{
    assert( tier >= WOLF_TIER_PROCESS && tier <= WOLF_TIER_FRAME );
    return states[ tier ].generation.load();
}

void tierEnd( int32_t tier )
{
    assert( tier == WOLF_TIER_SESSION || tier == WOLF_TIER_ROOM );
    const DispatchScope scope( nullptr );
    const RuntimeLock lock;
    endLocked( tier );
    if ( tier == WOLF_TIER_SESSION )
    {
        endLocked( WOLF_TIER_ROOM );
    }
}

void tierCleanup( WaitingArenas waiting )
{
    TierState &state = states[ waiting.tier ];
    // Unlocked: a waiting arena is never current, and every push reads current under the lock, so nothing else touches these arenas. Steps back through the
    // ring from the arena that ended last.
    for ( uint32_t back = 0; back < kTierRing - 1; ++back )
    {
        const uint32_t index = ( waiting.newest + kTierRing - back ) % kTierRing;
        if ( ( waiting.mask & ( 1U << index ) ) == 0 )
        {
            continue;
        }
        TierArena &arena = state.arenas[ index ];
        const Finalizer *head = arena.finalizers;
        arena.finalizers = nullptr;
        runFinalizers( head );
        reset( arena.arena );
        arena.handedOut = 0;
    }
    const RuntimeLock lock;
    state.waiting &= ~waiting.mask;
}

WaitingArenas tierWaiting( int32_t tier )
{
    assertLocked();
    const TierState &state = states[ tier ];
    return { .tier = tier, .mask = state.waiting, .newest = ( state.current + kTierRing - 1 ) % kTierRing };
}

void tierResetFrame()
{
    TierState &state = states[ WOLF_TIER_FRAME ];
    TierArena *ended = nullptr;
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
    return states[ WOLF_TIER_PROCESS ].arenas[ 0 ].arena;
}

const char *tierName( int32_t tier )
{
    assert( tier >= WOLF_TIER_PROCESS && tier <= WOLF_TIER_FRAME );
    constexpr const char *names[ kTierCount ] = { "process", "session", "room", "frame" };
    return names[ tier ];
}

} // namespace wolf
