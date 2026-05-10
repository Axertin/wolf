#include <catch2/catch_test_macros.hpp>

#include "runtime_tracer/watches.h"

using wolf_tracer::diffMaskedValue;

TEST_CASE("diffMaskedValue returns true when masked bits change", "[tracer]")
{
    REQUIRE(diffMaskedValue(0xFF00, 0x00FF, 0x12) == true);
}

TEST_CASE("diffMaskedValue returns false when only unmasked bits change", "[tracer]")
{
    // XOR(0x00AA, 0xFF00) = 0xFFAA. Mask 0x0055 selects bits 0/2/4/6 — none of
    // those are set in 0xFFAA, so masked diff is zero.
    REQUIRE(diffMaskedValue(0x00AA, 0xFF00, 0x0055) == false);
}

TEST_CASE("diffMaskedValue returns false when same masked bits", "[tracer]")
{
    REQUIRE(diffMaskedValue(0x12345678, 0x12345678, 0xFFFFFFFF) == false);
}
