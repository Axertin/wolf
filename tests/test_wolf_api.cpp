#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <catch2/catch_all.hpp>
#include <catch2/catch_test_macros.hpp>

#include "wolf.hpp"

// Simple stub runtime - just enough to test API contracts
class StubRuntime
{
    static inline std::vector<std::string> logs;
    static inline std::map<std::string, uintptr_t> modules;
    static inline WolfModId current_mod_id = 1;
    static inline WolfModId next_mod_id = 2;

  public:
    static void reset()
    {
        logs.clear();
        modules.clear();
        current_mod_id = 1;
        next_mod_id = 2;
    }

    static const std::vector<std::string> &getLogs()
    {
        return logs;
    }

    static void addModule(const char *name, uintptr_t base)
    {
        modules[name] = base;
    }

    // Stub runtime functions
    static WolfModId getCurrentModId()
    {
        return current_mod_id;
    }

    static WolfModId registerMod(const WolfModInterface *interface)
    {
        if (!interface)
            return -1;
        return next_mod_id++;
    }

    static void log(WolfModId mod_id, WolfLogLevel level, const char *message)
    {
        if (!message)
            message = "(null)";
        std::string level_str;
        switch (level)
        {
        case WOLF_LOG_INFO:
            level_str = "[INFO]";
            break;
        case WOLF_LOG_WARNING:
            level_str = "[WARN]";
            break;
        case WOLF_LOG_ERROR:
            level_str = "[ERROR]";
            break;
        case WOLF_LOG_DEBUG:
            level_str = "[DEBUG]";
            break;
        }
        logs.push_back(level_str + " " + std::string(message));
    }

    static void setLogPrefix(WolfModId mod_id, const char *prefix)
    {
        logs.push_back("Set log prefix: " + std::string(prefix ? prefix : ""));
    }

    static uintptr_t getModuleBase(const char *module_name)
    {
        if (!module_name)
            return 0;
        auto it = modules.find(module_name);
        return it != modules.end() ? it->second : 0;
    }

    static int isValidAddress(uintptr_t address)
    {
        return address != 0 ? 1 : 0;
    }

    static int readMemory(uintptr_t address, void *buffer, size_t size)
    {
        // Stub: just return success if address is non-zero
        return address != 0 ? 1 : 0;
    }

    static int writeMemory(uintptr_t address, const void *buffer, size_t size)
    {
        // Stub: just return success if address is non-zero
        return address != 0 ? 1 : 0;
    }

    static void findPattern(const char *pattern, const char *mask, const char *module_name, WolfPatternCallback callback, void *userdata)
    {
        // Stub: don't call callback, just return
    }

    static int watchMemory(WolfModId mod_id, uintptr_t start, size_t size, WolfMemoryWatchCallback callback, void *userdata, const char *description)
    {
        return 1; // Success
    }

    static int unwatchMemory(WolfModId mod_id, uintptr_t start)
    {
        return 1; // Success
    }

    static void registerGameTick(WolfModId mod_id, WolfGameEventCallback callback, void *userdata)
    {
    }
    static void registerGameStart(WolfModId mod_id, WolfGameEventCallback callback, void *userdata)
    {
    }
    static void registerGameStop(WolfModId mod_id, WolfGameEventCallback callback, void *userdata)
    {
    }
    static void registerPlayStart(WolfModId mod_id, WolfGameEventCallback callback, void *userdata)
    {
    }
    static void registerReturnToMenu(WolfModId mod_id, WolfGameEventCallback callback, void *userdata)
    {
    }
    static void registerItemPickup(WolfModId mod_id, WolfItemPickupCallback callback, void *userdata)
    {
    }

    static int hookFunction(uintptr_t address, void *detour, void **original)
    {
        return detour != nullptr ? 1 : 0;
    }

    static void addCommand(WolfModId mod_id, const char *name, WolfConsoleCommandCallback callback, void *userdata, const char *description)
    {
        logs.push_back("Registering command: " + std::string(name ? name : ""));
    }

    static void removeCommand(WolfModId mod_id, const char *name)
    {
    }
    static void executeCommand(const char *command_line)
    {
    }

    static void consolePrint(const char *message)
    {
        logs.push_back("CONSOLE: " + std::string(message ? message : ""));
    }

    static int isConsoleVisible()
    {
        return 0;
    }

    static void interceptResource(WolfModId mod_id, const char *filename, WolfResourceProvider provider, void *userdata)
    {
    }
    static void removeResourceInterception(WolfModId mod_id, const char *filename)
    {
    }
    static void interceptResourcePattern(WolfModId mod_id, const char *pattern, WolfResourceProvider provider, void *userdata)
    {
    }

    // Get stub runtime API
    static WolfRuntimeAPI *getAPI()
    {
        static WolfRuntimeAPI api = {.getCurrentModId = getCurrentModId,
                                     .registerMod = registerMod,
                                     .log = log,
                                     .setLogPrefix = setLogPrefix,
                                     .getModuleBase = getModuleBase,
                                     .isValidAddress = isValidAddress,
                                     .readMemory = readMemory,
                                     .writeMemory = writeMemory,
                                     .findPattern = findPattern,
                                     .watchMemory = watchMemory,
                                     .unwatchMemory = unwatchMemory,
                                     .registerGameTick = registerGameTick,
                                     .registerGameStart = registerGameStart,
                                     .registerGameStop = registerGameStop,
                                     .registerPlayStart = registerPlayStart,
                                     .registerReturnToMenu = registerReturnToMenu,
                                     .registerItemPickup = registerItemPickup,
                                     .hookFunction = hookFunction,
                                     .addCommand = addCommand,
                                     .removeCommand = removeCommand,
                                     .executeCommand = executeCommand,
                                     .consolePrint = consolePrint,
                                     .isConsoleVisible = isConsoleVisible,
                                     .interceptResource = interceptResource,
                                     .removeResourceInterception = removeResourceInterception,
                                     .interceptResourcePattern = interceptResourcePattern};
        return &api;
    }
};

class WolfAPITestFixture
{
  public:
    WolfAPITestFixture()
    {
        StubRuntime::reset();
        wolf::detail::g_runtime = StubRuntime::getAPI();

        // Set up test modules
        StubRuntime::addModule("test.dll", 0x10000000);
        StubRuntime::addModule("game.exe", 0x00400000);
    }

    ~WolfAPITestFixture()
    {
        wolf::detail::g_runtime = nullptr;
        StubRuntime::reset();
    }
};

TEST_CASE_METHOD(WolfAPITestFixture, "Wolf API logging", "[wolf_api][logging]")
{
    SECTION("Basic logging functions work")
    {
        wolf::logInfo("Info message");
        wolf::logWarning("Warning message");
        wolf::logError("Error message");
        wolf::logDebug("Debug message");

        const auto &logs = StubRuntime::getLogs();
        REQUIRE(logs.size() == 4);
        REQUIRE(logs[0] == "[INFO] Info message");
        REQUIRE(logs[1] == "[WARN] Warning message");
        REQUIRE(logs[2] == "[ERROR] Error message");
        REQUIRE(logs[3] == "[DEBUG] Debug message");
    }

    SECTION("Printf-style formatting works")
    {
        wolf::logInfo("Value: %d, String: %s", 42, "test");

        const auto &logs = StubRuntime::getLogs();
        REQUIRE(logs.size() == 1);
        REQUIRE(logs[0] == "[INFO] Value: 42, String: test");
    }

    SECTION("Log prefix works")
    {
        wolf::setLogPrefix("[TestMod]");

        const auto &logs = StubRuntime::getLogs();
        REQUIRE(logs.size() == 1);
        REQUIRE(logs[0] == "Set log prefix: [TestMod]");
    }
}

TEST_CASE_METHOD(WolfAPITestFixture, "Wolf API memory functions", "[wolf_api][memory]")
{
    SECTION("Module base lookup works")
    {
        REQUIRE(wolf::getModuleBase("test.dll") == 0x10000000);
        REQUIRE(wolf::getModuleBase("game.exe") == 0x00400000);
        REQUIRE(wolf::getModuleBase("nonexistent.dll") == 0);
    }

    SECTION("Address validation works")
    {
        REQUIRE(wolf::isValidAddress(0x12345) == true);
        REQUIRE(wolf::isValidAddress(0) == false);
    }

    SECTION("Memory read/write return appropriate values")
    {
        uint32_t buffer = 0;
        REQUIRE(wolf::readMemory(0x12345, &buffer, sizeof(buffer)) == true);
        REQUIRE(wolf::readMemory(0, &buffer, sizeof(buffer)) == false);

        REQUIRE(wolf::writeMemory(0x12345, &buffer, sizeof(buffer)) == true);
        REQUIRE(wolf::writeMemory(0, &buffer, sizeof(buffer)) == false);
    }

    SECTION("Pattern finding doesn't crash")
    {
        auto results = wolf::findPattern("\\x12\\x34", "xx", "test.dll");
        REQUIRE(results.size() == 0); // Stub returns no results
    }
}

TEST_CASE_METHOD(WolfAPITestFixture, "Wolf API MemoryAccessor type safety", "[wolf_api][memory_accessor]")
{
    SECTION("MemoryAccessor constructs correctly")
    {
        wolf::MemoryAccessor<int> int_accessor;
        REQUIRE(int_accessor.raw() == 0);

        wolf::MemoryAccessor<float> float_accessor(0x12345);
        REQUIRE(float_accessor.raw() == 0x12345);

        wolf::MemoryAccessor<uint64_t> uint64_accessor("test.dll", 0x100);
        REQUIRE(uint64_accessor.raw() == 0x10000100);
    }

    SECTION("MemoryAccessor binding works")
    {
        wolf::MemoryAccessor<int> accessor;

        accessor.bind(0x54321);
        REQUIRE(accessor.raw() == 0x54321);

        accessor.bind("test.dll", 0x200);
        REQUIRE(accessor.raw() == 0x10000200);
    }

    SECTION("MemoryAccessor works with different types")
    {
        // Test compilation with various types
        wolf::MemoryAccessor<uint8_t> byte_accessor;
        wolf::MemoryAccessor<uint16_t> word_accessor;
        wolf::MemoryAccessor<uint32_t> dword_accessor;
        wolf::MemoryAccessor<uint64_t> qword_accessor;

        // Test with struct
        struct TestStruct
        {
            int x;
            float y;
        };
        wolf::MemoryAccessor<TestStruct> struct_accessor;

        REQUIRE(byte_accessor.raw() == 0);
        REQUIRE(struct_accessor.raw() == 0);
    }

    SECTION("Helper functions work")
    {
        auto accessor1 = wolf::getMemoryAccessor<int>(0x12345);
        REQUIRE(accessor1.raw() == 0x12345);

        auto accessor2 = wolf::getMemoryAccessor<float>("test.dll", 0x300);
        REQUIRE(accessor2.raw() == 0x10000300);
    }
}

TEST_CASE_METHOD(WolfAPITestFixture, "Wolf API console functions", "[wolf_api][console]")
{
    SECTION("Console print functions work")
    {
        wolf::consolePrint("Test message");
        wolf::consolePrintf("Formatted: %d", 42);

        const auto &logs = StubRuntime::getLogs();
        REQUIRE(logs.size() == 2);
        REQUIRE(logs[0] == "CONSOLE: Test message");
        REQUIRE(logs[1] == "CONSOLE: Formatted: 42");
    }

    SECTION("Console visibility check works")
    {
        REQUIRE(wolf::isConsoleVisible() == false); // Stub returns false
    }

    SECTION("Command registration works")
    {
        wolf::addCommand(
            "testcmd",
            [](const std::vector<std::string> &args)
            {
                // Test command
            },
            "Test command");

        const auto &logs = StubRuntime::getLogs();
        REQUIRE(logs.size() >= 1);
        REQUIRE(logs[0] == "[INFO] Registering command: testcmd");
    }
}

TEST_CASE_METHOD(WolfAPITestFixture, "Wolf API version info", "[wolf_api][version]")
{
    SECTION("Version functions return valid strings")
    {
        const char *version = wolf::getVersion();
        REQUIRE(version != nullptr);
        REQUIRE(std::strlen(version) > 0);

        const char *build_info = wolf::getBuildInfo();
        REQUIRE(build_info != nullptr);
        REQUIRE(std::strlen(build_info) > 0);
    }
}

TEST_CASE_METHOD(WolfAPITestFixture, "Wolf API hook functions", "[wolf_api][hooks]")
{
    SECTION("Hook functions return appropriate values")
    {
        // Use proper function pointer instead of lambda
        auto dummy_func = +[]() -> int { return 42; }; // + converts lambda to function pointer

        // Should succeed with valid detour
        REQUIRE(wolf::hookFunction(0x12345, dummy_func) == true);

        // Should fail with null detour
        REQUIRE(wolf::hookFunction(0x12345, (int (*)()) nullptr) == false);
    }

    SECTION("Replace functions work")
    {
        // Use proper function pointer
        auto dummy_func = +[]() -> int { return 42; };

        REQUIRE(wolf::replaceFunction(0x12345, dummy_func) == true);
        REQUIRE(wolf::replaceFunction("test.dll", 0x100, dummy_func) == true);
    }
}
