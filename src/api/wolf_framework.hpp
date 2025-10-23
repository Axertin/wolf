/**
 * @file wolf_framework.hpp
 * @brief WOLF Okami Loader Framework - Main Header Entry Point
 *
 * This header includes all Wolf framework components in the correct dependency order.
 * When flattened, this becomes the single-header distribution.
 */

#pragma once

// Core runtime API (uses function table)
#include "wolf_core.hpp"

// Specialized API modules (order doesn't matter for these)
#include "wolf_bitfield.hpp"
#include "wolf_console.hpp"
#include "wolf_gui.hpp"
#include "wolf_hooks.hpp"
#include "wolf_logging.hpp"
#include "wolf_memory.hpp"
#include "wolf_misc.hpp"
#include "wolf_resources.hpp"
