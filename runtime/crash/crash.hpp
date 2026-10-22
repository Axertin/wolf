// The crash reporter: a minidump and one log line for every crash, written from a thread of its own.

#pragma once

namespace wolf {

// Main thread, with the loader lock released: loads dbghelp.dll from the system directory, starts the reporter thread, installs WOLF's unhandled-exception
// filter and makes std::terminate report 0xE0574F4C to that filter, so a C++ exception that escapes is reported too. gameDir ends in a backslash; dumps go to
// <gameDir>logs\crash-<date>-<time>.dmp.
//
// The filter runs on the faulting thread and only hands the fault to the reporter, which writes the line and then the dump, so a crash on a worker still gets
// a full dump. A stack overflow is taken at first chance by a vectored handler, which reports it the same way and ends the process. The filter waits up to 10
// seconds: it then ends the process when the report finished and passes the fault on when it did not, so a dump that hangs never hangs the game. A second crash
// during a report waits for that report and writes none of its own.
void crashStart( const wchar_t *gameDir );

// Main thread: installs WOLF's filter again, and logs when something else had replaced it.
void crashReassert();

} // namespace wolf
