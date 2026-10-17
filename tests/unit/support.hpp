// Shared test support: a recorder for runtime log lines, a start barrier for racing threads, and a drain that leaves the runtime with nothing waiting.

#pragma once

#include <wolf/wolf.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

struct LogLine
{
    WolfMod *mod = nullptr;
    int32_t level = 0;
    std::string text;
};

namespace logs {

void start(); // records every runtime line from here on and forgets earlier ones; the sink stays installed
std::vector<LogLine> lines();
size_t count( std::string_view needle ); // lines whose text contains needle

} // namespace logs

// Starts count threads that wait at one barrier with the caller, runs fn( index ) on each, and joins them. While they run, the caller repeats mainStep,
// when one is given, until every thread has returned.
void raceThreads( int count, const std::function<void( int )> &fn, const std::function<void()> &mainStep = {} );

// Ends the session and runs two frame boundaries: every session and room arena is clean, their records are retired, pending is applied and the run queue is
// empty.
void drainRuntime();
