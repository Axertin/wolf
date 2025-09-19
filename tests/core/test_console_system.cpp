#include <catch2/catch_all.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>

#include "wolf_types.h"
#include "console_system.h"
#include "test_mocks.h"

// Access to global state (declared in console_system.h)
extern std::mutex g_CommandMutex;
extern std::unordered_map<std::string, std::unique_ptr<WolfCommandInfo>> g_Commands;
extern std::vector<std::string> g_PendingCommands;

// Forward declare the helper function we want to test
void processPendingCommands();

// Access to global console (declared in test_mocks.h)
extern Console* g_Console;

// Test fixture for console system tests
class ConsoleSystemTestFixture 
{
    Console* original_console;
    
public:
    ConsoleSystemTestFixture() 
    {
        // Clear global state
        {
            std::lock_guard<std::mutex> lock(g_CommandMutex);
            g_Commands.clear();
            g_PendingCommands.clear();
        }
        
        // Reset mocks
        mock_logging::reset();
        
        // Store original console and start with console available
        original_console = g_Console;
        if (g_Console) {
            g_Console->reset();
        }
    }
    
    ~ConsoleSystemTestFixture() 
    {
        // Clean up
        {
            std::lock_guard<std::mutex> lock(g_CommandMutex);
            g_Commands.clear();
            g_PendingCommands.clear();
        }
        
        // Restore original console
        g_Console = original_console;
        if (g_Console) {
            g_Console->reset();
        }
    }
    
    void setConsoleAvailable(bool available) 
    {
        if (available) {
            g_Console = original_console;
            // Ensure we have a valid console and reset it
            if (g_Console) {
                g_Console->reset();
            }
        } else {
            g_Console = nullptr;
        }
    }
    
    Console* getMockConsole() { return g_Console; }
};

TEST_CASE_METHOD(ConsoleSystemTestFixture, "wolfRuntimeAddCommand - Parameter validation", "[console][validation]")
{
    static bool callback_executed = false;
    auto test_callback = [](int argc, const char** argv, void* userdata) { callback_executed = true; };
    
    SECTION("Rejects null name")
    {
        wolfRuntimeAddCommand(1, nullptr, test_callback, nullptr, "desc");
        
        std::lock_guard<std::mutex> lock(g_CommandMutex);
        REQUIRE(g_Commands.empty());
        
        // Should log error
        bool found_error = false;
        for (const auto& log : mock_logging::captured_logs) {
            if (log.find("ERROR") != std::string::npos && log.find("invalid parameters") != std::string::npos) {
                found_error = true;
                break;
            }
        }
        REQUIRE(found_error);
    }
    
    SECTION("Rejects null callback")
    {
        wolfRuntimeAddCommand(1, "test", nullptr, nullptr, "desc");
        
        std::lock_guard<std::mutex> lock(g_CommandMutex);
        REQUIRE(g_Commands.empty());
    }
    
    SECTION("Accepts null description")
    {
        wolfRuntimeAddCommand(1, "test", test_callback, nullptr, nullptr);
        
        std::lock_guard<std::mutex> lock(g_CommandMutex);
        REQUIRE(g_Commands.size() == 1);
        REQUIRE(g_Commands["test"]->description.empty());
    }
}

TEST_CASE_METHOD(ConsoleSystemTestFixture, "wolfRuntimeAddCommand - Command storage", "[console][storage]")
{
    static bool callback_executed = false;
    auto test_callback = [](int argc, const char** argv, void* userdata) { callback_executed = true; };
    
    SECTION("Stores command info correctly")
    {
        void* test_userdata = (void*)0x12345;
        wolfRuntimeAddCommand(42, "testcmd", test_callback, test_userdata, "Test command");
        
        std::lock_guard<std::mutex> lock(g_CommandMutex);
        REQUIRE(g_Commands.size() == 1);
        
        auto& cmd = g_Commands["testcmd"];
        REQUIRE(cmd->modId == 42);
        REQUIRE(cmd->callback == test_callback);
        REQUIRE(cmd->userdata == test_userdata);
        REQUIRE(cmd->description == "Test command");
    }
    
    SECTION("Overwrites existing command with same name")
    {
        auto callback1 = [](int argc, const char** argv, void* userdata) {};
        auto callback2 = [](int argc, const char** argv, void* userdata) {};
        
        wolfRuntimeAddCommand(1, "testcmd", callback1, nullptr, "First");
        wolfRuntimeAddCommand(2, "testcmd", callback2, nullptr, "Second");
        
        std::lock_guard<std::mutex> lock(g_CommandMutex);
        REQUIRE(g_Commands.size() == 1);
        REQUIRE(g_Commands["testcmd"]->modId == 2);
        REQUIRE(g_Commands["testcmd"]->description == "Second");
    }
}

TEST_CASE_METHOD(ConsoleSystemTestFixture, "wolfRuntimeAddCommand - Console interaction", "[console][integration]")
{
    auto test_callback = [](int argc, const char** argv, void* userdata) {};
    
    SECTION("Registers immediately when console available")
    {
        setConsoleAvailable(true);
        REQUIRE(g_Console != nullptr); // Ensure console is available
        
        wolfRuntimeAddCommand(1, "testcmd", test_callback, nullptr, "Test");
        
        REQUIRE(getMockConsole() != nullptr);
        REQUIRE(getMockConsole()->added_commands.size() == 1);
        REQUIRE(getMockConsole()->added_commands[0] == "testcmd:Test");
        
        std::lock_guard<std::mutex> lock(g_CommandMutex);
        REQUIRE(g_PendingCommands.empty());
    }
    
    SECTION("Defers registration when console not available")
    {
        setConsoleAvailable(false);
        
        wolfRuntimeAddCommand(1, "testcmd", test_callback, nullptr, "Test");
        
        // Console should be null, so no commands should be registered
        REQUIRE(g_Console == nullptr);
        
        std::lock_guard<std::mutex> lock(g_CommandMutex);
        REQUIRE(g_PendingCommands.size() == 1);
        REQUIRE(g_PendingCommands[0] == "testcmd");
    }
}

TEST_CASE_METHOD(ConsoleSystemTestFixture, "processPendingCommands - Core logic", "[console][pending]")
{
    auto test_callback = [](int argc, const char** argv, void* userdata) {};
    
    SECTION("Does nothing when console not available")
    {
        // Add pending command
        {
            std::lock_guard<std::mutex> lock(g_CommandMutex);
            g_PendingCommands.push_back("testcmd");
            
            auto cmd = std::make_unique<WolfCommandInfo>();
            cmd->modId = 1;
            cmd->callback = test_callback;
            cmd->description = "Test";
            g_Commands["testcmd"] = std::move(cmd);
        }
        
        setConsoleAvailable(false);
        processPendingCommands();
        
        REQUIRE(g_Console == nullptr);
        
        std::lock_guard<std::mutex> lock(g_CommandMutex);
        REQUIRE(g_PendingCommands.size() == 1); // Should remain pending
    }
    
    SECTION("Processes all pending commands when console available")
    {
        // Set up multiple pending commands
        {
            std::lock_guard<std::mutex> lock(g_CommandMutex);
            g_PendingCommands = {"cmd1", "cmd2", "cmd3"};
            
            for (int i = 1; i <= 3; i++) {
                auto cmd = std::make_unique<WolfCommandInfo>();
                cmd->modId = i;
                cmd->callback = test_callback;
                cmd->description = "Command " + std::to_string(i);
                g_Commands["cmd" + std::to_string(i)] = std::move(cmd);
            }
        }
        
        setConsoleAvailable(true);
        processPendingCommands();
        
        REQUIRE(getMockConsole()->added_commands.size() == 3);
        REQUIRE(getMockConsole()->added_commands[0] == "cmd1:Command 1");
        REQUIRE(getMockConsole()->added_commands[1] == "cmd2:Command 2");
        REQUIRE(getMockConsole()->added_commands[2] == "cmd3:Command 3");
        
        std::lock_guard<std::mutex> lock(g_CommandMutex);
        REQUIRE(g_PendingCommands.empty());
    }
    
    SECTION("Handles missing commands gracefully")
    {
        {
            std::lock_guard<std::mutex> lock(g_CommandMutex);
            g_PendingCommands = {"nonexistent", "cmd1"};
            
            auto cmd = std::make_unique<WolfCommandInfo>();
            cmd->modId = 1;
            cmd->callback = test_callback;
            cmd->description = "Real command";
            g_Commands["cmd1"] = std::move(cmd);
        }
        
        setConsoleAvailable(true);
        processPendingCommands();
        
        // Should only register the existing command
        REQUIRE(getMockConsole()->added_commands.size() == 1);
        REQUIRE(getMockConsole()->added_commands[0] == "cmd1:Real command");
        
        std::lock_guard<std::mutex> lock(g_CommandMutex);
        REQUIRE(g_PendingCommands.empty());
    }
}

TEST_CASE_METHOD(ConsoleSystemTestFixture, "wolfRuntimeRemoveCommand - Basic functionality", "[console][removal]")
{
    auto test_callback = [](int argc, const char** argv, void* userdata) {};
    
    SECTION("Handles null name gracefully")
    {
        wolfRuntimeRemoveCommand(1, nullptr);
        // Should not crash
    }
    
    SECTION("Removes command for correct mod")
    {
        setConsoleAvailable(true);
        
        // Add command
        wolfRuntimeAddCommand(1, "testcmd", test_callback, nullptr, "Test");
        {
            std::lock_guard<std::mutex> lock(g_CommandMutex);
            REQUIRE(g_Commands.size() == 1);
        }
        
        // Remove it
        wolfRuntimeRemoveCommand(1, "testcmd");
        
        {
            std::lock_guard<std::mutex> lock(g_CommandMutex);
            REQUIRE(g_Commands.empty());
        }
        REQUIRE(getMockConsole()->removed_commands.size() == 1);
        REQUIRE(getMockConsole()->removed_commands[0] == "testcmd");
    }
    
    SECTION("Does not remove command from different mod")
    {
        // Add command for mod 1
        {
            std::lock_guard<std::mutex> lock(g_CommandMutex);
            auto cmd = std::make_unique<WolfCommandInfo>();
            cmd->modId = 1;
            cmd->callback = test_callback;
            g_Commands["testcmd"] = std::move(cmd);
        }
        
        // Try to remove with mod 2
        wolfRuntimeRemoveCommand(2, "testcmd");
        
        {
            std::lock_guard<std::mutex> lock(g_CommandMutex);
            REQUIRE(g_Commands.size() == 1);
        }
    }
}