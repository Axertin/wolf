#include "boot/detours.hpp"

#include <windows.h>

#include <cstdint>

#include "boot/boot.hpp"
#include "core/threading.hpp"
#include "crash/crash.hpp"
#include "hooks/hooks.hpp"
#include "hooks/resolve.hpp"
#include "log/log.hpp"
#include "sites/sites.hpp"
#include "symbols.hpp"
#include "tables/registry.hpp"

namespace {

// Each detour calls on through what hookSite returned, which runs the mod links inside the site and then the game's function. Room_Change and Item_Add, whose
// void returns the KB only infers, return uintptr_t, so whatever the game leaves in RAX passes through.
using StartupFn = bool ( * )( bool warm );
using TickFn = bool ( * )();
using SoftResetFn = void ( * )();
using RoomChangeFn = uintptr_t ( * )( void *sceneCtx );
using ItemAddFn = uintptr_t ( * )( void *itemMgr, int32_t itemId, int32_t delta );

StartupFn startupNext = nullptr;
TickFn tickNext = nullptr;
SoftResetFn softResetNext = nullptr;
RoomChangeFn roomChangeNext = nullptr;
ItemAddFn itemAddNext = nullptr;
const uint16_t *currentRoom = nullptr; // g_currentRoomId
bool booted = false;

template <typename Fn>
void hookAt( void *target, Fn detour, Fn *next )
{
    *next = reinterpret_cast<Fn>( wolf::hookSite( target, reinterpret_cast<void *>( detour ) ) );
}

template <typename Fn>
void hookGame( const char *name, Fn detour, Fn *next )
{
    if ( void *target = wolf::symbolAddress( name ); target != nullptr )
    {
        hookAt( target, detour, next );
    }
}

bool tickDetour()
{
    wolf::siteTickEnter();
    const bool result = tickNext();
    wolf::siteTickReturn();
    return result;
}

void softResetDetour()
{
    wolf::writeLog( nullptr, WOLF_LOG_INFO, "soft reset: the session ends" );
    wolf::siteSessionEnd();
    softResetNext();
}

uintptr_t roomChangeDetour( void *sceneCtx )
{
    wolf::siteRoomChangeEnter();
    const uintptr_t result = roomChangeNext( sceneCtx );
    wolf::siteRoomChangeReturn( currentRoom != nullptr ? *currentRoom : WOLF_ROOM_NONE );
    return result;
}

uintptr_t itemAddDetour( void *itemMgr, int32_t itemId, int32_t delta )
{
    wolf::writeLog( nullptr, WOLF_LOG_DEBUG, "Item_Add id %d delta %d", itemId, delta );
    WolfItemAddCtx ctx{ .size = sizeof( WolfItemAddCtx ), .itemId = itemId, .delta = delta, .handled = 0, .fromMod = nullptr };
    wolf::siteItemAdd( ctx );
    if ( ctx.handled != 0 )
    {
        return 0;
    }
    return itemAddNext( itemMgr, itemId, ctx.delta );
}

// Main thread, loader lock released, before the game initialises: the rest of boot. The site heads exist before the pending registrations apply, so a mod
// link on a site function lands inside its site detour; all the new heads then go live in one batch.
void coldStartup()
{
    wolf::setMainThread();
    wolf::writeLog( nullptr, WOLF_LOG_INFO, "cold flower_startup on thread %lu", GetCurrentThreadId() );
    wolf::symbolsResolve( GetModuleHandleW( L"main.dll" ), GetModuleHandleW( L"flower_kernel.dll" ) );
    wolf::crashStart( wolf::gameDirectory() );
    hookGame( "flower_tick", &tickDetour, &tickNext );
    hookGame( "SoftReset_ExecuteAndReload", &softResetDetour, &softResetNext );
    hookGame( "Room_Change", &roomChangeDetour, &roomChangeNext );
    hookGame( "Item_Add", &itemAddDetour, &itemAddNext );
    currentRoom = static_cast<const uint16_t *>( wolf::symbolAddress( "g_currentRoomId" ) );
    wolf::applyPending();
    wolf::hooksCommit();
}

bool startupDetour( bool warm )
{
    if ( !booted )
    {
        // The first call is the cold one, whatever warm says.
        booted = true;
        coldStartup();
        const bool result = startupNext( warm );
        wolf::crashReassert();
        wolf::siteSessionStart( false );
        return result;
    }
    wolf::writeLog( nullptr, WOLF_LOG_INFO, "warm flower_startup" );
    const bool result = startupNext( warm );
    wolf::siteSessionStart( true );
    return result;
}

} // namespace

namespace wolf {

void hookFlowerStartup( HMODULE main )
{
    hookAt( reinterpret_cast<uint8_t *>( main ) + symbolRva( "flower_startup" ), &startupDetour, &startupNext );
    hooksCommit();
}

} // namespace wolf
