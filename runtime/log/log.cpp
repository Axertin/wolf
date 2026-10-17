#include "log/log.hpp"

#include <atomic>
#include <cstdarg>
#include <cstdio>

namespace {

std::atomic<wolf::LogSink> sink{ nullptr };

} // namespace

namespace wolf {

void setLogSink( LogSink newSink )
{
    sink.store( newSink, std::memory_order_release );
}

void writeLog( WolfMod *mod, int32_t level, const char *format, ... )
{
    const LogSink current = sink.load( std::memory_order_acquire );
    if ( current == nullptr )
    {
        return;
    }
    char line[ 1024 ];
    va_list args;
    va_start( args, format );
    vsnprintf( line, sizeof( line ), format, args );
    va_end( args );
    current( mod, level, line );
}

} // namespace wolf
