#include <algorithm>
#include <cctype>
#include <charconv>
#include <stdexcept>
#include <utility>

#include "dbg.hpp"

namespace dbg {

namespace {

[[noreturn]] void fail( const std::string &message )
{
    throw std::runtime_error( message );
}

std::string quote( std::string_view token )
{
    return "'" + std::string( token ) + "'";
}

uint64_t parseNumber( std::string_view token )
{
    std::string_view digits = token;
    uint64_t scale = 1;
    if ( digits.ends_with( 'K' ) || digits.ends_with( 'M' ) )
    {
        scale = digits.back() == 'K' ? 1024 : 1024 * 1024;
        digits.remove_suffix( 1 );
    }
    int base = 10;
    if ( digits.starts_with( "0x" ) )
    {
        base = 16;
        digits.remove_prefix( 2 );
    }
    uint64_t value = 0;
    const auto [ end, ec ] = std::from_chars( digits.data(), digits.data() + digits.size(), value, base );
    if ( digits.empty() || ec != std::errc() || end != digits.data() + digits.size() )
    {
        fail( "bad number " + quote( token ) );
    }
    return value * scale;
}

uint32_t parseNumber( std::string_view token, uint64_t min, uint64_t max )
{
    const uint64_t value = parseNumber( token );
    if ( value < min || value > max )
    {
        fail( quote( token ) + " is outside " + std::to_string( min ) + " to " + std::to_string( max ) );
    }
    return uint32_t( value );
}

void checkName( std::string_view name )
{
    const bool safe = std::ranges::all_of( name, []( char c ) { return std::isalnum( uint8_t( c ) ) != 0 || c == '_' || c == '.' || c == '-'; } );
    if ( name.empty() || name.size() > 31 || !safe )
    {
        fail( "name " + quote( name ) + " must be 1 to 31 letters, digits, '_', '.' or '-'" );
    }
}

// A term: main!VA, kernel!VA, a number, or a slot name of the record.
Step parseTerm( std::string_view term, const ModuleBases &bases, const std::vector<std::string_view> &slots )
{
    if ( const size_t bang = term.find( '!' ); bang != std::string_view::npos )
    {
        const std::string_view module = term.substr( 0, bang );
        const uint64_t va = parseNumber( term.substr( bang + 1 ) );
        if ( module != "main" && module != "kernel" )
        {
            fail( "unknown module " + quote( module ) );
        }
        const uintptr_t base = module == "main" ? bases.main : bases.kernel;
        if ( base == 0 )
        {
            fail( std::string( module == "main" ? "main.dll" : "flower_kernel.dll" ) + " is not loaded, for " + quote( term ) );
        }
        if ( va < kPreferredBase )
        {
            fail( quote( term ) + " is below the preferred base 0x180000000" );
        }
        return { .op = Step::Const, .value = int64_t( base + ( va - kPreferredBase ) ) };
    }
    if ( !term.empty() && std::isdigit( uint8_t( term[ 0 ] ) ) != 0 )
    {
        return { .op = Step::Const, .value = int64_t( parseNumber( term ) ) };
    }
    if ( const auto slot = std::ranges::find( slots, term ); slot != slots.end() )
    {
        return { .op = Step::Arg, .value = slot - slots.begin() };
    }
    fail( "unknown argument " + quote( term ) );
}

// An expression is one token: any number of '[', one term, then +N, -N and ']' in any order, where ']' reads 8 bytes at the value so far.
std::vector<Step> parseExpr( std::string_view text, const ModuleBases &bases, const std::vector<std::string_view> &slots )
{
    size_t at = 0;
    const auto word = [ & ] {
        const size_t end = std::min( text.find_first_of( "+-]", at ), text.size() );
        const std::string_view w = text.substr( at, end - at );
        at = end;
        return w;
    };
    size_t depth = 0;
    for ( ; at < text.size() && text[ at ] == '['; ++at )
    {
        ++depth;
    }
    std::vector<Step> steps{ parseTerm( word(), bases, slots ) };
    while ( at < text.size() )
    {
        const char op = text[ at++ ];
        if ( op == ']' && depth > 0 )
        {
            --depth;
            steps.push_back( { .op = Step::Deref, .value = 0 } );
        }
        else if ( op == '+' || op == '-' )
        {
            const auto n = int64_t( parseNumber( word() ) );
            steps.push_back( { .op = Step::Add, .value = op == '-' ? -n : n } );
        }
        else
        {
            fail( "bad expression " + quote( text ) );
        }
    }
    if ( depth != 0 )
    {
        fail( "missing ']' in " + quote( text ) );
    }
    return steps;
}

uint64_t parseConstant( std::string_view text, const ModuleBases &bases )
{
    uint64_t value = 0;
    for ( const Step &step : parseExpr( text, bases, {} ) )
    {
        if ( step.op == Step::Deref )
        {
            fail( quote( text ) + " is not a constant" );
        }
        value += uint64_t( step.value );
    }
    return value;
}

RecordKind callKind( const CatalogEntry &entry )
{
    return entry.kind == ProbeKind::Site ? RecordKind::Site : RecordKind::Call;
}

// The slot names of a record kind: what stages, filters and snapshots call arg[0] to arg[3].
std::vector<std::string_view> slotNames( RecordKind kind, const CatalogEntry *entry )
{
    if ( kind == RecordKind::Watch )
    {
        return { "value", "previous" };
    }
    std::vector<std::string_view> names;
    if ( kind == RecordKind::Return )
    {
        names.emplace_back( "ret" );
    }
    for ( uint8_t a = 0; a < catalogArgCount( *entry ) && names.size() < 4; ++a )
    {
        names.emplace_back( entry->args[ a ].name );
    }
    return names;
}

// "if COND [and COND]..." from tokens[from], where COND is "SLOT == VALUE" or "TEXTARG ~ "text"".
std::vector<Cond> parseConds( const std::vector<std::string_view> &tokens, size_t from, const std::vector<std::string_view> &slots, const char *textName,
                              const ModuleBases &bases )
{
    std::vector<Cond> conds;
    if ( from == tokens.size() )
    {
        return conds;
    }
    if ( tokens[ from ] != "if" )
    {
        fail( "expected 'if', not " + quote( tokens[ from ] ) );
    }
    for ( size_t i = from + 1;; i += 4 )
    {
        if ( i + 3 > tokens.size() )
        {
            fail( "a condition is ARG == VALUE or ARG ~ \"text\"" );
        }
        const std::string_view arg = tokens[ i ];
        const std::string_view op = tokens[ i + 1 ];
        const std::string_view value = tokens[ i + 2 ];
        const auto slot = std::ranges::find( slots, arg );
        if ( slot == slots.end() )
        {
            fail( "unknown argument " + quote( arg ) );
        }
        Cond cond{ .slot = uint8_t( slot - slots.begin() ), .text = op == "~", .value = 0, .needle = {} };
        if ( cond.text )
        {
            if ( textName == nullptr || arg != textName )
            {
                fail( quote( arg ) + " is not a text argument" );
            }
            if ( value.size() < 2 || !value.starts_with( '"' ) || !value.ends_with( '"' ) )
            {
                fail( "expected quoted text, not " + quote( value ) );
            }
            cond.needle = value.substr( 1, value.size() - 2 );
        }
        else if ( op == "==" )
        {
            cond.value = parseConstant( value, bases );
        }
        else
        {
            fail( "unknown operator " + quote( op ) );
        }
        conds.push_back( std::move( cond ) );
        if ( i + 3 == tokens.size() )
        {
            return conds;
        }
        if ( tokens[ i + 3 ] != "and" )
        {
            fail( "expected 'and', not " + quote( tokens[ i + 3 ] ) );
        }
    }
}

struct Parser
{
    explicit Parser( const ModuleBases &b ) : bases( b )
    {}

    const ModuleBases &bases;
    Config config;
    Trigger *trigger = nullptr;
    uint32_t triggerLine = 0;
    std::vector<std::string_view> lastSlots; // the final stage's slots, for snapshot expressions

    static void arity( const std::vector<std::string_view> &t, size_t min, size_t max )
    {
        if ( t.size() < min || t.size() > max )
        {
            fail( "wrong number of words for " + quote( t[ 0 ] ) );
        }
    }

    void topLevel( const std::vector<std::string_view> &t, uint32_t line )
    {
        if ( t[ 0 ] == "ring" )
        {
            arity( t, 2, 2 );
            config.ring = parseNumber( t[ 1 ], 1024, 4194304 );
        }
        else if ( t[ 0 ] == "limit" )
        {
            arity( t, 3, 3 );
            if ( t[ 1 ] != "captures" )
            {
                fail( "unknown limit " + quote( t[ 1 ] ) );
            }
            config.maxCaptures = parseNumber( t[ 2 ], 0, 1000000 );
        }
        else if ( t[ 0 ] == "probe" )
        {
            arity( t, 2, SIZE_MAX );
            const int32_t index = catalogFind( t[ 1 ] );
            if ( index < 0 )
            {
                fail( "unknown probe " + quote( t[ 1 ] ) );
            }
            if ( std::ranges::any_of( config.probes, [ & ]( const ProbeUse &p ) { return std::cmp_equal( p.catalog, index ); } ) )
            {
                fail( "duplicate probe " + quote( t[ 1 ] ) );
            }
            const CatalogEntry &entry = kCatalog[ index ];
            const RecordKind kind = callKind( entry );
            config.probes.push_back( { uint16_t( index ), parseConds( t, 2, slotNames( kind, &entry ), textName( entry ), bases ) } );
        }
        else if ( t[ 0 ] == "watch" )
        {
            arity( t, 4, 4 );
            checkName( t[ 1 ] );
            if ( findWatch( t[ 1 ] ) >= 0 )
            {
                fail( "duplicate watch " + quote( t[ 1 ] ) );
            }
            config.watches.push_back( { std::string( t[ 1 ] ), parseConstant( t[ 2 ], bases ), parseNumber( t[ 3 ], 1, 65536 ) } );
        }
        else if ( t[ 0 ] == "trigger" )
        {
            arity( t, 2, 2 );
            checkName( t[ 1 ] );
            if ( std::ranges::any_of( config.triggers, [ & ]( const Trigger &other ) { return other.name == t[ 1 ]; } ) )
            {
                fail( "duplicate trigger " + quote( t[ 1 ] ) );
            }
            trigger = &config.triggers.emplace_back();
            trigger->name = t[ 1 ];
            triggerLine = line;
        }
        else
        {
            fail( "unknown statement " + quote( t[ 0 ] ) );
        }
    }

    void inTrigger( const std::vector<std::string_view> &t )
    {
        if ( t[ 0 ] == "on" || t[ 0 ] == "then" )
        {
            arity( t, 2, SIZE_MAX );
            if ( ( t[ 0 ] == "on" ) != trigger->stages.empty() )
            {
                fail( t[ 0 ] == "on" ? "a trigger has one 'on', then 'then' stages" : "'then' before 'on'" );
            }
            if ( !trigger->snapshots.empty() )
            {
                fail( "stages come before snapshots" );
            }
            if ( trigger->stages.size() == 4 )
            {
                fail( "a trigger has at most 4 stages" );
            }
            trigger->stages.push_back( stage( t ) );
        }
        else if ( t[ 0 ] == "save" )
        {
            arity( t, 2, 2 );
            if ( t[ 1 ] == "once" || t[ 1 ] == "every" )
            {
                trigger->mode = t[ 1 ] == "once" ? SaveMode::Once : SaveMode::Every;
            }
            else
            {
                trigger->mode = SaveMode::Count;
                trigger->count = parseNumber( t[ 1 ], 1, 1000000 );
            }
        }
        else if ( t[ 0 ] == "window" )
        {
            arity( t, 3, 3 );
            trigger->before = parseNumber( t[ 1 ], 0, 1000000 );
            trigger->after = parseNumber( t[ 2 ], 0, 1000000 );
        }
        else if ( t[ 0 ] == "snapshot" )
        {
            snapshot( t );
        }
        else if ( t[ 0 ] == "end" )
        {
            arity( t, 1, 1 );
            if ( trigger->stages.empty() )
            {
                fail( "trigger " + quote( trigger->name ) + " has no 'on'" );
            }
            trigger = nullptr;
        }
        else
        {
            fail( "unknown trigger statement " + quote( t[ 0 ] ) );
        }
    }

    void snapshot( const std::vector<std::string_view> &t )
    {
        arity( t, 4, 5 );
        if ( t.size() == 5 && findDecoder( t[ 4 ] ) == nullptr )
        {
            fail( "unknown decoder " + quote( t[ 4 ] ) );
        }
        if ( trigger->stages.empty() )
        {
            fail( "a snapshot comes after the stages" );
        }
        checkName( t[ 1 ] );
        if ( std::ranges::any_of( trigger->snapshots, [ & ]( const SnapshotDef &s ) { return s.name == t[ 1 ]; } ) )
        {
            fail( "duplicate snapshot " + quote( t[ 1 ] ) );
        }
        trigger->snapshots.push_back( { std::string( t[ 1 ] ), parseExpr( t[ 2 ], bases, lastSlots ), parseExpr( t[ 3 ], bases, lastSlots ),
                                        t.size() == 5 ? std::string( t[ 4 ] ) : std::string() } );
    }

    Match stage( const std::vector<std::string_view> &t )
    {
        std::string_view source = t[ 1 ];
        const bool isReturn = source.ends_with( ".return" );
        if ( isReturn )
        {
            source.remove_suffix( 7 );
        }
        if ( const int32_t watch = findWatch( source ); watch >= 0 && !isReturn )
        {
            lastSlots = slotNames( RecordKind::Watch, nullptr );
            return { .kind = RecordKind::Watch, .index = uint16_t( watch ), .conds = parseConds( t, 2, lastSlots, nullptr, bases ) };
        }
        const int32_t index = catalogFind( source );
        if ( index < 0 )
        {
            fail( "unknown probe or watch " + quote( t[ 1 ] ) );
        }
        const CatalogEntry &entry = kCatalog[ index ];
        if ( std::ranges::none_of( config.probes, [ & ]( const ProbeUse &p ) { return std::cmp_equal( p.catalog, index ); } ) )
        {
            fail( quote( source ) + " has no 'probe' line" );
        }
        if ( isReturn && !entry.returns )
        {
            fail( quote( source ) + " does not return a value" );
        }
        const RecordKind kind = isReturn ? RecordKind::Return : callKind( entry );
        lastSlots = slotNames( kind, &entry );
        return { .kind = kind, .index = uint16_t( index ), .conds = parseConds( t, 2, lastSlots, textName( entry ), bases ) };
    }

    [[nodiscard]] int32_t findWatch( std::string_view name ) const
    {
        const auto it = std::ranges::find( config.watches, name, &WatchUse::name );
        return it == config.watches.end() ? -1 : int32_t( it - config.watches.begin() );
    }

    static const char *textName( const CatalogEntry &entry )
    {
        return entry.textArg < 0 ? nullptr : entry.args[ entry.textArg ].name;
    }
};

std::vector<std::string_view> tokenize( std::string_view line )
{
    line = line.substr( 0, line.find( '#' ) );
    std::vector<std::string_view> tokens;
    constexpr std::string_view kSpace = " \t\r";
    for ( size_t at = line.find_first_not_of( kSpace ); at != std::string_view::npos; at = line.find_first_not_of( kSpace, at ) )
    {
        const size_t end = std::min( line.find_first_of( kSpace, at ), line.size() );
        tokens.push_back( line.substr( at, end - at ) );
        at = end;
    }
    return tokens;
}

} // namespace

std::optional<Config> configParse( std::string_view text, const ModuleBases &bases, ConfigError *error )
{
    if ( text.starts_with( "\xEF\xBB\xBF" ) )
    {
        text.remove_prefix( 3 );
    }
    Parser parser( bases );
    uint32_t line = 0;
    try
    {
        while ( !text.empty() )
        {
            ++line;
            const size_t end = std::min( text.find( '\n' ), text.size() );
            const std::vector<std::string_view> tokens = tokenize( text.substr( 0, end ) );
            text.remove_prefix( std::min( end + 1, text.size() ) );
            if ( tokens.empty() )
            {
                continue;
            }
            if ( parser.trigger != nullptr )
            {
                parser.inTrigger( tokens );
            }
            else
            {
                parser.topLevel( tokens, line );
            }
        }
        if ( parser.trigger != nullptr )
        {
            line = parser.triggerLine;
            fail( "trigger " + quote( parser.trigger->name ) + " has no 'end'" );
        }
    }
    catch ( const std::runtime_error &e )
    {
        if ( error != nullptr )
        {
            *error = { .line = line, .message = e.what() };
        }
        return std::nullopt;
    }
    return std::move( parser.config );
}

} // namespace dbg
