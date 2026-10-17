// The runtime lock, and the scope that wraps every call into mod code.

#pragma once

#include "mods/mod.hpp"

namespace wolf {

// The runtime lock: one SRWLOCK guarding every mutation of runtime state. Not reentrant. No mod code runs under it, and no thread waits on another thread
// while holding it. Entry points take it; internal functions require it and call assertLocked.
struct RuntimeLock
{
    RuntimeLock(); // debug builds assert the calling thread does not already hold it
    ~RuntimeLock();
    RuntimeLock( const RuntimeLock & ) = delete;
    RuntimeLock &operator=( const RuntimeLock & ) = delete;
};

void assertLocked(); // debug builds: the calling thread holds the runtime lock

// The mod whose code this thread is running for the runtime, for the crash report. Native TLS: llvm-mingw emits _tls_index, not emutls.
extern thread_local WolfMod *g_dispatchMod;

// Wraps every call into mod code, every tier end and every boundary (the runtime's own steps pass a null mod). Sets g_dispatchMod for the scope, restoring the
// outer one on exit, and counts depth; the first thread to enter while another thread is inside logs once per process with both thread ids. Hook detours the
// game calls through a chain enter no scope and are not counted.
struct DispatchScope
{
    explicit DispatchScope( WolfMod *mod );
    ~DispatchScope();
    DispatchScope( const DispatchScope & ) = delete;
    DispatchScope &operator=( const DispatchScope & ) = delete;

    WolfMod *outer = nullptr;
};

} // namespace wolf
