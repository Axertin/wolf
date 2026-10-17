// The seam between runtime code and wherever log lines end up.

#pragma once

#include <wolf/wolf.h>

#include <cstdint>

namespace wolf {

using LogSink = void ( * )( WolfMod *mod, int32_t level, const char *line );

// Installs the sink that receives every line, or removes it with nullptr. The sink may run with the runtime lock held, so it never calls into the runtime
// and never waits on a thread that may be waiting for the runtime lock.
void setLogSink( LogSink sink );

// Formats into a 1024-byte stack buffer (longer lines are cut) and hands the line to the sink. Without a sink the line is dropped.
void writeLog( WolfMod *mod, int32_t level, const char *format, ... );

} // namespace wolf
