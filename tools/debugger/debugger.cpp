// The Okami debugger mod: reads capture.cfg from its directory, installs the probes and watches it names, records them into the session, and writes each
// capture under <game>\logs\captures\ from a thread of its own.

#define NOMINMAX
#include <windows.h>

#include <wolf/sites.h>
#include <wolf/wolf.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>
#include <utility>

#include "dbg.hpp"

#ifdef _MSC_VER
#include <intrin.h>
#define DBG_RETURN_ADDRESS() uint64_t( _ReturnAddress() )
#else
#define DBG_RETURN_ADDRESS() uint64_t( __builtin_return_address( 0 ) )
#endif

namespace {

const WolfApi *gApi = nullptr;
WolfMod *gMod = nullptr;
dbg::Session *gSession = nullptr;
std::array<void *, dbg::kCatalogSize> gOriginal{};

void log( int32_t level, const std::string &text )
{
    gApi->log( gMod, level, text.c_str() );
}

void copyText( dbg::Record &rec, const char *text )
{
    if ( text != nullptr )
    {
        std::memcpy( rec.text, text, strnlen( text, sizeof( rec.text ) ) );
    }
}

using Forward = uint64_t ( * )( uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t );

// Every catalog function takes only integer or pointer arguments, so a detour with 8 integer parameters forwards any of them: the parameters past the
// function's own read the caller's stack and pass through unread.
template <size_t I>
uint64_t detour( uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7 )
{
    constexpr const dbg::CatalogEntry &entry = dbg::kCatalog[ I ];
    const uint64_t args[ 8 ] = { a0, a1, a2, a3, a4, a5, a6, a7 };
    dbg::Record rec{};
    rec.kind = dbg::RecordKind::Call;
    rec.probe = uint16_t( I );
    rec.caller = DBG_RETURN_ADDRESS();
    for ( uint8_t a = 0; a < dbg::catalogArgCount( entry ); ++a )
    {
        rec.arg[ a ] = args[ entry.args[ a ].position ];
    }
    if constexpr ( entry.textArg >= 0 )
    {
        copyText( rec, reinterpret_cast<const char *>( args[ entry.args[ entry.textArg ].position ] ) );
    }
    const bool kept = dbg::sessionRecord( gSession, rec );
    const uint64_t ret = reinterpret_cast<Forward>( gOriginal[ I ] )( a0, a1, a2, a3, a4, a5, a6, a7 );
    if constexpr ( entry.returns )
    {
        if ( !kept )
        {
            return ret;
        }
        dbg::Record r = rec;
        r.kind = dbg::RecordKind::Return;
        r.arg[ 0 ] = ret;
        std::copy_n( rec.arg, 3, r.arg + 1 );
        dbg::sessionRecord( gSession, r );
    }
    return ret;
}

template <size_t... I>
std::array<void *, sizeof...( I )> detourTable( std::index_sequence<I...> /*unused*/ )
{
    return { reinterpret_cast<void *>( &detour<I> )... };
}

const std::array<void *, dbg::kCatalogSize> kDetours = detourTable( std::make_index_sequence<dbg::kCatalogSize>() );

// CompleteRequest( mgr, out, decrypt, wait ) completes the request at the head of the manager's queue, and clears it before it returns, so the request is read
// at entry: its path at +0x30 (char[ 64 ]), size at +0x18 and buffer at +0x28. A completion is recorded only when *out is nonzero. decrypt == 0 is a cancel,
// whose buffer may still hold ciphered bytes.
uint64_t completeDetour( uint64_t mgr, uint64_t out, uint64_t decrypt, uint64_t wait )
{
    constexpr auto index = uint16_t( dbg::catalogFind( "CompleteRequest" ) );
    dbg::Record rec{};
    rec.kind = dbg::RecordKind::Call;
    rec.probe = index;
    rec.caller = DBG_RETURN_ADDRESS();
    rec.arg[ 3 ] = decrypt;
    const auto *req = *reinterpret_cast<const uint8_t *const *>( mgr );
    if ( req != nullptr )
    {
        rec.arg[ 0 ] = uint64_t( req + 0x30 );
        rec.arg[ 1 ] = *reinterpret_cast<const uint32_t *>( req + 0x18 );
        rec.arg[ 2 ] = *reinterpret_cast<const uint64_t *>( req + 0x28 );
        copyText( rec, reinterpret_cast<const char *>( req + 0x30 ) );
    }
    const uint64_t ret = reinterpret_cast<uint64_t ( * )( uint64_t, uint64_t, uint64_t, uint64_t )>( gOriginal[ index ] )( mgr, out, decrypt, wait );
    if ( req != nullptr && out != 0 && *reinterpret_cast<const uint32_t *>( out ) != 0 )
    {
        dbg::sessionRecord( gSession, rec );
    }
    return ret;
}

void onSite( WolfMod * /*mod*/, void *ctx, void *user )
{
    const auto *room = static_cast<const WolfRoomChangeCtx *>( ctx );
    dbg::Record rec{};
    rec.kind = dbg::RecordKind::Site;
    rec.probe = uint16_t( reinterpret_cast<uintptr_t>( user ) );
    rec.arg[ 0 ] = room->from;
    rec.arg[ 1 ] = room->to;
    dbg::sessionRecord( gSession, rec );
}

void onWatch( WolfMod * /*mod*/, void *ctx, void *user )
{
    const auto *watch = static_cast<const WolfWatchCtx *>( ctx );
    dbg::Record rec{};
    rec.kind = dbg::RecordKind::Watch;
    rec.probe = uint16_t( reinterpret_cast<uintptr_t>( user ) );
    std::memcpy( &rec.arg[ 0 ], watch->addr, std::min<size_t>( watch->n, 8 ) );
    std::memcpy( &rec.arg[ 1 ], watch->previous, std::min<size_t>( watch->n, 8 ) );
    dbg::sessionRecord( gSession, rec );
}

void onTick( WolfMod * /*mod*/, void *ctx, void * /*user*/ )
{
    dbg::sessionTick( gSession, uint32_t( static_cast<const WolfTickCtx *>( ctx )->frame ) );
}

// The base and SizeOfImage of a loaded module, or zeros.
std::pair<uintptr_t, uint32_t> moduleRange( const wchar_t *name, uint32_t *timestamp = nullptr )
{
    const auto *base = reinterpret_cast<const uint8_t *>( GetModuleHandleW( name ) );
    if ( base == nullptr )
    {
        return { 0, 0 };
    }
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>( base + reinterpret_cast<const IMAGE_DOS_HEADER *>( base )->e_lfanew );
    if ( timestamp != nullptr )
    {
        *timestamp = nt->FileHeader.TimeDateStamp;
    }
    return { uintptr_t( base ), nt->OptionalHeader.SizeOfImage };
}

// The writer thread: every 100 ms, writes the captures the session has closed.
void writeCaptures( const std::filesystem::path &dir, const std::string &config, uint32_t maxCaptures )
{
    uint32_t timestamp = 0;
    const auto [ mainBase, mainSize ] = moduleRange( L"main.dll", &timestamp );
    const auto [ kernelBase, kernelSize ] = moduleRange( L"flower_kernel.dll" );
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency( &frequency );
    for ( ;; )
    {
        for ( dbg::Capture &c : dbg::sessionTakeReady( gSession ) )
        {
            SYSTEMTIME t;
            GetLocalTime( &t );
            char localTime[ 32 ];
            std::snprintf( localTime, sizeof( localTime ), "%04u-%02u-%02u %02u:%02u:%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond );
            c.info = { { "wolf.version", gApi->runtimeVersion },
                       { "tools.version", WOLF_TOOLS_VERSION },
                       { "game.timestamp", dbg::hex( timestamp ) },
                       { "main.base", dbg::hex( mainBase ) },
                       { "main.size", dbg::hex( mainSize ) },
                       { "kernel.base", dbg::hex( kernelBase ) },
                       { "kernel.size", dbg::hex( kernelSize ) },
                       { "qpc.frequency", std::to_string( frequency.QuadPart ) },
                       { "local.time", localTime } };
            c.config = config;
            std::string error;
            const std::filesystem::path path = dbg::captureWrite( c, dir, &error );
            if ( path.empty() )
            {
                log( WOLF_LOG_WARN, "capture " + c.trigger + " not written: " + error );
            }
            else
            {
                // The directory name is ASCII; the full path may not convert to a narrow string.
                log( WOLF_LOG_INFO, "capture " + path.filename().string() + ": " + std::to_string( c.records.size() ) + " records" +
                                        ( c.truncated ? ", truncated: the window outran the ring" : "" ) );
            }
            if ( c.number == maxCaptures )
            {
                log( WOLF_LOG_WARN, "capture limit of " + std::to_string( maxCaptures ) + " reached; no more captures this launch" );
            }
        }
        Sleep( 100 );
    }
}

WolfHandle install( const dbg::CatalogEntry &entry, uint16_t index )
{
    if ( entry.kind == dbg::ProbeKind::Site )
    {
        return gApi->on( gMod, entry.symbol, onSite, reinterpret_cast<void *>( uintptr_t( index ) ), WOLF_LIFETIME_PROCESS );
    }
    void *detourFn = entry.kind == dbg::ProbeKind::Completion ? reinterpret_cast<void *>( &completeDetour ) : kDetours[ index ];
    if ( entry.symbol != nullptr )
    {
        return gApi->hook( gMod, entry.symbol, detourFn, &gOriginal[ index ] );
    }
    return gApi->hookRaw( gMod, entry.module == dbg::ProbeModule::Kernel ? "flower_kernel.dll" : "main.dll", entry.rva, detourFn, &gOriginal[ index ] );
}

} // namespace

extern "C" __declspec( dllexport ) int32_t wolf_mod_init( const WolfApi *api, WolfMod *mod )
{
    gApi = api;
    gMod = mod;
    const std::filesystem::path modDir( reinterpret_cast<const char8_t *>( api->modPath( mod ) ) );
    std::ifstream file( modDir / "capture.cfg", std::ios::binary );
    if ( !file )
    {
        log( WOLF_LOG_INFO, "debugger idle: no capture.cfg" );
        return 0;
    }
    const std::string text( ( std::istreambuf_iterator<char>( file ) ), {} );
    const dbg::ModuleBases bases{ .main = moduleRange( L"main.dll" ).first, .kernel = moduleRange( L"flower_kernel.dll" ).first };
    dbg::ConfigError error;
    const std::optional<dbg::Config> config = dbg::configParse( text, bases, &error );
    if ( !config )
    {
        log( WOLF_LOG_ERROR, "capture.cfg line " + std::to_string( error.line ) + ": " + error.message + "; the debugger is idle" );
        return 0;
    }
    gSession = dbg::sessionCreate( *config );

    for ( size_t w = 0; w < config->watches.size(); ++w )
    {
        const dbg::WatchUse &watch = config->watches[ w ];
        if ( api->watch( mod, reinterpret_cast<const void *>( watch.address ), watch.size, onWatch, reinterpret_cast<void *>( uintptr_t( w ) ),
                         WOLF_LIFETIME_PROCESS ) == 0 )
        {
            log( WOLF_LOG_WARN, "watch " + watch.name + " did not install" );
        }
    }
    size_t probes = 0;
    for ( const dbg::ProbeUse &probe : config->probes )
    {
        if ( install( dbg::kCatalog[ probe.catalog ], probe.catalog ) == 0 )
        {
            log( WOLF_LOG_WARN, std::string( "probe " ) + dbg::kCatalog[ probe.catalog ].name + " did not install" );
            continue;
        }
        ++probes;
    }
    api->on( mod, WOLF_SITE_TICK, onTick, nullptr, WOLF_LIFETIME_PROCESS );

    const std::filesystem::path captures = modDir.parent_path().parent_path().parent_path() / "logs" / "captures";
    std::thread( writeCaptures, captures, text, config->maxCaptures ).detach();
    log( WOLF_LOG_INFO, "debugger: " + std::to_string( probes ) + " probes, " + std::to_string( config->watches.size() ) + " watches, " +
                            std::to_string( config->triggers.size() ) + " triggers" );
    return 0;
}
