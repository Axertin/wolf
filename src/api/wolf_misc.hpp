/**
 * @file wolf_misc.hpp
 * @brief WOLF Framework Miscelaneous Functions
 */

#pragma once

#include "wolf_core.hpp"

namespace wolf
{
inline void giveItem(int okamiItemID, int quantity)
{
    detail::g_runtime->giveInvItem(okamiItemID, quantity);
}
} // namespace wolf
