/* WOLF sites: the events a mod subscribes to with on( mod, site, cb, user, lifetime ), and the context each one passes as cb's ctx.
 *
 * Every context struct starts with its size, and sites and their structs only ever grow at the end, so a mod checks size before reading a field added after
 * the runtime it was built against. Callbacks of one site run in load order, so the mod loaded last runs last and has the final word on writable fields.
 * A callback registered during a frame runs from the next frame on.
 *
 * C11. Keep constexpr, nullptr and typeof out of this file. */
#ifndef WOLF_SITES_H
#define WOLF_SITES_H

#include <wolf/wolf.h>

/* This header is C11, and these checks require C++ syntax. */
// NOLINTBEGIN(modernize-use-using)

#ifdef __cplusplus
extern "C" {
#endif

#define WOLF_SITE_SESSION_START "session.start"
#define WOLF_SITE_SESSION_END "session.end"
#define WOLF_SITE_TICK "tick"
#define WOLF_SITE_TICK_POST "tick.post"
#define WOLF_SITE_ROOM_CHANGE "room.change"
#define WOLF_SITE_ITEM_ADD "item.add"

/* Room ids are stage << 8 | room, as the game's g_currentRoomId holds them. WOLF_ROOM_NONE is the from of the first room change. */
#define WOLF_ROOM_NONE 0xFFFFu

/* session.start, on the main thread, when flower_startup returns. warm is 0 for the start of the process and 1 after a soft reset. A warm start fires after
 * the old session's registrations have been retired, after registrations made since the frame began have taken effect and after runInGame calls queued
 * since then have run. */
typedef struct WolfSessionStartCtx
{
    uint32_t size;
    int32_t warm;
} WolfSessionStartCtx;

/* session.end, on the main thread, when a soft reset begins. It runs the callbacks of the session that is ending, and that session's memory is still
 * valid here. Anything a callback registers or allocates here, in any lifetime, belongs to the next session, so session.end is where a mod prepares it. */
typedef struct WolfSessionEndCtx
{
    uint32_t size;
} WolfSessionEndCtx;

/* tick, on the main thread at the start of each game frame, after the runtime's own frame work; and tick.post when that frame's update returns. frame is
 * WOLF's own count of frames, the first being 1, and tick.post carries the same value as the tick before it. */
typedef struct WolfTickCtx
{
    uint32_t size;
    uint32_t reserved;
    uint64_t frame;
} WolfTickCtx;

/* room.change, on the map task worker thread, when a room has finished loading. It fires for the title room 0x0C00 after boot and after every soft reset,
 * and the worker is a new thread after every soft reset, so a callback keeps no thread-local state across one. The room lifetime ended when the change
 * began, so a callback in the room lifetime never sees the change that ends its room; one that follows every change is registered in the session or process
 * lifetime. */
typedef struct WolfRoomChangeCtx
{
    uint32_t size;
    uint32_t from;
    uint32_t to;
} WolfRoomChangeCtx;

/* item.add, before the game adds an item, on the thread that adds it: main at startup, the map task worker in play, and main during play for a giveItem
 * queued to the frame boundary. A callback may change delta, or set handled to nonzero to keep the game from adding the item at all. fromMod is the mod whose
 * request added the item, or null when the game adds it. The game itself adds item 16 with delta 1 on the main thread at every cold and warm start. */
typedef struct WolfItemAddCtx
{
    uint32_t size;
    int32_t itemId;
    int32_t delta;   /* writable */
    int32_t handled; /* writable */
    WolfMod *fromMod;
} WolfItemAddCtx;

/* The ctx a watch callback receives, on the main thread at the frame boundary. previous is the snapshot from before the change and is valid for the call; the
 * snapshot then takes the new bytes. */
typedef struct WolfWatchCtx
{
    uint32_t size;
    uint32_t n;
    const void *addr;
    const void *previous;
} WolfWatchCtx;

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-use-using)

#endif /* WOLF_SITES_H */
