// wolf-capture: lists the captures in a directory, and prints a capture's records or one of its snapshots, decoded.

#include <algorithm>
#include <charconv>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <exception>
#include <iostream>
#include <vector>

#include "dbg.hpp"

namespace dbg {

namespace {

constexpr const char *kUsage = "usage: wolf-capture list DIR\n"
                               "       wolf-capture records CAPTURE [--probe NAME] [--frames A..B] [--json]\n"
                               "       wolf-capture snapshot CAPTURE NAME [--json]\n";

uint64_t infoNumber( const Capture &c, const char *key )
{
    const auto it = c.info.find( key );
    return it == c.info.end() ? 0 : std::strtoull( it->second.c_str(), nullptr, 0 );
}

// The caller as module!VA when it falls inside main.dll or flower_kernel.dll, else as plain hex.
std::string callerName( const Capture &c, uint64_t caller )
{
    for ( const char *module : { "main", "kernel" } )
    {
        const uint64_t base = infoNumber( c, ( std::string( module ) + ".base" ).c_str() );
        const uint64_t size = infoNumber( c, ( std::string( module ) + ".size" ).c_str() );
        if ( base != 0 && caller >= base && caller - base < size )
        {
            return std::string( module ) + "!" + hex( caller - base + kPreferredBase );
        }
    }
    return hex( caller );
}

struct Described
{
    std::string name;
    const char *kind;
    std::vector<std::string> args;
};

Described describe( const Capture &c, const Record &r )
{
    const auto probe = c.probes.find( r.probe );
    const std::string probeName = probe == c.probes.end() ? "probe" + std::to_string( r.probe ) : probe->second.name;
    const std::vector<std::string> probeArgs = probe == c.probes.end() ? std::vector<std::string>{} : probe->second.args;
    switch ( r.kind )
    {
    case RecordKind::Call:
        return { .name = probeName, .kind = "call", .args = probeArgs };
    case RecordKind::Return: {
        std::vector<std::string> args{ "ret" };
        args.insert( args.end(), probeArgs.begin(), probeArgs.begin() + std::min<ptrdiff_t>( 3, ptrdiff_t( probeArgs.size() ) ) );
        return { .name = probeName, .kind = "return", .args = args };
    }
    case RecordKind::Site:
        return { .name = probeName, .kind = "site", .args = probeArgs };
    case RecordKind::Watch:
        return {
            .name = r.probe < c.watches.size() ? c.watches[ r.probe ] : "watch" + std::to_string( r.probe ), .kind = "watch", .args = { "value", "previous" } };
    case RecordKind::Trigger:
        return { .name = r.probe < c.triggers.size() ? c.triggers[ r.probe ] : "trigger" + std::to_string( r.probe ), .kind = "trigger", .args = { "count" } };
    }
    return { .name = probeName, .kind = "unknown", .args = {} };
}

std::string recordLine( const Capture &c, const Record &r, bool json )
{
    const Described d = describe( c, r );
    const std::string path( r.text, strnlen( r.text, sizeof( r.text ) ) );
    const uint64_t frequency = infoNumber( c, "qpc.frequency" );
    const double seconds = frequency == 0 ? 0.0 : double( int64_t( r.time - c.fireTime ) ) / double( frequency );
    if ( json )
    {
        Node line{ .kind = Node::Object, .key = {}, .number = 0, .text = {}, .children = {} };
        const auto add = [ & ]( Node::Kind kind, const char *key, uint64_t number, std::string text ) {
            return &line.children.emplace_back( Node{ .kind = kind, .key = key, .number = number, .text = std::move( text ), .children = {} } );
        };
        add( Node::Number, "frame", r.frame, {} );
        add( Node::Number, "thread", r.thread, {} );
        add( Node::Number, "us", uint64_t( int64_t( seconds * 1e6 ) ), {} );
        add( Node::Text, "probe", 0, d.name );
        add( Node::Text, "kind", 0, d.kind );
        Node *args = add( Node::Object, "args", 0, {} );
        for ( size_t i = 0; i < d.args.size() && i < 4; ++i )
        {
            args->children.push_back( { .kind = Node::Number, .key = d.args[ i ], .number = r.arg[ i ], .text = {}, .children = {} } );
        }
        if ( !path.empty() )
        {
            add( Node::Text, "path", 0, path );
        }
        if ( r.caller != 0 )
        {
            add( Node::Text, "caller", 0, callerName( c, r.caller ) );
        }
        return printJson( line ) + "\n";
    }
    char head[ 64 ];
    std::snprintf( head, sizeof( head ), "%u %u %+.6f ", r.frame, r.thread, seconds );
    std::string out = head + d.name + " " + d.kind;
    for ( size_t i = 0; i < d.args.size() && i < 4; ++i )
    {
        out += " " + d.args[ i ] + "=" + hex( r.arg[ i ] );
    }
    if ( !path.empty() )
    {
        out += " path=\"" + path + "\"";
    }
    if ( r.caller != 0 )
    {
        out += " caller=" + callerName( c, r.caller );
    }
    return out + "\n";
}

bool parseFrame( std::string_view text, uint32_t *out )
{
    const auto [ end, ec ] = std::from_chars( text.data(), text.data() + text.size(), *out );
    return !text.empty() && ec == std::errc() && end == text.data() + text.size();
}

bool hasError( const Node &node )
{
    return node.kind == Node::Error || std::ranges::any_of( node.children, hasError );
}

std::optional<Capture> load( std::string_view dir, std::ostream &err )
{
    std::string error;
    std::optional<Capture> c = captureLoad( std::filesystem::path( dir ), &error );
    if ( !c )
    {
        err << error << "\n";
    }
    return c;
}

int list( std::string_view dir, std::ostream &out, std::ostream &err )
{
    if ( !std::filesystem::is_directory( std::filesystem::path( dir ) ) )
    {
        err << dir << ": not a directory\n";
        return 2;
    }
    int code = 0;
    for ( const std::filesystem::path &path : captureList( std::filesystem::path( dir ) ) )
    {
        const std::optional<Capture> c = load( path.string(), err );
        if ( !c )
        {
            code = 2;
            continue;
        }
        out << path.filename().string() << " " << c->trigger << " " << c->fireFrame << " " << c->records.size() << " " << c->snapshots.size()
            << ( c->truncated ? " truncated" : "" ) << "\n";
    }
    return code;
}

int records( std::span<const std::string_view> args, std::ostream &out, std::ostream &err )
{
    std::string_view probe;
    uint32_t first = 0;
    uint32_t last = UINT32_MAX;
    bool json = false;
    for ( size_t i = 1; i < args.size(); ++i )
    {
        if ( args[ i ] == "--json" )
        {
            json = true;
        }
        else if ( args[ i ] == "--probe" && i + 1 < args.size() )
        {
            probe = args[ ++i ];
        }
        else if ( args[ i ] == "--frames" && i + 1 < args.size() )
        {
            const std::string_view range = args[ ++i ];
            const size_t dots = range.find( ".." );
            if ( dots == std::string_view::npos || !parseFrame( range.substr( 0, dots ), &first ) || !parseFrame( range.substr( dots + 2 ), &last ) )
            {
                err << "--frames takes A..B\n" << kUsage;
                return 1;
            }
        }
        else
        {
            err << kUsage;
            return 1;
        }
    }
    const std::optional<Capture> c = load( args[ 0 ], err );
    if ( !c )
    {
        return 2;
    }
    for ( const Record &r : c->records )
    {
        if ( r.frame >= first && r.frame <= last && ( probe.empty() || describe( *c, r ).name == probe ) )
        {
            out << recordLine( *c, r, json );
        }
    }
    return 0;
}

int snapshot( std::span<const std::string_view> args, std::ostream &out, std::ostream &err )
{
    const bool json = args.size() == 3 && args[ 2 ] == "--json";
    if ( args.size() != 2 && !json )
    {
        err << kUsage;
        return 1;
    }
    const std::optional<Capture> c = load( args[ 0 ], err );
    if ( !c )
    {
        return 2;
    }
    const auto snap = std::ranges::find( c->snapshots, args[ 1 ], &Snapshot::name );
    if ( snap == c->snapshots.end() || !snap->error.empty() )
    {
        err << "snapshot " << args[ 1 ] << ": " << ( snap == c->snapshots.end() ? "not in this capture" : snap->error ) << "\n";
        return 2;
    }
    const Decoder decoder = findDecoder( snap->decoder );
    if ( decoder == nullptr )
    {
        for ( size_t at = 0; at < snap->bytes.size(); at += 16 )
        {
            char line[ ( 16 * 3 ) + 16 ];
            int n = std::snprintf( line, sizeof( line ), "%08zx ", at );
            for ( size_t i = at; i < std::min( at + 16, snap->bytes.size() ); ++i )
            {
                n += std::snprintf( line + n, sizeof( line ) - size_t( n ), " %02x", snap->bytes[ i ] );
            }
            out << line << "\n";
        }
        return 0;
    }
    const Node node = decoder( snap->bytes );
    out << ( json ? printJson( node ) + "\n" : printText( node ) );
    return hasError( node ) ? 2 : 0;
}

// args excludes the program name. Returns 0, 1 for a usage error, or 2 when a capture fails to load or decode.
int captureMain( std::span<const std::string_view> args, std::ostream &out, std::ostream &err )
{
    if ( args.size() == 2 && args[ 0 ] == "list" )
    {
        return list( args[ 1 ], out, err );
    }
    if ( args.size() >= 2 && args[ 0 ] == "records" )
    {
        return records( args.subspan( 1 ), out, err );
    }
    if ( args.size() >= 2 && args[ 0 ] == "snapshot" )
    {
        return snapshot( args.subspan( 1 ), out, err );
    }
    err << kUsage;
    return 1;
}

} // namespace

} // namespace dbg

int main( int argc, char **argv )
{
    try
    {
        const std::vector<std::string_view> args( argv + 1, argv + argc );
        return dbg::captureMain( args, std::cout, std::cerr );
    }
    catch ( const std::exception &e )
    {
        std::cerr << "wolf-capture: " << e.what() << "\n";
        return 2;
    }
}
