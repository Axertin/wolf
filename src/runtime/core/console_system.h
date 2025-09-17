#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "wolf_types.h"

// Console command structure
struct WolfCommandInfo
{
    WolfModId modId;
    WolfConsoleCommandCallback callback;
    void *userdata;
    std::string description;
};

// Global console command storage (defined in console_system.cpp)
extern std::mutex g_CommandMutex;
extern std::unordered_map<std::string, std::unique_ptr<WolfCommandInfo>> g_Commands;
extern std::vector<std::string> g_PendingCommands;

// Helper functions
void processPendingCommands();

// C API functions for console system
extern "C"
{
    void __cdecl wolfRuntimeAddCommand(WolfModId mod_id, const char *name, WolfConsoleCommandCallback callback, void *userdata, const char *description);
    void __cdecl wolfRuntimeRemoveCommand(WolfModId mod_id, const char *name);
    void __cdecl wolfRuntimeExecuteCommand(const char *command_line);
    void __cdecl wolfRuntimeConsolePrint(const char *message);
    int __cdecl wolfRuntimeIsConsoleVisible(void);
}