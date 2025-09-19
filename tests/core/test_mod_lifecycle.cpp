#include <catch2/catch_all.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>

#include "wolf_types.h"
#include "mod_lifecycle.h"
#include "test_mocks.h"

// Access to real global mod state (declared in mod_lifecycle.h)
extern std::recursive_mutex g_ModMutex;
extern std::unordered_map<WolfModId, std::unique_ptr<ModInfo>> g_Mods;
extern WolfModId g_NextModId;
extern thread_local WolfModId g_CurrentModId;

// Forward declarations for functions we're testing
extern "C" 
{
    WolfModId wolfRuntimeGetCurrentModId(void);
    WolfModId wolfRuntimeRegisterMod(const WolfModInterface *modInterface);
}

// Forward declare internal functions we want to test
namespace wolf::runtime::internal
{
    bool checkVersionCompatibility(unsigned int modFrameworkVersion, unsigned int modImGuiVersion, const std::string &modName);
    void callPreGameInit();
    void callEarlyGameInit();
    void callLateGameInit();
    void shutdownMods();
}

// Forward declare helper functions
ModInfo *findMod(WolfModId modId);
void logModException(const std::string &context, const std::string &modName, const std::exception &e);
void logModException(const std::string &context, const std::string &modName);

// Test fixture for mod lifecycle tests
class ModLifecycleTestFixture 
{
public:
    ModLifecycleTestFixture() 
    {
        // Clear real global mod state
        {
            std::lock_guard<std::recursive_mutex> lock(g_ModMutex);
            g_Mods.clear();
            g_NextModId = 1;
            g_CurrentModId = 0;
        }
        
        // Reset mock logging
        mock_logging::reset();
    }
    
    ~ModLifecycleTestFixture() 
    {
        // Clean up global mod state
        {
            std::lock_guard<std::recursive_mutex> lock(g_ModMutex);
            g_Mods.clear();
            g_NextModId = 1;
            g_CurrentModId = 0;
        }
    }
    
    // Helper to create a test mod interface
    WolfModInterface createTestModInterface(const std::string& name, const std::string& version = "1.0.0") 
    {
        static std::unordered_map<std::string, std::string> stored_names;
        static std::unordered_map<std::string, std::string> stored_versions;
        
        // Store the strings to ensure they persist
        stored_names[name] = name;
        stored_versions[name] = version;
        
        WolfModInterface interface = {};
        interface.getName = [](){ 
            static auto it = stored_names.begin();
            return it->second.c_str(); 
        };
        interface.getVersion = [](){ 
            static auto it = stored_versions.begin();
            return it->second.c_str(); 
        };
        interface.shutdown = [](){};
        
        return interface;
    }
    
    // Helper to add a test mod directly to global state
    WolfModId addTestMod(const std::string& name, const std::string& version = "1.0.0", const std::string& logPrefix = "") 
    {
        std::lock_guard<std::recursive_mutex> lock(g_ModMutex);
        
        WolfModId id = g_NextModId++;
        auto mod = std::make_unique<ModInfo>();
        mod->id = id;
        mod->name = name;
        mod->version = version;
        mod->logPrefix = logPrefix.empty() ? "[" + name + "]" : logPrefix;
        
        g_Mods[id] = std::move(mod);
        return id;
    }
};

TEST_CASE_METHOD(ModLifecycleTestFixture, "findMod - Basic functionality", "[mod_lifecycle][findMod]")
{
    SECTION("Returns nullptr for non-existent mod")
    {
        REQUIRE(findMod(999) == nullptr);
    }
    
    SECTION("Returns correct mod for existing ID")
    {
        WolfModId id = addTestMod("TestMod", "1.0.0");
        
        ModInfo* mod = findMod(id);
        REQUIRE(mod != nullptr);
        REQUIRE(mod->id == id);
        REQUIRE(mod->name == "TestMod");
        REQUIRE(mod->version == "1.0.0");
    }
    
    SECTION("Returns correct mod among multiple mods")
    {
        WolfModId id1 = addTestMod("TestMod1", "1.0.0");
        WolfModId id2 = addTestMod("TestMod2", "2.0.0");
        WolfModId id3 = addTestMod("TestMod3", "3.0.0");
        
        ModInfo* mod2 = findMod(id2);
        REQUIRE(mod2 != nullptr);
        REQUIRE(mod2->name == "TestMod2");
        REQUIRE(mod2->version == "2.0.0");
    }
}

TEST_CASE_METHOD(ModLifecycleTestFixture, "wolfRuntimeGetCurrentModId", "[mod_lifecycle][current_mod]")
{
    SECTION("Returns 0 initially")
    {
        REQUIRE(wolfRuntimeGetCurrentModId() == 0);
    }
    
    SECTION("Returns correct ID when set")
    {
        g_CurrentModId = 42;
        REQUIRE(wolfRuntimeGetCurrentModId() == 42);
        
        g_CurrentModId = 0;
        REQUIRE(wolfRuntimeGetCurrentModId() == 0);
    }
}

TEST_CASE_METHOD(ModLifecycleTestFixture, "wolfRuntimeRegisterMod - Parameter validation", "[mod_lifecycle][registration]")
{
    SECTION("Rejects null interface")
    {
        REQUIRE(wolfRuntimeRegisterMod(nullptr) == 0);
    }
    
    SECTION("Rejects interface with null getName")
    {
        WolfModInterface interface = {};
        interface.shutdown = [](){};
        // getName is null
        
        REQUIRE(wolfRuntimeRegisterMod(&interface) == 0);
    }
    
    SECTION("Rejects interface with null shutdown")
    {
        WolfModInterface interface = {};
        interface.getName = []() { return "TestMod"; };
        // shutdown is null
        
        REQUIRE(wolfRuntimeRegisterMod(&interface) == 0);
    }
}

TEST_CASE_METHOD(ModLifecycleTestFixture, "wolfRuntimeRegisterMod - Successful registration", "[mod_lifecycle][registration]")
{
    SECTION("Registers mod with minimum required interface")
    {
        WolfModInterface interface = {};
        interface.getName = []() { return "TestMod"; };
        interface.shutdown = [](){};
        
        WolfModId id = wolfRuntimeRegisterMod(&interface);
        
        REQUIRE(id != 0);
        REQUIRE(id == 1); // First mod should get ID 1
        
        // Verify mod was stored correctly
        ModInfo* mod = findMod(id);
        REQUIRE(mod != nullptr);
        REQUIRE(mod->id == id);
        REQUIRE(mod->name == "TestMod");
        REQUIRE(mod->version == "0.0.0"); // Default version
        REQUIRE(mod->logPrefix == "[TestMod]");
        
        // Current mod ID should be set
        REQUIRE(g_CurrentModId == id);
    }
    
    SECTION("Registers mod with version")
    {
        WolfModInterface interface = {};
        interface.getName = []() { return "TestMod"; };
        interface.getVersion = []() { return "2.1.3"; };
        interface.shutdown = [](){};
        
        WolfModId id = wolfRuntimeRegisterMod(&interface);
        
        ModInfo* mod = findMod(id);
        REQUIRE(mod != nullptr);
        REQUIRE(mod->version == "2.1.3");
    }
    
    SECTION("Assigns sequential IDs")
    {
        WolfModInterface interface1 = {};
        interface1.getName = []() { return "TestMod1"; };
        interface1.shutdown = [](){};
        
        WolfModInterface interface2 = {};
        interface2.getName = []() { return "TestMod2"; };
        interface2.shutdown = [](){};
        
        WolfModId id1 = wolfRuntimeRegisterMod(&interface1);
        WolfModId id2 = wolfRuntimeRegisterMod(&interface2);
        
        REQUIRE(id1 == 1);
        REQUIRE(id2 == 2);
        REQUIRE(id2 == id1 + 1);
    }
    
    SECTION("Logs successful registration")
    {
        WolfModInterface interface = {};
        interface.getName = []() { return "TestMod"; };
        interface.getVersion = []() { return "1.2.3"; };
        interface.shutdown = [](){};
        
        wolfRuntimeRegisterMod(&interface);
        
        bool found_registration_log = false;
        for (const auto& log : mock_logging::captured_logs) {
            if (log.find("[INFO]") != std::string::npos && 
                log.find("Registered mod: TestMod v1.2.3") != std::string::npos) {
                found_registration_log = true;
                break;
            }
        }
        REQUIRE(found_registration_log);
    }
}

TEST_CASE_METHOD(ModLifecycleTestFixture, "checkVersionCompatibility - Framework version checks", "[mod_lifecycle][version_compat]")
{
    // Mock WOLF_VERSION_INT for testing (normally comes from wolf_version.h)
    // Let's assume runtime version is 1.2.3 = 0x01020003
    const unsigned int MOCK_RUNTIME_VERSION = 0x01020003; // 1.2.3
    
    SECTION("Compatible versions pass")
    {
        // Same version should be compatible
        REQUIRE(wolf::runtime::internal::checkVersionCompatibility(0x01020003, 0, "TestMod") == true);
        
        // Older mod version should be compatible
        REQUIRE(wolf::runtime::internal::checkVersionCompatibility(0x01020002, 0, "TestMod") == true);
        REQUIRE(wolf::runtime::internal::checkVersionCompatibility(0x01010000, 0, "TestMod") == true);
    }
    
    SECTION("Major version mismatch fails")
    {
        // Different major version should fail
        REQUIRE(wolf::runtime::internal::checkVersionCompatibility(0x02000000, 0, "TestMod") == false);
        REQUIRE(wolf::runtime::internal::checkVersionCompatibility(0x00020003, 0, "TestMod") == false);
        
        // Check error was logged
        bool found_major_error = false;
        for (const auto& log : mock_logging::captured_logs) {
            if (log.find("[ERROR]") != std::string::npos && 
                log.find("Major version mismatch") != std::string::npos) {
                found_major_error = true;
                break;
            }
        }
        REQUIRE(found_major_error);
    }
    
    SECTION("Newer minor version required fails")
    {
        // Mod requires newer minor version than runtime provides
        REQUIRE(wolf::runtime::internal::checkVersionCompatibility(0x01030000, 0, "TestMod") == false);
        
        bool found_version_error = false;
        for (const auto& log : mock_logging::captured_logs) {
            if (log.find("[ERROR]") != std::string::npos && 
                log.find("Runtime version too old") != std::string::npos) {
                found_version_error = true;
                break;
            }
        }
        REQUIRE(found_version_error);
    }
    
    SECTION("Newer patch version required fails")
    {
        // Same major.minor but newer patch required
        REQUIRE(wolf::runtime::internal::checkVersionCompatibility(0x01020004, 0, "TestMod") == false);
    }
}

TEST_CASE_METHOD(ModLifecycleTestFixture, "checkVersionCompatibility - ImGui version checks", "[mod_lifecycle][imgui_compat]")
{
    // Using our mock IMGUI_VERSION_NUM = 19000
    
    SECTION("Skips ImGui check when mod version is 0")
    {
        REQUIRE(wolf::runtime::internal::checkVersionCompatibility(0x01020003, 0, "TestMod") == true);
        
        // Should not log any ImGui errors
        for (const auto& log : mock_logging::captured_logs) {
            REQUIRE(log.find("ImGui version compatibility error") == std::string::npos);
        }
    }
    
    SECTION("Passes when ImGui versions match")
    {
        REQUIRE(wolf::runtime::internal::checkVersionCompatibility(0x01020003, 19000, "TestMod") == true);
    }
    
    SECTION("Fails when ImGui versions mismatch")
    {
        REQUIRE(wolf::runtime::internal::checkVersionCompatibility(0x01020003, 18000, "TestMod") == false);
        
        bool found_imgui_error = false;
        for (const auto& log : mock_logging::captured_logs) {
            if (log.find("[ERROR]") != std::string::npos && 
                log.find("ImGui version compatibility error") != std::string::npos) {
                found_imgui_error = true;
                break;
            }
        }
        REQUIRE(found_imgui_error);
    }
}

TEST_CASE_METHOD(ModLifecycleTestFixture, "logModException functions", "[mod_lifecycle][exception_logging]")
{
    SECTION("Logs standard exception with details")
    {
        std::runtime_error error("Test error message");
        logModException("testFunction", "TestMod", error);
        
        bool found_exception_log = false;
        for (const auto& log : mock_logging::captured_logs) {
            if (log.find("[ERROR]") != std::string::npos && 
                log.find("Standard exception in testFunction for mod 'TestMod': Test error message") != std::string::npos) {
                found_exception_log = true;
                break;
            }
        }
        REQUIRE(found_exception_log);
    }
    
    SECTION("Logs unknown exception")
    {
        logModException("testFunction", "TestMod");
        
        bool found_unknown_log = false;
        for (const auto& log : mock_logging::captured_logs) {
            if (log.find("[ERROR]") != std::string::npos && 
                log.find("Unknown exception in testFunction for mod 'TestMod'") != std::string::npos) {
                found_unknown_log = true;
                break;
            }
        }
        REQUIRE(found_unknown_log);
    }
}

TEST_CASE_METHOD(ModLifecycleTestFixture, "Lifecycle callback functions", "[mod_lifecycle][callbacks]")
{
    // Track callback execution
    static std::vector<std::string> callback_log;
    
    SECTION("callPreGameInit executes earlyGameInit callbacks")
    {
        callback_log.clear();
        
        // Create mod with earlyGameInit callback
        WolfModId id = addTestMod("TestMod", "1.0.0");
        {
            std::lock_guard<std::recursive_mutex> lock(g_ModMutex);
            g_Mods[id]->modInterface.earlyGameInit = []() {
                callback_log.push_back("earlyGameInit called");
            };
        }
        
        wolf::runtime::internal::callPreGameInit();
        
        REQUIRE(callback_log.size() == 1);
        REQUIRE(callback_log[0] == "earlyGameInit called");
        
        // Current mod ID should be cleared after execution
        REQUIRE(g_CurrentModId == 0);
    }
    
    SECTION("callEarlyGameInit is alias for callPreGameInit")
    {
        callback_log.clear();
        
        WolfModId id = addTestMod("TestMod", "1.0.0");
        {
            std::lock_guard<std::recursive_mutex> lock(g_ModMutex);
            g_Mods[id]->modInterface.earlyGameInit = []() {
                callback_log.push_back("earlyGameInit via alias");
            };
        }
        
        wolf::runtime::internal::callEarlyGameInit();
        
        REQUIRE(callback_log.size() == 1);
        REQUIRE(callback_log[0] == "earlyGameInit via alias");
    }
    
    SECTION("callLateGameInit executes lateGameInit callbacks")
    {
        callback_log.clear();
        
        WolfModId id = addTestMod("TestMod", "1.0.0");
        {
            std::lock_guard<std::recursive_mutex> lock(g_ModMutex);
            g_Mods[id]->modInterface.lateGameInit = []() {
                callback_log.push_back("lateGameInit called");
            };
        }
        
        wolf::runtime::internal::callLateGameInit();
        
        REQUIRE(callback_log.size() == 1);
        REQUIRE(callback_log[0] == "lateGameInit called");
        REQUIRE(g_CurrentModId == 0);
    }
    
    SECTION("Multiple mods execute callbacks in registration order")
    {
        callback_log.clear();
        
        WolfModId id1 = addTestMod("Mod1", "1.0.0");
        WolfModId id2 = addTestMod("Mod2", "1.0.0");
        WolfModId id3 = addTestMod("Mod3", "1.0.0");
        
        {
            std::lock_guard<std::recursive_mutex> lock(g_ModMutex);
            g_Mods[id1]->modInterface.earlyGameInit = []() { callback_log.push_back("Mod1 early"); };
            g_Mods[id2]->modInterface.earlyGameInit = []() { callback_log.push_back("Mod2 early"); };
            g_Mods[id3]->modInterface.earlyGameInit = []() { callback_log.push_back("Mod3 early"); };
        }
        
        wolf::runtime::internal::callPreGameInit();
        
        REQUIRE(callback_log.size() == 3);
        REQUIRE(callback_log[0] == "Mod1 early");
        REQUIRE(callback_log[1] == "Mod2 early");
        REQUIRE(callback_log[2] == "Mod3 early");
    }
    
    SECTION("Skips mods without callbacks")
    {
        callback_log.clear();
        
        WolfModId id1 = addTestMod("Mod1", "1.0.0"); // No callbacks
        WolfModId id2 = addTestMod("Mod2", "1.0.0");
        
        {
            std::lock_guard<std::recursive_mutex> lock(g_ModMutex);
            g_Mods[id2]->modInterface.earlyGameInit = []() { callback_log.push_back("Mod2 early"); };
        }
        
        wolf::runtime::internal::callPreGameInit();
        
        REQUIRE(callback_log.size() == 1);
        REQUIRE(callback_log[0] == "Mod2 early");
    }
    
    SECTION("Exception handling in earlyGameInit")
    {
        callback_log.clear();
        
        WolfModId id = addTestMod("TestMod", "1.0.0");
        {
            std::lock_guard<std::recursive_mutex> lock(g_ModMutex);
            g_Mods[id]->modInterface.earlyGameInit = []() {
                throw std::runtime_error("Test exception");
            };
        }
        
        // Should not crash, should log exception
        wolf::runtime::internal::callPreGameInit();
        
        // Check that exception was logged
        bool found_exception_log = false;
        for (const auto& log : mock_logging::captured_logs) {
            if (log.find("[ERROR]") != std::string::npos && 
                log.find("Standard exception in earlyGameInit") != std::string::npos &&
                log.find("Test exception") != std::string::npos) {
                found_exception_log = true;
                break;
            }
        }
        REQUIRE(found_exception_log);
        REQUIRE(g_CurrentModId == 0); // Should be cleared even after exception
    }
    
    SECTION("Exception handling in lateGameInit")
    {
        WolfModId id = addTestMod("TestMod", "1.0.0");
        {
            std::lock_guard<std::recursive_mutex> lock(g_ModMutex);
            g_Mods[id]->modInterface.lateGameInit = []() {
                throw std::runtime_error("Late init exception");
            };
        }
        
        wolf::runtime::internal::callLateGameInit();
        
        // Should log generic exception message for lateGameInit
        bool found_exception_log = false;
        for (const auto& log : mock_logging::captured_logs) {
            if (log.find("[ERROR]") != std::string::npos && 
                log.find("Exception in lateGameInit for mod: TestMod") != std::string::npos) {
                found_exception_log = true;
                break;
            }
        }
        REQUIRE(found_exception_log);
    }
}

TEST_CASE_METHOD(ModLifecycleTestFixture, "shutdownMods function", "[mod_lifecycle][shutdown]")
{
    static std::vector<std::string> shutdown_log;
    
    SECTION("Calls shutdown callbacks in reverse order")
    {
        shutdown_log.clear();
        
        WolfModId id1 = addTestMod("Mod1", "1.0.0");
        WolfModId id2 = addTestMod("Mod2", "1.0.0");
        WolfModId id3 = addTestMod("Mod3", "1.0.0");
        
        {
            std::lock_guard<std::recursive_mutex> lock(g_ModMutex);
            g_Mods[id1]->modInterface.shutdown = []() { shutdown_log.push_back("Mod1 shutdown"); };
            g_Mods[id2]->modInterface.shutdown = []() { shutdown_log.push_back("Mod2 shutdown"); };
            g_Mods[id3]->modInterface.shutdown = []() { shutdown_log.push_back("Mod3 shutdown"); };
        }
        
        wolf::runtime::internal::shutdownMods();
        
        REQUIRE(shutdown_log.size() == 3);
        // Should be in reverse order of registration
        REQUIRE(shutdown_log[0] == "Mod3 shutdown");
        REQUIRE(shutdown_log[1] == "Mod2 shutdown");
        REQUIRE(shutdown_log[2] == "Mod1 shutdown");
        
        // All mods should be cleared
        REQUIRE(g_Mods.empty());
        REQUIRE(g_CurrentModId == 0);
    }
    
    SECTION("Handles shutdown exceptions gracefully")
    {
        shutdown_log.clear();
        
        WolfModId id1 = addTestMod("Mod1", "1.0.0");
        WolfModId id2 = addTestMod("BadMod", "1.0.0");
        WolfModId id3 = addTestMod("Mod3", "1.0.0");
        
        {
            std::lock_guard<std::recursive_mutex> lock(g_ModMutex);
            g_Mods[id1]->modInterface.shutdown = []() { shutdown_log.push_back("Mod1 shutdown"); };
            g_Mods[id2]->modInterface.shutdown = []() { 
                throw std::runtime_error("Shutdown failure");
            };
            g_Mods[id3]->modInterface.shutdown = []() { shutdown_log.push_back("Mod3 shutdown"); };
        }
        
        wolf::runtime::internal::shutdownMods();
        
        // Even with exception, all other mods should still shut down
        REQUIRE(shutdown_log.size() == 2);
        REQUIRE(shutdown_log[0] == "Mod3 shutdown");
        REQUIRE(shutdown_log[1] == "Mod1 shutdown");
        
        // Exception should be logged
        bool found_shutdown_exception = false;
        for (const auto& log : mock_logging::captured_logs) {
            if (log.find("[ERROR]") != std::string::npos && 
                log.find("Standard exception in shutdown for mod 'BadMod': Shutdown failure") != std::string::npos) {
                found_shutdown_exception = true;
                break;
            }
        }
        REQUIRE(found_shutdown_exception);
        
        // All mods should still be cleared
        REQUIRE(g_Mods.empty());
    }
    
    SECTION("Skips mods without shutdown callbacks")
    {
        shutdown_log.clear();
        
        WolfModId id1 = addTestMod("Mod1", "1.0.0"); // No shutdown callback
        WolfModId id2 = addTestMod("Mod2", "1.0.0");
        
        {
            std::lock_guard<std::recursive_mutex> lock(g_ModMutex);
            g_Mods[id2]->modInterface.shutdown = []() { shutdown_log.push_back("Mod2 shutdown"); };
        }
        
        wolf::runtime::internal::shutdownMods();
        
        REQUIRE(shutdown_log.size() == 1);
        REQUIRE(shutdown_log[0] == "Mod2 shutdown");
        REQUIRE(g_Mods.empty());
    }
    
    SECTION("Sets current mod ID during each shutdown")
    {
        static WolfModId captured_mod_id = 0;
        static WolfModId expected_id = 0;
        
        WolfModId id = addTestMod("TestMod", "1.0.0");
        expected_id = id; // Store in static for capture-less lambda
        
        {
            std::lock_guard<std::recursive_mutex> lock(g_ModMutex);
            g_Mods[id]->modInterface.shutdown = []() { 
                captured_mod_id = g_CurrentModId;
            };
        }
        
        wolf::runtime::internal::shutdownMods();
        
        REQUIRE(captured_mod_id == expected_id);
        REQUIRE(g_CurrentModId == 0); // Should be cleared after all shutdowns
    }
}