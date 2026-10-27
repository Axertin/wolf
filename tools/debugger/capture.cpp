#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "dbg.hpp"

namespace fs = std::filesystem;

namespace dbg {

namespace {

constexpr char kMagic[ 8 ] = { 'W', 'O', 'L', 'F', 'R', 'E', 'C', '1' };

void writeFile( const fs::path &file, std::initializer_list<std::string_view> parts )
{
    std::ofstream out( file, std::ios::binary );
    for ( const std::string_view part : parts )
    {
        out.write( part.data(), std::streamsize( part.size() ) );
    }
    if ( !out )
    {
        throw std::runtime_error( "cannot write " + file.filename().string() );
    }
}

std::string readFile( const fs::path &file )
{
    std::ifstream in( file, std::ios::binary );
    if ( !in )
    {
        throw std::runtime_error( "cannot read " + file.filename().string() );
    }
    return { std::istreambuf_iterator<char>( in ), {} };
}

std::string_view bytesOf( const auto &v )
{
    return { reinterpret_cast<const char *>( v.data() ), v.size() * sizeof( v[ 0 ] ) };
}

std::string joined( const std::vector<std::string> &parts )
{
    std::string out;
    for ( const std::string &p : parts )
    {
        out += ( out.empty() ? "" : "," ) + p;
    }
    return out;
}

std::vector<std::string> split( std::string_view text )
{
    std::vector<std::string> parts;
    while ( !text.empty() )
    {
        const size_t comma = std::min( text.find( ',' ), text.size() );
        parts.emplace_back( text.substr( 0, comma ) );
        text.remove_prefix( std::min( comma + 1, text.size() ) );
    }
    return parts;
}

std::string manifest( const Capture &c )
{
    std::ostringstream m;
    m << "format=1\ntrigger=" << c.trigger << "\nnumber=" << c.number << "\nfire.frame=" << c.fireFrame << "\nfire.time=" << c.fireTime
      << "\ntruncated=" << int( c.truncated ) << "\n";
    for ( const auto &[ key, value ] : c.info )
    {
        m << key << "=" << value << "\n";
    }
    for ( const auto &[ id, names ] : c.probes )
    {
        m << "probe." << id << "=" << names.name << " " << joined( names.args ) << "\n";
    }
    for ( size_t i = 0; i < c.watches.size(); ++i )
    {
        m << "watch." << i << "=" << c.watches[ i ] << "\n";
    }
    for ( size_t i = 0; i < c.triggers.size(); ++i )
    {
        m << "trigger." << i << "=" << c.triggers[ i ] << "\n";
    }
    for ( size_t i = 0; i < c.snapshots.size(); ++i )
    {
        const Snapshot &s = c.snapshots[ i ];
        const std::string key = "snapshot." + std::to_string( i );
        m << key << ".name=" << s.name << "\n"
          << key << ".decoder=" << s.decoder << "\n"
          << key << ".address=" << hex( s.address ) << "\n"
          << key << ".size=" << s.bytes.size() << "\n"
          << key << ".error=" << s.error << "\n";
    }
    return m.str();
}

Capture load( const fs::path &dir )
{
    std::map<std::string, std::string> keys;
    std::istringstream manifestText( readFile( dir / "manifest.txt" ) );
    for ( std::string line; std::getline( manifestText, line ); )
    {
        const size_t eq = line.find( '=' );
        if ( eq != std::string::npos )
        {
            keys[ line.substr( 0, eq ) ] = line.substr( eq + 1 );
        }
    }
    if ( keys[ "format" ] != "1" )
    {
        throw std::runtime_error( "manifest.txt has format '" + keys[ "format" ] + "', not 1" );
    }
    const auto number = [ & ]( const std::string &key ) {
        if ( keys[ key ].empty() )
        {
            throw std::runtime_error( "manifest.txt has no " + key );
        }
        return std::stoull( keys[ key ], nullptr, 0 );
    };

    Capture c;
    c.trigger = keys[ "trigger" ];
    c.number = uint32_t( number( "number" ) );
    c.fireFrame = uint32_t( number( "fire.frame" ) );
    c.fireTime = number( "fire.time" );
    c.truncated = keys[ "truncated" ] == "1";
    for ( const auto &[ key, value ] : keys )
    {
        const size_t dot = key.find( '.' );
        const std::string group = key.substr( 0, dot );
        if ( group == "probe" )
        {
            const size_t space = value.find( ' ' );
            c.probes[ uint32_t( std::stoul( key.substr( dot + 1 ) ) ) ] = { .name = value.substr( 0, space ),
                                                                            .args = split( space == std::string::npos ? "" : value.substr( space + 1 ) ) };
        }
        else if ( group != "watch" && group != "trigger" && group != "snapshot" && group != "format" && group != "number" && group != "fire" &&
                  group != "truncated" )
        {
            c.info[ key ] = value;
        }
    }
    for ( size_t i = 0; keys.contains( "watch." + std::to_string( i ) ); ++i )
    {
        c.watches.push_back( keys[ "watch." + std::to_string( i ) ] );
    }
    for ( size_t i = 0; keys.contains( "trigger." + std::to_string( i ) ); ++i )
    {
        c.triggers.push_back( keys[ "trigger." + std::to_string( i ) ] );
    }
    for ( size_t i = 0; keys.contains( "snapshot." + std::to_string( i ) + ".name" ); ++i )
    {
        const std::string key = "snapshot." + std::to_string( i );
        Snapshot &s = c.snapshots.emplace_back();
        s.name = keys[ key + ".name" ];
        s.decoder = keys[ key + ".decoder" ];
        s.error = keys[ key + ".error" ];
        s.address = number( key + ".address" );
        if ( s.error.empty() )
        {
            const std::string file = "snap-" + s.name + ".bin";
            const std::string bytes = readFile( dir / file );
            if ( bytes.size() != number( key + ".size" ) )
            {
                throw std::runtime_error( file + " holds " + std::to_string( bytes.size() ) + " bytes, not " + keys[ key + ".size" ] );
            }
            s.bytes.assign( bytes.begin(), bytes.end() );
        }
    }

    const std::string records = readFile( dir / "records.bin" );
    uint32_t count = 0;
    if ( records.size() >= 12 )
    {
        std::memcpy( &count, records.data() + 8, 4 );
    }
    if ( records.size() < 12 || std::memcmp( records.data(), kMagic, 8 ) != 0 || records.size() != 12 + ( uint64_t( count ) * sizeof( Record ) ) )
    {
        throw std::runtime_error( "records.bin is damaged: " + std::to_string( records.size() ) + " bytes for " + std::to_string( count ) + " records" );
    }
    c.records.resize( count );
    std::memcpy( c.records.data(), records.data() + 12, count * sizeof( Record ) );
    c.config = readFile( dir / "capture.cfg" );
    return c;
}

} // namespace

std::string hex( uint64_t value )
{
    char text[ 24 ];
    std::snprintf( text, sizeof( text ), "0x%llx", static_cast<unsigned long long>( value ) );
    return text;
}

fs::path captureWrite( const Capture &c, const fs::path &capturesDir, std::string *error )
{
    try
    {
        SYSTEMTIME t;
        GetLocalTime( &t );
        char stamp[ 32 ];
        std::snprintf( stamp, sizeof( stamp ), "%04u%02u%02u-%02u%02u%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond );
        const fs::path dir = capturesDir / ( std::string( stamp ) + "-" + c.trigger + "-" + std::to_string( c.number ) );
        fs::path tmp = dir;
        tmp += ".tmp";
        fs::create_directories( tmp );
        writeFile( tmp / "manifest.txt", { manifest( c ) } );
        writeFile( tmp / "capture.cfg", { c.config } );
        const auto count = uint32_t( c.records.size() );
        writeFile( tmp / "records.bin",
                   { std::string_view( kMagic, 8 ), std::string_view( reinterpret_cast<const char *>( &count ), 4 ), bytesOf( c.records ) } );
        for ( const Snapshot &s : c.snapshots )
        {
            if ( s.error.empty() )
            {
                writeFile( tmp / ( "snap-" + s.name + ".bin" ), { bytesOf( s.bytes ) } );
            }
        }
        fs::rename( tmp, dir );
        return dir;
    }
    catch ( const std::exception &e )
    {
        *error = e.what();
        return {};
    }
}

std::optional<Capture> captureLoad( const fs::path &dir, std::string *error )
{
    try
    {
        return load( dir );
    }
    catch ( const std::exception &e )
    {
        *error = dir.filename().string() + ": " + e.what();
        return std::nullopt;
    }
}

std::vector<fs::path> captureList( const fs::path &capturesDir )
{
    std::vector<fs::path> dirs;
    std::error_code ec;
    for ( const fs::directory_entry &entry : fs::directory_iterator( capturesDir, ec ) )
    {
        if ( entry.path().extension() != ".tmp" && fs::exists( entry.path() / "manifest.txt" ) )
        {
            dirs.push_back( entry.path() );
        }
    }
    std::ranges::sort( dirs );
    return dirs;
}

} // namespace dbg
