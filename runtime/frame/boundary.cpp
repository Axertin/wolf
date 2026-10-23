#include "frame/boundary.hpp"

#include <wolf/wolf.h>

#include "arena/lifetime.hpp"
#include "core/threading.hpp"
#include "frame/run_queue.hpp"
#include "hooks/hooks.hpp"
#include "memory/memory.hpp"
#include "tables/registry.hpp"

namespace wolf {

namespace {

// The lifetime generation each lifetime was last retired at.
constinit uint32_t retired[ kLifetimeCount ] = {};

// With the lock held: withdraws and frees the records of every room and session generation that ended since the last retire, room first.
void retireEnded()
{
    constexpr int32_t kEnding[] = { WOLF_LIFETIME_ROOM, WOLF_LIFETIME_SESSION };
    for ( const int32_t lifetime : kEnding )
    {
        if ( const uint32_t generation = lifetimeGeneration( lifetime ); generation != retired[ lifetime ] )
        {
            retired[ lifetime ] = generation;
            registriesRetire( lifetime );
        }
    }
}

} // namespace

void frameBoundary()
{
    assertMainThread();
    const DispatchScope scope( nullptr );
    // Room data may point into session data, so room goes first.
    WaitingArenas room = {};
    WaitingArenas session = {};
    {
        const RuntimeLock lock;
        retireEnded();
        room = lifetimeWaiting( WOLF_LIFETIME_ROOM );
        session = lifetimeWaiting( WOLF_LIFETIME_SESSION );
    }
    lifetimeCleanup( room );
    lifetimeCleanup( session );
    applyPending();
    hooksCommit();
    lifetimeResetFrame();
    runQueued();
    watchesCompare();
}

void warmStartBoundary()
{
    assertMainThread();
    const DispatchScope scope( nullptr );
    {
        const RuntimeLock lock;
        retireEnded();
    }
    applyPending();
    hooksCommit();
    runQueued();
}

} // namespace wolf
