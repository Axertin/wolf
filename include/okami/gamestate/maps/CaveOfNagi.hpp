#pragma once
#include <unordered_map>

// Auto-generated from src/devtools/game-data/maps/CaveOfNagi.yml
// Do not edit manually - regenerate with scripts/generate_gamestate_headers.py

namespace okami::game_state::maps::CaveOfNagi
{

const std::unordered_map<unsigned, const char *> worldStateBits = {{9, "Entered cave cutscene trigger"},
                                                                   {10, "? unset after fixing sword"},
                                                                   {17, "Close enough to use rejuvenation on broken sword"},
                                                                   {20, "? unset after tachi constellation"},
                                                                   {21, "? set after fixing sword"},
                                                                   {22, "? set after tachi constellation"},
                                                                   {24, "? issun stone nag dialog enable"},
                                                                   {25, "? issun stone nag dialog trigger"},
                                                                   {26, "? issun stone nag dialog enable"},
                                                                   {27, "? Set when nagged about missing"},
                                                                   {28, "? Set when nagged about missing"},
                                                                   {29, "? Set when stone slashed"},
                                                                   {38, "Constellation pulsing"},
                                                                   {41, "? unset after fixing sword"},
                                                                   {42, "? unset after tachi constellation"},
                                                                   {43, "Set while in cave area"},
                                                                   {44, "Spoke to Susano first time during festival"},
                                                                   {45, "Spoke to Susano second time during festival"},
                                                                   {46, "Spoke to Kushi first time during festival"},
                                                                   {47, "Spoke to Kushi second time during festival"},
                                                                   {49, "? set after fixing sword"},
                                                                   {51, "? unset after tachi constellation"},
                                                                   {53, "? set when stone slashed"}};

const std::unordered_map<unsigned, const char *> userIndices = {};

const std::unordered_map<unsigned, const char *> collectedObjects = {{0, "Chest on the other side of the river (Stray Bead)"}};

const std::unordered_map<unsigned, const char *> areasRestored = {};

const std::unordered_map<unsigned, const char *> treesBloomed = {};

const std::unordered_map<unsigned, const char *> cursedTreesBloomed = {};

const std::unordered_map<unsigned, const char *> fightsCleared = {};

const std::unordered_map<unsigned, const char *> npcs = {{0, "Susano"}, {1, "Kushi"}};

const std::unordered_map<unsigned, const char *> mapsExplored = {};

const std::unordered_map<unsigned, const char *> field_DC = {};

const std::unordered_map<unsigned, const char *> field_E0 = {};

} // namespace okami::game_state::maps::CaveOfNagi
