// The log sink that writes every line to logs/wolf.log in the game directory.

#pragma once

namespace wolf {

// Creates <gameDir>logs\ when it is missing, truncates or creates wolf.log there and makes it the log sink. gameDir ends in a backslash. False, with the sink
// left as it was, when the file cannot be opened. Each line goes out in one WriteFile at a byte range it reserved with an atomic add, so lines from several
// threads never interleave and no lock is taken. The handle stays open for the process.
bool startFileLog( const wchar_t *gameDir );

} // namespace wolf
