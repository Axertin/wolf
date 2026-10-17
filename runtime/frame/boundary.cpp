#include "frame/boundary.hpp"

#include <wolf/wolf.h>

#include "arena/tier.hpp"
#include "core/threading.hpp"
#include "frame/run_queue.hpp"
#include "tables/registry.hpp"

namespace wolf {

namespace {

// The tier generation each tier was last retired at.
constinit uint32_t retired[ kTierCount ] = {};

// With the lock held: withdraws and frees the records of every room and session generation that ended since the last retire, room first.
void retireEnded()
{
    constexpr int32_t kEnding[] = { WOLF_TIER_ROOM, WOLF_TIER_SESSION };
    for ( const int32_t tier : kEnding )
    {
        if ( const uint32_t generation = tierGeneration( tier ); generation != retired[ tier ] )
        {
            retired[ tier ] = generation;
            registriesRetire( tier );
        }
    }
}

} // namespace

void frameBoundary()
{
    const DispatchScope scope( nullptr );
    // Room data may point into session data, so room goes first.
    WaitingArenas room = {};
    WaitingArenas session = {};
    {
        const RuntimeLock lock;
        retireEnded();
        room = tierWaiting( WOLF_TIER_ROOM );
        session = tierWaiting( WOLF_TIER_SESSION );
    }
    tierCleanup( room );
    tierCleanup( session );
    applyPending();
    tierResetFrame();
    runQueued();
}

void warmStartBoundary()
{
    const DispatchScope scope( nullptr );
    {
        const RuntimeLock lock;
        retireEnded();
    }
    applyPending();
    runQueued();
}

} // namespace wolf
