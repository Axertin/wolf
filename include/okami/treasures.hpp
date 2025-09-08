#pragma once

namespace okami
{
namespace Treasures
{
/**
 * @brief Enum representing all treasures.
 */
enum Enum
{
    GlassBead,
    DragonflyBead,
    WoodenBear,
    CoralFragment,
    Vase,
    IncenseBurner,
    LacquerwareSet,
    BullHorn,
    RatStatue,
    BullStatue,
    TigerStatue,
    RabbitStatue,
    DragonStatue,
    SnakeStatue,
    HorseStatue,
    SheepStatue,
    MonkeyStatue,
    RoosterStatue,
    DogStatue,
    BoarStatue,
    CatStatue,
    Crystal,
    Pearl,
    AmethystTassels,
    AmberTassels,
    JadeTassels,
    CatsEyeTassels,
    AgateTassels,
    TurquoiseTassels,
    RubyTassels,
    SapphireTassels,
    EmeraldTassels,
    KutaniPottery,
    WhitePorcelainPot,
    EtchedGlass,
    SilverPocketWatch,
    NUM_TREASURES
};
static const char *name[NUM_TREASURES] = {
    "Glass Bead",          "Dragonfly Bead",    "Wooden Bear",    "Coral Fragment",      "Vase",
    "Incense Burner",      "Lacquerware Set",   "Bull Horn",      "Rat Statue",          "Bull Statue",
    "Tiger Statue",        "Rabbit Statue",     "Dragon Statue",  "Snake Statue",        "Horse Statue",
    "Sheep Statue",        "Monkey Statue",     "Rooster Statue", "Dog Statue",          "Boar Statue",
    "Cat Statue",          "Crystal",           "Pearl",          "Amethyst Tassels",    "Amber Tassels",
    "Jade Tassels",        "Cat's Eye Tassels", "Agate Tassels",  "Turquoise Tassels",   "Ruby Tassels",
    "Sapphire Tassels",    "Emerald Tassels",   "Kutani Pottery", "White Porcelain Pot", "Etched Glass",
    "Silver Pocket Watch",
};

inline const char *GetName(unsigned index)
{
    if (index < NUM_TREASURES)
    {
        return name[index];
    }
    return "invalid";
}
} // namespace Treasures
} // namespace okami
