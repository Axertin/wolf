#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <bit>
#include <cstring>
#include <mutex>
#include <utility>

#include "dbg.hpp"

namespace dbg {

namespace {

constexpr uint64_t kMaxSnapshot = uint64_t( 64 ) << 20;

struct OpenCapture
{
    Capture capture;
    size_t trigger;
    uint32_t closeFrame;
};

bool readMemory( uint64_t address, void *out, size_t n )
{
    SIZE_T read = 0;
    return ReadProcessMemory( GetCurrentProcess(), std::bit_cast<const void *>( address ), out, n, &read ) != 0 && read == n;
}

bool matches( const std::vector<Cond> &conds, const Record &rec )
{
    const std::string_view text( rec.text, strnlen( rec.text, sizeof( rec.text ) ) );
    return std::ranges::all_of( conds,
                                [ & ]( const Cond &c ) { return c.text ? text.find( c.needle ) != std::string_view::npos : rec.arg[ c.slot ] == c.value; } );
}

} // namespace

// One mutex guards the whole session. Probes run on any thread, and a single lock keeps the ring, the trigger stages and the open captures consistent with no
// further rules.
struct Session
{
    std::mutex mutex;
    Config config;
    std::vector<const ProbeUse *> filters; // by catalog index; null for a probe with no 'probe' line
    std::vector<Record> ring;
    uint64_t written = 0; // records ever written; the next one goes to ring[ written % ring.size() ]
    uint32_t frame = 0;
    uint32_t captures = 0;
    std::vector<size_t> stage;   // by trigger: the stage it waits on
    std::vector<uint32_t> fired; // by trigger
    std::vector<OpenCapture> open;
    std::vector<Capture> ready;

    void push( const Record &rec )
    {
        ring[ written++ % ring.size() ] = rec;
    }

    void fire( size_t t, const Record &rec )
    {
        const Trigger &trigger = config.triggers[ t ];
        ++captures;
        Record mark = rec;
        mark.kind = RecordKind::Trigger;
        mark.probe = uint16_t( t );
        mark.arg[ 0 ] = ++fired[ t ];
        push( mark );

        Capture c;
        c.trigger = trigger.name;
        c.number = captures;
        c.fireFrame = frame;
        c.fireTime = rec.time;
        for ( const SnapshotDef &def : trigger.snapshots )
        {
            Snapshot &snap = c.snapshots.emplace_back();
            snap.name = def.name;
            snap.decoder = def.decoder;
            uint64_t size = 0;
            if ( !exprEval( def.address, rec, &snap.address ) || !exprEval( def.size, rec, &size ) )
            {
                snap.error = "unreadable";
            }
            else if ( size > kMaxSnapshot )
            {
                snap.error = "too large";
            }
            else
            {
                snap.bytes.resize( size );
                if ( size != 0 && !readMemory( snap.address, snap.bytes.data(), size ) )
                {
                    snap.error = "unreadable";
                    snap.bytes.clear();
                }
            }
        }
        open.push_back( { .capture = std::move( c ), .trigger = t, .closeFrame = frame + trigger.after } );
    }

    void advance( const Record &rec )
    {
        for ( size_t t = 0; t < config.triggers.size(); ++t )
        {
            const Trigger &trigger = config.triggers[ t ];
            const bool spent = ( trigger.mode == SaveMode::Once && fired[ t ] >= 1 ) || ( trigger.mode == SaveMode::Count && fired[ t ] >= trigger.count );
            if ( spent || captures >= config.maxCaptures )
            {
                continue;
            }
            const Match &m = trigger.stages[ stage[ t ] ];
            if ( m.kind != rec.kind || m.index != rec.probe || !matches( m.conds, rec ) )
            {
                continue;
            }
            if ( ++stage[ t ] == trigger.stages.size() )
            {
                stage[ t ] = 0;
                fire( t, rec );
            }
        }
    }

    void close( OpenCapture &o )
    {
        Capture &c = o.capture;
        const Trigger &trigger = config.triggers[ o.trigger ];
        const uint64_t first = written > ring.size() ? written - ring.size() : 0;
        for ( uint64_t seq = first; seq < written; ++seq )
        {
            const Record &rec = ring[ seq % ring.size() ];
            if ( rec.frame + trigger.before < c.fireFrame )
            {
                continue;
            }
            if ( seq == first && first != 0 )
            {
                c.truncated = true;
            }
            c.records.push_back( rec );
        }
        for ( const ProbeUse &p : config.probes )
        {
            const CatalogEntry &entry = kCatalog[ p.catalog ];
            Names &names = c.probes[ p.catalog ];
            names.name = entry.name;
            for ( uint8_t a = 0; a < catalogArgCount( entry ); ++a )
            {
                names.args.emplace_back( entry.args[ a ].name );
            }
        }
        for ( const WatchUse &w : config.watches )
        {
            c.watches.push_back( w.name );
        }
        for ( const Trigger &other : config.triggers )
        {
            c.triggers.push_back( other.name );
        }
        ready.push_back( std::move( c ) );
    }
};

bool exprEval( const std::vector<Step> &steps, const Record &fire, uint64_t *out )
{
    uint64_t value = 0;
    for ( const Step &step : steps )
    {
        switch ( step.op )
        {
        case Step::Const:
            value = uint64_t( step.value );
            break;
        case Step::Arg:
            value = fire.arg[ step.value ];
            break;
        case Step::Add:
            value += uint64_t( step.value );
            break;
        case Step::Deref:
            if ( !readMemory( value, &value, sizeof( value ) ) )
            {
                return false;
            }
            break;
        }
    }
    *out = value;
    return true;
}

Session *sessionCreate( const Config &config )
{
    auto *s = new Session;
    s->config = config;
    s->filters.assign( kCatalogSize, nullptr );
    for ( const ProbeUse &p : s->config.probes )
    {
        s->filters[ p.catalog ] = &p;
    }
    s->ring.resize( config.ring );
    s->stage.assign( config.triggers.size(), 0 );
    s->fired.assign( config.triggers.size(), 0 );
    return s;
}

bool sessionRecord( Session *s, Record &rec )
{
    if ( rec.kind == RecordKind::Call || rec.kind == RecordKind::Site )
    {
        const ProbeUse *filter = s->filters[ rec.probe ];
        if ( filter != nullptr && !matches( filter->conds, rec ) )
        {
            return false;
        }
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter( &now );
    rec.time = uint64_t( now.QuadPart );
    rec.thread = GetCurrentThreadId();
    const std::scoped_lock lock( s->mutex );
    rec.frame = s->frame;
    s->push( rec );
    s->advance( rec );
    return true;
}

void sessionTick( Session *s, uint32_t frame )
{
    const std::scoped_lock lock( s->mutex );
    s->frame = frame;
    const auto closing = std::ranges::stable_partition( s->open, [ & ]( const OpenCapture &o ) { return o.closeFrame > frame; } );
    if ( closing.empty() )
    {
        return;
    }
    for ( OpenCapture &o : closing )
    {
        s->close( o );
    }
    s->open.erase( closing.begin(), closing.end() );
}

std::vector<Capture> sessionTakeReady( Session *s )
{
    const std::scoped_lock lock( s->mutex );
    return std::exchange( s->ready, {} );
}

} // namespace dbg
