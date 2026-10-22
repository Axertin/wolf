// The ordered work at the start of every game frame.

#pragma once

namespace wolf {

// Main thread, at flower_tick entry. One hold retires the ended room and session generations' records and detaches their arenas; then, unlocked, room
// before session, their finalizers run and the arenas reset; then pending mutations apply, new hook heads are enabled in one batch, the frame lifetime ends and
// the queued runInGame calls run.
void frameBoundary();

// Main thread, when a warm flower_startup returns, before session.start: retires ended generations' records, applies pending mutations, enables new hook heads
// and runs the queued runInGame calls. Arenas, the frame lifetime and their finalizers wait for the next frameBoundary.
void warmStartBoundary();

} // namespace wolf
