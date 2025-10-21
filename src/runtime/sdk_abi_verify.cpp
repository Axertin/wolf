// SDK ABI Stability Verification
//
// This file contains compile-time assertions that verify the binary layout
// of SDK structures remains stable across versions. These checks prevent
// accidental ABI-breaking changes that would cause mod compatibility issues.
//
// If any assertion in this file fails, you have almost certainly broken binary
// compatibility with existing compiled mods. If this is intentional, then update
// this file to suit the breaking changes and ensure to mark the commit with !.

#include <cstddef>

#include "wolf_types.h"

// Verification macros
#define OFFCHECK(type, member, offset) static_assert(offsetof(type, member) == (offset), #type "." #member " at wrong offset, expected @ " #offset)

#define SIZECHECK(type, size) static_assert(sizeof(type) == (size), #type " is the wrong size, expected " #size)

//==============================================================================
// WolfModInterface
//==============================================================================
//
// This structure is the foundation of mod loading. Field order, size, and
// alignment are contractual obligations that must never change without a
// major version bump.
//
// Structure layout (x64):
// Offset  Size  Field
// ------  ----  -----
// 0x00    8     earlyGameInit (function pointer)
// 0x08    8     lateGameInit (function pointer)
// 0x10    8     shutdown (function pointer)
// 0x18    8     getName (function pointer)
// 0x20    8     getVersion (function pointer)
// 0x28    4     frameworkVersionInt (unsigned int)
// 0x2C    4     imguiVersionInt (unsigned int)
// 0x30    -     (end, 48 bytes total)

SIZECHECK(WolfModInterface, 48);

OFFCHECK(WolfModInterface, earlyGameInit, 0x00);
OFFCHECK(WolfModInterface, lateGameInit, 0x08);
OFFCHECK(WolfModInterface, shutdown, 0x10);
OFFCHECK(WolfModInterface, getName, 0x18);
OFFCHECK(WolfModInterface, getVersion, 0x20);
OFFCHECK(WolfModInterface, frameworkVersionInt, 0x28);
OFFCHECK(WolfModInterface, imguiVersionInt, 0x2C);

// Alignment verification
static_assert(alignof(WolfModInterface) == 8, "WolfModInterface alignment changed - ABI break");

//==============================================================================
// Fundamental Types
//==============================================================================

SIZECHECK(WolfModId, sizeof(int));
SIZECHECK(WolfBitfieldMonitorHandle, sizeof(void *));

// WolfLogLevel is an enum, verify it's int-sized
static_assert(sizeof(WolfLogLevel) == sizeof(int), "WolfLogLevel size changed - ABI break");

//==============================================================================
// Callback Function Pointer Types
//==============================================================================
//
// All callbacks must remain pointer-sized

SIZECHECK(WolfGameEventCallback, sizeof(void *));
SIZECHECK(WolfItemPickupCallback, sizeof(void *));
SIZECHECK(WolfItemPickupBlockingCallback, sizeof(void *));
SIZECHECK(WolfBrushEditCallback, sizeof(void *));
SIZECHECK(WolfPatternCallback, sizeof(void *));
SIZECHECK(WolfMemoryWatchCallback, sizeof(void *));
SIZECHECK(WolfConsoleCommandCallback, sizeof(void *));
SIZECHECK(WolfResourceProvider, sizeof(void *));
SIZECHECK(WolfBitfieldChangeCallback, sizeof(void *));
SIZECHECK(WolfGuiWindowCallback, sizeof(void *));
SIZECHECK(WolfWndProcCallback, sizeof(void *));

//==============================================================================
// ABI Stability Notes
//==============================================================================
//
// Breaking changes include:
// - Reordering struct fields
// - Changing field types
// - Changing struct size or alignment
// - Changing function calling conventions
// - Changing enum underlying types
//
// Safe changes include:
// - Adding new callback types (new typedefs)
// - Appending new API functions
// - Updating documentation
// - Appending new enum values at the end (with caution)
//
// If you must make breaking changes:
// 1. Mark with a ! marker on the Conventional commit type
// 3. Document migration guide for mod authors
