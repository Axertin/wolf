#include "tables/registry.hpp"

#include <cassert>
#include <cstring>

#include "arena/lifetime.hpp"
#include "core/threading.hpp"
#include "log/log.hpp"

namespace wolf {

namespace {

struct PendingOp
{
    WolfHandle handle = 0;
    bool remove = false;
};

constinit Registry *registries[ 16 ] = {}; // indexed by kind
constinit Table pending{ .name = "pending", .stride = sizeof( PendingOp ), .onceBit = kOncePendingFull };

// The registry holding the handle's record when the handle names a pending or live record.
Registry *findRegistry( WolfHandle handle )
{
    Registry *registry = registries[ static_cast<uint8_t>( handleKind( handle ) ) ];
    const uint32_t index = handleIndex( handle );
    if ( registry == nullptr || index == 0 || index >= tableEnd( registry->table ) )
    {
        return nullptr;
    }
    const Record &record = registryRecord( *registry, index );
    if ( record.state.load( std::memory_order_acquire ) == RecordState::Free ||
         record.generation.load( std::memory_order_relaxed ) != handleGeneration( handle ) )
    {
        return nullptr;
    }
    return registry;
}

// With the lock held: withdraws a live record and frees its slot, which makes every handle to it stale.
void dropRecord( Registry &registry, uint32_t index )
{
    Record &record = registryRecord( registry, index );
    if ( record.state.load( std::memory_order_relaxed ) == RecordState::Live && registry.withdraw != nullptr )
    {
        registry.withdraw( registry, index );
    }
    record.state.store( RecordState::Free, std::memory_order_release );
    record.generation.fetch_add( 1, std::memory_order_relaxed );
    record.removing = false;
    record.nextFree = 0;
    if ( registry.freeTail != 0 )
    {
        registryRecord( registry, registry.freeTail ).nextFree = index;
    }
    else
    {
        registry.freeHead = index;
    }
    registry.freeTail = index;
    --registry.used;
}

} // namespace

void registryInstall( Registry &registry )
{
    registry.table.stride = ( static_cast<uint32_t>( sizeof( Record ) ) + registry.dataSize + 7U ) & ~7U;
    registry.table.onceBit = kOnceRegistryFull + static_cast<uint32_t>( registry.kind );
    registries[ static_cast<uint8_t>( registry.kind ) ] = &registry;
}

WolfHandle registryAdd( Registry &registry, WolfMod *mod, int32_t lifetime, const void *data )
{
    const RuntimeLock lock;
    return registryAddLocked( registry, mod, lifetime, data );
}

WolfHandle registryAddLocked( Registry &registry, WolfMod *mod, int32_t lifetime, const void *data )
{
    assert( lifetime >= WOLF_LIFETIME_PROCESS && lifetime < WOLF_LIFETIME_FRAME );
    assertLocked();
    Table &table = registry.table;
    // Past three quarters of the committed chunks a freed slot comes first. That includes every append that would need a new chunk or find the table full.
    uint32_t index = registry.freeHead;
    Record *record = nullptr;
    if ( index != 0 && tableEnd( table ) > ( table.chunkCount * kChunkEntries * 3 ) / 4 )
    {
        record = &registryRecord( registry, index );
        registry.freeHead = record->nextFree;
        if ( registry.freeHead == 0 )
        {
            registry.freeTail = 0;
        }
    }
    else
    {
        record = static_cast<Record *>( tableAppend( table, mod, &index ) );
        if ( record == nullptr )
        {
            return 0;
        }
    }
    ++registry.used;
    record->lifetime = static_cast<uint8_t>( lifetime );
    record->lifetimeGeneration = lifetimeGeneration( lifetime );
    record->mod = mod;
    std::memcpy( registryData( registry, index ), data, registry.dataSize );
    record->state.store( RecordState::Pending, std::memory_order_release );

    const WolfHandle handle = makeHandle( registry.kind, index, record->generation.load( std::memory_order_relaxed ) );
    uint32_t opIndex = 0;
    auto *op = static_cast<PendingOp *>( tableAppend( pending, mod, &opIndex ) );
    if ( op == nullptr )
    {
        dropRecord( registry, index );
        return 0;
    }
    op->handle = handle;
    warnPast( mod, registry.table.name, registry.expected, registry.nextWarn, registry.used );
    return handle;
}

void registryRemove( WolfMod *mod, WolfHandle handle )
{
    const RuntimeLock lock;
    Registry *registry = findRegistry( handle );
    if ( registry == nullptr )
    {
        return;
    }
    Record &record = registryRecord( *registry, handleIndex( handle ) );
    if ( record.mod != mod || record.removing )
    {
        return;
    }
    uint32_t opIndex = 0;
    auto *op = static_cast<PendingOp *>( tableAppend( pending, mod, &opIndex ) );
    if ( op == nullptr )
    {
        return;
    }
    op->handle = handle;
    op->remove = true;
    record.removing = true;
}

void *registryResolve( WolfHandle handle )
{
    Registry *registry = findRegistry( handle );
    return registry != nullptr ? registryData( *registry, handleIndex( handle ) ) : nullptr;
}

Record &registryRecord( Registry &registry, uint32_t index )
{
    return *static_cast<Record *>( tableAt( registry.table, index ) );
}

void *registryData( Registry &registry, uint32_t index )
{
    return static_cast<uint8_t *>( tableAt( registry.table, index ) ) + sizeof( Record );
}

void *registryLive( Registry &registry, uint32_t index )
{
    const Record &record = registryRecord( registry, index );
    if ( record.state.load( std::memory_order_acquire ) != RecordState::Live || record.lifetimeGeneration != lifetimeGeneration( record.lifetime ) )
    {
        return nullptr;
    }
    return registryData( registry, index );
}

void applyPending()
{
    assertMainThread();
    const RuntimeLock lock;
    for ( uint32_t i = 1; i < tableEnd( pending ); ++i )
    {
        const auto &op = *static_cast<const PendingOp *>( tableAt( pending, i ) );
        Registry *registry = findRegistry( op.handle );
        if ( registry == nullptr )
        {
            continue;
        }
        const uint32_t index = handleIndex( op.handle );
        Record &record = registryRecord( *registry, index );
        if ( record.removing && !op.remove )
        {
            continue; // the add waits for its remove op, which drops the record
        }
        // A refused mod's ops free their slots instead of applying.
        if ( op.remove || record.mod->refused.load( std::memory_order_acquire ) || record.lifetimeGeneration != lifetimeGeneration( record.lifetime ) )
        {
            dropRecord( *registry, index );
            continue;
        }
        if ( registry->apply != nullptr )
        {
            registry->apply( *registry, index );
        }
        record.state.store( RecordState::Live, std::memory_order_release );
    }
    tableClear( pending );
}

void registriesRetire( int32_t lifetime )
{
    assertLocked();
    const auto lifetimeId = static_cast<uint8_t>( lifetime );
    const uint32_t current = lifetimeGeneration( lifetime );
    for ( Registry *registry : registries )
    {
        if ( registry == nullptr )
        {
            continue;
        }
        for ( uint32_t index = tableEnd( registry->table ); index-- > 1; )
        {
            const Record &record = registryRecord( *registry, index );
            if ( record.state.load( std::memory_order_relaxed ) != RecordState::Free && record.lifetime == lifetimeId && record.lifetimeGeneration != current )
            {
                dropRecord( *registry, index );
            }
        }
    }
}

} // namespace wolf
