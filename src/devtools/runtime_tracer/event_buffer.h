#pragma once

#include <cstddef>
#include <deque>
#include <mutex>
#include <vector>

#include "trace_event.h"

namespace wolf_tracer
{

// Thread-safe bounded ring buffer of trace events. Drops oldest on overflow.
// Designed for one producer (game thread) and one consumer (game thread, but
// at a different point in the frame), with an internal mutex covering both.
class EventBuffer
{
  public:
    explicit EventBuffer(std::size_t capacity) : capacity_(capacity)
    {
    }

    void push(TraceEvent ev)
    {
        std::lock_guard lock(mu_);
        if (events_.size() >= capacity_)
            events_.pop_front();
        events_.push_back(std::move(ev));
    }

    std::vector<TraceEvent> snapshot() const
    {
        std::lock_guard lock(mu_);
        return std::vector<TraceEvent>(events_.begin(), events_.end());
    }

    void clear()
    {
        std::lock_guard lock(mu_);
        events_.clear();
    }

  private:
    mutable std::mutex mu_;
    std::deque<TraceEvent> events_;
    std::size_t capacity_;
};

} // namespace wolf_tracer
