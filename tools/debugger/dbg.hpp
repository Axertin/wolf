// The Okami debugger's library: records, the capture config, the recording session, capture files, decoders and the reader's commands.

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "catalog.hpp"

namespace dbg {

enum class RecordKind : uint8_t { Call, Return, Watch, Site, Trigger };

// One ring entry, and the on-disk record.
struct Record
{
    uint64_t time; // QueryPerformanceCounter
    uint64_t caller;
    uint64_t arg[ 4 ]; // Return: the return value, then recorded args 0 to 2. Watch: value, previous. Site: from, to. Trigger: fire count
    uint32_t frame;    // WOLF's frame number, 0 before the first tick
    uint32_t thread;
    uint16_t probe; // catalog, watch or trigger index, by kind
    RecordKind kind;
    uint8_t pad[ 5 ];
    char text[ 32 ]; // the probe's string argument, truncated and NUL-padded
};
static_assert( sizeof( Record ) == 96 );

// ---- config

struct ModuleBases
{
    uintptr_t main = 0, kernel = 0;
};

// An expression runs as steps on one value: Const and Arg load it, Add adds, Deref reads 8 bytes at it.
struct Step
{
    enum Op : uint8_t { Const, Arg, Add, Deref } op;
    int64_t value; // Arg: the record slot
};

struct Cond
{
    uint8_t slot;
    bool text; // a substring of Record::text instead of a slot compare
    uint64_t value;
    std::string needle;
};

struct Match
{
    RecordKind kind;
    uint16_t index;
    std::vector<Cond> conds;
};

struct ProbeUse
{
    uint16_t catalog;
    std::vector<Cond> conds;
};

struct WatchUse
{
    std::string name;
    uintptr_t address;
    uint32_t size;
};

struct SnapshotDef
{
    std::string name;
    std::vector<Step> address, size;
    std::string decoder;
};

enum class SaveMode : uint8_t { Once, Every, Count };

struct Trigger
{
    std::string name;
    std::vector<Match> stages;
    SaveMode mode = SaveMode::Once;
    uint32_t count = 1;
    uint32_t before = 60, after = 30; // frames
    std::vector<SnapshotDef> snapshots;
};

struct Config
{
    uint32_t ring = 65536;
    uint32_t maxCaptures = 8;
    std::vector<ProbeUse> probes;
    std::vector<WatchUse> watches;
    std::vector<Trigger> triggers;
};

struct ConfigError
{
    uint32_t line = 0;
    std::string message;
};

std::optional<Config> configParse( std::string_view text, const ModuleBases &bases, ConfigError *error );

// 0x and lowercase hex digits.
std::string hex( uint64_t value );

// ---- captures

struct Names
{
    std::string name;
    std::vector<std::string> args;
};

struct Snapshot
{
    std::string name, decoder;
    std::string error; // empty when the bytes were read
    uint64_t address = 0;
    std::vector<uint8_t> bytes;
};

struct Capture
{
    std::string trigger;
    uint32_t number = 0; // 1 for the first capture of the launch
    uint32_t fireFrame = 0;
    uint64_t fireTime = 0;
    bool truncated = false; // the ring no longer held the start of the before-window
    std::vector<Record> records;
    std::vector<Snapshot> snapshots;
    std::map<uint32_t, Names> probes; // by catalog index: the config's probes
    std::vector<std::string> watches, triggers;
    std::map<std::string, std::string> info; // filled by the mod: versions, fingerprint, bases, qpc frequency, local time
    std::string config;
};

// ---- session

struct Session;

// The session lives for the process.
Session *sessionCreate( const Config &config );
// Any thread. Fills time, frame and thread, and returns false when the probe's filter drops the record.
bool sessionRecord( Session *s, Record &rec );
// The main thread, once a frame: closes the captures whose window has ended.
void sessionTick( Session *s, uint32_t frame );
// The captures closed since the last call.
std::vector<Capture> sessionTakeReady( Session *s );
// Runs an expression against the firing record; false when a Deref is unreadable.
bool exprEval( const std::vector<Step> &steps, const Record &fire, uint64_t *out );

// ---- capture files

// Writes <YYYYMMDD-HHMMSS>-<trigger>-<number>/ under capturesDir, through a .tmp directory renamed when complete. Returns the directory, or an empty path
// with error set.
std::filesystem::path captureWrite( const Capture &c, const std::filesystem::path &capturesDir, std::string *error );
std::optional<Capture> captureLoad( const std::filesystem::path &dir, std::string *error );
// The complete captures under capturesDir, in name order.
std::vector<std::filesystem::path> captureList( const std::filesystem::path &capturesDir );

// ---- decoders and the reader

// A decoded value. Error: text is the message and number the offset it was found at.
struct Node
{
    enum Kind : uint8_t { Number, Text, Object, List, Error } kind;
    std::string key;
    uint64_t number = 0; // signed fields are sign-extended
    std::string text;
    std::vector<Node> children;
};

using Decoder = Node ( * )( std::span<const uint8_t> bytes );
Decoder findDecoder( std::string_view tag ); // null when unknown
std::string printText( const Node &node );   // indented key: value lines
std::string printJson( const Node &node );

} // namespace dbg
