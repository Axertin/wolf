// A C++ test mod whose init throws, so WOLF_MOD_ENTRY catches the exception and WOLF refuses the mod. It reports to the fake game through FakeMain_Report,
// looked up as fake.h does for the C test mods.
#include <wolf/wolf.hpp>

#include <cstdint>
#include <stdexcept>

namespace {

using ReportFn = void ( * )( const char *mod, const char *what, const char *text, int64_t a, int64_t b );

void report( const char *what )
{
    if ( const auto fn = reinterpret_cast<ReportFn>( GetProcAddress( GetModuleHandleA( "main.dll" ), "FakeMain_Report" ) ); fn != nullptr )
    {
        fn( "throwing", what, nullptr, 0, 0 );
    }
}

bool init()
{
    report( "init" );
    // WOLF refuses the mod once init throws, so this callback never runs and never reports.
    wolf::on<wolf::site::Tick>( []( WolfTickCtx & /*tick*/ ) { report( "tick" ); } );
    throw std::runtime_error( "boom" );
}

} // namespace

WOLF_MOD_ENTRY( init )
