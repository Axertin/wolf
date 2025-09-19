#pragma once

// Mock version information for unit tests
// This ensures tests are stable regardless of actual project version

#define WOLF_VERSION_MAJOR 1
#define WOLF_VERSION_MINOR 2
#define WOLF_VERSION_PATCH 3
#define WOLF_VERSION_PRERELEASE ""
#define WOLF_VERSION_IS_PRERELEASE FALSE
#define WOLF_VERSION_BUILD ""
#define WOLF_VERSION_STRING "1.2.3"

// Semantic version as single integer for comparisons (1.2.3 = 0x01020003)
#define WOLF_VERSION_INT ((1U << 24) | (2U << 16) | 3U)

// Build information
#define WOLF_BUILD_TYPE "Test"
#define WOLF_COMPILER "Test Compiler"
