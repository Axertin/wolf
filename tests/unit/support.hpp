// Shared test support: a recorder for runtime log lines, which only the logger's own tests read, a start barrier for racing threads, a drain that leaves the
// runtime with nothing waiting, and fresh temporary directories.

#pragma once

#include <wolf/wolf.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
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

} // namespace logs

// Starts count threads that wait at one barrier with the caller, runs fn( index ) on each, and joins them. While they run, the caller repeats mainStep,
// when one is given, until every thread has returned.
void raceThreads( int count, const std::function<void( int )> &fn, const std::function<void()> &mainStep = {} );

// Ends the session and runs two frame boundaries: every session and room arena is clean, their records are retired, pending is applied and the run queue is
// empty.
void drainRuntime();

// A new empty directory under the temporary directory, with a trailing backslash. Never removed; each call creates a directory that did not exist before.
std::wstring freshTempDir();

// The whole file at path, or an empty string when it cannot be read.
std::string readFile( const std::wstring &path );
