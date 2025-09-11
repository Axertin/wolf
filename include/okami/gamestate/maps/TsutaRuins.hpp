#pragma once
#include <unordered_map>

// Auto-generated from src/devtools/game-data/maps/TsutaRuins.yml
// Do not edit manually - regenerate with scripts/generate_gamestate_headers.py

namespace okami::game_state::maps::TsutaRuins {

const std::unordered_map<unsigned, const char*> worldStateBits = {
        {1, "Ball in greenway room de-petrified"},
        {2, "Triggered battle for enemy bud ogre"},
        {3, "Ball in greenway room placed in slot"},
        {4, "Pond room battle triggered"},
        {5, "Pond room battle cleared"},
        {6, "Pond room sunshine added"},
        {7, "Issun trigger at entrance path 2F"},
        {8, "Blockhead defeated"},
        {9, "Blockhead defeated"},
        {10, "Blockhead defeated"},
        {11, "Left room 2F door unlocked"},
        {12, "Bridge above entrance restored"},
        {13, "Hell gates room defeated"},
        {14, "Hell gates room sunshine added"},
        {15, "Poison pots destroyed"},
        {16, "Center room right side cursed grass restored"},
        {17, "Center room back side cursed grass restored"},
        {18, "Center room left side cursed grass restored"},
        {19, "Center room right side cursed grass restored"},
        {20, "Center room back side cursed grass restored"},
        {21, "Center room left side cursed grass restored"},
        {22, "Vine tutorial completed"},
        {23, "Constellation completed"},
        {24, "Failed to draw constellation once"},
        {25, "Entrance 4F lid lifted"},
        {26, "Issun pre-boss room trigger"},
        {29, "Issun text after failing constellation"},
        {30, "Constellation completed"},
        {31, "Vine tutorial brush hint overlay"},
        {32, "Vine used first time"},
        {33, "Entrance room path 2F collapsing"},
        {34, "Entered inner ruins"},
        {35, "Blockhead introduced"},
        {36, "Issun asks if you really want to leave"},
        {48, "Issun commented on ruins restoration"},
        {51, "Left Vine tutorial room through top exit"},
        {56, "Pond room 2F crack bombed"},
        {57, "Pond room 2F hidden wall bombed"},
        {58, "Blew up wall leading to Stray Bead chest"},
        {64, "Bud ogre stunned tutorial"},
        {67, "Bud ogre stunned tutorial"},
        {80, "Intro triggered"},
        {81, "View scene triggered in greenway room by waterfall"},
        {82, "View scene triggered at entrance path 2F"},
        {83, "View scene triggered post-vine at entrance 2F center"},
        {84, "View scene triggered at hell gates room"}
};

const std::unordered_map<unsigned, const char*> userIndices = {};

const std::unordered_map<unsigned, const char*> collectedObjects = {
        {0, "Greenway room bulb lower level (Incense Burner)"},
        {1, "Blockhead room 2F bulb behind bomb wall (Stray Bead)"},
        {2, "Pond room bulb (Vengeance Slip)"},
        {3, "Clover pond room 2F ledge"},
        {4, "Gloomy room bulb (Exorcism Slip S)"},
        {5, "Clover entrance room 2F just outside origin mirror room"},
        {6, "Clover entrance room middle"},
        {7, "Clover entrance room left side island"},
        {8, "Entrance bulb (Traveler's Charm)"},
        {9, "Entrance room bulb right side (Steel Soul Sake)"},
        {10, "Clover right side room on island"},
        {11, "Clover above greenway room entrance"},
        {12, "Greenway room bulb by stone ball (Steel Fist Sake)"},
        {13, "Entrance room bulb left side (Exorcism Slip S)"},
        {14, "Clover at greenway room's highest point"},
        {15, "Bulb at greenway room's highest point (Stray Bead)"},
        {16, "Clover main room right side ledge"},
        {17, "Entrance chest ([Enhancing Divinity])"},
        {18, "Blockhead room 2F bulb (Golden Peach)"},
        {19, "Clover blockhead room 2F left"},
        {20, "Clover blockhead room 2F right"},
        {21, "Boss hallway chest left ([Godhood Tips])"},
        {22, "Boss hallway chest right (Holy Bone S)"},
        {23, "Hell gates room bulb left (Lacquerware Set)"},
        {24, "Hell gates room bulb right (Holy Bone S)"},
        {25, "Hell gates room bulb behind waterfall bombable wall (Stray Bead)"},
        {26, "Entrance room chest right side (Vase)"},
        {27, "Hell gates room chest (Tsuta Ruins Map)"}
};

const std::unordered_map<unsigned, const char*> areasRestored = {
        {16, "Hell gates room"},
        {17, "Pond room restored"},
        {18, "Center room right side cursed grass"},
        {19, "Center room back side cursed grass"},
        {20, "Center room left side cursed grass"},
        {21, "Tsuta Ruins restored"},
        {22, "Hell gates room mushrooms"},
        {23, "Left room"},
        {26, "Center room fully"}
};

const std::unordered_map<unsigned, const char*> treesBloomed = {};

const std::unordered_map<unsigned, const char*> cursedTreesBloomed = {
        {0, "Cursed tree by entrance right side bloomed"},
        {1, "First cursed tree in right side room"},
        {2, "Second cursed tree in right side room"},
        {3, "Cursed tree entrance room left side island bloomed"}
};

const std::unordered_map<unsigned, const char*> fightsCleared = {
        {1, "Gloomy room slip"}
};

const std::unordered_map<unsigned, const char*> npcs = {};

const std::unordered_map<unsigned, const char*> mapsExplored = {
        {1, "Entrance"},
        {2, "Right side room"},
        {3, "Greenway room 1F"},
        {4, "Pond room 1F"},
        {5, "Hallway room 2F"},
        {6, "Left side room"},
        {7, "Gloomy room 2F"},
        {8, "Hell gates room 2F"},
        {9, "Center room"},
        {10, "Deep mirror room 3F"},
        {11, "Boss room 3F"},
        {12, "Origin mirror room 2F"}
};

const std::unordered_map<unsigned, const char*> field_DC = {};

const std::unordered_map<unsigned, const char*> field_E0 = {};

} // namespace okami::game_state::maps::TsutaRuins
