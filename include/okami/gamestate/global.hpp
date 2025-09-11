#pragma once
#include <unordered_map>

// Auto-generated from src/devtools/game-data/global.yml
// Do not edit manually - regenerate with scripts/generate_gamestate_headers.py

namespace okami::game_state::global
{

const std::unordered_map<unsigned, const char *> brushUpgrades =
{
    { 0, "Power Slash 2" },
    { 6, "Cherry Bomb 2" },
    { 10, "Power Slash 3" },
    { 11, "Cherry Bomb 3" }
};

const std::unordered_map<unsigned, const char *> areasRestored =
{
    { 1, "Kamiki Village" },
    { 2, "Hana Valley" },
    { 3, "Shinshu Field" },
    { 4, "Tsuta Ruins" },
    { 5, "Agata Forest" },
    { 6, "Taka Pass" }
};

const std::unordered_map<unsigned, const char *> commonStates ={ { 30, "All trees bloomed" }, { 31, "Visited" } };

const std::unordered_map<unsigned, const char *> gameProgress =
{
    { 2, "Battle started after talking with merchant" },
    { 3, "Yomigami gives rejuvenation" },
    { 4, "Tachigami gives power slash" },
    { 6, "After gaining sunrise and entering battle" },
    { 7, "After picking up susano first time" },
    { 9, "Guardian Sapling Trouble journal added" },
    { 10, "Konohana shuffle started" },
    { 11, "Sakuya tree bloomed" },
    { 12, "lily pad constellation appears" },
    { 13, "finished lily pad constellation" },
    { 14, "finished lily pad tutorial" },
    { 15, "spider queen defeated" },
    { 16, "spoke with susano after mural battle in Hana Valley" },
    { 17, "mural battle (new enemy) in Hana Valley" },
    { 18, "restored mural in Hana Valley" },
    { 19, "restored sapling and shown constellation Hana Valley" },
    { 20, "greensprout obtained" },
    { 24, "revived Shinshu sapling" },
    { 26, "Vine constellation shown" },
    { 27, "Vine constellation completed" },
    { 28, "Bud Ogre enemy introduced" },
    { 29, "Spider queen combat started" },
    { 30, "tsuta ruins pond room battle triggered" },
    { 31, "cherry bomb constellation" },
    { 32, "received cherry bomb" },
    { 36, "river of heavens stars twinkling sets this" },
    { 37, "boulder slash minigame failed" },
    { 41, "Mrs Orange gave Cherry Cake for quest" },
    { 42, "cave of nagi constellation sets this" },
    { 45, "boulder removed" },
    { 46, "waka fight" },
    { 47, "waka story time" },
    { 50, "log minigame started" },
    { 51, "got ruins key" },
    { 52, "Ume rescued from spider queen: post-dialogue" },
    { 53, "Hana Valley bear defeated" },
    { 60, "first fish demon encountered" },
    { 61, "waka 2 fight started" },
    { 62, "waka 2 fight finished" }
};

const std::unordered_map<unsigned, const char *> keyItemsFound =
{
    { 0, "Charcoal" },
    { 1, "Blinding Snow" },
    { 2, "Treasure Box" },
    { 4, "Herbal Medicine" },
    { 5, "Pinwheel" },
    { 6, "Marlin Rod" }
};

const std::unordered_map<unsigned, const char *> goldDustsFound =
{
    { 0, "Agata Forest Merchant 1" },
    { 1, "Agata Forest Merchant 2" },
    { 2, "Kusa Village Merchant 1" },
    { 3, "Kusa Village Merchant 2" },
    { 4, "Sei-an City Merchant 1" },
    { 5, "Sei-an City Merchant 2" },
    { 6, "Wep'keer Merchant 1" },
    { 7, "Wep'keer Merchant 2" },
    { 8, "Ark of Yamato Merchant 1" },
    { 9, "Ark of Yamato Merchant 2" }
};

const std::unordered_map<unsigned, const char *> animalsFound =
{
    { 1, "Hana Valley campfire - Monkey" },
    { 2, "Hana Valley mural - Boar Piglet" },
    { 3, "Kamiki village - Chicken" },
    { 4, "Kamiki village entrance - Hare" },
    { 5, "Kamiki village Hayabusa - Dog" },
    { 6, "Kamiki village hill - Sparrow" },
    { 7, "Kamiki village pond - Sparrow" },
    { 8, "Kamiki village farm - Sparrow" },
    { 10, "Shinshu Field gate area by guardian tree - Hare" },
    { 11, "Shinshu Field kiln - Hare" },
    { 12, "Shinshu Field north catwalk field - Hare" },
    { 13, "Shinshu Field east catwalk field - Hare" },
    { 14, "Shinshu Field grass patch by entrance - Boar Piglet" },
    { 19, "Agata Forest Ume - Dog" },
    { 20, "Agata Forest grass patch in front of forest hut - Hare" },
    { 21, "Agata Forest near dungeon entrance along path - Sparrow" },
    { 22, "Agata Forest by waterfall - Monkey" },
    { 23, "Agata Forest Hitoshio Spring - Deer" },
    { 25, "Agata Forest Taka Pass exit - Hare" },
    { 26, "Agata Forest forest hut - Nightingale" },
    { 32, "Taka Pass mermaid spring - Deer" },
    { 33, "Taka Pass sapling field path by cave - Horse" },
    { 34, "Taka Pass right of Sasa exit - Sparrow" },
    { 35, "Taka Pass field between Cutters path and moles - Boar Piglet" },
    { 36, "Taka Pass right of Checkpoint exit - Fox" },
    { 37, "Shinshu Field Agata Forest Port - Boar" },
    { 38, "Shinshu Field dojo - Pig" },
    { 39, "Shinshu Field east path - Hare" },
    { 40, "Shinshu Field near Tama's - Sparrow" },
    { 41, "Shinshu Field near dojo - Horse" },
    { 42, "Agata Forest dungeon entrance - Bear" },
    { 43, "Agata Forest in front of dungeon entrance by water - Deer" },
    { 44, "Agata Forest path to dungeon from Kiba closer to dungeon - Deer" },
    { 45, "Agata Forest path to dungeon from Kiba closer to Kiba - Deer" },
    { 46, "Agata Forest Madame Fawn's house - Hare" },
    { 47, "Agata Forest by Kiba - Sparrow" },
    { 48, "Agata Forest sapling - Hare" },
    { 49, "Taka Pass bamboo field by origin mirror - Tiger" },
    { 50, "Taka Pass near Checkpoint exit - Deer" },
    { 51, "Taka Pass stairs to Kusa - Monkey" },
    { 52, "Taka Pass right side of Cutters path junction - Dog" },
    { 53, "Taka Pass intersection to Cutters House entrance - Dog" },
    { 54, "Taka Pass path near sapling - Deer" },
    { 55, "Taka Pass by Sasa Gate sign - Horse" },
    { 56, "Taka Pass in sapling cave - Fox" },
    { 57, "Taka Pass by shrine in front of Princess Fuse's - Sparrow" },
    { 59, "Kusa Village above ladder by Komuso - Raccoon" },
    { 60, "Kusa Village path to Mr Bamboo - Sparrow" },
    { 64, "Kusa Village gourd farmer garden - Pig" },
    { 65, "Kusa Village by stairs to Princess Fuse's - Chicken" },
    { 79, "Ryoshima Coast front of water cave - Cow" },
    { 81, "Ryoshima Coast by graveyard - Fox" },
    { 82, "Ryoshima Coast by entrance origin mirror - Dog" },
    { 84, "Ryoshima Coast right tri-cursed grass - Boar" },
    { 85, "Ryoshima Coast beach front right of merchant - Monkey" },
    { 86, "Ryoshima Coast north ryoshima gate - Horse" },
    { 87, "Ryoshima Coast outside Seian gate - Hare" },
    { 88, "Ryoshima Coast behind bell - Hare" },
    { 89, "Ryoshima Coast dojo island - Monkey" },
    { 90, "Taka Pass sapling - Nightingale" },
    { 100, "Ryoshima Coast Inaba - Hare" },
    { 106, "Kamui in front of Wali's house - Sheep" },
    { 107, "Kamui bottom of sapling ledge - Bear" },
    { 112, "Kamui north area under the bridge - Hare" },
    { 114, "Kamui (Ezofuji) lower north ledge - Sheep" },
    { 116, "Kamui (Ezofuji) outside power slash 3 cave - Monkey" },
    { 117, "Kamui (Ezofuji) west part along path - Bear" },
    { 119, "Kamui (Ezofuji) southwest part along path - Raccoon" },
    { 137, "Ryoshima Coast middle tri-cursed grass - Boar" },
    { 138, "Ryoshima Coast nook by entrance to Seian - Cow" },
    { 139, "Ryoshima Coast end of dock - Cat" },
    { 140, "Ryoshima Coast sapling - Nightingale" },
    { 141, "Ryoshima Coast left tri-cursed grass - Pig" },
    { 160, "Taka Pass Stray Bead 26 cave - Monkey" }
};

const std::unordered_map<unsigned, const char *> animalsFedFirstTime =
{
    { 16, "Hare" },
    { 17, "Dog" },
    { 19, "Sparrow" },
    { 20, "Monkey" },
    { 21, "Boar Piglet" },
    { 22, "Boar" },
    { 23, "Hayabusa (dog)" },
    { 24, "Ume (dog)" },
    { 31, "Deer" },
    { 33, "Bear" },
    { 34, "Pig" },
    { 37, "Tiger" },
    { 38, "Horse" },
    { 39, "Cat" },
    { 40, "Cow" },
    { 41, "Sheep" },
    { 42, "Raccoon" },
    { 43, "Nightingale" },
    { 44, "Inaba (hare)" }
};

const std::unordered_map<unsigned, const char *> globalGameState =
{
    { 1, "Scene border is up, in conversation" },
    { 4, "Main menu open" },
    { 5, "Celestial brush open" },
    { 6, "Loading area" },
    { 8, "Popup map open" },
    { 10, "Item shop menu" },
    { 12, "In battle" },
    { 18, "Skill shop menu" },
    { 21, "Disabled interactions" },
    { 34, "Mirror warp menu" },
    { 37, "Demon fang shop menu" },
    { 41, "Save menu" },
    { 44, "Disable menus" },
    { 46, "Save window up at origin mirror" },
    { 50, "Mirror warp menu" },
    { 54, "Popup map open" },
    { 55, "In battle" },
    { 60, "Issun jumping over to something" },
    { 61, "Examine window up" },
    { 69, "Celestial brush on cooldown" },
    { 71, "Disabled character controls" }
};

} // namespace okami::game_state::global
