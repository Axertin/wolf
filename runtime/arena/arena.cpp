#include "arena/arena.hpp"

#include <windows.h>

#include <cstring>
#include <new>

namespace wolf {

// Sits at offset 0 of every block; allocations start after it.
struct ArenaBlock
{
    ArenaBlock *prev = nullptr; // next older block in the chain
    size_t size = 0;            // reserved bytes, header included
    size_t used = 0;            // offset of the first free byte
    size_t committed = 0;       // offset where committed memory ends
};

} // namespace wolf

namespace {

using wolf::Arena;
using wolf::ArenaBlock;

constexpr size_t kHeaderSize = 64;
static_assert( sizeof( ArenaBlock ) <= kHeaderSize );
constexpr size_t kPageSize = 4096;

uint8_t *bytes( ArenaBlock *block )
{
    return reinterpret_cast<uint8_t *>( block );
}

// Offset of the first byte at or after used that is aligned to align.
size_t alignedOffset( ArenaBlock *block, size_t align )
{
    const auto base = reinterpret_cast<uintptr_t>( block );
    return block->used + ( ( uintptr_t{ 0 } - ( base + block->used ) ) & ( align - 1 ) );
}

ArenaBlock *mapBlock( size_t size, ArenaBlock *prev )
{
    auto *memory = static_cast<uint8_t *>( VirtualAlloc( nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE ) );
    if ( memory == nullptr )
    {
        return nullptr;
    }
    return new ( memory ) ArenaBlock{ .prev = prev, .size = size, .used = kHeaderSize, .committed = size };
}

// Hands out n bytes at offset, which the caller has made fit below the committed end.
void *take( ArenaBlock *block, size_t offset, size_t n )
{
    block->used = offset + n;
    return bytes( block ) + offset;
}

} // namespace

namespace wolf {

void *push( Arena &arena, size_t n, size_t align )
{
    if ( n > kMaxPush || align > kMaxPush )
    {
        return nullptr;
    }
    if ( ArenaBlock *block = arena.current )
    {
        const size_t offset = alignedOffset( block, align );
        const size_t end = offset + n;
        if ( end <= block->committed )
        {
            return take( block, offset, n );
        }
        if ( end <= block->size )
        {
            // Commits through the end of the request's last page.
            const size_t to = ( end + ( kPageSize - 1 ) ) & ~( kPageSize - 1 );
            if ( VirtualAlloc( bytes( block ) + block->committed, to - block->committed, MEM_COMMIT, PAGE_READWRITE ) == nullptr )
            {
                return nullptr;
            }
            block->committed = to;
            return take( block, offset, n );
        }
    }
    // Worst case for one allocation in a fresh block: header, padding, payload.
    const size_t worst = kHeaderSize + ( align - 1 ) + n;
    if ( worst > arena.blockSize )
    {
        ArenaBlock *dedicated = mapBlock( ( worst + ( kArenaGranularity - 1 ) ) & ~( kArenaGranularity - 1 ), arena.large );
        if ( dedicated == nullptr )
        {
            return nullptr;
        }
        arena.large = dedicated;
        return take( dedicated, alignedOffset( dedicated, align ), n );
    }
    ArenaBlock *fresh = mapBlock( arena.blockSize, arena.current );
    if ( fresh == nullptr )
    {
        return nullptr;
    }
    if ( arena.first == nullptr )
    {
        arena.first = fresh;
    }
    arena.current = fresh;
    return take( fresh, alignedOffset( fresh, align ), n );
}

void reset( Arena &arena )
{
    for ( ArenaBlock *block = arena.current; block != nullptr && block != arena.first; )
    {
        ArenaBlock *older = block->prev;
        VirtualFree( block, 0, MEM_RELEASE );
        block = older;
    }
    while ( arena.large != nullptr )
    {
        ArenaBlock *older = arena.large->prev;
        VirtualFree( arena.large, 0, MEM_RELEASE );
        arena.large = older;
    }
    ArenaBlock *first = arena.first;
    if ( first == nullptr )
    {
        return;
    }
    const size_t used = first->used;
    const size_t headerPage = used < kPageSize ? used : kPageSize;
    std::memset( bytes( first ) + kHeaderSize, 0, headerPage - kHeaderSize );
#ifdef NDEBUG
    if ( used > kPageSize )
    {
        // Decommitting and recommitting hands back zeroed pages. When the recommit fails, pushes past the first page commit on demand.
        const size_t length = ( ( used + ( kPageSize - 1 ) ) & ~( kPageSize - 1 ) ) - kPageSize;
        VirtualFree( bytes( first ) + kPageSize, length, MEM_DECOMMIT );
        if ( VirtualAlloc( bytes( first ) + kPageSize, length, MEM_COMMIT, PAGE_READWRITE ) == nullptr )
        {
            first->committed = kPageSize;
        }
    }
#else
    // Left uncommitted, a pointer kept across the reset faults where it is used.
    VirtualFree( bytes( first ) + kPageSize, first->size - kPageSize, MEM_DECOMMIT );
    first->committed = kPageSize;
#endif
    first->used = kHeaderSize;
    arena.current = first;
}

} // namespace wolf
