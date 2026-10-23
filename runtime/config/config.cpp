#include "config/config.hpp"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <system_error>

#include "arena/lifetime.hpp"
#include "config/ini.hpp"
#include "core/threading.hpp"
#include "log/log.hpp"
#include "tables/table.hpp"

namespace wolf {

namespace {

struct ConfigEntry
{
    const char *key;
    const char *value;
};

constinit Table config{ .name = "config", .stride = sizeof( ConfigEntry ), .expected = 4096, .onceBit = kOnceConfigFull };

bool equalsNoCase( std::string_view text, std::string_view lower )
{
    if ( text.size() != lower.size() )
    {
        return false;
    }
    for ( size_t i = 0; i < text.size(); ++i )
    {
        const char c = text[ i ] >= 'A' && text[ i ] <= 'Z' ? static_cast<char>( text[ i ] + ( 'a' - 'A' ) ) : text[ i ];
        if ( c != lower[ i ] )
        {
            return false;
        }
    }
    return true;
}

// "section.key" in process memory; the caller holds the runtime lock. Null when out of memory.
const char *joinKey( WolfMod *mod, std::string_view section, std::string_view key )
{
    auto *joined = static_cast<char *>( lifetimeAllocLocked( mod, WOLF_LIFETIME_PROCESS, section.size() + 1 + key.size() + 1, 1 ) );
    if ( joined != nullptr )
    {
        std::ranges::copy( section, joined );
        joined[ section.size() ] = '.';
        std::ranges::copy( key, joined + section.size() + 1 ); // the arena hands out zeroed memory, so the terminator is already there
    }
    return joined;
}

} // namespace

void configParse( WolfMod *mod, std::string_view text, const char *label )
{
    const RuntimeLock lock;
    mod->firstConfig = tableEnd( config ) > 1 ? tableEnd( config ) : 1;
    mod->configCount = 0;
    IniReader reader = iniOpen( text );
    std::string_view key;
    std::string_view value;
    for ( IniItem item = iniNext( reader, &key, &value ); item != IniItem::End; item = iniNext( reader, &key, &value ) )
    {
        if ( item == IniItem::Malformed )
        {
            writeLog( mod, WOLF_LOG_WARN, "%s line %u has no key = value; skipped", label, reader.line );
            continue;
        }
        uint32_t index = 0;
        auto *entry = static_cast<ConfigEntry *>( tableAppend( config, mod, &index ) );
        if ( entry == nullptr )
        {
            return;
        }
        entry->key = reader.section.empty() ? processString( key ) : joinKey( mod, reader.section, key );
        entry->value = processString( value );
        ++mod->configCount;
    }
}

// The mod's range is final once configParse returns, so the scan needs no lock.
const char *configValue( WolfMod *mod, const char *key )
{
    for ( uint32_t i = mod->firstConfig + mod->configCount; i-- > mod->firstConfig; )
    {
        const auto *entry = static_cast<const ConfigEntry *>( tableAt( config, i ) );
        if ( entry->key != nullptr && std::strcmp( entry->key, key ) == 0 )
        {
            return entry->value;
        }
    }
    return nullptr;
}

int64_t configInt( WolfMod *mod, const char *key, int64_t fallback )
{
    const char *value = configValue( mod, key );
    int64_t parsed = 0;
    return value != nullptr && parseInt( value, &parsed ) ? parsed : fallback;
}

// from_chars ignores the C locale, which the game and other mods share, and stops at a hex prefix. It takes no leading '+', so one is skipped here, and a
// digit or '.' must follow the sign, which refuses inf and nan.
double configFloat( WolfMod *mod, const char *key, double fallback )
{
    const char *value = configValue( mod, key );
    if ( value == nullptr )
    {
        return fallback;
    }
    const std::string_view text( value );
    const size_t sign = text.starts_with( '+' ) || text.starts_with( '-' ) ? 1 : 0;
    if ( text.size() == sign || ( ( text[ sign ] < '0' || text[ sign ] > '9' ) && text[ sign ] != '.' ) )
    {
        return fallback;
    }
    const char *first = text.data() + ( text.starts_with( '+' ) ? 1 : 0 );
    double parsed = 0.0;
    const auto [ end, error ] = std::from_chars( first, text.data() + text.size(), parsed );
    return error == std::errc{} && end == text.data() + text.size() ? parsed : fallback;
}

int32_t configBool( WolfMod *mod, const char *key, int32_t fallback )
{
    const char *value = configValue( mod, key );
    if ( value == nullptr )
    {
        return fallback;
    }
    const std::string_view text( value );
    for ( const std::string_view word : { "1", "true", "yes", "on" } )
    {
        if ( equalsNoCase( text, word ) )
        {
            return 1;
        }
    }
    for ( const std::string_view word : { "0", "false", "no", "off" } )
    {
        if ( equalsNoCase( text, word ) )
        {
            return 0;
        }
    }
    return fallback;
}

const char *configStr( WolfMod *mod, const char *key, const char *fallback )
{
    const char *value = configValue( mod, key );
    return value != nullptr ? value : fallback;
}

} // namespace wolf
