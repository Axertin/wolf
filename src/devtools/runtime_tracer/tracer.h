#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "trace_event.h"

namespace wolf_tracer
{

void initializeRuntimeTracer();
void shutdownRuntimeTracer();

// Hot-path: emit an event into the ring buffer if tracing is enabled and the
// current map passes the allowlist filter. Cheap (mutex-only) when disabled.
void emitEvent(TraceEvent ev);

// Returns false if tracing is off OR the map filter rejects the current map.
// Hooks should call this first to skip work on filtered events.
bool isTracingForCurrentMap();

// Snapshots the recent events for UI / dump consumers.
std::vector<TraceEvent> snapshotEvents();

void setEnabled(bool on);
bool isEnabled();

void setMapAllowlist(std::vector<uint16_t> ids); // empty = log everything
const std::vector<uint16_t> &mapAllowlist();

uint64_t currentTick();

// Increment the tick counter. Called from the on-tick aggregator.
void advanceTick();

} // namespace wolf_tracer
