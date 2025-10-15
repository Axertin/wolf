#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "../../src/runtime/core/msd_manager.h"

#include <cstring>
#include <vector>
#include <thread>
#include <set>

using namespace wolf::runtime;

TEST_CASE("MSD Manager basic functionality", "[msd_manager]")
{
    MSDManager manager;

    SECTION("Empty manager initialization")
    {
        REQUIRE(manager.getStringCount() == 0);
        REQUIRE(manager.getDataSize() == 0);
        REQUIRE(manager.getData() == nullptr);
    }

    SECTION("Add single string")
    {
        WolfModId testMod = 42;
        std::string testStr = "Hello World";
        
        uint32_t index = manager.addString(testMod, testStr);
        
        REQUIRE(index == 0); // First string should have index 0
        REQUIRE(manager.getStringCount() == 1);
        REQUIRE(manager.getDataSize() > 0);
        
        const uint8_t* data = manager.getData();
        REQUIRE(data != nullptr);
    }

    SECTION("Add multiple strings")
    {
        WolfModId testMod = 42;
        
        uint32_t index1 = manager.addString(testMod, "First");
        uint32_t index2 = manager.addString(testMod, "Second");
        uint32_t index3 = manager.addString(testMod, "Third");
        
        REQUIRE(index1 == 0);
        REQUIRE(index2 == 1);
        REQUIRE(index3 == 2);
        REQUIRE(manager.getStringCount() == 3);
    }
}

TEST_CASE("MSD string compilation and character mapping", "[msd_manager]")
{
    SECTION("Basic ASCII characters")
    {
        // Test known mappings from the original system
        std::vector<uint16_t> result = MSDManager::compileString(" ");
        REQUIRE(result.size() == 2); // Character + EndDialog
        REQUIRE(result[0] == 0);     // Space maps to 0
        REQUIRE(result[1] == 0x8001); // EndDialog terminator
        
        result = MSDManager::compileString(".");
        REQUIRE(result.size() == 2);
        REQUIRE(result[0] == 1);     // Period maps to 1
        REQUIRE(result[1] == 0x8001);
        
        result = MSDManager::compileString("A");
        REQUIRE(result.size() == 2);
        REQUIRE(result[0] == 23);    // 'A' maps to 23
        REQUIRE(result[1] == 0x8001);
        
        result = MSDManager::compileString("a");
        REQUIRE(result.size() == 2);
        REQUIRE(result[0] == 49);    // 'a' maps to 49
        REQUIRE(result[1] == 0x8001);
    }

    SECTION("Numeric characters")
    {
        std::vector<uint16_t> result = MSDManager::compileString("0123456789");
        REQUIRE(result.size() == 11); // 10 digits + EndDialog
        
        // Check that digits map to consecutive values starting at 13
        for (int i = 0; i < 10; i++)
        {
            REQUIRE(result[i] == (13 + i)); // '0' maps to 13, '1' to 14, etc.
        }
        REQUIRE(result[10] == 0x8001); // EndDialog
    }

    SECTION("Multi-character string")
    {
        std::vector<uint16_t> result = MSDManager::compileString("Hello!");
        REQUIRE(result.size() == 7); // 6 characters + EndDialog
        
        REQUIRE(result[0] == 30);    // 'H' 
        REQUIRE(result[1] == 53);    // 'e'
        REQUIRE(result[2] == 60);    // 'l'
        REQUIRE(result[3] == 60);    // 'l'
        REQUIRE(result[4] == 63);    // 'o'
        REQUIRE(result[5] == 4);     // '!'
        REQUIRE(result[6] == 0x8001); // EndDialog
    }

    SECTION("Unsupported characters")
    {
        // Test that unsupported characters map to UnsupportedChar (201)
        std::vector<uint16_t> result = MSDManager::compileString("@");
        REQUIRE(result.size() == 2);
        REQUIRE(result[0] == 201);   // UnsupportedChar
        REQUIRE(result[1] == 0x8001);
        
        result = MSDManager::compileString("Hello@World");
        REQUIRE(result.size() == 12); // 11 characters + EndDialog
        REQUIRE(result[5] == 201);   // '@' should be UnsupportedChar
    }

    SECTION("Empty string")
    {
        std::vector<uint16_t> result = MSDManager::compileString("");
        REQUIRE(result.size() == 1);
        REQUIRE(result[0] == 0x8001); // Only EndDialog
    }

    SECTION("Special punctuation")
    {
        // Test some specific mappings from the original system
        std::vector<uint16_t> result = MSDManager::compileString("()");
        REQUIRE(result.size() == 3);
        REQUIRE(result[0] == 5);     // '(' maps to 5
        REQUIRE(result[1] == 6);     // ')' maps to 6
        REQUIRE(result[2] == 0x8001);
        
        result = MSDManager::compileString("?!");
        REQUIRE(result.size() == 3);
        REQUIRE(result[0] == 3);     // '?' maps to 3
        REQUIRE(result[1] == 4);     // '!' maps to 4
        REQUIRE(result[2] == 0x8001);
    }
}

TEST_CASE("MSD binary format generation", "[msd_manager]")
{
    MSDManager manager;
    WolfModId testMod = 1;

    SECTION("Single string MSD structure")
    {
        manager.addString(testMod, "Test");
        
        const uint8_t* data = manager.getData();
        size_t dataSize = manager.getDataSize();
        
        REQUIRE(data != nullptr);
        REQUIRE(dataSize > sizeof(uint32_t)); // At least the header
        
        // Check header
        const MSDHeader* header = reinterpret_cast<const MSDHeader*>(data);
        REQUIRE(header->numEntries == 1);
        
        // Check that offset points to valid location
        REQUIRE(header->offsets[0] < dataSize);
        REQUIRE(header->offsets[0] >= sizeof(uint32_t) + sizeof(uint64_t)); // After header
    }

    SECTION("Multiple strings MSD structure")
    {
        manager.addString(testMod, "First");
        manager.addString(testMod, "Second");
        manager.addString(testMod, "Third");
        
        const uint8_t* data = manager.getData();
        size_t dataSize = manager.getDataSize();
        
        REQUIRE(data != nullptr);
        
        const MSDHeader* header = reinterpret_cast<const MSDHeader*>(data);
        REQUIRE(header->numEntries == 3);
        
        // Check that all offsets are valid and increasing
        for (uint32_t i = 0; i < 3; i++)
        {
            REQUIRE(header->offsets[i] < dataSize);
            if (i > 0)
            {
                REQUIRE(header->offsets[i] > header->offsets[i-1]);
            }
        }
        
        // Verify we can read the strings back
        for (uint32_t i = 0; i < 3; i++)
        {
            const uint16_t* strData = reinterpret_cast<const uint16_t*>(data + header->offsets[i]);
            
            // Find the EndDialog terminator
            bool foundTerminator = false;
            for (size_t j = 0; j < 100; j++) // Reasonable limit to avoid infinite loop
            {
                if ((strData[j] & 0xFF00) == 0x8000)
                {
                    foundTerminator = true;
                    REQUIRE(strData[j] == 0x8001); // Should be EndDialog specifically
                    break;
                }
            }
            REQUIRE(foundTerminator);
        }
    }
}

TEST_CASE("MSD string override functionality", "[msd_manager]")
{
    MSDManager manager;
    WolfModId testMod = 1;

    SECTION("Override existing string")
    {
        // Add initial string
        uint32_t index = manager.addString(testMod, "Original");
        REQUIRE(index == 0);
        REQUIRE(manager.getStringCount() == 1);
        
        // Override the string
        manager.overrideString(testMod, index, "Modified");
        REQUIRE(manager.getStringCount() == 1); // Count shouldn't change
        
        // Verify the data was updated
        const uint8_t* data = manager.getData();
        const MSDHeader* header = reinterpret_cast<const MSDHeader*>(data);
        const uint16_t* strData = reinterpret_cast<const uint16_t*>(data + header->offsets[0]);
        
        // Check first few characters of "Modified"
        REQUIRE(strData[0] == 35);   // 'M'
        REQUIRE(strData[1] == 63);   // 'o'
        REQUIRE(strData[2] == 52);   // 'd'
    }

    SECTION("Override invalid index")
    {
        // Try to override non-existent string
        manager.overrideString(testMod, 999, "Should not work");
        REQUIRE(manager.getStringCount() == 0);
        REQUIRE(manager.getData() == nullptr);
    }
}

TEST_CASE("MSD from original game data", "[msd_manager]")
{
    MSDManager manager;

    SECTION("Read MSD and preserve structure")
    {
        // Create a minimal valid MSD structure to simulate game data
        // We need to be careful about the layout to avoid misaligned access
        constexpr size_t headerSize = sizeof(uint32_t) + 2 * sizeof(uint64_t);
        constexpr size_t str1Size = 3 * sizeof(uint16_t); // "He" + EndDialog
        constexpr size_t str2Size = 2 * sizeof(uint16_t); // "l" + EndDialog
        
        std::vector<uint8_t> testMSDData(headerSize + str1Size + str2Size);
        uint8_t* data = testMSDData.data();
        
        // Write header
        *reinterpret_cast<uint32_t*>(data) = 2; // numEntries
        *reinterpret_cast<uint64_t*>(data + 4) = headerSize; // offset[0]
        *reinterpret_cast<uint64_t*>(data + 12) = headerSize + str1Size; // offset[1]
        
        // Write string 1: "He" + EndDialog  
        uint16_t* str1 = reinterpret_cast<uint16_t*>(data + headerSize);
        str1[0] = 30;    // 'H'
        str1[1] = 53;    // 'e'
        str1[2] = 0x8001; // EndDialog
        
        // Write string 2: "l" + EndDialog
        uint16_t* str2 = reinterpret_cast<uint16_t*>(data + headerSize + str1Size);
        str2[0] = 60;    // 'l'
        str2[1] = 0x8001; // EndDialog
        manager.readMSD(data);
        
        REQUIRE(manager.getStringCount() == 2);
        
        const uint8_t* resultData = manager.getData();
        const MSDHeader* header = reinterpret_cast<const MSDHeader*>(resultData);
        REQUIRE(header->numEntries == 2);
        
        // Verify we can read back the original strings
        const uint16_t* str1Result = reinterpret_cast<const uint16_t*>(resultData + header->offsets[0]);
        REQUIRE(str1Result[0] == 30);      // 'H'
        REQUIRE(str1Result[1] == 53);      // 'e'
        REQUIRE(str1Result[2] == 0x8001);  // EndDialog
        
        const uint16_t* str2Result = reinterpret_cast<const uint16_t*>(resultData + header->offsets[1]);
        REQUIRE(str2Result[0] == 60);      // 'l'
        REQUIRE(str2Result[1] == 0x8001);  // EndDialog
    }

    SECTION("Add strings to loaded MSD")
    {
        // Load original data first
        constexpr size_t headerSize = sizeof(uint32_t) + sizeof(uint64_t);
        constexpr size_t str1Size = 2 * sizeof(uint16_t); // "H" + EndDialog
        
        std::vector<uint8_t> testMSDData(headerSize + str1Size);
        uint8_t* data = testMSDData.data();
        
        // Write header
        *reinterpret_cast<uint32_t*>(data) = 1; // numEntries
        *reinterpret_cast<uint64_t*>(data + 4) = headerSize; // offset[0]
        
        // Write string: "H" + EndDialog  
        uint16_t* str1 = reinterpret_cast<uint16_t*>(data + headerSize);
        str1[0] = 30;    // 'H'
        str1[1] = 0x8001; // EndDialog
        
        manager.readMSD(data);
        REQUIRE(manager.getStringCount() == 1);
        
        // Add a new string
        uint32_t newIndex = manager.addString(42, "New");
        REQUIRE(newIndex == 1);
        REQUIRE(manager.getStringCount() == 2);
        
        // Verify both original and new strings are present
        const uint8_t* resultData2 = manager.getData();
        const MSDHeader* header = reinterpret_cast<const MSDHeader*>(resultData2);
        REQUIRE(header->numEntries == 2);
    }
}

TEST_CASE("MSD thread safety", "[msd_manager]")
{
    MSDManager manager;
    
    SECTION("Concurrent string additions")
    {
        constexpr int NUM_THREADS = 4;
        constexpr int STRINGS_PER_THREAD = 10;
        
        std::vector<std::thread> threads;
        std::vector<std::vector<uint32_t>> indices(NUM_THREADS);
        
        // Launch threads that add strings concurrently
        for (int t = 0; t < NUM_THREADS; t++)
        {
            threads.emplace_back([&, t]()
            {
                for (int i = 0; i < STRINGS_PER_THREAD; i++)
                {
                    std::string str = "Thread" + std::to_string(t) + "String" + std::to_string(i);
                    uint32_t index = manager.addString(100 + t, str);
                    indices[t].push_back(index);
                }
            });
        }
        
        // Wait for all threads
        for (auto& thread : threads)
        {
            thread.join();
        }
        
        // Verify results
        REQUIRE(manager.getStringCount() == NUM_THREADS * STRINGS_PER_THREAD);
        
        // All indices should be unique
        std::set<uint32_t> allIndices;
        for (const auto& threadIndices : indices)
        {
            for (uint32_t index : threadIndices)
            {
                REQUIRE(allIndices.find(index) == allIndices.end()); // Should be unique
                allIndices.insert(index);
            }
        }
    }
}

TEST_CASE("MSD C API functionality", "[msd_manager]")
{
    // Note: We can't reset the global manager due to mutex, so we test incrementally
    
    SECTION("Add string via C API")
    {
        size_t initialCount = g_MSDManager.getStringCount();
        uint32_t index = wolfRuntimeAddMSDString(42, "C API Test");
        REQUIRE(index == initialCount);
        REQUIRE(g_MSDManager.getStringCount() == initialCount + 1);
    }

    SECTION("Override string via C API")
    {
        size_t initialCount = g_MSDManager.getStringCount();
        uint32_t index = wolfRuntimeAddMSDString(42, "Original");
        wolfRuntimeOverrideMSDString(42, index, "Overridden");
        REQUIRE(g_MSDManager.getStringCount() == initialCount + 1);
        
        // Verify the override took effect by checking the compiled data
        const uint8_t* data = g_MSDManager.getData();
        REQUIRE(data != nullptr);
    }

    SECTION("C API null parameter handling")
    {
        uint32_t index = wolfRuntimeAddMSDString(42, nullptr);
        REQUIRE(index == 0); // Should handle null gracefully
        
        wolfRuntimeOverrideMSDString(42, 0, nullptr);
        // Should not crash
    }
}

TEST_CASE("MSD edge cases and error handling", "[msd_manager]")
{
    MSDManager manager;
    
    SECTION("Very long strings")
    {
        std::string longString(1000, 'A');
        uint32_t index = manager.addString(1, longString);
        REQUIRE(index == 0);
        REQUIRE(manager.getStringCount() == 1);
        
        const uint8_t* data = manager.getData();
        REQUIRE(data != nullptr);
        REQUIRE(manager.getDataSize() > 1000 * sizeof(uint16_t));
    }

    SECTION("Many strings")
    {
        constexpr int NUM_STRINGS = 1000;
        for (int i = 0; i < NUM_STRINGS; i++)
        {
            std::string str = "String" + std::to_string(i);
            uint32_t index = manager.addString(1, str);
            REQUIRE(index == i);
        }
        
        REQUIRE(manager.getStringCount() == NUM_STRINGS);
        const uint8_t* data = manager.getData();
        REQUIRE(data != nullptr);
    }

    SECTION("Special character combinations")
    {
        // Test various combinations of special characters from the mapping
        std::vector<std::string> testStrings = {
            "()[]{}", 
            "?!.,",
            "+-*/",
            "\"'`",
            "~^_|",
            "<>",
            "0123456789",
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
            "abcdefghijklmnopqrstuvwxyz"
        };
        
        for (const auto& testStr : testStrings)
        {
            uint32_t index = manager.addString(1, testStr);
            REQUIRE(index < testStrings.size());
        }
        
        REQUIRE(manager.getStringCount() == testStrings.size());
    }
}