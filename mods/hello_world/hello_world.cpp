// The smallest useful WOLF mod: it logs a greeting from its config, then a line for each session start, room change and item the game adds.
#include <wolf/wolf.hpp>

namespace {

bool init()
{
    wolf::log( WOLF_LOG_INFO, "%s", wolf::configStr( "greeting", "hello" ) );

    wolf::on<wolf::site::SessionStart>( []( WolfSessionStartCtx &session ) { wolf::log( WOLF_LOG_INFO, "session start, warm %d", session.warm ); } );

    // Room ids are stage << 8 | room.
    wolf::on<wolf::site::RoomChange>( []( WolfRoomChangeCtx &room ) { wolf::log( WOLF_LOG_INFO, "room %04X -> %04X", room.from, room.to ); } );

    wolf::on<wolf::site::ItemAdd>( []( WolfItemAddCtx &item ) {
        wolf::log( WOLF_LOG_INFO, "item.add %d x%d %s", item.itemId, item.delta, item.fromMod == nullptr ? "from the game" : "from a mod" );
    } );
    return true;
}

} // namespace

WOLF_MOD_ENTRY( init )
