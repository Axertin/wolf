#pragma once

#include <cstdint>

namespace wolf_tracer
{

struct DecodedStateBit
{
    uint32_t subStructIndex; // high16 of enc
    uint32_t bitIndex;       // low16 of enc
    uint32_t wordOffset;     // offset within the world-state struct (relative to base)
    uint32_t mask;           // 32-bit mask within the target word
};

// Decode the argument passed to FUN_180170830 into its addressing components.
// Pure function: does not read any memory.
DecodedStateBit decodeStateBit(uint32_t enc) noexcept;

} // namespace wolf_tracer
