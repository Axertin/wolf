#include "tracer.h"

#include <atomic>
#include <mutex>

#include "console.h"
#include "event_buffer.h"
#include "hooks.h"
#include "memory_accessors.h"
#include "overlay.h"
#include "symbol_resolver.h"
#include "watches.h"
#include "wolf_framework.hpp"

namespace wolf_tracer
{

namespace
{

constexpr std::size_t kBufferCapacity = 4096;

EventBuffer g_buffer{kBufferCapacity};
std::atomic<bool> g_enabled{false}; // default-off; user opts in via 'trace start'
std::atomic<uint64_t> g_tick{0};

std::mutex g_filterMu;
std::vector<uint16_t> g_mapAllowlist; // empty = all

uint16_t readCurrentMapId()
{
    if (!ExteriorMapID.is_bound())
        return 0;
    return static_cast<uint16_t>(ExteriorMapID.get() & 0xFFFF);
}

bool mapPassesFilter(uint16_t mapId)
{
    std::lock_guard lock(g_filterMu);
    if (g_mapAllowlist.empty())
        return true;
    for (auto id : g_mapAllowlist)
        if (id == mapId)
            return true;
    return false;
}

} // namespace

void initializeRuntimeTracer()
{
    initializeSymbolResolver();
    registerTracerCommands();
    if (!installTracerHooks())
        wolf::logError("RuntimeTracer: one or more required hooks failed; trace will be incomplete. Run 'trace status' for per-hook detail.");
    wolf::onGameTick(
        []()
        {
            advanceTick();
            tickWatches();
        });
    // Note: the tracer overlay does not register a separate wolf GUI window —
    // it draws inside devtools_gui's existing WOLF_IMGUI_BEGIN/END frame
    // (wolf only supports one registered window per mod). See
    // src/devtools/devtools_gui.cpp's call to wolf_tracer::drawTracerOverlay.
    wolf::logInfo("RuntimeTracer: initialize");
    wolf::registerCleanupHandler([]() { shutdownRuntimeTracer(); });
}

void shutdownRuntimeTracer()
{
    g_enabled.store(false);
    g_buffer.clear();
    wolf::logInfo("RuntimeTracer: shutdown");
}

void emitEvent(TraceEvent ev)
{
    ev.tick = g_tick.load(std::memory_order_relaxed);
    ev.mapId = readCurrentMapId();
    g_buffer.push(std::move(ev));
}

bool isTracingForCurrentMap()
{
    if (!g_enabled.load(std::memory_order_relaxed))
        return false;
    return mapPassesFilter(readCurrentMapId());
}

std::vector<TraceEvent> snapshotEvents()
{
    return g_buffer.snapshot();
}

void setEnabled(bool on)
{
    g_enabled.store(on);
}

bool isEnabled()
{
    return g_enabled.load();
}

void setMapAllowlist(std::vector<uint16_t> ids)
{
    std::lock_guard lock(g_filterMu);
    g_mapAllowlist = std::move(ids);
}

const std::vector<uint16_t> &mapAllowlist()
{
    // Caller must not modify; the vector is stable while held.
    return g_mapAllowlist;
}

uint64_t currentTick()
{
    return g_tick.load(std::memory_order_relaxed);
}

void advanceTick()
{
    g_tick.fetch_add(1, std::memory_order_relaxed);
}

} // namespace wolf_tracer
