#pragma once

namespace okami
{
namespace MoveListTome
{
enum Enum
{
    FourWinds,
    FiveWinds,
    SpiritStorm,
    SpiritArmageddon,
    ThreeShears,
    FourShears,
    WailingMirror,
    BeadString,
    SwordDance,
    Fleetfoot,
    CounterDodge,
    DiggingChamp,
    HolyEagle,
    GoldenFury,
    BrownRage,
    Hardhead,
    HolyFalcon,
    NUM_MOVE_LIST_ENTRIES
};

static const char *name[NUM_MOVE_LIST_ENTRIES] = {
    "Reflector Technique: 4 Winds", "Reflector Technique: 5 Winds", "Rosary Technique: Spirit Storm", "Rosary Technique: Spirit Armageddon",
    "Glaive Technique: 3 Shears",   "Glaive Technique: 4 Shears",   "God Technique: Wailing Mirror",  "God Technique: Bead String",
    "God Technique: Sword Dance",   "God Technique: Fleetfoot",     "God Technique: Counter Dodge",   "God Technique: Digging Champ",
    "God Technique: Holy Eagle",    "God Technique: Golden Fury",   "God Technique: Brown Rage",      "God Technique: Hardhead",
    "God Technique: Holy Falcon",
};

inline const char *GetName(unsigned index)
{
    if (index < NUM_MOVE_LIST_ENTRIES)
    {
        return name[index];
    }
    return "invalid";
}
} // namespace MoveListTome
} // namespace okami
