#include "sites/sites.hpp"

#include <atomic>
#include <cassert>
#include <string_view>

#include "arena/lifetime.hpp"
#include "core/threading.hpp"
#include "frame/boundary.hpp"
#include "log/log.hpp"
#include "tables/registry.hpp"

namespace {

using wolf::Site;

constexpr std::string_view kNames[ wolf::kSiteCount ] = { WOLF_SITE_SESSION_START, WOLF_SITE_SESSION_END, WOLF_SITE_TICK,
                                                          WOLF_SITE_TICK_POST,     WOLF_SITE_ROOM_CHANGE, WOLF_SITE_ITEM_ADD };

struct Subscription
{
    Site site = Site::SessionStart;
    WolfCallback cb = nullptr;
    void *user = nullptr;
    uint32_t sequence = 0;
    uint32_t next = 0; // next subscription of the same site in dispatch order, 0 at the end
};

void applySubscription( wolf::Registry &registry, uint32_t index );
void withdrawSubscription( wolf::Registry &registry, uint32_t index );

wolf::Registry subscriptions{
    .kind = wolf::Kind::Subscription,
    .dataSize = sizeof( Subscription ),
    .expected = 512U * wolf::kSiteCount,
    .apply = &applySubscription,
    .withdraw = &withdrawSubscription,
    .table = { .name = "site subscriptions" },
};

// First subscription of each site. Changed only by the registry ops, on the main thread at a boundary, which never overlaps a dispatch.
uint32_t heads[ wolf::kSiteCount ] = {};
uint32_t sequence = 0;
bool initialised = false;

uint64_t frame = 0;
uint32_t fromRoom = WOLF_ROOM_NONE;
std::atomic<bool> started{ false };

Subscription &subscriptionAt( uint32_t index )
{
    return *static_cast<Subscription *>( wolf::registryData( subscriptions, index ) );
}

// True when the record at a runs before the one at b.
bool before( uint32_t a, uint32_t b )
{
    const WolfMod *modA = wolf::registryRecord( subscriptions, a ).mod;
    const WolfMod *modB = wolf::registryRecord( subscriptions, b ).mod;
    if ( modA->loadIndex != modB->loadIndex )
    {
        return modA->loadIndex < modB->loadIndex;
    }
    return subscriptionAt( a ).sequence < subscriptionAt( b ).sequence;
}

void applySubscription( wolf::Registry & /*registry*/, uint32_t index )
{
    Subscription &subscription = subscriptionAt( index );
    uint32_t *link = &heads[ static_cast<uint8_t>( subscription.site ) ];
    while ( *link != 0 && before( *link, index ) )
    {
        link = &subscriptionAt( *link ).next;
    }
    subscription.next = *link;
    *link = index;
}

void withdrawSubscription( wolf::Registry & /*registry*/, uint32_t index )
{
    const Subscription &subscription = subscriptionAt( index );
    uint32_t *link = &heads[ static_cast<uint8_t>( subscription.site ) ];
    while ( *link != index )
    {
        link = &subscriptionAt( *link ).next;
    }
    *link = subscription.next;
}

// Live and of the current generation of its lifetime, or, with ended set, of the generation that ended last. A lifetime ends at most once between two
// boundaries, and the boundary after an end retires the ended generation, so the one before the current is always the one that just ended.
const Subscription *runnable( uint32_t index, bool ended )
{
    const wolf::Record &record = wolf::registryRecord( subscriptions, index );
    if ( record.state.load( std::memory_order_acquire ) != wolf::RecordState::Live )
    {
        return nullptr;
    }
    const uint32_t current = wolf::lifetimeGeneration( record.lifetime );
    const bool justEnded = ended && record.lifetimeGeneration + 1 == current;
    return record.lifetimeGeneration == current || justEnded ? &subscriptionAt( index ) : nullptr;
}

void dispatch( Site site, void *ctx, bool ended )
{
    const wolf::DispatchScope scope( nullptr );
    for ( uint32_t index = heads[ static_cast<uint8_t>( site ) ]; index != 0; index = subscriptionAt( index ).next )
    {
        const Subscription *subscription = runnable( index, ended );
        if ( subscription == nullptr )
        {
            continue;
        }
        WolfMod *mod = wolf::registryRecord( subscriptions, index ).mod;
        const wolf::DispatchScope callback( mod );
        subscription->cb( mod, ctx, subscription->user );
    }
}

// The site's index in kNames, or -1 for a name no site has.
int siteIndex( std::string_view name )
{
    for ( int i = 0; i < wolf::kSiteCount; ++i )
    {
        if ( kNames[ i ] == name )
        {
            return i;
        }
    }
    return -1;
}

} // namespace

namespace wolf {

void sitesInit()
{
    if ( initialised )
    {
        return;
    }
    initialised = true;
    registryInstall( subscriptions );
}

bool siteNamed( const char *name )
{
    return siteIndex( name ) >= 0;
}

bool gameStarted()
{
    return started.load( std::memory_order_acquire );
}

WolfHandle siteOn( WolfMod *mod, const char *name, WolfCallback cb, void *user, int32_t lifetime )
{
    assert( initialised );
    const int index = siteIndex( name );
    assert( index >= 0 );
    const RuntimeLock lock;
    const Subscription data{ .site = static_cast<Site>( index ), .cb = cb, .user = user, .sequence = ++sequence };
    return registryAddLocked( subscriptions, mod, lifetime, &data );
}

void siteDispatch( Site site, void *ctx )
{
    dispatch( site, ctx, false );
}

void siteSessionStart( bool warm )
{
    if ( warm )
    {
        warmStartBoundary();
    }
    else
    {
        started.store( true, std::memory_order_release );
    }
    WolfSessionStartCtx ctx{ .size = sizeof( WolfSessionStartCtx ), .warm = warm ? 1 : 0 };
    siteDispatch( Site::SessionStart, &ctx );
}

void siteSessionEnd()
{
    // The lifetime ends first, so whatever session.end registers or allocates belongs to the next session. The callbacks that run are the ending session's,
    // whose memory stays intact until the next flower_tick entry.
    lifetimeEnd( WOLF_LIFETIME_SESSION );
    WolfSessionEndCtx ctx{ .size = sizeof( WolfSessionEndCtx ) };
    dispatch( Site::SessionEnd, &ctx, true );
}

void siteTickEnter()
{
    frameBoundary();
    ++frame;
    WolfTickCtx ctx{ .size = sizeof( WolfTickCtx ), .reserved = 0, .frame = frame };
    siteDispatch( Site::Tick, &ctx );
}

void siteTickReturn()
{
    WolfTickCtx ctx{ .size = sizeof( WolfTickCtx ), .reserved = 0, .frame = frame };
    siteDispatch( Site::TickPost, &ctx );
}

void siteRoomChangeEnter()
{
    lifetimeEnd( WOLF_LIFETIME_ROOM );
}

void siteRoomChangeReturn( uint32_t to )
{
    WolfRoomChangeCtx ctx{ .size = sizeof( WolfRoomChangeCtx ), .from = fromRoom, .to = to };
    writeLog( nullptr, WOLF_LOG_DEBUG, "room 0x%04X to 0x%04X", fromRoom, to );
    fromRoom = to;
    siteDispatch( Site::RoomChange, &ctx );
}

void siteItemAdd( WolfItemAddCtx &ctx )
{
    siteDispatch( Site::ItemAdd, &ctx );
}

} // namespace wolf
