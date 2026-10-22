#include "frame/run_queue.hpp"

#include <cassert>
#include <cstring>

#include "core/threading.hpp"
#include "tables/table.hpp"

namespace wolf {

namespace {

struct RunEntry
{
    WolfMod *mod = nullptr;
    WolfRunFn fn = nullptr;
    uint32_t len = 0;
    alignas( 16 ) uint8_t data[ kRunMaxPayload ] = {};
};

constexpr uint32_t kExpectedCalls = 1024;

// One buffer takes calls while the other runs; runQueued swaps them.
constinit Table buffers[ 2 ] = {
    { .name = "run queue", .stride = sizeof( RunEntry ), .expected = kExpectedCalls, .onceBit = kOnceRunQueueFull },
    { .name = "run queue", .stride = sizeof( RunEntry ), .expected = kExpectedCalls, .onceBit = kOnceRunQueueFull },
};
constinit uint32_t active = 0; // the buffer taking calls; runtime lock

} // namespace

int32_t runInGame( WolfMod *mod, WolfRunFn fn, const void *data, uint32_t len )
{
    assert( fn != nullptr && len <= kRunMaxPayload && ( len == 0 || data != nullptr ) );
    const RuntimeLock lock;
    uint32_t index = 0;
    auto *entry = static_cast<RunEntry *>( tableAppend( buffers[ active ], mod, &index ) );
    if ( entry == nullptr )
    {
        return 0;
    }
    entry->mod = mod;
    entry->fn = fn;
    entry->len = len;
    if ( len != 0 )
    {
        std::memcpy( entry->data, data, len );
    }
    return 1;
}

void runQueued()
{
    assertMainThread();
    Table *taken = nullptr;
    {
        const RuntimeLock lock;
        taken = &buffers[ active ];
        active ^= 1U;
    }
    // Unlocked: runInGame appends only to the other buffer now.
    Table &running = *taken;
    const uint32_t end = tableEnd( running );
    for ( uint32_t i = 1; i < end; ++i )
    {
        auto *entry = static_cast<RunEntry *>( tableAt( running, i ) );
        const DispatchScope scope( entry->mod );
        entry->fn( entry->mod, entry->data, entry->len );
    }
    tableClear( running );
}

} // namespace wolf
