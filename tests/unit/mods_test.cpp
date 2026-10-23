#include <windows.h>

#include <doctest/doctest.h>

#include <config/config.hpp>
#include <mods/mods.hpp>
#include <string>
#include <vector>

#include "support.hpp"

namespace {

void writeText( const std::wstring &path, const std::string &text )
{
    const HANDLE file = CreateFileW( path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
    REQUIRE( file != INVALID_HANDLE_VALUE );
    DWORD written = 0;
    WriteFile( file, text.data(), static_cast<DWORD>( text.size() ), &written, nullptr );
    CloseHandle( file );
}

// <dir>mods\<name>\ , created.
std::wstring makeMod( const std::wstring &dir, const std::wstring &name )
{
    CreateDirectoryW( ( dir + L"mods" ).c_str(), nullptr );
    const std::wstring modDir = dir + L"mods\\" + name + L"\\";
    CreateDirectoryW( modDir.c_str(), nullptr );
    return modDir;
}

std::vector<std::string> ids( const wolf::ModList &list )
{
    std::vector<std::string> result;
    result.reserve( list.count );
    for ( uint32_t i = 0; i < list.count; ++i )
    {
        result.emplace_back( list.mods[ i ]->id );
    }
    return result;
}

} // namespace

TEST_SUITE( "mods" )
{
    TEST_CASE( "load order is priority, then the id bytewise" )
    {
        const std::wstring dir = freshTempDir();
        writeText( makeMod( dir, L"b" ) + L"mod.ini", "priority = 1\n" );
        writeText( makeMod( dir, L"a" ) + L"mod.ini", "priority = 1\n" );
        makeMod( dir, L"Z" );
        writeText( makeMod( dir, L"c" ) + L"mod.ini", "priority = -1\n" );
        const wolf::ModList list = wolf::modsDiscover( dir.c_str() );
        REQUIRE( list.count == 4 );
        CHECK( ids( list ) == std::vector<std::string>{ "c", "Z", "a", "b" } );
        CHECK( list.mods[ 2 ]->loadIndex == 2 );
    }

    TEST_CASE( "mod.ini sets name, version and priority; a missing one defaults" )
    {
        const std::wstring dir = freshTempDir();
        writeText( makeMod( dir, L"big" ) + L"mod.ini", "name = Big Mod\nversion = 1.2\npriority = 0x10\n" );
        makeMod( dir, L"plain" );
        const wolf::ModList list = wolf::modsDiscover( dir.c_str() );
        REQUIRE( list.count == 2 );
        const WolfMod *big = list.mods[ 1 ];
        CHECK( std::string( big->name ) == "Big Mod" );
        CHECK( std::string( big->version ) == "1.2" );
        CHECK( big->priority == 16 );
        const WolfMod *plain = list.mods[ 0 ];
        CHECK( std::string( plain->name ) == "plain" );
        CHECK( std::string( plain->version ).empty() );
        CHECK( plain->priority == 0 );
    }

    TEST_CASE( "a bad priority is 0" )
    {
        const std::wstring dir = freshTempDir();
        writeText( makeMod( dir, L"m" ) + L"mod.ini", "priority = high\n" );
        const wolf::ModList list = wolf::modsDiscover( dir.c_str() );
        REQUIRE( list.count == 1 );
        CHECK( list.mods[ 0 ]->priority == 0 );
    }

    TEST_CASE( "a priority past 32 bits is 0" )
    {
        const std::wstring dir = freshTempDir();
        writeText( makeMod( dir, L"m" ) + L"mod.ini", "priority = 4294967296\n" );
        const wolf::ModList list = wolf::modsDiscover( dir.c_str() );
        REQUIRE( list.count == 1 );
        CHECK( list.mods[ 0 ]->priority == 0 );
    }

    TEST_CASE( "mod.ini skips a malformed line and unknown keys" )
    {
        const std::wstring dir = freshTempDir();
        writeText( makeMod( dir, L"m" ) + L"mod.ini", "priority = 2\nprority = 5\nno pair here\nprority = 6\n[extra]\nname = x\nname = y\n" );
        const wolf::ModList list = wolf::modsDiscover( dir.c_str() );
        REQUIRE( list.count == 1 );
        CHECK( list.mods[ 0 ]->priority == 2 );
        CHECK( std::string( list.mods[ 0 ]->name ) == "m" );
    }

    TEST_CASE( "a stray file is skipped" )
    {
        const std::wstring dir = freshTempDir();
        makeMod( dir, L"a" );
        writeText( dir + L"mods\\readme.txt", "hi" );
        const wolf::ModList list = wolf::modsDiscover( dir.c_str() );
        CHECK( list.count == 1 );
    }

    TEST_CASE( "a directory with no DLL is asset-only" )
    {
        const std::wstring dir = freshTempDir();
        makeMod( dir, L"a" );
        writeText( makeMod( dir, L"b" ) + L"b.dll", "x" );
        const wolf::ModList list = wolf::modsDiscover( dir.c_str() );
        REQUIRE( list.count == 2 );
        CHECK( list.mods[ 0 ]->dllPath == nullptr );
        REQUIRE( list.mods[ 1 ]->dllPath != nullptr );
        const std::wstring dll = list.mods[ 1 ]->dllPath;
        CHECK( dll.ends_with( L"\\b\\b.dll" ) );
    }

    TEST_CASE( "path ends in a backslash and config is the mod's own" )
    {
        const std::wstring dir = freshTempDir();
        writeText( makeMod( dir, L"a" ) + L"config.ini", "k = 1\n" );
        writeText( makeMod( dir, L"b" ) + L"config.ini", "k = 2\n" );
        const wolf::ModList list = wolf::modsDiscover( dir.c_str() );
        REQUIRE( list.count == 2 );
        CHECK( std::string( list.mods[ 0 ]->path ).ends_with( "\\mods\\a\\" ) );
        CHECK( wolf::configInt( list.mods[ 0 ], "k", 0 ) == 1 );
        CHECK( wolf::configInt( list.mods[ 1 ], "k", 0 ) == 2 );
    }

    TEST_CASE( "a non-ASCII mod directory keeps its id and path as UTF-8" )
    {
        const std::wstring dir = freshTempDir();
        makeMod( dir, L"müd" );
        const wolf::ModList list = wolf::modsDiscover( dir.c_str() );
        REQUIRE( list.count == 1 );
        CHECK( std::string( list.mods[ 0 ]->id ) == "m\xC3\xBC"
                                                    "d" );
        CHECK( std::string( list.mods[ 0 ]->path )
                   .ends_with( "\\mods\\m\xC3\xBC"
                               "d\\" ) );
    }

    TEST_CASE( "a file over 1 MiB is ignored" )
    {
        const std::wstring dir = freshTempDir();
        writeText( makeMod( dir, L"m" ) + L"config.ini", "k = 1\n" + std::string( ( size_t{ 1 } << 20 ), '#' ) );
        const wolf::ModList list = wolf::modsDiscover( dir.c_str() );
        REQUIRE( list.count == 1 );
        CHECK( wolf::configValue( list.mods[ 0 ], "k" ) == nullptr );
    }

    TEST_CASE( "a path too long is skipped" )
    {
        std::wstring dir = L"\\\\?\\" + freshTempDir();
        for ( int i = 0; i < 3; ++i )
        {
            dir += std::wstring( 250, L'd' ) + L"\\";
            REQUIRE( CreateDirectoryW( dir.c_str(), nullptr ) != 0 );
        }
        makeMod( dir, L"fits" );
        makeMod( dir, L"long" + std::wstring( 200, L'x' ) );
        const wolf::ModList list = wolf::modsDiscover( dir.c_str() );
        CHECK( list.count == 1 );
    }

    TEST_CASE( "no mods directory gives an empty list" )
    {
        const std::wstring dir = freshTempDir();
        const wolf::ModList list = wolf::modsDiscover( dir.c_str() );
        CHECK( list.count == 0 );
    }

    TEST_CASE( "modRefuse sets refused" )
    {
        const std::wstring dir = freshTempDir();
        makeMod( dir, L"a" );
        const wolf::ModList list = wolf::modsDiscover( dir.c_str() );
        REQUIRE( list.count == 1 );
        wolf::modRefuse( list.mods[ 0 ], "test" );
        CHECK( list.mods[ 0 ]->refused.load() );
    }
}
