#pragma once
#include <unordered_map>

// Auto-generated from src/devtools/game-data/maps/TakaPass.yml
// Do not edit manually - regenerate with scripts/generate_gamestate_headers.py

namespace okami::game_state::maps::TakaPass
{

const std::unordered_map<unsigned, const char *> worldStateBits =
{
    { 1, "Hard Working Son quest ended" },
    { 2, "Crack to tree bombed" },
    { 3, "Crack to tree bombed" },
    { 4, "Waka 2 triggered" },
    { 5, "Waka 2 fight triggered" },
    { 6, "Waka 2 defeated; Mysterious Windmill started" },
    { 7, "One of the Cutters is chasing you" },
    { 8, "Got close enough to a Cutter to trigger an attack" },
    { 10, "Dragged Mrs Cutter first time" },
    { 13, "Triggered Cutters house view pan" },
    { 14, "Bombed Cutters roof" },
    { 15, "Bring Mrs Cutter to moonlight" },
    { 16, "Defeated the Cutters" },
    { 17, "Rejection quest ended; Animal Hating Couple quest ended" },
    { 18, "Released Chun" },
    { 19, "Sapling bloomed" },
    { 20, "Sapling bloomed" },
    { 22, "Sapling finished restoring area dialogue" },
    { 23, "Mermaid spring restored" },
    { 24, "Sapling bridge repaired" },
    { 25, "Spoke to Tea Master first time" },
    { 26, "Spoke to Tea Master post-restoration" },
    { 27, "Returned Tea Master's teacup from Moley (Golden Mushroom)" },
    { 28, "Spoke to Tea Customer first time" },
    { 29, "Spoke to Tea Customer post-restoration" },
    { 32, "Gave Pinwheel to Tea Customer" },
    { 33, "Refused to give Pinwheel to Tea Customer" },
    { 34, "Spoke to Tea Customer after giving Pinwheel" },
    { 35, "Spoke to Sasa Traveler" },
    { 36, "Spoke to Sasa Traveler post-Crimson Helm" },
    { 37, "Spoke to Sasa Traveler with Chun" },
    { 38, "Spoke to Bingo first time" },
    { 39, "Spoke to Bingo after removing leaves" },
    { 40, "Spoke to Bingo second time after removing leaves" },
    { 41, "Leaf pile by Bingo removed" },
    { 42, "Didn't understand Bingo's digging explanation" },
    { 43, "Understood Bingo's digging explanation" },
    { 44, "Finished digging minigame" },
    { 45, "Spoke to Bingo post-minigame success" },
    { 46, "Spoke to Spring Girl before restoring spring" },
    { 47, "Spoke to Spring Girl post-spring restoration" },
    { 48, "Spoke to Merchant first time" },
    { 49, "Spoke to Merchant post-Crimson Helm" },
    { 50, "Moley first dialogue triggered" },
    { 51, "Moley game active" },
    { 52, "Moley hit 1 time" },
    { 53, "Moley hit 2 times" },
    { 54, "Teacup Moley stole spawns" },
    { 55, "Moley hit 1 time (2nd+ game)" },
    { 56, "Moley hit 2 times (2nd+ game)" },
    { 57, "Prize from Moley spawns (2nd+ game)" },
    { 58, "Moley minigame completed" },
    { 60, "Able to give Pinwheel to Tea Customer" },
    { 61, "Barrier to stray bead cave dug out" }
};

const std::unordered_map<unsigned, const char *> userIndices = {};

const std::unordered_map<unsigned, const char *> collectedObjects =
{
    { 1, "Chest on Cutters' house (Glass Beads)" },
    { 2, "Clover on ledge above Cutters spout" },
    { 3, "Chest on right banner pole (Golden Peach)" },
    { 4, "Second fire chest in sapling path (Stray Bead)" },
    { 5, "Fire chest in sapling path (Crystal)" },
    { 6, "Clover in sapling cave" },
    { 7, "Buried chest behind Cutters house by spout (Stray Bead)" },
    { 8, "Buried chest between 2 rocks by mermaid spring" },
    { 12, "Buried chest by Tea Master stall" },
    { 13, "Buried chest by Checkpoint exit (Stray Bead)" },
    { 14, "Buried chest behind right banner pole by tree (Holy Bone S)" },
    { 15, "Leaf buried chest in sapling field by cave (Incense Burner)" },
    { 16, "Leaf buried chest by sapling (Vengeance Slip)" },
    { 17, "Leaf buried chest behind Cutters house (Lacquerware Set)" },
    { 18, "Leaf buried chest by mermaid spring (Stray Bead)" },
    { 21, "Clover on catwalk ledge by start of Kusa path" },
    { 22, "Buried chest under leaf pile by entrance (Vase)" },
    { 23, "Buried chest under leaf pile by entrance origin mirror (Lacquerware Set)" },
    { 24, "Buried chest under leaf pile field between Cutters path and moles (Coral Fragment)" },
    { 25, "Leaf buried chest just off sapling field path bottom of ledge (Pearl)" }
};

const std::unordered_map<unsigned, const char *> areasRestored =
{
    { 16, "Mermaid spring" },
    { 17, "Bamboo field by entrance origin mirror" },
    { 18, "Bamboo field near Checkpoint exit" },
    { 31, "Taka Pass" }
};

const std::unordered_map<unsigned, const char *> treesBloomed =
{
    { 0, "Path from sapling 1" },
    { 1, "Path from sapling 2" },
    { 2, "Path from sapling 3" },
    { 3, "Left of Checkpoint exit" },
    { 4, "Right of Checkpoint exit" },
    { 5, "Behind right banner pole" },
    { 6, "Right of Sasa exit 1" },
    { 7, "Right of Sasa exit 2" },
    { 8, "Right of Sasa exit 3" },
    { 9, "Just outside stray bead cave" }
};

const std::unordered_map<unsigned, const char *> cursedTreesBloomed = {};

const std::unordered_map<unsigned, const char *> fightsCleared =
{
    { 9, "Mr and Mrs Cutter" },
    { 11, "Gate by mermaid spring" },
    { 12, "Gate in field by entrance origin mirror" },
    { 13, "Gate by Checkpoint exit" }
};

const std::unordered_map<unsigned, const char *> npcs =
{
    { 0, "Mrs Cutter" },
    { 3, "Tea Master" },
    { 4, "Tea Customer" },
    { 5, "Sasa Traveler" },
    { 6, "Bingo" }
};

const std::unordered_map<unsigned, const char *> mapsExplored ={ { 1, "Cave to tree" } };

const std::unordered_map<unsigned, const char *> field_DC = {};

const std::unordered_map<unsigned, const char *> field_E0 = {};

} // namespace okami::game_state::maps::TakaPass
