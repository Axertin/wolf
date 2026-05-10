#pragma once

namespace wolf_tracer
{

struct HookInstallStatus
{
    bool stateBitSet = false;
    bool scheduleCb = false;
    bool scheduleScript = false; // documented as commonly inlined
    bool waitToken = false;      // documented as commonly inlined
    bool scriptStatus = false;
    bool frameAdvance = false;
};

// Install all universal-touchpoint hooks. Returns true if all *non-inlined*
// hooks succeeded; logs a warning per failed install (some hooks may fail
// because the target was inlined — that is expected and not a fatal error).
bool installTracerHooks();

// Read the most recent install result. Used by `trace status` so the user
// can confirm hooks took effect without scrolling the wolf log.
HookInstallStatus getHookInstallStatus();

// Currently a no-op (wolf does not expose hook removal in the public API
// today). Kept for symmetry / future use.
void uninstallTracerHooks();

} // namespace wolf_tracer
