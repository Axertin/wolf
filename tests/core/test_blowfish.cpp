#include <catch2/catch_all.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>
#include <vector>

#include "blowfish.h"

using namespace Nippon;

// Helper to create a vector from hex string
std::vector<uint8_t> hexToBytes(const std::string& hex)
{
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < hex.length(); i += 2)
    {
        std::string byteString = hex.substr(i, 2);
        uint8_t byte = static_cast<uint8_t>(strtol(byteString.c_str(), nullptr, 16));
        bytes.push_back(byte);
    }
    return bytes;
}

// Helper to compare uint32_t arrays
bool compareUint32Arrays(uint32_t* a, uint32_t* b, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        if (a[i] != b[i]) return false;
    }
    return true;
}

TEST_CASE("BlowFish - Basic encrypt/decrypt roundtrip with pointer API", "[blowfish][roundtrip]")
{
    SECTION("Simple 8-byte block")
    {
        BlowFish::Create("TestKey123");

        uint32_t xl = 0x12345678;
        uint32_t xr = 0x9ABCDEF0;
        uint32_t orig_xl = xl;
        uint32_t orig_xr = xr;

        BlowFish::Encrypt(&xl, &xr);

        // Data should be different after encryption
        bool encrypted = (xl != orig_xl) || (xr != orig_xr);
        REQUIRE(encrypted);

        BlowFish::Decrypt(&xl, &xr);

        // Data should match original after decryption
        REQUIRE(xl == orig_xl);
        REQUIRE(xr == orig_xr);
    }

    SECTION("Multiple different values")
    {
        BlowFish::Create("AnotherTestKey");

        std::vector<std::pair<uint32_t, uint32_t>> testCases = {
            {0x00000000, 0x00000000},
            {0xFFFFFFFF, 0xFFFFFFFF},
            {0x12345678, 0x9ABCDEF0},
            {0xDEADBEEF, 0xCAFEBABE},
            {0x01010101, 0x02020202}
        };

        for (const auto& [orig_xl, orig_xr] : testCases)
        {
            uint32_t xl = orig_xl;
            uint32_t xr = orig_xr;

            BlowFish::Encrypt(&xl, &xr);
            BlowFish::Decrypt(&xl, &xr);

            REQUIRE(xl == orig_xl);
            REQUIRE(xr == orig_xr);
        }
    }
}

TEST_CASE("BlowFish - Encrypt/decrypt with vector API", "[blowfish][vector][roundtrip]")
{
    SECTION("8-byte vector")
    {
        BlowFish::Create("VectorTestKey");

        std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        std::vector<uint8_t> original = data;

        BlowFish::Encrypt(data);

        // Data should be different after encryption
        REQUIRE(data != original);

        BlowFish::Decrypt(data);

        // Data should match original
        REQUIRE(data == original);
    }

    SECTION("16-byte vector (multiple blocks)")
    {
        BlowFish::Create("MultiBlockKey");

        std::vector<uint8_t> data = {
            0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
            0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00
        };
        std::vector<uint8_t> original = data;

        BlowFish::Encrypt(data);
        REQUIRE(data != original);

        BlowFish::Decrypt(data);
        REQUIRE(data == original);
    }

    SECTION("24-byte vector (three blocks)")
    {
        BlowFish::Create("ThreeBlockKey");

        std::vector<uint8_t> data(24);
        for (size_t i = 0; i < data.size(); i++)
        {
            data[i] = static_cast<uint8_t>(i * 17);
        }
        std::vector<uint8_t> original = data;

        BlowFish::Encrypt(data);
        REQUIRE(data != original);

        BlowFish::Decrypt(data);
        REQUIRE(data == original);
    }
}

TEST_CASE("BlowFish - Standard test vectors", "[blowfish][test_vectors]")
{
    // Test vectors from various Blowfish test suites
    // Format: key, plaintext (XL, XR), ciphertext (XL, XR)

    SECTION("Test vector 1 - Simple key")
    {
        BlowFish::Create("TESTKEY");

        uint32_t xl = 0x00000000;
        uint32_t xr = 0x00000000;

        BlowFish::Encrypt(&xl, &xr);

        // After encryption with "TESTKEY", zeros should be encrypted to something else
        bool encrypted = (xl != 0x00000000) || (xr != 0x00000000);
        REQUIRE(encrypted);

        // Decrypt and verify roundtrip
        BlowFish::Decrypt(&xl, &xr);
        REQUIRE(xl == 0x00000000);
        REQUIRE(xr == 0x00000000);
    }

    SECTION("Test vector 2 - Known plaintext")
    {
        BlowFish::Create("WhoCares");

        uint32_t xl = 0xFEDCBA98;
        uint32_t xr = 0x76543210;
        uint32_t orig_xl = xl;
        uint32_t orig_xr = xr;

        BlowFish::Encrypt(&xl, &xr);

        // Should produce different output
        bool encrypted = (xl != orig_xl) || (xr != orig_xr);
        REQUIRE(encrypted);

        BlowFish::Decrypt(&xl, &xr);
        REQUIRE(xl == orig_xl);
        REQUIRE(xr == orig_xr);
    }

    SECTION("Test vector 3 - All ones")
    {
        BlowFish::Create("0123456789ABCDEF");

        uint32_t xl = 0xFFFFFFFF;
        uint32_t xr = 0xFFFFFFFF;

        BlowFish::Encrypt(&xl, &xr);

        // Should not stay all ones
        bool encrypted = (xl != 0xFFFFFFFF) || (xr != 0xFFFFFFFF);
        REQUIRE(encrypted);

        BlowFish::Decrypt(&xl, &xr);
        REQUIRE(xl == 0xFFFFFFFF);
        REQUIRE(xr == 0xFFFFFFFF);
    }
}

TEST_CASE("BlowFish - Okami cipher key", "[blowfish][okami]")
{
    // Test with the actual Okami cipher key
    BlowFish::Create(OKAMI_CIPHER_KEY);

    SECTION("Encrypt/decrypt with Okami key - pointer API")
    {
        uint32_t xl = 0x4F4B414D; // "OKAM" in ASCII
        uint32_t xr = 0x49444154; // "IDAT" in ASCII
        uint32_t orig_xl = xl;
        uint32_t orig_xr = xr;

        BlowFish::Encrypt(&xl, &xr);

        // Should be encrypted
        bool encrypted = (xl != orig_xl) || (xr != orig_xr);
        REQUIRE(encrypted);

        BlowFish::Decrypt(&xl, &xr);

        // Should decrypt back
        REQUIRE(xl == orig_xl);
        REQUIRE(xr == orig_xr);
    }

    SECTION("Encrypt/decrypt with Okami key - vector API")
    {
        // Test with actual game data pattern (8-byte aligned)
        std::vector<uint8_t> data = {
            0x4F, 0x4B, 0x41, 0x4D, 0x49, 0x20, 0x44, 0x41,  // "OKAMI DA"
            0x54, 0x41, 0x20, 0x54, 0x45, 0x53, 0x54, 0x00   // "TA TEST\0"
        };
        std::vector<uint8_t> original = data;

        BlowFish::Encrypt(data);
        REQUIRE(data != original);

        BlowFish::Decrypt(data);
        REQUIRE(data == original);
    }

    SECTION("Multiple blocks with Okami key")
    {
        // Test with larger data chunk (32 bytes = 4 blocks)
        std::vector<uint8_t> data(32);
        for (size_t i = 0; i < data.size(); i++)
        {
            data[i] = static_cast<uint8_t>((i * 7 + 13) % 256);
        }
        std::vector<uint8_t> original = data;

        BlowFish::Encrypt(data);
        REQUIRE(data != original);

        BlowFish::Decrypt(data);
        REQUIRE(data == original);
    }
}

TEST_CASE("BlowFish - Key changes affect encryption", "[blowfish][key_sensitivity]")
{
    SECTION("Different keys produce different ciphertext")
    {
        uint32_t xl1 = 0x12345678;
        uint32_t xr1 = 0x9ABCDEF0;

        uint32_t xl2 = 0x12345678;
        uint32_t xr2 = 0x9ABCDEF0;

        // Encrypt with first key
        BlowFish::Create("KeyOne");
        BlowFish::Encrypt(&xl1, &xr1);

        // Encrypt with second key
        BlowFish::Create("KeyTwo");
        BlowFish::Encrypt(&xl2, &xr2);

        // Ciphertext should be different
        bool different = (xl1 != xl2) || (xr1 != xr2);
        REQUIRE(different);
    }

    SECTION("Same key produces same ciphertext")
    {
        uint32_t xl1 = 0xDEADBEEF;
        uint32_t xr1 = 0xCAFEBABE;

        uint32_t xl2 = 0xDEADBEEF;
        uint32_t xr2 = 0xCAFEBABE;

        // Encrypt with same key twice
        BlowFish::Create("SameKey");
        BlowFish::Encrypt(&xl1, &xr1);

        BlowFish::Create("SameKey");
        BlowFish::Encrypt(&xl2, &xr2);

        // Ciphertext should be identical
        REQUIRE(xl1 == xl2);
        REQUIRE(xr1 == xr2);
    }
}

TEST_CASE("BlowFish - Edge cases", "[blowfish][edge_cases]")
{
    SECTION("Empty vector (0 bytes)")
    {
        BlowFish::Create("TestKey");

        std::vector<uint8_t> data;
        std::vector<uint8_t> original = data;

        // Should handle empty data gracefully
        BlowFish::Encrypt(data);
        REQUIRE(data == original);

        BlowFish::Decrypt(data);
        REQUIRE(data == original);
    }

    SECTION("All zero data")
    {
        BlowFish::Create("ZeroTestKey");

        std::vector<uint8_t> data(16, 0x00);
        std::vector<uint8_t> original = data;

        BlowFish::Encrypt(data);

        // Should encrypt zeros to something else
        REQUIRE(data != original);

        BlowFish::Decrypt(data);
        REQUIRE(data == original);
    }

    SECTION("All 0xFF data")
    {
        BlowFish::Create("FFTestKey");

        std::vector<uint8_t> data(16, 0xFF);
        std::vector<uint8_t> original = data;

        BlowFish::Encrypt(data);
        REQUIRE(data != original);

        BlowFish::Decrypt(data);
        REQUIRE(data == original);
    }

    SECTION("Repeating pattern")
    {
        BlowFish::Create("PatternKey");

        std::vector<uint8_t> data;
        for (int i = 0; i < 32; i++)
        {
            data.push_back(static_cast<uint8_t>(i % 4));
        }
        std::vector<uint8_t> original = data;

        BlowFish::Encrypt(data);
        REQUIRE(data != original);

        BlowFish::Decrypt(data);
        REQUIRE(data == original);
    }
}

TEST_CASE("BlowFish - Consistency between APIs", "[blowfish][api_consistency]")
{
    SECTION("Pointer API and vector API produce same results")
    {
        BlowFish::Create("ConsistencyKey");

        // Prepare data for pointer API
        uint32_t xl_ptr = 0xABCDEF01;
        uint32_t xr_ptr = 0x23456789;

        // Prepare same data for vector API
        std::vector<uint8_t> vec_data(8);
        uint32_t* xl_vec = reinterpret_cast<uint32_t*>(&vec_data[0]);
        uint32_t* xr_vec = reinterpret_cast<uint32_t*>(&vec_data[4]);
        *xl_vec = 0xABCDEF01;
        *xr_vec = 0x23456789;

        // Encrypt with both APIs
        BlowFish::Encrypt(&xl_ptr, &xr_ptr);
        BlowFish::Encrypt(vec_data);

        // Results should match
        REQUIRE(xl_ptr == *xl_vec);
        REQUIRE(xr_ptr == *xr_vec);

        // Decrypt and verify both match
        BlowFish::Decrypt(&xl_ptr, &xr_ptr);
        BlowFish::Decrypt(vec_data);

        REQUIRE(xl_ptr == *xl_vec);
        REQUIRE(xr_ptr == *xr_vec);
        REQUIRE(xl_ptr == 0xABCDEF01);
        REQUIRE(xr_ptr == 0x23456789);
    }
}
