// The C++ binding compiled with exceptions off. Nothing links it; it exists so every exceptions-off path in wolf.hpp builds.
#include <wolf/wolf.hpp>

namespace {

volatile int sink = 0;

struct Counted
{
    int value = 0;
    Counted() = default;
    Counted( const Counted & ) = delete;
    Counted &operator=( const Counted & ) = delete;
    Counted( Counted && ) = delete;
    Counted &operator=( Counted && ) = delete;
    ~Counted()
    {
        sink = value;
    }
};

bool init()
{
    const int k = sink; // read through a volatile, so k is not a constant and the lambdas really capture it
    wolf::on<wolf::site::Tick>( []( WolfTickCtx &tick ) { sink = static_cast<int>( tick.frame ); } );
    wolf::on<wolf::site::RoomChange>( [ k ]( WolfRoomChangeCtx &change ) { sink = static_cast<int>( change.to ) + k; }, wolf::Lifetime::Session );
    wolf::hook<int( int )>( "Item_Add", []( auto original, int x ) { return original( x ); } );
    wolf::hookRaw<void()>( "main.dll", 0x10, [ k ]( auto original ) {
        sink = k;
        original();
    } );
    const Counted *counted = wolf::make<Counted>( wolf::Lifetime::Session );
    const int *values = wolf::alloc<int>( wolf::Lifetime::Room, 4 );
    wolf::runInGame( [ k ] { sink = k; } );
    static int target = 0;
    wolf::write( &target, k );
    wolf::patch( &target, &k, sizeof( k ), wolf::Lifetime::Session );
    return counted != nullptr && values != nullptr;
}

} // namespace

WOLF_MOD_ENTRY( init )
