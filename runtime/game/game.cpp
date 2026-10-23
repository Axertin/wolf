#include "game/game.hpp"

#include <atomic>
#include <cassert>
#include <cstddef>

#include "core/threading.hpp"
#include "frame/run_queue.hpp"
#include "hooks/resolve.hpp"
#include "log/log.hpp"
#include "sites/sites.hpp"

namespace {

using FlagFn = void ( * )( uint32_t ref );
using ItemAddFn = void ( * )( void *itemMgr, int32_t itemId, int32_t delta );

struct Gift
{
    int32_t itemId;
    int32_t delta;
};

// Saved and restored around each call, so a giveItem made from inside another one's item.add names its own mod.
constinit thread_local WolfMod *requester = nullptr;

// The game writes these pointers on its own threads, so they are read atomically.
void *gameGlobal( const char *name )
{
    auto *slot = static_cast<void **>( wolf::symbolAddress( name ) );
    assert( slot != nullptr );
    return std::atomic_ref<void *>( *slot ).load( std::memory_order_relaxed );
}

// The flag block, or null before the game has set it, which logs once per mod.
uint8_t *flagBlock( WolfMod *mod, const char *entry )
{
    auto *block = static_cast<uint8_t *>( gameGlobal( "g_progressFlagBlockPtr" ) );
    if ( block == nullptr && wolf::logOnce( mod, wolf::kOnceFlagsNotReady ) )
    {
        wolf::writeLog( mod, WOLF_LOG_WARN, "flags are not ready until the game has started; %s ignored", entry );
    }
    return block;
}

void callFlag( WolfMod *mod, const char *entry, const char *function, uint32_t group, uint32_t bit )
{
    if ( flagBlock( mod, entry ) != nullptr )
    {
        reinterpret_cast<FlagFn>( wolf::symbolAddress( function ) )( ( group << 16 ) | bit );
    }
}

void addItem( WolfMod *mod, int32_t itemId, int32_t delta )
{
    WolfMod *const outer = requester;
    requester = mod;
    reinterpret_cast<ItemAddFn>( wolf::symbolAddress( "Item_Add" ) )( gameGlobal( "g_ItemMgrPtr" ), itemId, delta );
    requester = outer;
}

// The run step calls this on the main thread, after the game has started.
void runGift( WolfMod *mod, void *data, uint32_t /*len*/ )
{
    const Gift &gift = *static_cast<const Gift *>( data );
    addItem( mod, gift.itemId, gift.delta );
}

} // namespace

namespace wolf {

void flagSet( WolfMod *mod, uint32_t group, uint32_t bit )
{
    callFlag( mod, "flagSet", "ProgressFlag_Set", group, bit );
}

void flagClear( WolfMod *mod, uint32_t group, uint32_t bit )
{
    callFlag( mod, "flagClear", "ProgressFlag_Clear", group, bit );
}

int32_t flagTest( WolfMod *mod, uint32_t group, uint32_t bit )
{
    uint8_t *block = flagBlock( mod, "flagTest" );
    if ( block == nullptr )
    {
        return 0;
    }
    auto *word = reinterpret_cast<uint32_t *>( block + 0x35c + ( size_t{ group } * 0x20 ) + ( size_t{ bit >> 5 } * 4 ) );
    return ( std::atomic_ref<uint32_t>( *word ).load( std::memory_order_relaxed ) & ( 0x80000000U >> ( bit & 31U ) ) ) != 0 ? 1 : 0;
}

int32_t giveItem( WolfMod *mod, int32_t itemId, int32_t delta )
{
    if ( inDispatch() && gameStarted() )
    {
        addItem( mod, itemId, delta );
        return 1;
    }
    const Gift gift{ .itemId = itemId, .delta = delta };
    return runInGame( mod, &runGift, &gift, sizeof( gift ) );
}

WolfMod *itemRequester()
{
    return requester;
}

} // namespace wolf
