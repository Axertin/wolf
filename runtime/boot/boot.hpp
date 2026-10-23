// Boot: what DllMain does when the game loads dinput8.dll.

#pragma once

#include <windows.h>

#include <cstdint>

namespace wolf {

// True when -MODDED is a whole whitespace-delimited token of commandLine outside double quotes, in any case.
bool hasModdedFlag( const wchar_t *commandLine );

struct Fingerprint
{
    uint32_t timeDateStamp = 0;
    uint32_t sizeOfImage = 0;
};

// The one main.dll build the symbol table describes.
inline constexpr Fingerprint kSupportedMain{ .timeDateStamp = 0x60E54536, .sizeOfImage = 0x1150000 };

// The PE header values of the image mapped at image. False unless the first page holds an MZ header, a PE signature and a PE32+ optional header.
bool readFingerprint( const void *image, Fingerprint *out );

// The directory holding dinput8.dll, with a trailing backslash, once bootAttach has recorded it.
const wchar_t *gameDirectory();

// DllMain, DLL_PROCESS_ATTACH, with the loader lock held. Without -MODDED it only pins the module. With it, it opens the log and, as soon as main.dll is
// mapped (now, or when the loader reports it, since an overlay can load dinput8.dll first), checks that it is the supported build, initialises MinHook and
// hooks flower_startup, whose cold call boots the rest.
void bootAttach( HMODULE self );

// DllMain, DLL_PROCESS_DETACH. dinput8.dll is pinned, so this runs only when the process ends through ExitProcess, and its line marks a clean exit; a crash
// or TerminateProcess ends the process without it.
void bootDetach();

} // namespace wolf
