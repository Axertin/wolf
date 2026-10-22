#include "tables/table.hpp"

#include <wolf/wolf.h>

#include <cstring>

#include "arena/arena.hpp"
#include "arena/lifetime.hpp"
#include "core/threading.hpp"
#include "log/log.hpp"

namespace wolf {

void warnPast( WolfMod *mod, const char *name, uint32_t expected, uint32_t &nextWarn, uint32_t entries )
{
    if ( expected == 0 )
    {
        return;
    }
    const uint32_t threshold = nextWarn != 0 ? nextWarn : expected;
    if ( entries <= threshold )
    {
        return;
    }
    nextWarn = threshold * 2;
    writeLog( mod, WOLF_LOG_WARN, "%s table passed %u entries", name, threshold );
}

void *tableAppend( Table &table, WolfMod *mod, uint32_t *index )
{
    assertLocked();
    const uint32_t count = table.count.load( std::memory_order_relaxed );
    const uint32_t next = count > 1 ? count : 1;
    bool ok = next < kMaxEntries;
    if ( ok && ( next >> kChunkShift ) == table.chunkCount )
    {
        auto *chunk = static_cast<uint8_t *>( push( processArena(), static_cast<size_t>( kChunkEntries ) * table.stride, 64 ) );
        ok = chunk != nullptr;
        if ( ok )
        {
            table.chunks[ table.chunkCount++ ] = chunk;
        }
    }
    if ( !ok )
    {
        if ( logOnce( mod, table.onceBit ) )
        {
            writeLog( mod, WOLF_LOG_ERROR, "%s table is full", table.name );
        }
        return nullptr;
    }
    void *entry = tableAt( table, next );
    std::memset( entry, 0, table.stride );
    table.count.store( next + 1, std::memory_order_release );
    warnPast( mod, table.name, table.expected, table.nextWarn, next );
    *index = next;
    return entry;
}

void tableClear( Table &table )
{
    table.count.store( 0, std::memory_order_release );
}

} // namespace wolf
