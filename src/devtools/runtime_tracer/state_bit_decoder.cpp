#include "state_bit_decoder.h"

namespace wolf_tracer
{

DecodedStateBit decodeStateBit(uint32_t enc) noexcept
{
    DecodedStateBit d{};
    d.subStructIndex = enc >> 16;
    d.bitIndex = enc & 0xFFFF;
    d.wordOffset = d.subStructIndex * 0x20 + 0x35C + (d.bitIndex / 32) * 4;
    d.mask = 0x80000000u >> (d.bitIndex % 32);
    return d;
}

} // namespace wolf_tracer
