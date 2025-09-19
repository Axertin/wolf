#include <catch2/catch_all.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>

#include "wolf_types.h"
#include "mod_lifecycle.h"
#include "logging.h"
#include "test_mocks.h"

// Access to real global mod state (declared in mod_lifecycle.h)
extern std::recursive_mutex g_ModMutex;
extern std::unordered_map<WolfModId, std::unique_ptr<ModInfo>> g_Mods;

// Forward declarations for functions we're testing
extern "C" 
{
    void wolfRuntimeLog(WolfModId mod_id, WolfLogLevel level, const char *message);
    void wolfRuntimeSetLogPrefix(WolfModId mod_id, const char *prefix);
}

// Test fixture for logging system tests
class LoggingTestFixture 
{
public:
    LoggingTestFixture() 
    {
        // Clear real global mod state
        {
            std::lock_guard<std::recursive_mutex> lock(g_ModMutex);
            g_Mods.clear();
        }
        
        // Reset mock logging
        mock_logging::reset();
    }
    
    ~LoggingTestFixture() 
    {
        // Clean up global mod state
        {
            std::lock_guard<std::recursive_mutex> lock(g_ModMutex);
            g_Mods.clear();
        }
    }
    
    // Helper to add test mods to real global state
    void addTestMod(WolfModId id, const std::string& logPrefix) 
    {
        std::lock_guard<std::recursive_mutex> lock(g_ModMutex);
        
        auto mod = std::make_unique<ModInfo>();
        mod->id = id;
        mod->logPrefix = logPrefix;
        
        g_Mods[id] = std::move(mod);
    }
    
    // Helper to get mod from real global state
    ModInfo* getMod(WolfModId id) 
    {
        std::lock_guard<std::recursive_mutex> lock(g_ModMutex);
        auto it = g_Mods.find(id);
        return it != g_Mods.end() ? it->second.get() : nullptr;
    }
};

TEST_CASE_METHOD(LoggingTestFixture, "wolfRuntimeLog - Basic functionality", "[logging]")
{
    SECTION("Handles null message gracefully")
    {
        wolfRuntimeLog(1, WOLF_LOG_INFO, nullptr);
        REQUIRE(mock_logging::captured_logs.empty());
    }

    SECTION("Logs message with unknown mod prefix when mod not found")
    {
        wolfRuntimeLog(999, WOLF_LOG_INFO, "Test message");
        
        REQUIRE(mock_logging::captured_logs.size() == 1);
        REQUIRE(mock_logging::captured_logs[0] == "[INFO] [Unknown] Test message");
    }

    SECTION("Logs message with mod prefix when mod exists")
    {
        addTestMod(123, "[TestMod]");
        
        wolfRuntimeLog(123, WOLF_LOG_INFO, "Test message");
        
        REQUIRE(mock_logging::captured_logs.size() == 1);
        REQUIRE(mock_logging::captured_logs[0] == "[INFO] [TestMod] Test message");
    }
}

TEST_CASE_METHOD(LoggingTestFixture, "wolfRuntimeLog - Log level handling", "[logging]")
{
    addTestMod(1, "[TestMod]");

    SECTION("INFO level logs correctly")
    {
        wolfRuntimeLog(1, WOLF_LOG_INFO, "Info message");
        REQUIRE(mock_logging::captured_logs[0] == "[INFO] [TestMod] Info message");
    }

    SECTION("WARNING level logs correctly")
    {
        wolfRuntimeLog(1, WOLF_LOG_WARNING, "Warning message");
        REQUIRE(mock_logging::captured_logs[0] == "[WARNING] [TestMod] Warning message");
    }

    SECTION("ERROR level logs correctly")
    {
        wolfRuntimeLog(1, WOLF_LOG_ERROR, "Error message");
        REQUIRE(mock_logging::captured_logs[0] == "[ERROR] [TestMod] Error message");
    }

    SECTION("DEBUG level logs correctly")
    {
        wolfRuntimeLog(1, WOLF_LOG_DEBUG, "Debug message");
        REQUIRE(mock_logging::captured_logs[0] == "[DEBUG] [TestMod] Debug message");
    }

    SECTION("Invalid log level defaults to INFO")
    {
        wolfRuntimeLog(1, static_cast<WolfLogLevel>(999), "Default message");
        REQUIRE(mock_logging::captured_logs[0] == "[INFO] [TestMod] Default message");
    }
}

TEST_CASE_METHOD(LoggingTestFixture, "wolfRuntimeSetLogPrefix - Basic functionality", "[logging]")
{
    SECTION("Handles null prefix gracefully")
    {
        addTestMod(1, "[OldPrefix]");
        
        wolfRuntimeSetLogPrefix(1, nullptr);
        
        // Prefix should remain unchanged
        ModInfo* mod = getMod(1);
        REQUIRE(mod != nullptr);
        REQUIRE(mod->logPrefix == "[OldPrefix]");
    }

    SECTION("Does nothing when mod not found")
    {
        // This should not crash
        wolfRuntimeSetLogPrefix(999, "[NewPrefix]");
        // No way to verify behavior other than no crash
    }

    SECTION("Updates log prefix for existing mod")
    {
        addTestMod(1, "[OldPrefix]");
        
        wolfRuntimeSetLogPrefix(1, "[NewPrefix]");
        
        ModInfo* mod = getMod(1);
        REQUIRE(mod != nullptr);
        REQUIRE(mod->logPrefix == "[NewPrefix]");
    }

    SECTION("Empty prefix is allowed")
    {
        addTestMod(1, "[OldPrefix]");
        
        wolfRuntimeSetLogPrefix(1, "");
        
        ModInfo* mod = getMod(1);
        REQUIRE(mod != nullptr);
        REQUIRE(mod->logPrefix.empty());
    }
}

TEST_CASE_METHOD(LoggingTestFixture, "wolfRuntimeLog - Message formatting edge cases", "[logging]")
{
    addTestMod(1, "[TestMod]");

    SECTION("Empty message")
    {
        wolfRuntimeLog(1, WOLF_LOG_INFO, "");
        REQUIRE(mock_logging::captured_logs[0] == "[INFO] [TestMod] ");
    }

    SECTION("Very long message")
    {
        std::string long_message(1000, 'A');
        wolfRuntimeLog(1, WOLF_LOG_INFO, long_message.c_str());
        REQUIRE(mock_logging::captured_logs[0] == "[INFO] [TestMod] " + long_message);
    }

    SECTION("Message with special characters")
    {
        wolfRuntimeLog(1, WOLF_LOG_INFO, "Message with \t tabs \n newlines \r returns");
        REQUIRE(mock_logging::captured_logs[0] == "[INFO] [TestMod] Message with \t tabs \n newlines \r returns");
    }
}

TEST_CASE_METHOD(LoggingTestFixture, "wolfRuntimeLog - Integration scenario", "[logging]")
{
    // Set up multiple mods
    addTestMod(1, "[Mod1]");
    addTestMod(2, "[Mod2]");
    
    SECTION("Multiple mods can log independently")
    {
        wolfRuntimeLog(1, WOLF_LOG_INFO, "Message from mod 1");
        wolfRuntimeLog(2, WOLF_LOG_ERROR, "Error from mod 2");
        wolfRuntimeLog(1, WOLF_LOG_WARNING, "Warning from mod 1");
        
        REQUIRE(mock_logging::captured_logs.size() == 3);
        REQUIRE(mock_logging::captured_logs[0] == "[INFO] [Mod1] Message from mod 1");
        REQUIRE(mock_logging::captured_logs[1] == "[ERROR] [Mod2] Error from mod 2");
        REQUIRE(mock_logging::captured_logs[2] == "[WARNING] [Mod1] Warning from mod 1");
    }

    SECTION("Changing prefix affects subsequent logs")
    {
        wolfRuntimeLog(1, WOLF_LOG_INFO, "Before prefix change");
        
        wolfRuntimeSetLogPrefix(1, "[UpdatedMod1]");
        
        wolfRuntimeLog(1, WOLF_LOG_INFO, "After prefix change");
        
        REQUIRE(mock_logging::captured_logs.size() == 2);
        REQUIRE(mock_logging::captured_logs[0] == "[INFO] [Mod1] Before prefix change");
        REQUIRE(mock_logging::captured_logs[1] == "[INFO] [UpdatedMod1] After prefix change");
    }
}