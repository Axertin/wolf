#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <utility>

#include "dbg.hpp"

namespace dbg {

namespace {

// A read outside the bytes, or bytes that contradict the layout. It stops the decoder, which leaves what it decoded so far and an Error node.
struct DecodeError
{
    size_t offset;
    std::string message;
};

Node number( std::string key, uint64_t value )
{
    return { .kind = Node::Number, .key = std::move( key ), .number = value, .text = {}, .children = {} };
}

Node text( std::string key, std::string value )
{
    return { .kind = Node::Text, .key = std::move( key ), .number = 0, .text = std::move( value ), .children = {} };
}

Node group( Node::Kind kind, std::string key )
{
    return { .kind = kind, .key = std::move( key ), .number = 0, .text = {}, .children = {} };
}

template <typename T>
T read( std::span<const uint8_t> b, size_t at )
{
    if ( at > b.size() || b.size() - at < sizeof( T ) )
    {
        throw DecodeError{ .offset = at, .message = std::to_string( sizeof( T ) ) + " bytes past the end at " + std::to_string( b.size() ) };
    }
    T value;
    std::memcpy( &value, b.data() + at, sizeof( T ) );
    return value;
}

// A fixed-size char field, without its trailing NULs and spaces.
std::string chars( std::span<const uint8_t> b, size_t at, size_t n )
{
    read<uint8_t>( b, at + n - 1 );
    std::string s( reinterpret_cast<const char *>( b.data() + at ), n );
    s.erase( std::min( s.find( '\0' ), s.size() ) );
    s.erase( s.find_last_not_of( ' ' ) + 1 );
    return s;
}

Node decodeWith( std::span<const uint8_t> bytes, void ( *decode )( std::span<const uint8_t>, Node & ) )
{
    Node root = group( Node::Object, "" );
    try
    {
        decode( bytes, root );
    }
    catch ( const DecodeError &e )
    {
        root.children.push_back( { .kind = Node::Error, .key = "error", .number = e.offset, .text = e.message, .children = {} } );
    }
    return root;
}

// ---- runofs: u32 n; u32 ofs[ n ]; char tag[ n ][ 4 ]. Each entry has a 0x20-byte descriptor before it: u32 occurrence; u32 texFlag; char type[ 4 ];
// char name[ 20 ]. When the last tag is ROF, the entry it points at is the footer: "RUNOFS64" then int64 ofs64[ n ], which texture fixup rewrites. A DDP
// entry is itself a runofs container.

// The RUNOFS64 footer: each ofs64 that differs from its table offset shows beside it.
void runofsFooter( std::span<const uint8_t> b, size_t footer, const std::vector<uint32_t> &ofs, Node &entries )
{
    if ( chars( b, footer, 8 ) != "RUNOFS64" )
    {
        throw DecodeError{ .offset = footer, .message = "the footer has no RUNOFS64" };
    }
    entries.children.back().children.push_back( text( "footer", "RUNOFS64" ) );
    for ( size_t i = 0; i < ofs.size(); ++i )
    {
        const auto ofs64 = read<int64_t>( b, footer + 8 + ( i * 8 ) );
        if ( std::cmp_not_equal( ofs64, ofs[ i ] ) )
        {
            entries.children[ i ].children.push_back( number( "offset64", uint64_t( ofs64 ) ) );
        }
    }
}

// An entry ends at the next entry's descriptor, or at the footer.
size_t entryEnd( const std::vector<uint32_t> &ofs, size_t i, size_t footer )
{
    size_t end = footer;
    for ( const uint32_t other : ofs )
    {
        if ( other > ofs[ i ] && other != footer )
        {
            end = std::min( end, size_t( other ) - 0x20 );
        }
    }
    return std::max( end, size_t( ofs[ i ] ) );
}

void runofs( std::span<const uint8_t> b, Node &out, int depth )
{
    const auto n = read<uint32_t>( b, 0 );
    const size_t headerEnd = 4 + ( size_t( n ) * 8 );
    if ( headerEnd > b.size() )
    {
        throw DecodeError{ .offset = 0, .message = std::to_string( n ) + " entries do not fit in " + std::to_string( b.size() ) + " bytes" };
    }
    std::vector<uint32_t> ofs( n );
    for ( uint32_t i = 0; i < n; ++i )
    {
        ofs[ i ] = read<uint32_t>( b, 4 + ( size_t( i ) * 4 ) );
    }
    const bool hasFooter = n > 0 && chars( b, 4 + ( size_t( n ) * 4 ) + ( size_t( n - 1 ) * 4 ), 4 ) == "ROF";
    const size_t footer = hasFooter ? std::min<size_t>( ofs[ n - 1 ], b.size() ) : b.size();

    Node &entries = out.children.emplace_back( group( Node::List, "entries" ) );
    for ( uint32_t i = 0; i < n; ++i )
    {
        Node &entry = entries.children.emplace_back( group( Node::Object, std::to_string( i ) ) );
        const std::string tag = chars( b, 4 + ( size_t( n ) * 4 ) + ( size_t( i ) * 4 ), 4 );
        entry.children.push_back( text( "tag", tag ) );
        entry.children.push_back( number( "offset", ofs[ i ] ) );
        if ( ofs[ i ] == 0 || ( hasFooter && i == n - 1 ) )
        {
            continue;
        }
        if ( ofs[ i ] > b.size() )
        {
            throw DecodeError{ .offset = 4 + ( size_t( i ) * 4 ), .message = "entry " + std::to_string( i ) + " starts past the end" };
        }
        if ( ofs[ i ] < headerEnd + 0x20 )
        {
            throw DecodeError{ .offset = ofs[ i ], .message = "entry " + std::to_string( i ) + "'s descriptor starts inside the header" };
        }
        const size_t d = ofs[ i ] - 0x20;
        entry.children.push_back( number( "occurrence", read<uint32_t>( b, d ) ) );
        entry.children.push_back( number( "texFlag", read<uint32_t>( b, d + 4 ) ) );
        entry.children.push_back( text( "type", chars( b, d + 8, 4 ) ) );
        entry.children.push_back( text( "name", chars( b, d + 12, 20 ) ) );
        const size_t end = entryEnd( ofs, i, footer );
        entry.children.push_back( number( "size", end - ofs[ i ] ) );
        if ( tag == "DDP" && depth < 4 )
        {
            try
            {
                runofs( b.subspan( ofs[ i ], end - ofs[ i ] ), entry, depth + 1 );
            }
            catch ( DecodeError &e )
            {
                e.offset += ofs[ i ];
                throw;
            }
        }
    }
    if ( hasFooter )
    {
        runofsFooter( b, footer, ofs, entries );
    }
}

// ---- fixed-size structures, as arrays of records

struct Field
{
    const char *name;
    uint16_t offset;
    char type; // b u8, h i16, w u32, i i32, q u64, t a 4-char tag
};

void records( std::span<const uint8_t> b, Node &out, size_t size, std::initializer_list<Field> fields, void ( *check )( Node &, size_t ) )
{
    if ( b.size() % size != 0 )
    {
        throw DecodeError{ .offset = b.size() - ( b.size() % size ),
                           .message = std::to_string( b.size() ) + " bytes is not a whole number of " + std::to_string( size ) + "-byte records" };
    }
    Node &list = out.children.emplace_back( group( Node::List, "records" ) );
    for ( size_t at = 0; at < b.size(); at += size )
    {
        Node &rec = list.children.emplace_back( group( Node::Object, std::to_string( at / size ) ) );
        for ( const Field &f : fields )
        {
            const size_t o = at + f.offset;
            switch ( f.type )
            {
            case 'b':
                rec.children.push_back( number( f.name, read<uint8_t>( b, o ) ) );
                break;
            case 'h':
                rec.children.push_back( number( f.name, uint64_t( int64_t( read<int16_t>( b, o ) ) ) ) );
                break;
            case 'w':
                rec.children.push_back( number( f.name, read<uint32_t>( b, o ) ) );
                break;
            case 'i':
                rec.children.push_back( number( f.name, uint64_t( int64_t( read<int32_t>( b, o ) ) ) ) );
                break;
            case 'q':
                rec.children.push_back( number( f.name, read<uint64_t>( b, o ) ) );
                break;
            default:
                rec.children.push_back( text( f.name, chars( b, o, 4 ) ) );
                break;
            }
        }
        if ( check != nullptr )
        {
            check( rec, at );
        }
    }
}

const Node &field( const Node &rec, std::string_view name )
{
    return *std::ranges::find( rec.children, name, &Node::key );
}

// The object registry header.
void registry( std::span<const uint8_t> b, Node &out )
{
    records( b, out, 0x20, { { "groupId", 0, 'w' }, { "heap", 8, 'q' }, { "slotCount", 0x10, 'i' }, { "slots", 0x18, 'q' } }, nullptr );
}

// Object registry slots; a refCount of -100 pins the slot.
constexpr uint64_t kPinnedRefCount = ~uint64_t( 99 ); // -100, sign-extended
void slots( std::span<const uint8_t> b, Node &out )
{
    records( b, out, 0x48,
             { { "state", 0, 'w' },
               { "refCount", 4, 'h' },
               { "pendingCount", 6, 'h' },
               { "flags", 8, 'w' },
               { "resourceKey", 0xc, 'w' },
               { "data", 0x10, 'q' },
               { "heap", 0x18, 'q' },
               { "loadHandle", 0x20, 'w' },
               { "objId", 0x24, 'w' },
               { "memTicket", 0x28, 'i' },
               { "holders.0", 0x2c, 'w' },
               { "holders.1", 0x30, 'w' },
               { "holders.2", 0x34, 'w' },
               { "holders.3", 0x38, 'w' },
               { "residentLatch", 0x3c, 'b' },
               { "trackerHandle", 0x40, 'i' } },
             []( Node &rec, size_t /*at*/ ) {
                 if ( field( rec, "refCount" ).number == kPinnedRefCount )
                 {
                     rec.children.push_back( number( "pinned", 1 ) );
                 }
             } );
}

// GXTextureManager slots: inUse, then an hx::Texture whose tag is HXTX while the slot is in use. A slot that breaks this gets an Error and the rest still
// decode, since a slot can be mid-free when the snapshot is read.
void texpool( std::span<const uint8_t> b, Node &out )
{
    records( b, out, 0x20, { { "inUse", 0, 'w' }, { "tag", 8, 't' }, { "texture2d", 0x10, 'q' }, { "useCount", 0x18, 'i' }, { "altSampler", 0x1c, 'b' } },
             []( Node &rec, size_t at ) {
                 if ( field( rec, "inUse" ).number != 0 && field( rec, "tag" ).text != "HXTX" )
                 {
                     rec.children.push_back(
                         { .kind = Node::Error, .key = "error", .number = at + 8, .text = "the texture tag of an in-use slot is not HXTX", .children = {} } );
                 }
             } );
}

// The wk::mem block header, 0x28 bytes before each allocation.
void memhdr( std::span<const uint8_t> b, Node &out )
{
    records( b, out, 0x28,
             { { "prev", 0, 'q' }, { "next", 8, 'q' }, { "blockSize", 0x10, 'w' }, { "freeSize", 0x14, 'w' }, { "heap", 0x18, 'q' }, { "flags", 0x20, 'w' } },
             nullptr );
}

void printText( const Node &node, int indent, std::string &out )
{
    const std::string pad( size_t( indent ) * 2, ' ' );
    switch ( node.kind )
    {
    case Node::Number: {
        char value[ 24 ];
        if ( int64_t( node.number ) < 0 )
        {
            std::snprintf( value, sizeof( value ), "%" PRId64, int64_t( node.number ) );
        }
        else
        {
            std::snprintf( value, sizeof( value ), "0x%" PRIx64, node.number );
        }
        out += pad + node.key + ": " + value + "\n";
        break;
    }
    case Node::Text:
        out += pad + node.key + ": " + node.text + "\n";
        break;
    case Node::Error: {
        char at[ 24 ];
        std::snprintf( at, sizeof( at ), "0x%" PRIx64, node.number );
        out += pad + "error at " + at + ": " + node.text + "\n";
        break;
    }
    case Node::Object:
    case Node::List:
        if ( !node.key.empty() )
        {
            out += pad + node.key + ":\n";
        }
        for ( const Node &child : node.children )
        {
            printText( child, node.key.empty() ? indent : indent + 1, out );
        }
        break;
    }
}

void jsonString( std::string_view s, std::string &out )
{
    out += '"';
    for ( const char c : s )
    {
        if ( c == '"' || c == '\\' )
        {
            out += '\\';
            out += c;
        }
        else if ( uint8_t( c ) < 0x20 || uint8_t( c ) >= 0x80 )
        {
            char escaped[ 8 ];
            std::snprintf( escaped, sizeof( escaped ), "\\u%04x", unsigned( uint8_t( c ) ) );
            out += escaped;
        }
        else
        {
            out += c;
        }
    }
    out += '"';
}

void printJson( const Node &node, std::string &out )
{
    switch ( node.kind )
    {
    case Node::Number:
        out += int64_t( node.number ) < 0 ? std::to_string( int64_t( node.number ) ) : std::to_string( node.number );
        break;
    case Node::Text:
        jsonString( node.text, out );
        break;
    case Node::Error:
        out += "{\"error\":";
        jsonString( node.text, out );
        out += ",\"offset\":" + std::to_string( node.number ) + "}";
        break;
    case Node::Object:
    case Node::List:
        out += node.kind == Node::Object ? '{' : '[';
        for ( size_t i = 0; i < node.children.size(); ++i )
        {
            out += i == 0 ? "" : ",";
            if ( node.kind == Node::Object )
            {
                jsonString( node.children[ i ].key, out );
                out += ':';
            }
            printJson( node.children[ i ], out );
        }
        out += node.kind == Node::Object ? '}' : ']';
        break;
    }
}

} // namespace

Decoder findDecoder( std::string_view tag )
{
    static constexpr std::pair<std::string_view, Decoder> kDecoders[] = {
        { "runofs",
          []( std::span<const uint8_t> b ) { return decodeWith( b, []( std::span<const uint8_t> bytes, Node &out ) { runofs( bytes, out, 0 ); } ); } },
        { "objres.registry", []( std::span<const uint8_t> b ) { return decodeWith( b, registry ); } },
        { "objres.slots", []( std::span<const uint8_t> b ) { return decodeWith( b, slots ); } },
        { "texpool.slots", []( std::span<const uint8_t> b ) { return decodeWith( b, texpool ); } },
        { "memhdr", []( std::span<const uint8_t> b ) { return decodeWith( b, memhdr ); } },
    };
    const auto *it = std::ranges::find( kDecoders, tag, &std::pair<std::string_view, Decoder>::first );
    return it == std::end( kDecoders ) ? nullptr : it->second;
}

std::string printText( const Node &node )
{
    std::string out;
    printText( node, 0, out );
    return out;
}

std::string printJson( const Node &node )
{
    std::string out;
    printJson( node, out );
    return out;
}

} // namespace dbg
