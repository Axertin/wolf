#include "hooks/hooks.hpp"

#include <windows.h>

#include <MinHook.h>

#include <atomic>
#include <cassert>
#include <cstring>

#include "core/threading.hpp"
#include "log/log.hpp"
#include "tables/registry.hpp"
#include "tables/table.hpp"

namespace {

using wolf::tableAt;
using wolf::tableEnd;

enum class Head : uint8_t { None = 0, Installed = 1, Failed = 2 };

struct Target
{
    uint8_t *address = nullptr;
    uint8_t *stub = nullptr;
    void *trampoline = nullptr;
    uint32_t outermost = 0; // first linked link, 0 when none
    Head head = Head::None;
};

struct Link
{
    uint32_t target = 0;
    void *detour = nullptr;
    void **original = nullptr;
    WolfMod *mod = nullptr;
    uint32_t sequence = 0; // when the link was first added; orders links of one mod
    uint32_t inner = 0;    // next linked link inward, 0 for the function itself; valid while linked
    bool linked = false;
    WolfHandle owner = 0; // the record that last added it
};

struct HookData
{
    uint32_t link = 0;
};

constexpr uint8_t kStubCode[ 8 ] = { 0xFF, 0x25, 0x02, 0x00, 0x00, 0x00, 0xCC, 0xCC }; // jmp [rip+2]; int3; int3
constexpr size_t kStubSize = 16;
constexpr size_t kStubBlock = size_t{ 64 } * 1024; // one VirtualAlloc reservation

constinit wolf::Table targets{ .name = "hook targets", .stride = sizeof( Target ), .expected = 256, .onceBit = wolf::kOnceHookTargetsFull };
constinit wolf::Table links{ .name = "hook links", .stride = sizeof( Link ), .expected = 4096, .onceBit = wolf::kOnceHookLinksFull };

uint8_t *stubBlock = nullptr;
size_t stubUsed = kStubBlock;
uint32_t sequence = 0;
bool ready = false;
bool initialised = false;
bool queued = false; // a head was queued since the last commit; main thread only

void applyHook( wolf::Registry &registry, uint32_t index );
void withdrawHook( wolf::Registry &registry, uint32_t index );

wolf::Registry hookRegistry{
    .kind = wolf::Kind::Hook,
    .dataSize = sizeof( HookData ),
    .apply = &applyHook,
    .withdraw = &withdrawHook,
    .table = { .name = "hooks" },
};

Target &targetAt( uint32_t index )
{
    return *static_cast<Target *>( tableAt( targets, index ) );
}

Link &linkAt( uint32_t index )
{
    return *static_cast<Link *>( tableAt( links, index ) );
}

void store( void **slot, void *value )
{
    std::atomic_ref<void *>( *slot ).store( value, std::memory_order_release );
}

void **entrySlot( const Target &target )
{
    return reinterpret_cast<void **>( target.stub + 8 );
}

// What a link or the entry slot calls to go further in from position: the detour of link, or the trampoline when link is 0.
void *inward( const Target &target, uint32_t link )
{
    return link != 0 ? linkAt( link ).detour : target.trampoline;
}

// The slot pointing at the link after outer: outer's *original, or the entry slot when outer is 0.
void **slotAfter( const Target &target, uint32_t outer )
{
    return outer != 0 ? linkAt( outer ).original : entrySlot( target );
}

// True when a sits further out than b.
bool outside( const Link &a, const Link &b )
{
    if ( a.mod->loadIndex != b.mod->loadIndex )
    {
        return a.mod->loadIndex > b.mod->loadIndex;
    }
    return a.sequence > b.sequence;
}

// With the lock held: a new stub, or null when no block can be mapped.
uint8_t *newStub()
{
    wolf::assertLocked();
    if ( stubUsed == kStubBlock )
    {
        void *block = VirtualAlloc( nullptr, kStubBlock, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE );
        if ( block == nullptr )
        {
            return nullptr;
        }
        stubBlock = static_cast<uint8_t *>( block );
        stubUsed = 0;
    }
    uint8_t *stub = stubBlock + stubUsed;
    stubUsed += kStubSize;
    std::memcpy( stub, kStubCode, sizeof( kStubCode ) );
    return stub;
}

// With the lock held: the target entry for address, appended with its stub when it is new. 0 when a table or the stub block cannot grow.
uint32_t findTarget( WolfMod *mod, void *address )
{
    for ( uint32_t i = 1; i < tableEnd( targets ); ++i )
    {
        if ( targetAt( i ).address == address )
        {
            return i;
        }
    }
    uint8_t *stub = newStub();
    if ( stub == nullptr )
    {
        if ( wolf::logOnce( mod, wolf::kOnceOutOfMemory ) )
        {
            wolf::writeLog( mod, WOLF_LOG_ERROR, "out of memory for a hook stub" );
        }
        return 0;
    }
    uint32_t index = 0;
    auto *target = static_cast<Target *>( wolf::tableAppend( targets, mod, &index ) );
    if ( target == nullptr )
    {
        return 0;
    }
    target->address = static_cast<uint8_t *>( address );
    target->stub = stub;
    return index;
}

// With the lock held: creates and queues the target's MinHook head with detour as its first jump. False, logging once for the target, when MinHook refuses.
bool createHead( Target &target, void *detour )
{
    MH_STATUS status = MH_CreateHook( target.address, detour, &target.trampoline );
    if ( status == MH_OK )
    {
        store( entrySlot( target ), target.trampoline );
        status = MH_QueueEnableHook( target.address );
        if ( status == MH_OK )
        {
            target.head = Head::Installed;
            queued = true;
            return true;
        }
    }
    target.head = Head::Failed;
    wolf::writeLog( nullptr, WOLF_LOG_ERROR, "hook head on %p failed: %s", static_cast<void *>( target.address ), MH_StatusToString( status ) );
    return false;
}

void applyHook( wolf::Registry &registry, uint32_t index )
{
    Link &link = linkAt( static_cast<HookData *>( wolf::registryData( registry, index ) )->link );
    Target &target = targetAt( link.target );
    if ( target.head == Head::None && !createHead( target, target.stub ) )
    {
        return;
    }
    if ( target.head == Head::Failed || link.linked )
    {
        return;
    }
    uint32_t outer = 0;
    uint32_t inner = target.outermost;
    while ( inner != 0 && outside( linkAt( inner ), link ) )
    {
        outer = inner;
        inner = linkAt( inner ).inner;
    }
    // The link's own *original is complete before the store that makes it reachable.
    store( link.original, inward( target, inner ) );
    link.inner = inner;
    const uint32_t self = static_cast<HookData *>( wolf::registryData( registry, index ) )->link;
    store( slotAfter( target, outer ), link.detour );
    if ( outer == 0 )
    {
        target.outermost = self;
    }
    else
    {
        linkAt( outer ).inner = self;
    }
    link.linked = true;
}

void withdrawHook( wolf::Registry &registry, uint32_t index )
{
    const uint32_t self = static_cast<HookData *>( wolf::registryData( registry, index ) )->link;
    Link &link = linkAt( self );
    if ( !link.linked )
    {
        return;
    }
    Target &target = targetAt( link.target );
    uint32_t outer = 0;
    for ( uint32_t at = target.outermost; at != self; at = linkAt( at ).inner )
    {
        outer = at;
    }
    // The withdrawn link keeps its *original, so a thread still inside it carries on inward.
    store( slotAfter( target, outer ), inward( target, link.inner ) );
    if ( outer == 0 )
    {
        target.outermost = link.inner;
    }
    else
    {
        linkAt( outer ).inner = link.inner;
    }
    link.linked = false;
}

// With the lock held: the record that added a link still stands and is not being removed.
bool claimed( WolfHandle owner )
{
    if ( owner == 0 || wolf::registryResolve( owner ) == nullptr )
    {
        return false;
    }
    return !wolf::registryRecord( hookRegistry, wolf::handleIndex( owner ) ).removing;
}

} // namespace

namespace wolf {

void hooksInit()
{
    if ( initialised )
    {
        return;
    }
    initialised = true;
    registryInstall( hookRegistry );
    const MH_STATUS status = MH_Initialize();
    ready = status == MH_OK;
    if ( !ready )
    {
        writeLog( nullptr, WOLF_LOG_ERROR, "MinHook did not initialise: %s", MH_StatusToString( status ) );
    }
}

bool hooksReady()
{
    return ready;
}

WolfHandle hookAdd( WolfMod *mod, void *target, void *detour, void **original )
{
    assert( initialised );
    // Relinking is one aligned 8-byte store into *original; the C table refuses a misaligned original.
    assert( ( reinterpret_cast<uintptr_t>( original ) & 7U ) == 0 );
    const RuntimeLock lock;
    uint32_t found = 0;
    for ( uint32_t i = 1; i < tableEnd( links ); ++i )
    {
        const Link &link = linkAt( i );
        if ( link.detour != detour && link.original != original )
        {
            continue;
        }
        if ( link.detour != detour || link.original != original || targetAt( link.target ).address != target )
        {
            writeLog( mod, WOLF_LOG_WARN, "hook refused: detour %p or original %p already belongs to another hook", detour, static_cast<void *>( original ) );
            return 0;
        }
        found = i;
        break;
    }
    if ( found != 0 && claimed( linkAt( found ).owner ) )
    {
        writeLog( mod, WOLF_LOG_WARN, "hook refused: detour %p is already linked on %p", detour, target );
        return 0;
    }
    if ( found == 0 )
    {
        const uint32_t targetIndex = findTarget( mod, target );
        if ( targetIndex == 0 )
        {
            return 0;
        }
        auto *link = static_cast<Link *>( tableAppend( links, mod, &found ) );
        if ( link == nullptr )
        {
            return 0;
        }
        link->target = targetIndex;
        link->detour = detour;
        link->original = original;
        link->sequence = ++sequence;
    }
    const HookData data{ .link = found };
    const WolfHandle handle = registryAddLocked( hookRegistry, mod, WOLF_LIFETIME_PROCESS, &data );
    if ( handle != 0 )
    {
        Link &link = linkAt( found );
        link.mod = mod;
        link.owner = handle;
    }
    return handle;
}

void *hookSite( void *target, void *siteDetour )
{
    const RuntimeLock lock;
    const uint32_t index = findTarget( nullptr, target );
    if ( index == 0 )
    {
        return nullptr;
    }
    Target &entry = targetAt( index );
    assert( entry.head == Head::None );
    if ( !createHead( entry, siteDetour ) )
    {
        return nullptr;
    }
    return entry.stub;
}

void hooksCommit()
{
    assertMainThread();
    if ( !queued )
    {
        return;
    }
    queued = false;
    if ( const MH_STATUS status = MH_ApplyQueued(); status != MH_OK )
    {
        writeLog( nullptr, WOLF_LOG_ERROR, "MinHook could not enable the queued hooks: %s", MH_StatusToString( status ) );
    }
}

void *rawTarget( const char *module, uintptr_t rva )
{
    const HMODULE handle = GetModuleHandleA( module );
    if ( handle == nullptr )
    {
        return nullptr;
    }
    auto *base = reinterpret_cast<uint8_t *>( handle );
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>( base );
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>( base + dos->e_lfanew );
    if ( rva >= nt->OptionalHeader.SizeOfImage )
    {
        return nullptr;
    }
    return base + rva;
}

} // namespace wolf
