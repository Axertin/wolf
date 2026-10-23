/* The fake game's exports the C test mods call: the trace their detours append to, and the event list they report what they see to. fakeConnect looks both
 * up in main.dll, the fake game; a mod calls it at the start of its init, before it registers anything, so every later call sees the pointers set. */
#pragma once

#include <windows.h>

#include <stdint.h>

typedef void ( *FakeTraceFn )( char c );
typedef void ( *FakeReportFn )( const char *mod, const char *what, const char *text, int64_t a, int64_t b );

static FakeTraceFn fakeTraceFn;
static FakeReportFn fakeReportFn;

static inline void fakeConnect( void )
{
    const HMODULE fake = GetModuleHandleA( "main.dll" );
    fakeTraceFn = (FakeTraceFn)GetProcAddress( fake, "FakeMain_Trace" );
    fakeReportFn = (FakeReportFn)GetProcAddress( fake, "FakeMain_Report" );
}

static inline void fakeTrace( char c )
{
    if ( fakeTraceFn != NULL )
    {
        fakeTraceFn( c );
    }
}

/* text may be null. */
static inline void fakeReport( const char *mod, const char *what, const char *text, int64_t a, int64_t b )
{
    if ( fakeReportFn != NULL )
    {
        fakeReportFn( mod, what, text, a, b );
    }
}
