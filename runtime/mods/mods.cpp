#include "mods/mods.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <span>
#include <string_view>
#include <utility>

#include "arena/lifetime.hpp"
#include "config/config.hpp"
#include "config/ini.hpp"
#include "core/threading.hpp"
#include "log/log.hpp"
#include "tables/table.hpp"

namespace wolf {

namespace {

constexpr size_t kMaxPath = 1024;
constexpr size_t kMaxFile = size_t{ 1 } << 20;

constinit Table modsTable{ .name = "mods", .stride = sizeof( WolfMod ), .expected = 64, .onceBit = kOnceModsFull };

struct Utf8
{
    char text[ ( kMaxPath * 3 ) + 1 ];
};

// The wide text as NUL-terminated UTF-8 in out.
void toUtf8( std::wstring_view wide, Utf8 &out )
{
    const int n =
        WideCharToMultiByte( CP_UTF8, 0, wide.data(), static_cast<int>( wide.size() ), out.text, static_cast<int>( sizeof( out.text ) - 1 ), nullptr, nullptr );
    out.text[ n > 0 ? n : 0 ] = '\0';
}

// The file's bytes in frame memory, or empty when it is missing, unreadable or over 1 MiB (which logs naming mod).
std::string_view readCapped( WolfMod *mod, const wchar_t *path, const char *file )
{
    const HANDLE handle = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );
    if ( handle == INVALID_HANDLE_VALUE )
    {
        return {};
    }
    LARGE_INTEGER size = {};
    std::string_view result;
    if ( GetFileSizeEx( handle, &size ) != 0 )
    {
        if ( std::cmp_greater( size.QuadPart, kMaxFile ) )
        {
            writeLog( mod, WOLF_LOG_WARN, "mods/%s/%s is over 1 MiB; ignored", mod->id, file );
        }
        else if ( auto *buffer = static_cast<char *>( lifetimeAlloc( nullptr, WOLF_LIFETIME_FRAME, static_cast<size_t>( size.QuadPart ), 1 ) );
                  buffer != nullptr )
        {
            DWORD got = 0;
            if ( ReadFile( handle, buffer, static_cast<DWORD>( size.QuadPart ), &got, nullptr ) != 0 )
            {
                result = std::string_view( buffer, got );
            }
        }
    }
    CloseHandle( handle );
    return result;
}

bool inOrder( const WolfMod *a, const WolfMod *b )
{
    return a->priority != b->priority ? a->priority < b->priority : std::strcmp( a->id, b->id ) < 0;
}

// True when a line of text before line is a pair with this section and key. Each unknown key rescans the lines above it, which costs quadratic time only in
// a file made of unknown keys, and mod.ini is capped at 1 MiB.
bool seenBefore( std::string_view text, uint32_t line, std::string_view section, std::string_view key )
{
    IniReader reader = iniOpen( text );
    std::string_view earlierKey;
    std::string_view value;
    for ( IniItem item = iniNext( reader, &earlierKey, &value ); item != IniItem::End && reader.line < line; item = iniNext( reader, &earlierKey, &value ) )
    {
        if ( item == IniItem::Pair && earlierKey == key && reader.section == section )
        {
            return true;
        }
    }
    return false;
}

// Reads mod.ini and config.ini of the mod whose directory is dir (trailing backslash, room left for the longest file name).
void readFiles( WolfMod *mod, wchar_t *dir, size_t dirLength )
{
    char label[ ( kMaxPath * 3 ) + 32 ];
    std::snprintf( label, sizeof( label ), "mods/%s/mod.ini", mod->id );
    std::swprintf( dir + dirLength, kMaxPath - dirLength, L"mod.ini" );
    const std::string_view text = readCapped( mod, dir, "mod.ini" );
    IniReader reader = iniOpen( text );
    std::string_view key;
    std::string_view value;
    for ( IniItem item = iniNext( reader, &key, &value ); item != IniItem::End; item = iniNext( reader, &key, &value ) )
    {
        if ( item == IniItem::Malformed )
        {
            writeLog( mod, WOLF_LOG_WARN, "%s line %u has no key = value; skipped", label, reader.line );
        }
        else if ( !reader.section.empty() || ( key != "name" && key != "version" && key != "priority" ) )
        {
            if ( !seenBefore( text, reader.line, reader.section, key ) )
            {
                writeLog( mod, WOLF_LOG_WARN, "%s: unknown key %.*s%s%.*s; ignored", label, static_cast<int>( reader.section.size() ), reader.section.data(),
                          reader.section.empty() ? "" : ".", static_cast<int>( key.size() ), key.data() );
            }
        }
        else if ( key == "name" )
        {
            const RuntimeLock lock;
            mod->name = processString( value );
        }
        else if ( key == "version" )
        {
            const RuntimeLock lock;
            mod->version = processString( value );
        }
        else
        {
            int64_t parsed = 0;
            if ( parseInt( value, &parsed ) && parsed >= INT32_MIN && parsed <= INT32_MAX )
            {
                mod->priority = static_cast<int32_t>( parsed );
            }
            else
            {
                writeLog( mod, WOLF_LOG_WARN, "%s: priority %.*s is not an integer; using 0", label, static_cast<int>( value.size() ), value.data() );
            }
        }
    }
    std::swprintf( dir + dirLength, kMaxPath - dirLength, L"config.ini" );
    std::snprintf( label, sizeof( label ), "mods/%s/config.ini", mod->id );
    configParse( mod, readCapped( mod, dir, "config.ini" ), label );
}

} // namespace

ModList modsDiscover( const wchar_t *gameDir )
{
    const std::wstring_view root( gameDir );
    wchar_t pattern[ kMaxPath ];
    if ( root.size() + 8 >= kMaxPath )
    {
        writeLog( nullptr, WOLF_LOG_WARN, "mods: game directory path is too long" );
        return {};
    }
    std::swprintf( pattern, kMaxPath, L"%lsmods\\*", gameDir );
    WIN32_FIND_DATAW found = {};
    const HANDLE find = FindFirstFileExW( pattern, FindExInfoBasic, &found, FindExSearchNameMatch, nullptr, 0 );
    if ( find == INVALID_HANDLE_VALUE )
    {
        writeLog( nullptr, WOLF_LOG_WARN, "mods: no mods directory" );
        return {};
    }

    uint32_t first = 0;
    uint32_t end = 0;
    do
    {
        const std::wstring_view name( found.cFileName );
        if ( name == L"." || name == L".." )
        {
            continue;
        }
        Utf8 id;
        toUtf8( name, id );
        if ( ( found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) == 0 )
        {
            writeLog( nullptr, WOLF_LOG_WARN, "mods: skipping stray file %s", id.text );
            continue;
        }
        // <root>mods\<name>\ plus the longest file inside: <name>.dll or config.ini.
        const size_t dirLength = root.size() + 5 + name.size() + 1;
        if ( dirLength + std::max<size_t>( name.size() + 4, 10 ) >= kMaxPath )
        {
            writeLog( nullptr, WOLF_LOG_WARN, "mods: path of %s is too long; skipped", id.text );
            continue;
        }
        wchar_t dir[ kMaxPath ];
        std::swprintf( dir, kMaxPath, L"%lsmods\\%ls\\", gameDir, found.cFileName );
        Utf8 path;
        toUtf8( dir, path );

        WolfMod *mod = nullptr;
        {
            const RuntimeLock lock;
            uint32_t index = 0;
            mod = static_cast<WolfMod *>( tableAppend( modsTable, nullptr, &index ) );
            if ( mod == nullptr )
            {
                break;
            }
            if ( first == 0 )
            {
                first = index;
            }
            end = index + 1;
            mod->id = processString( id.text );
            mod->name = mod->id;
            mod->version = "";
            mod->path = processString( path.text );
        }
        std::swprintf( dir + dirLength, kMaxPath - dirLength, L"%ls.dll", found.cFileName );
        if ( GetFileAttributesW( dir ) != INVALID_FILE_ATTRIBUTES )
        {
            const RuntimeLock lock;
            mod->dllPath = processWideString( dir );
        }
        dir[ dirLength ] = L'\0';
        readFiles( mod, dir, dirLength );
    } while ( FindNextFileW( find, &found ) != 0 );
    FindClose( find );

    ModList list;
    if ( first == 0 )
    {
        return list;
    }
    list.count = end - first;
    list.mods = static_cast<WolfMod **>( lifetimeAlloc( nullptr, WOLF_LIFETIME_PROCESS, list.count * sizeof( WolfMod * ), alignof( WolfMod * ) ) );
    if ( list.mods == nullptr )
    {
        list.count = 0;
        return list;
    }
    for ( uint32_t i = 0; i < list.count; ++i )
    {
        list.mods[ i ] = static_cast<WolfMod *>( tableAt( modsTable, first + i ) );
    }
    std::ranges::sort( std::span( list.mods, list.count ), inOrder );
    for ( uint32_t i = 0; i < list.count; ++i )
    {
        WolfMod *mod = list.mods[ i ];
        mod->loadIndex = i;
        writeLog( nullptr, WOLF_LOG_INFO, "mod %s %s priority %d, %s", mod->id, mod->version, mod->priority, mod->dllPath != nullptr ? "code" : "assets only" );
    }
    return list;
}

void modRefuse( WolfMod *mod, const char *reason )
{
    mod->refused.store( true );
    writeLog( mod, WOLF_LOG_WARN, "mod %s refused: %s; its registrations, assets and config are dropped", mod->id, reason );
}

void modsLoad( const ModList &list, const WolfApi *api )
{
    for ( uint32_t i = 0; i < list.count; ++i )
    {
        WolfMod *mod = list.mods[ i ];
        if ( mod->dllPath == nullptr )
        {
            continue;
        }
        // The scope covers the DLL's DllMain and static constructors too, so a crash in any of them names the mod.
        const DispatchScope scope( mod );
        char reason[ 64 ];
        // A file that is not a DLL otherwise raises the loader's modal error box, which blocks boot until the player closes it.
        DWORD errorMode = 0;
        SetThreadErrorMode( SEM_FAILCRITICALERRORS, &errorMode );
        const HMODULE dll = LoadLibraryExW( mod->dllPath, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH );
        const DWORD loadError = GetLastError();
        SetThreadErrorMode( errorMode, nullptr );
        if ( dll == nullptr )
        {
            std::snprintf( reason, sizeof( reason ), "its DLL did not load (error %lu)", loadError );
            modRefuse( mod, reason );
            continue;
        }
        const auto init = reinterpret_cast<WolfModInitFn>( GetProcAddress( dll, "wolf_mod_init" ) );
        if ( init == nullptr )
        {
            modRefuse( mod, "its DLL exports no wolf_mod_init" );
            continue;
        }
        if ( const int32_t result = init( api, mod ); result != 0 )
        {
            std::snprintf( reason, sizeof( reason ), "wolf_mod_init returned %d", result );
            modRefuse( mod, reason );
        }
    }
}

} // namespace wolf
