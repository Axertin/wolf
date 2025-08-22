#pragma once

/**
 * @brief Initialize DevTools data finder system
 *
 * Sets up bitfield monitoring using WOLF framework to detect and log
 * game state changes with human-readable descriptions from YAML configuration.
 */
void initializeDevDataFinder();

/**
 * @brief Shutdown data finder system
 *
 * Cleanup happens automatically via WOLF's RAII system.
 */
void shutdownDevDataFinder();
