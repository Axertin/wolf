#include "core/threading.hpp"

#include <windows.h>

#include <atomic>
#include <cassert>

#include "log/log.hpp"

namespace {

SRWLOCK lock = SRWLOCK_INIT;
// Thread id of the holder, 0 while the lock is free. Only the holder stores its own id, so a thread reading its own id holds the lock.
std::atomic<DWORD> owner{ 0 };

std::atomic<uint32_t> dispatchDepth{ 0 };
// Thread that took the depth from 0. Best effort: a thread entering between that thread's depth increment and the owner store reads the previous owner, so the
// overlap line can name a stale thread or miss that overlap.
std::atomic<uint32_t> dispatchOwner{ 0 };
std::atomic<bool> overlapLogged{ false };

std::atomic<DWORD> mainThread{ 0 };

} // namespace

namespace wolf {

constinit thread_local WolfMod *g_dispatchMod = nullptr;

RuntimeLock::RuntimeLock()
{
    assert( owner.load( std::memory_order_relaxed ) != GetCurrentThreadId() );
    AcquireSRWLockExclusive( &lock );
    owner.store( GetCurrentThreadId(), std::memory_order_relaxed );
}

RuntimeLock::~RuntimeLock()
{
    owner.store( 0, std::memory_order_relaxed );
    ReleaseSRWLockExclusive( &lock );
}

void assertLocked()
{
    assert( owner.load( std::memory_order_relaxed ) == GetCurrentThreadId() );
}

void setMainThread()
{
    mainThread.store( GetCurrentThreadId(), std::memory_order_relaxed );
}

void assertMainThread()
{
    [[maybe_unused]] const DWORD main = mainThread.load( std::memory_order_relaxed );
    assert( main == 0 || main == GetCurrentThreadId() );
}

DispatchScope::DispatchScope( WolfMod *mod )
{
    const auto self = static_cast<uint32_t>( GetCurrentThreadId() );
    if ( dispatchDepth.fetch_add( 1 ) == 0 )
    {
        dispatchOwner.store( self );
    }
    else if ( const uint32_t holder = dispatchOwner.load(); holder != self && !overlapLogged.exchange( true ) )
    {
        writeLog( mod, WOLF_LOG_WARN, "dispatch overlap: thread %u entered while thread %u was dispatching", self, holder );
    }
    outer = g_dispatchMod;
    g_dispatchMod = mod;
}

DispatchScope::~DispatchScope()
{
    g_dispatchMod = outer;
    dispatchDepth.fetch_sub( 1 );
}

} // namespace wolf
