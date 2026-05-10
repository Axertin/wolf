#pragma once

#include <cstdint>
#include <string>

namespace wolf_tracer
{

enum class TraceEventKind : uint8_t
{
    StateBitSet,      // FUN_180170830 fired
    ScheduleCallback, // FUN_1803ef3c0 fired
    ScheduleScript,   // FUN_1803f35f0 fired (often inlined)
    WaitToken,        // FUN_1803f3970 fired (often inlined)
    ScriptStatus,     // FUN_1801c9430 polled
    FrameAdvance,     // FUN_1804567C0 fired
    WatchChange,      // a watched memory word changed
};

struct TraceEvent
{
    uint64_t tick;  // wolf game-tick counter at emit
    uint16_t mapId; // value of ExteriorMapID at emit
    TraceEventKind kind;
    uint64_t a;        // per-kind payload word 0 (e.g. enc, callback ptr)
    uint64_t b;        // per-kind payload word 1 (e.g. decoded addr)
    uint64_t c;        // per-kind payload word 2 (e.g. mask, oldVal)
    uint64_t d;        // per-kind payload word 3 (e.g. newVal)
    std::string label; // human-readable summary if available, otherwise empty
};

inline const char *kindName(TraceEventKind k) noexcept
{
    switch (k)
    {
    case TraceEventKind::StateBitSet:
        return "state_bit_set";
    case TraceEventKind::ScheduleCallback:
        return "schedule_callback";
    case TraceEventKind::ScheduleScript:
        return "schedule_script";
    case TraceEventKind::WaitToken:
        return "wait_token";
    case TraceEventKind::ScriptStatus:
        return "script_status";
    case TraceEventKind::FrameAdvance:
        return "frame_advance";
    case TraceEventKind::WatchChange:
        return "watch_change";
    }
    return "?";
}

} // namespace wolf_tracer
