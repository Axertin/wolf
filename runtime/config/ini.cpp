#include "config/ini.hpp"

#include <cstddef>

namespace wolf {

namespace {

std::string_view trim( std::string_view text )
{
    constexpr std::string_view kSpace = " \t\r";
    const size_t first = text.find_first_not_of( kSpace );
    if ( first == std::string_view::npos )
    {
        return {};
    }
    return text.substr( first, text.find_last_not_of( kSpace ) - first + 1 );
}

} // namespace

IniReader iniOpen( std::string_view text )
{
    if ( text.starts_with( "\xEF\xBB\xBF" ) )
    {
        text.remove_prefix( 3 );
    }
    IniReader reader;
    reader.rest = text;
    return reader;
}

IniItem iniNext( IniReader &reader, std::string_view *key, std::string_view *value )
{
    while ( !reader.rest.empty() )
    {
        const size_t end = reader.rest.find( '\n' );
        const std::string_view line = trim( reader.rest.substr( 0, end ) );
        reader.rest = end == std::string_view::npos ? std::string_view{} : reader.rest.substr( end + 1 );
        ++reader.line;
        if ( line.empty() || line[ 0 ] == ';' || line[ 0 ] == '#' )
        {
            continue;
        }
        if ( line[ 0 ] == '[' && line.back() == ']' )
        {
            reader.section = trim( line.substr( 1, line.size() - 2 ) );
            continue;
        }
        const size_t eq = line.find( '=' );
        *key = eq == std::string_view::npos ? std::string_view{} : trim( line.substr( 0, eq ) );
        if ( key->empty() )
        {
            return IniItem::Malformed;
        }
        *value = trim( line.substr( eq + 1 ) );
        return IniItem::Pair;
    }
    return IniItem::End;
}

bool parseInt( std::string_view text, int64_t *out )
{
    bool negative = false;
    if ( !text.empty() && ( text[ 0 ] == '-' || text[ 0 ] == '+' ) )
    {
        negative = text[ 0 ] == '-';
        text.remove_prefix( 1 );
    }
    uint64_t base = 10;
    if ( text.starts_with( "0x" ) || text.starts_with( "0X" ) )
    {
        base = 16;
        text.remove_prefix( 2 );
    }
    if ( text.empty() )
    {
        return false;
    }
    // The magnitude of INT64_MIN is one past INT64_MAX.
    const uint64_t limit = negative ? uint64_t{ 1 } << 63 : ( uint64_t{ 1 } << 63 ) - 1;
    uint64_t magnitude = 0;
    for ( const char c : text )
    {
        uint64_t digit = 0;
        if ( c >= '0' && c <= '9' )
        {
            digit = static_cast<uint64_t>( c - '0' );
        }
        else if ( base == 16 && ( c | 0x20 ) >= 'a' && ( c | 0x20 ) <= 'f' )
        {
            digit = static_cast<uint64_t>( ( c | 0x20 ) - 'a' ) + 10;
        }
        else
        {
            return false;
        }
        if ( magnitude > ( limit - digit ) / base )
        {
            return false;
        }
        magnitude = ( magnitude * base ) + digit;
    }
    *out = negative ? static_cast<int64_t>( ~magnitude + 1 ) : static_cast<int64_t>( magnitude );
    return true;
}

} // namespace wolf
