#include <catch2/catch_test_macros.hpp>

#include "runtime_tracer/state_bit_decoder.h"

using wolf_tracer::DecodedStateBit;
using wolf_tracer::decodeStateBit;

TEST_CASE("decodeStateBit splits enc into high16 + low16", "[tracer]")
{
    auto d = decodeStateBit(0x00020009);
    REQUIRE(d.subStructIndex == 2);
    REQUIRE(d.bitIndex == 9);
}

TEST_CASE("decodeStateBit produces +0x39C bit 9 for the doc example", "[tracer]")
{
    // Per event-triggers-runtime.md: FUN_180170830(0x20009) sets bit 9
    // in DAT_180b205c0 + 0x39C.
    auto d = decodeStateBit(0x00020009);
    REQUIRE(d.wordOffset == 0x39C);
    REQUIRE(d.mask == 0x00400000u); // 0x80000000 >> (9 % 32)
}

TEST_CASE("decodeStateBit handles bit 0 of substruct 0", "[tracer]")
{
    auto d = decodeStateBit(0x00000000);
    REQUIRE(d.subStructIndex == 0);
    REQUIRE(d.bitIndex == 0);
    REQUIRE(d.wordOffset == 0x35C);
    REQUIRE(d.mask == 0x80000000u);
}

TEST_CASE("decodeStateBit handles a bit that crosses 32-bit word boundary", "[tracer]")
{
    // bit 32 -> word offset 0x35C + 4 = 0x360, mask 0x80000000 (bit 0 of next word)
    auto d = decodeStateBit(0x00000020);
    REQUIRE(d.bitIndex == 32);
    REQUIRE(d.wordOffset == 0x360);
    REQUIRE(d.mask == 0x80000000u);
}
