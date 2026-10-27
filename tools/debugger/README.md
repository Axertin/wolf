# Okami debugger

The debugger is a WOLF code mod that records what Okami's engine does while you play. Built-in probes watch engine functions and memory, and a ring buffer holds the latest records all the time. When a trigger from the capture config fires, the debugger saves a window of the ring around it to disk, along with any memory snapshots the config asks for. `wolf-capture` reads the saved captures afterwards, without the game.

It only observes. Every probe calls through to the game unchanged, and the debugger never writes game memory.

## Deploying

1. Build it from the repository root. It is a CMake project of its own, outside WOLF's build:
   ```
   cmake -S tools/debugger -B build/debugger -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build/debugger
   ```
   From Linux, add `--toolchain "$PWD/cmake/llvm-mingw.cmake"` to the first command. The mod lands in `build/debugger/mods/debugger/` and the reader in `build/debugger/wolf-capture.exe`.
2. Copy `build/debugger/mods/debugger/` into the game's `mods/` directory.
3. Copy one of the configs in that directory to `capture.cfg`. Without a `capture.cfg` the debugger loads and stays idle.
4. Start the game with `-MODDED` and do what the config's opening comment says.

`wolf.log` says how many probes, watches and triggers installed, and logs a line for every capture it writes. A config error is logged with its line, and the debugger then stays idle.

Captures land in `<game>/logs/captures/`, one directory per capture, named `<date>-<time>-<trigger>-<number>`. Each directory holds everything needed to read it: the manifest, the config, the records and the snapshots.

## Capture config

One statement per line, and `#` starts a comment. Numbers are decimal or `0x` hex, with an optional `K` or `M` suffix.

```
ring N                       records the ring holds, 1024 to 4194304 (default 65536)
limit captures N             captures saved per launch, across all triggers (default 8)
probe NAME [if COND [and COND]...]
watch NAME ADDR SIZE         memory compared at every frame boundary, SIZE 1 to 65536
trigger NAME
  on SOURCE[.return] [if COND [and COND]...]
  then SOURCE[.return] [if ...]      up to 4 stages in all
  save once|every|N          (default once)
  window BEFORE AFTER        frames kept before and after the fire (default 60 30)
  snapshot NAME ADDR SIZE [DECODER]
end
```

- A probe names a catalog entry. Its filter drops the calls that do not match, and dropped calls never reach the ring.
- A condition is `ARG == VALUE`, or `ARG ~ "text"` for a substring of the probe's path argument.
- A stage's source is a probe that has a `probe` line, or a watch. `.return` matches the probe's return instead of its call.
- A trigger fires when its stages match in order, then starts over at the first stage.
- Argument names depend on the record:
  - a call has the probe's argument names;
  - a return has `ret` and the first three argument names;
  - a watch has `value` and `previous`;
  - `room.change` has `from` and `to`.
- An address is one token with no spaces:
  - `main!0xVA` or `kernel!0xVA` is a VA at the module's preferred base 0x180000000;
  - an argument name of the trigger's last stage;
  - `[expr]` reads 8 bytes at expr;
  - any of these can be followed by `+N` or `-N`.
- Snapshots are read when the trigger fires, so they show memory exactly at the event. A snapshot that cannot be read is saved as an error.
- The decoders are `runofs`, `objres.registry`, `objres.slots`, `texpool.slots` and `memhdr`.
- Trigger and snapshot names become file names, so they are 1 to 31 letters, digits, `_`, `.` or `-`.

The probes are the table in `catalog.hpp`. Observing a function the catalog lacks takes a new entry there and a rebuild.

## Reading captures

```
wolf-capture list <game>/logs/captures
wolf-capture records <capture> --probe CompleteRequest --frames 1200..1500
wolf-capture snapshot <capture> reg1.slots --json
```

- `list` prints each capture's directory, trigger, fire frame, record count and snapshot count.
- `records` prints one line per record: frame, thread, seconds from the fire, probe, kind, arguments, path and caller. `--json` prints one object per line instead.
- `snapshot` prints a decoded snapshot, or a hex dump when the snapshot has no decoder.

A capture that fails to load, or a snapshot that fails to decode, exits with status 2 and says what is wrong.
