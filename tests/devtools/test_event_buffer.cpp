#include <catch2/catch_test_macros.hpp>

#include "runtime_tracer/event_buffer.h"
#include "runtime_tracer/trace_event.h"

using wolf_tracer::EventBuffer;
using wolf_tracer::TraceEvent;
using wolf_tracer::TraceEventKind;

static TraceEvent makeEvent(uint64_t tick, TraceEventKind kind)
{
    TraceEvent e{};
    e.tick = tick;
    e.kind = kind;
    return e;
}

TEST_CASE("EventBuffer push then snapshot returns events in order", "[tracer]")
{
    EventBuffer buf{4};
    buf.push(makeEvent(1, TraceEventKind::StateBitSet));
    buf.push(makeEvent(2, TraceEventKind::ScheduleCallback));
    buf.push(makeEvent(3, TraceEventKind::WatchChange));

    auto snap = buf.snapshot();
    REQUIRE(snap.size() == 3);
    REQUIRE(snap[0].tick == 1);
    REQUIRE(snap[1].tick == 2);
    REQUIRE(snap[2].tick == 3);
}

TEST_CASE("EventBuffer drops oldest when full", "[tracer]")
{
    EventBuffer buf{2};
    buf.push(makeEvent(1, TraceEventKind::StateBitSet));
    buf.push(makeEvent(2, TraceEventKind::StateBitSet));
    buf.push(makeEvent(3, TraceEventKind::StateBitSet));

    auto snap = buf.snapshot();
    REQUIRE(snap.size() == 2);
    REQUIRE(snap[0].tick == 2);
    REQUIRE(snap[1].tick == 3);
}

TEST_CASE("EventBuffer clear empties the buffer", "[tracer]")
{
    EventBuffer buf{4};
    buf.push(makeEvent(1, TraceEventKind::StateBitSet));
    buf.clear();
    REQUIRE(buf.snapshot().empty());
}
