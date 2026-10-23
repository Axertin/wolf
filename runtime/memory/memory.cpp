#include "memory/memory.hpp"

#include <windows.h>

#include <wolf/sites.h>

#include <algorithm>
#include <cassert>
#include <cstring>

#include "arena/lifetime.hpp"
#include "core/threading.hpp"
#include "log/log.hpp"
#include "tables/registry.hpp"
#include "tables/table.hpp"

namespace {

struct Patch
{
    uint8_t *addr = nullptr;
    uint8_t *bytes = nullptr; // in the patch's lifetime arena: n bytes to write, then the n bytes they replaced
    size_t n = 0;
    bool image = false;   // every region MEM_IMAGE at registration
    bool applied = false; // written at the boundary; false while queued, when not applied and once withdrawn
};

void applyPatch( wolf::Registry &registry, uint32_t index );
void withdrawPatch( wolf::Registry &registry, uint32_t index );

wolf::Registry patches{
    .kind = wolf::Kind::Patch,
    .dataSize = sizeof( Patch ),
    .expected = 4096,
    .apply = &applyPatch,
    .withdraw = &withdrawPatch,
    .table = { .name = "patches" },
};

struct Watch
{
    const uint8_t *addr = nullptr;
    uint8_t *snapshot = nullptr; // in the watch's lifetime arena: the n bytes seen at the last boundary
    size_t n = 0;
    WolfCallback cb = nullptr;
    void *user = nullptr;
    bool armed = false; // the snapshot is taken; false while queued and when the memory was unreadable at apply
};

void applyWatch( wolf::Registry &registry, uint32_t index );

wolf::Registry watches{
    .kind = wolf::Kind::Watch,
    .dataSize = sizeof( Watch ),
    .expected = 1024,
    .apply = &applyWatch,
    .table = { .name = "watches" },
};

bool initialised = false;

constexpr DWORD kWritable = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
constexpr DWORD kExecutable = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;

// Calls each( info, offset, length ) for every region [addr, addr + n) touches, with the part of the range inside it, while each returns true. A zero-length
// range touches the region holding addr. False when a region is not committed, is PAGE_NOACCESS, PAGE_GUARD or execute-only PAGE_EXECUTE, or each returns
// false. Execute-only pages count as unusable because a watch reads its range every frame and a patch reads the bytes it replaces.
template <class F>
bool forEachRegion( const void *addr, size_t n, F &&each )
{
    const auto start = reinterpret_cast<uintptr_t>( addr );
    size_t offset = 0;
    do
    {
        MEMORY_BASIC_INFORMATION info{};
        if ( VirtualQuery( static_cast<const uint8_t *>( addr ) + offset, &info, sizeof( info ) ) != sizeof( info ) || info.State != MEM_COMMIT ||
             ( info.Protect & ( PAGE_NOACCESS | PAGE_GUARD | PAGE_EXECUTE ) ) != 0 )
        {
            return false;
        }
        const uintptr_t regionEnd = reinterpret_cast<uintptr_t>( info.BaseAddress ) + info.RegionSize;
        const size_t length = std::min( n - offset, static_cast<size_t>( regionEnd - ( start + offset ) ) );
        if ( !each( info, offset, length ) )
        {
            return false;
        }
        offset += length;
    } while ( offset < n );
    return true;
}

// Copies length bytes from src to at, which lie in one region with protection protect.
bool writeRegion( DWORD protect, uint8_t *at, const uint8_t *src, size_t length )
{
    const bool executable = ( protect & kExecutable ) != 0;
    const bool flip = ( protect & kWritable ) == 0;
    DWORD old = 0;
    if ( flip && VirtualProtect( at, length, executable ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE, &old ) == 0 )
    {
        return false;
    }
    std::memcpy( at, src, length );
    if ( flip )
    {
        VirtualProtect( at, length, old, &old );
    }
    if ( executable )
    {
        FlushInstructionCache( GetCurrentProcess(), at, length );
    }
    return true;
}

// Copies n bytes from src to to, region by region, until a region fails. The bytes written, n when every region was.
size_t writeRegions( uint8_t *to, const uint8_t *from, size_t n )
{
    size_t done = 0;
    forEachRegion( to, n, [ & ]( const MEMORY_BASIC_INFORMATION &info, size_t offset, size_t length ) {
        if ( !writeRegion( info.Protect, to + offset, from + offset, length ) )
        {
            return false;
        }
        done = offset + length;
        return true;
    } );
    return done;
}

// With the lock held, so no other write changes the protection of these pages between the query and the restore. Copies n bytes from src to addr, first
// saving the bytes it replaces into saved when saved is not null. 0 when the range is not readable memory or a region fails partway; then, with saved, the
// written part gets its saved bytes back, and without it that part stays written.
int32_t writeLocked( void *addr, const void *src, size_t n, void *saved )
{
    wolf::assertLocked();
    if ( !wolf::memoryReadable( addr, n ) )
    {
        return 0;
    }
    if ( n == 0 )
    {
        return 1;
    }
    if ( saved != nullptr )
    {
        std::memcpy( saved, addr, n );
    }
    auto *to = static_cast<uint8_t *>( addr );
    const size_t done = writeRegions( to, static_cast<const uint8_t *>( src ), n );
    if ( done == n )
    {
        return 1;
    }
    if ( saved != nullptr )
    {
        writeRegions( to, static_cast<const uint8_t *>( saved ), done );
    }
    return 0;
}

Patch &patchAt( uint32_t index )
{
    return *static_cast<Patch *>( wolf::registryData( patches, index ) );
}

void applyPatch( wolf::Registry & /*registry*/, uint32_t index )
{
    Patch &patch = patchAt( index );
    const wolf::Record &record = wolf::registryRecord( patches, index );
    for ( uint32_t i = 1; i < wolf::tableEnd( patches.table ); ++i )
    {
        const Patch &other = patchAt( i );
        if ( other.applied && other.addr < patch.addr + patch.n && patch.addr < other.addr + other.n )
        {
            wolf::writeLog( record.mod, WOLF_LOG_WARN, "patch at %p (%zu bytes) by %s overlaps a patch by %s; not applied", static_cast<void *>( patch.addr ),
                            patch.n, record.mod->id, wolf::registryRecord( patches, i ).mod->id );
            return;
        }
    }
    patch.applied = writeLocked( patch.addr, patch.bytes, patch.n, patch.bytes + patch.n ) != 0;
    if ( !patch.applied )
    {
        wolf::writeLog( record.mod, WOLF_LOG_WARN, "patch at %p (%zu bytes) could not be written; not applied", static_cast<void *>( patch.addr ), patch.n );
    }
}

// The saved bytes go back when the patch is removed or covers image memory. Other memory at its lifetime's end is the game's again, rebuilt, so it is left.
void withdrawPatch( wolf::Registry & /*registry*/, uint32_t index )
{
    Patch &patch = patchAt( index );
    if ( patch.applied && ( wolf::registryRecord( patches, index ).removing || patch.image ) )
    {
        writeLocked( patch.addr, patch.bytes + patch.n, patch.n, nullptr );
    }
    patch.applied = false;
}

Watch &watchAt( uint32_t index )
{
    return *static_cast<Watch *>( wolf::registryData( watches, index ) );
}

// The memory may have been released between registration and this boundary, so the range is checked again before it is read. The copy runs as the mod's
// dispatch, so a fault on memory released after the check names the mod.
void applyWatch( wolf::Registry & /*registry*/, uint32_t index )
{
    Watch &watch = watchAt( index );
    WolfMod *mod = wolf::registryRecord( watches, index ).mod;
    watch.armed = wolf::memoryReadable( watch.addr, watch.n );
    if ( watch.armed )
    {
        const wolf::DispatchScope scope( mod );
        std::memcpy( watch.snapshot, watch.addr, watch.n );
        return;
    }
    wolf::writeLog( mod, WOLF_LOG_WARN, "watch at %p (%zu bytes) is not readable memory; not armed", static_cast<const void *>( watch.addr ), watch.n );
}

} // namespace

namespace wolf {

void memoryInit()
{
    if ( initialised )
    {
        return;
    }
    initialised = true;
    registryInstall( patches );
    registryInstall( watches );
}

bool memoryReadable( const void *addr, size_t n )
{
    return forEachRegion( addr, n, []( const MEMORY_BASIC_INFORMATION & /*info*/, size_t /*offset*/, size_t /*length*/ ) { return true; } );
}

int32_t memoryWrite( void *addr, const void *src, size_t n )
{
    const RuntimeLock lock;
    return writeLocked( addr, src, n, nullptr );
}

WolfHandle patchAdd( WolfMod *mod, void *addr, const void *src, size_t n, int32_t lifetime )
{
    assert( initialised );
    const RuntimeLock lock;
    // The C table found the range readable. A walk that fails now, on memory released since, leaves the patch to the check at apply, which logs it.
    bool image = true;
    forEachRegion( addr, n, [ & ]( const MEMORY_BASIC_INFORMATION &info, size_t /*offset*/, size_t /*length*/ ) {
        image = image && info.Type == MEM_IMAGE;
        return true;
    } );
    // The copy and the registration share this hold, so the bytes sit in the arena of the lifetime generation the record names.
    auto *bytes = static_cast<uint8_t *>( lifetimeAllocLocked( mod, lifetime, n * 2, 1 ) );
    if ( bytes == nullptr )
    {
        return 0;
    }
    if ( n != 0 )
    {
        std::memcpy( bytes, src, n );
    }
    const Patch data{ .addr = static_cast<uint8_t *>( addr ), .bytes = bytes, .n = n, .image = image };
    return registryAddLocked( patches, mod, lifetime, &data );
}

WolfHandle watchAdd( WolfMod *mod, const void *addr, size_t n, WolfCallback cb, void *user, int32_t lifetime )
{
    assert( initialised && n >= 1 );
    const RuntimeLock lock;
    // The snapshot and the registration share this hold, so the snapshot sits in the arena of the lifetime generation the record names.
    auto *snapshot = static_cast<uint8_t *>( lifetimeAllocLocked( mod, lifetime, n, 1 ) );
    if ( snapshot == nullptr )
    {
        return 0;
    }
    const Watch data{ .addr = static_cast<const uint8_t *>( addr ), .snapshot = snapshot, .n = n, .cb = cb, .user = user };
    return registryAddLocked( watches, mod, lifetime, &data );
}

void watchesCompare()
{
    assertMainThread();
    const uint32_t end = tableEnd( watches.table );
    for ( uint32_t i = 1; i < end; ++i )
    {
        auto *watch = static_cast<Watch *>( registryLive( watches, i ) );
        if ( watch == nullptr || !watch->armed )
        {
            continue;
        }
        // The compare and the copy read the watched memory, so they run as the mod's dispatch too.
        WolfMod *mod = registryRecord( watches, i ).mod;
        const DispatchScope scope( mod );
        if ( std::memcmp( watch->addr, watch->snapshot, watch->n ) == 0 )
        {
            continue;
        }
        WolfWatchCtx ctx{ .size = sizeof( WolfWatchCtx ), .n = static_cast<uint32_t>( watch->n ), .addr = watch->addr, .previous = watch->snapshot };
        watch->cb( mod, &ctx, watch->user );
        std::memcpy( watch->snapshot, watch->addr, watch->n );
    }
}

} // namespace wolf
