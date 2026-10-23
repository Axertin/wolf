// A line-oriented reader for ini text.

#pragma once

#include <cstdint>
#include <string_view>

namespace wolf {

enum class IniItem : uint8_t { End, Pair, Malformed };

struct IniReader
{
    std::string_view rest;
    std::string_view section;
    uint32_t line = 0; // 1-based number of the line iniNext last returned
};

// Starts at the beginning of text, skipping a UTF-8 byte order mark.
IniReader iniOpen( std::string_view text );

// The next key = value line. Blank lines and lines whose first non-space character is ; or # are skipped, and [name] sets reader.section. Key and value are
// trimmed of spaces, tabs and \r; the value is everything after the first =. Malformed is a line with no = or an empty key; reader.line is its number.
IniItem iniNext( IniReader &reader, std::string_view *key, std::string_view *value );

// Decimal or 0x hex with an optional sign, covering the whole text. False on anything else and on overflow of int64_t.
bool parseInt( std::string_view text, int64_t *out );

} // namespace wolf
