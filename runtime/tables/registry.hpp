// Registries: handle-addressed records whose adds and removes queue on any
// thread and take effect on the main thread at applyPending.

#pragma once

#include <wolf/wolf.h>

#include <atomic>
#include <cstdint>

#include "mods/mod.hpp"
#include "tables/table.hpp"

namespace wolf {

// The kind field of a handle. Each kind is served by exactly one registry.
enum class Kind : uint8_t { None = 0, Subscription = 1, Hook = 2, Watch = 3, Patch = 4, ResourceOverride = 5, Provide = 6 };

// generation:8 | kind:4 | index:20
constexpr WolfHandle makeHandle( Kind kind, uint32_t index, uint8_t generation )
{
    return ( static_cast<uint32_t>( generation ) << 24 ) | ( static_cast<uint32_t>( kind ) << 20 ) | index;
}

constexpr uint32_t handleIndex( WolfHandle handle )
{
    return handle & ( kMaxEntries - 1 );
}

constexpr Kind handleKind( WolfHandle handle )
{
    return static_cast<Kind>( ( handle >> 20 ) & 0xFU );
}

constexpr uint8_t handleGeneration( WolfHandle handle )
{
    return static_cast<uint8_t>( handle >> 24 );
}

enum class RecordState : uint8_t { Free = 0, Pending = 1, Live = 2 };

// Header at the start of every registry entry; the kind's data follows at offset 24.
struct Record
{
    std::atomic<RecordState> state{ RecordState::Free };
    std::atomic<uint8_t> generation{ 0 }; // handle generation of this slot, advanced when it is freed; registryResolve reads it unlocked
    uint8_t lifetime = 0;
    bool removing = false;           // a remove op is queued
    uint32_t lifetimeGeneration = 0; // lifetime generation the registration was made in
    uint32_t nextFree = 0;
    WolfMod *mod = nullptr;
};
static_assert( sizeof( Record ) == 24 );

struct Registry;

// Called on the main thread with the runtime lock held. Never calls mod code.
using RegistryOp = void ( * )( Registry &registry, uint32_t index );

struct Registry
{
    Kind kind = Kind::None;
    uint32_t dataSize = 0;
    uint32_t expected = 0;         // used count past which the size warnings start
    RegistryOp apply = nullptr;    // makes a pending record take effect; may be null
    RegistryOp withdraw = nullptr; // undoes apply before the slot is freed; may be null
    uint32_t freeHead = 0;         // oldest freed slot, reused first so generation wraps spread over every freed slot
    uint32_t freeTail = 0;         // newest freed slot
    uint32_t used = 0;             // pending plus live records
    uint32_t nextWarn = 0;
    Table table = {}; // a definition sets its name; its expected stays 0, since the registry warns on used instead
};

// Sets the table's stride and once bit, and routes the kind's handles here. Unlocked: main thread at boot, before any add to the registry.
void registryInstall( Registry &registry );

// Reserves a slot, copies dataSize bytes from data into it and queues it for the next applyPending. Any thread. 0 for a full registry or pending list. lifetime
// is process, session or room; the C table checks it.
WolfHandle registryAdd( Registry &registry, WolfMod *mod, int32_t lifetime, const void *data );

// registryAdd for a caller that already holds the runtime lock, so work around the add happens in the same hold.
WolfHandle registryAddLocked( Registry &registry, WolfMod *mod, int32_t lifetime, const void *data );

// Queues removal. Ignores 0, stale handles, unknown kinds, another mod's handles and a handle already being removed. Any thread.
void registryRemove( WolfMod *mod, WolfHandle handle );

// Data of a pending or live record, or null. Main thread, or with the runtime lock held; the pointer stays valid until the next frame boundary.
void *registryResolve( WolfHandle handle );

Record &registryRecord( Registry &registry, uint32_t index );

void *registryData( Registry &registry, uint32_t index );

// Data when the record is live and its lifetime generation is current, else null. Dispatch loops use this from 1 to tableEnd(registry.table). Any game-context
// thread; the pointer stays valid until the next frame boundary.
void *registryLive( Registry &registry, uint32_t index );

// Main thread: applies queued adds and removes in order, then empties the list.
void applyPending();

// Main thread: withdraws and frees every record of the lifetime from a generation that has ended, highest index first in each registry. The caller holds the
// runtime lock.
void registriesRetire( int32_t lifetime );

} // namespace wolf
