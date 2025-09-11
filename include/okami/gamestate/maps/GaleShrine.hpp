#pragma once
#include <unordered_map>

// Auto-generated from src/devtools/game-data/maps/GaleShrine.yml
// Do not edit manually - regenerate with scripts/generate_gamestate_headers.py

namespace okami::game_state::maps::GaleShrine
{

const std::unordered_map<unsigned, const char *> worldStateBits =
{
    { 1, "Obtained Galestorm" },
    { 2, "Spoke to Yatsu first time" },
    { 3, "Spoke to Yatsu second time" },
    { 5, "Approached Yatsu with Satomi Orbs" },
    { 7, "Yatsu realized you have Satomi Orbs" },
    { 10, "Spoke to Susano first time" },
    { 11, "Toggle between Susano 2nd/3rd nightmare dialogue" },
    { 12, "Susano left first room" },
    { 15, "Started talking to Princess Fuse post-Crimson Helm" },
    { 16, "Re-entered Gale Shrine post-Crimson Helm" },
    { 17, "Started Yatsu's final words" },
    { 18, "Yatsu disappeared" },
    { 19, "Spawned first key" },
    { 20, "Defeated first Lockjaw" },
    { 21, "Spawned second key" },
    { 22, "Defeated second Lockjaw" },
    { 23, "Locked on windmill platform cutscene" },
    { 24, "Cleared Galestorm tutorial" },
    { 25, "Issun text after Galestorm" },
    { 26, "In Galestorm demo" },
    { 27, "On first Galestorm demo" },
    { 28, "On second Galestorm demo" },
    { 29, "On third Galestorm demo" },
    { 30, "Failed first Galestorm demo 1st time" },
    { 31, "Failed first Galestorm demo 2nd time" },
    { 32, "Failed first Galestorm demo 3rd time" },
    { 35, "Cleared Galestorm constellation minigame" },
    { 36, "Failed Galestorm constellation minigame" },
    { 37, "Failed Galestorm tutorial 1st time" },
    { 38, "Cleared Galestorm tutorial" },
    { 39, "Door to Gale Shrine opening cutscene" },
    { 40, "Door to Gale Shring has been opened" },
    { 41, "Satomi Orbs removed from Ammy" },
    { 42, "Used Galestorm on flame pillars first time" },
    { 43, "Showing first Galestorm demo guide (failed 4 times)" },
    { 44, "Showing second Galestorm demo guide (failed 3 times)" },
    { 45, "Showing third Galestorm demo guide (failed 3 times)" },
    { 46, "Showing Galestorm tutorial guide (failed 3 times)" },
    { 47, "Finished Kazegami cutscene" },
    { 48, "Cleared first evil wind" },
    { 49, "Cleared second evil wind" },
    { 50, "Cleared third evil wind" },
    { 51, "Cleared Galestorm tutorial" },
    { 54, "Pond camera pan cutscene" },
    { 55, "Wind bridge camera pan cutscene" },
    { 56, "Issun hanging scrolls dialogue" },
    { 57, "Finished Kazegami cutscene" },
    { 58, "First Chimaera cutscene" },
    { 59, "Started Chimaera Demon Gate" },
    { 71, "Re-entered Gale Shrine post-Crimson Helm" },
    { 73, "Turned wind bridge for 1st time" },
    { 77, "Turned wind bridge for 1st time" }
};

const std::unordered_map<unsigned, const char *> userIndices = {};

const std::unordered_map<unsigned, const char *> collectedObjects =
{
    { 0, "Chest above staircase in top of windmill (Sun Fragment)" },
    { 1, "Flaming chest on 3rd floor outside elevator (Dungeon Map)" },
    { 2, "Flaming chest on 2nd floor balcony (Stray Bead)" },
    { 3, "Flaming chest under hanging scrolls (Inkfinity Stone)" },
    { 4, "Flaming chest near hanging scrolls in rafters (Lacquerware Set)" },
    { 5, "Chest in high rafters above hanging scrolls (Steel Soul Sake)" },
    { 7, "Chest in rafters across scrolls bridge 2 (Exorcism Slip S)" },
    { 9, "Chest in front under elevator (Stray Bead)" },
    { 10, "Chest on back right under elevator (Holy Bone S)" },
    { 11, "Chest on back left under elevator (Rabbit Statue)" },
    { 12, "Chest at beginning of scrolls room ([Brush Tips])" },
    { 13, "Chest in pond right side (Holy Bone S)" },
    { 14, "Chest in pond middle (Dragonfly Bead)" },
    { 15, "Chest in pond left side (Bull Horn)" },
    { 16, "Clover on right side under elevator" },
    { 17, "Clover on left side under elevator" }
};

const std::unordered_map<unsigned, const char *> areasRestored = {};

const std::unordered_map<unsigned, const char *> treesBloomed = {};

const std::unordered_map<unsigned, const char *> cursedTreesBloomed = {};

const std::unordered_map<unsigned, const char *> fightsCleared ={ { 0, "Started Chimaera Demon Gate" }, { 1, "Blue Demon Scroll on 2nd floor" }, { 2, "Defeated Chimaera Demon Gate" } };

const std::unordered_map<unsigned, const char *> npcs ={ { 0, "Yatsu" } };

const std::unordered_map<unsigned, const char *> mapsExplored =
{
    { 2, "Elevator area" },
    { 3, "Flame pillar room" },
    { 5, "Past Satomi Orbs gate" },
    { 6, "Kazegami hanging scrolls room" }
};

const std::unordered_map<unsigned, const char *> field_DC = {};

const std::unordered_map<unsigned, const char *> field_E0 = {};

} // namespace okami::game_state::maps::GaleShrine
