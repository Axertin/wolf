#pragma once
#include <unordered_map>

// Auto-generated from src/devtools/game-data/maps/SasaSanctuary.yml
// Do not edit manually - regenerate with scripts/generate_gamestate_headers.py

namespace okami::game_state::maps::SasaSanctuary
{

const std::unordered_map<unsigned, const char *> worldStateBits =
{
    { 1, "Entry dialogue trigger" },
    { 4, "Waterspout constellation appeared" },
    { 5, "Kaguya's Memories ended" },
    { 6, "Finished second part of Waterspout tutorial" },
    { 8, "Finish first part of Waterspout tutorial" },
    { 9, "Spring No More ended" },
    { 11, "Entering first time cutscene" },
    { 12, "Rejection quest started" },
    { 13, "Chun returned home; gate opened" },
    { 14, "Reward for finding Chun cutscene started" },
    { 16, "Finished Waterspout constellation minigame" },
    { 17, "Blocked Path ended" },
    { 18, "Cut glowing bamboo with Take (far left)" },
    { 19, "Cut glowing bamboo with Take (middle left)" },
    { 20, "Cut glowing bamboo with Take (middle)" },
    { 21, "Cut glowing bamboo with Take (middle right)" },
    { 22, "Cut glowing bamboo with Take (far right)" },
    { 23, "Take left bamboo" },
    { 24, "Started Take fight" },
    { 25, "Defeated Take" },
    { 26, "Enter after defeating Crimson Helm" },
    { 27, "Nuregami intro cutscene" },
    { 28, "Waterspout tutorial ending cutscene" },
    { 30, "Patrons spawned in bath" },
    { 31, "Finished glowing bamboo cutscene" },
    { 32, "Cut wrong bamboo" },
    { 34, "Bamboo begins to glow" },
    { 35, "Blocked Path started" },
    { 36, "Mermaid coin stock added to shops" },
    { 38, "Spoke to Mr Bamboo during the day post-Crimson Helm" },
    { 39, "Spoke to Mr Bamboo at night post-Crimson Helm" },
    { 44, "Spoke to inside Bath Attendant first time" },
    { 45, "Talked to Tai first time (Canine tracker appears)" },
    { 47, "Talked to Tai after defeating Take" },
    { 48, "Refused to fight Take" },
    { 50, "Spoke to Tai after defeating Crimson Helm" },
    { 52, "Spoke to Head Servant first time" },
    { 53, "Spoke to blue hair pin Servant first time" },
    { 54, "Spoke to low double bun Servant first time" },
    { 55, "Spoke to yellow flower Servant first time" },
    { 56, "Spoke to high double buns Servant first time" },
    { 58, "Spoke to glasses Servant first time" },
    { 59, "Spoke to tiger Guest's room Servant first time" },
    { 60, "Spoke to Mistress first time" },
    { 61, "Spring No More started" },
    { 62, "Spoke to Bath Attendant outside 2nd time" },
    { 63, "Spoke to Bath Attendant after getting Waterspout" },
    { 64, "Spoke to large sparrow Bather first time" },
    { 65, "Spoke to tiger Guest first time" },
    { 66, "Spoke to Merchant first time" },
    { 68, "Talked to Take after defeating him" },
    { 69, "Spoke to Take after defeating Crimson Helm" },
    { 70, "Spoke to Take first time" }
};

const std::unordered_map<unsigned, const char *> userIndices = {};

const std::unordered_map<unsigned, const char *> collectedObjects =
{
    { 0, "4th chest on the left in Jumba's room (Vase)" },
    { 1, "2nd chest on the left in Jumba's room (Dragonfly Beads)" },
    { 2, "5th chest on the right in Jumba's room (Coral Fragment)" },
    { 3, "3rd chest on the right in Jumba's room (Wooden Bear)" },
    { 4, "1st chest on the right in Jumba's room (Glass Beads)" },
    { 5, "4th chest on the right in Jumba's room (Holy Bone S)" },
    { 6, "2nd chest on the right in Jumba's room (Traveler's Charm)" },
    { 7, "5th chest on the left in Jumba's room (Exorcism Slip S)" },
    { 8, "3rd chest on the left in Jumba's room (Steel Fist Sake)" },
    { 9, "1st chest on the left in Jumba's room (Steel Soul Sake)" },
    { 10, "Clover on ledge above hot spring" },
    { 11, "Buried chest right outside Sanctuary (Stray Bead)" },
    { 12, "Buried chest in grass patch left of the hot spring (Lacquerware Set)" },
    { 13, "Buried chest up 2 sets of stairs in bamboo forest (Incense Burner)" },
    { 14, "Daruma doll in Merchant's room (Stray Bead)" },
    { 16, "Buried chest on right in rabbit clearing (Stray Bead)" },
    { 17, "Buried chest on left in rabbit clearing (Golden Peach)" }
};

const std::unordered_map<unsigned, const char *> areasRestored = {};

const std::unordered_map<unsigned, const char *> treesBloomed = {};

const std::unordered_map<unsigned, const char *> cursedTreesBloomed = {};

const std::unordered_map<unsigned, const char *> fightsCleared = {};

const std::unordered_map<unsigned, const char *> npcs ={ { 0, "Mr Bamboo" }, { 5, "Bath Attendant" } };

const std::unordered_map<unsigned, const char *> mapsExplored = {};

const std::unordered_map<unsigned, const char *> field_DC = {};

const std::unordered_map<unsigned, const char *> field_E0 = {};

} // namespace okami::game_state::maps::SasaSanctuary
