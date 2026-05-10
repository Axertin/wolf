#include "hooks.h"

#include <atomic>
#include <cstdint>

#include "state_bit_decoder.h"
#include "symbol_resolver.h"
#include "tracer.h"
#include "wolf_framework.hpp"

namespace wolf_tracer
{

namespace
{

// Offsets from main.dll base (Ghidra image base 0x180000000):
constexpr uintptr_t kOff_StateBitSet = 0x170830;    // FUN_180170830
constexpr uintptr_t kOff_ScheduleCb = 0x3EF3C0;     // FUN_1803ef3c0
constexpr uintptr_t kOff_ScheduleScript = 0x3F35F0; // FUN_1803f35f0  (often inlined)
constexpr uintptr_t kOff_WaitToken = 0x3F3970;      // FUN_1803f3970  (often inlined)
constexpr uintptr_t kOff_ScriptStatus = 0x1C9430;   // FUN_1801c9430
constexpr uintptr_t kOff_FrameAdvance = 0x4567C0;   // FUN_1804567C0

using StateBitSetFn = void(__fastcall *)(uint32_t);
using ScheduleCbFn = int(__fastcall *)(void *, void *, uint32_t);
using ScheduleScriptFn = void(__fastcall *)(void *, uint16_t, void *, void *);
using WaitTokenFn = void(__fastcall *)(void *, uint16_t, void *);
using ScriptStatusFn = short *(__fastcall *)(uintptr_t, short);
using FrameAdvanceFn = void(__fastcall *)(void *, uint32_t);

StateBitSetFn g_origStateBitSet = nullptr;
ScheduleCbFn g_origScheduleCb = nullptr;
ScheduleScriptFn g_origScheduleScript = nullptr;
WaitTokenFn g_origWaitToken = nullptr;
ScriptStatusFn g_origScriptStatus = nullptr;
FrameAdvanceFn g_origFrameAdvance = nullptr;

HookInstallStatus g_status{};

void __fastcall stateBitSetDetour(uint32_t enc)
{
    if (isTracingForCurrentMap())
    {
        auto d = decodeStateBit(enc);
        TraceEvent ev{};
        ev.kind = TraceEventKind::StateBitSet;
        ev.a = enc;
        ev.b = d.wordOffset;
        ev.c = d.bitIndex;
        ev.d = d.mask;
        emitEvent(std::move(ev));
    }
    if (g_origStateBitSet)
        g_origStateBitSet(enc);
}

int __fastcall scheduleCbDetour(void *sched, void *callback, uint32_t flags)
{
    if (isTracingForCurrentMap())
    {
        TraceEvent ev{};
        ev.kind = TraceEventKind::ScheduleCallback;
        ev.a = reinterpret_cast<uint64_t>(callback);
        ev.b = flags;
        ev.label = resolveLabel(reinterpret_cast<uintptr_t>(callback));
        emitEvent(std::move(ev));
    }
    return g_origScheduleCb ? g_origScheduleCb(sched, callback, flags) : 0;
}

void __fastcall scheduleScriptDetour(void *self, uint16_t scriptId, void *a, void *b)
{
    if (isTracingForCurrentMap())
    {
        TraceEvent ev{};
        ev.kind = TraceEventKind::ScheduleScript;
        ev.a = scriptId;
        emitEvent(std::move(ev));
    }
    if (g_origScheduleScript)
        g_origScheduleScript(self, scriptId, a, b);
}

void __fastcall waitTokenDetour(void *self, uint16_t token, void *a)
{
    if (isTracingForCurrentMap())
    {
        TraceEvent ev{};
        ev.kind = TraceEventKind::WaitToken;
        ev.a = token;
        emitEvent(std::move(ev));
    }
    if (g_origWaitToken)
        g_origWaitToken(self, token, a);
}

short *__fastcall scriptStatusDetour(uintptr_t self, short token)
{
    // This fires very frequently (polled every frame by waiters). Skip the
    // per-call event entirely; the wait-token hook above is the meaningful
    // signal. We keep the detour wired so the stat is at least available
    // for future use (e.g. to count polls), but emit nothing now.
    return g_origScriptStatus ? g_origScriptStatus(self, token) : nullptr;
}

void __fastcall frameAdvanceDetour(void *sched, uint32_t count)
{
    // Fires every frame inside every wait loop. Sample only.
    static std::atomic<uint32_t> s_counter{0};
    constexpr uint32_t kSampleEvery = 60; // ~1 Hz at 60fps
    if ((s_counter.fetch_add(1, std::memory_order_relaxed) % kSampleEvery) == 0 && isTracingForCurrentMap())
    {
        TraceEvent ev{};
        ev.kind = TraceEventKind::FrameAdvance;
        ev.a = count;
        emitEvent(std::move(ev));
    }
    if (g_origFrameAdvance)
        g_origFrameAdvance(sched, count);
}

bool tryHook(const char *what, uintptr_t offset, void *detour, void **orig)
{
    bool ok =
        wolf::detail::g_runtime != nullptr && wolf::hookFunction("main.dll", offset, reinterpret_cast<void (*)()>(detour), reinterpret_cast<void (**)()>(orig));
    if (ok)
        wolf::logInfo("RuntimeTracer: hooked %s @ main.dll+0x%llX", what, static_cast<unsigned long long>(offset));
    else
        wolf::logWarning("RuntimeTracer: failed to hook %s @ main.dll+0x%llX (likely inlined)", what, static_cast<unsigned long long>(offset));
    return ok;
}

} // namespace

bool installTracerHooks()
{
    uintptr_t base = wolf::getModuleBase("main.dll");
    if (base == 0)
    {
        wolf::logError("RuntimeTracer: cannot install hooks — main.dll base unknown");
        return false;
    }

    // Pre-label our own hook target functions so trace output reads nicely.
    addLabel(base + kOff_StateBitSet, "FUN_180170830 (state_bit_set)");
    addLabel(base + kOff_ScheduleCb, "FUN_1803ef3c0 (schedule_callback)");
    addLabel(base + kOff_ScheduleScript, "FUN_1803f35f0 (schedule_script)");
    addLabel(base + kOff_WaitToken, "FUN_1803f3970 (wait_token)");
    addLabel(base + kOff_ScriptStatus, "FUN_1801c9430 (script_status)");
    addLabel(base + kOff_FrameAdvance, "FUN_1804567C0 (frame_advance)");

    g_status.stateBitSet =
        tryHook("state_bit_set", kOff_StateBitSet, reinterpret_cast<void *>(stateBitSetDetour), reinterpret_cast<void **>(&g_origStateBitSet));
    g_status.scheduleCb =
        tryHook("schedule_callback", kOff_ScheduleCb, reinterpret_cast<void *>(scheduleCbDetour), reinterpret_cast<void **>(&g_origScheduleCb));
    g_status.scheduleScript =
        tryHook("schedule_script", kOff_ScheduleScript, reinterpret_cast<void *>(scheduleScriptDetour), reinterpret_cast<void **>(&g_origScheduleScript));
    g_status.waitToken = tryHook("wait_token", kOff_WaitToken, reinterpret_cast<void *>(waitTokenDetour), reinterpret_cast<void **>(&g_origWaitToken));
    g_status.scriptStatus =
        tryHook("script_status", kOff_ScriptStatus, reinterpret_cast<void *>(scriptStatusDetour), reinterpret_cast<void **>(&g_origScriptStatus));
    g_status.frameAdvance =
        tryHook("frame_advance", kOff_FrameAdvance, reinterpret_cast<void *>(frameAdvanceDetour), reinterpret_cast<void **>(&g_origFrameAdvance));

    // Non-inlined hooks (state-bit-set, schedule-callback, script-status,
    // frame-advance) are required for useful tracing; if any of them fail
    // we treat the install as "not all OK". schedule_script + wait_token
    // are best-effort and don't gate the result.
    return g_status.stateBitSet && g_status.scheduleCb && g_status.scriptStatus && g_status.frameAdvance;
}

HookInstallStatus getHookInstallStatus()
{
    return g_status;
}

void uninstallTracerHooks()
{
    // wolf framework does not currently expose a hookFunction inverse.
    // Detours read g_orig* so they are safe across shutdown if disabled first.
}

} // namespace wolf_tracer
