#include "console_system.h"

#include <vector>

#include "../utilities/console.h"
#include "../utilities/logger.h"

// Global console command storage
std::mutex g_CommandMutex;
std::unordered_map<std::string, std::unique_ptr<WolfCommandInfo>> g_Commands;
std::vector<std::string> g_PendingCommands; // Commands waiting for console to be ready

// Helper function to process pending commands
void processPendingCommands()
{
    std::lock_guard<std::mutex> lock(g_CommandMutex);

    if (!g_Console || g_PendingCommands.empty())
        return;

    ::logInfo("[WOLF] Processing " + std::to_string(g_PendingCommands.size()) + " pending commands");

    for (const auto &cmdName : g_PendingCommands)
    {
        auto cmdIt = g_Commands.find(cmdName);
        if (cmdIt != g_Commands.end())
        {
            ::logInfo("[WOLF] Registering deferred command: " + cmdName);
            std::string cmdDescription = cmdIt->second->description;
            g_Console->addCommand(
                cmdName,
                [cmdName](const std::vector<std::string> &args)
                {
                    std::lock_guard<std::mutex> innerLock(g_CommandMutex);
                    auto cmdIt = g_Commands.find(cmdName);
                    if (cmdIt != g_Commands.end())
                    {
                        // Convert std::vector<std::string> to C-style arrays
                        std::vector<const char *> argv;
                        for (const auto &arg : args)
                        {
                            argv.push_back(arg.c_str());
                        }

                        cmdIt->second->callback(static_cast<int>(argv.size()), argv.data(), cmdIt->second->userdata);
                    }
                },
                cmdDescription);
        }
    }

    g_PendingCommands.clear();
    ::logInfo("[WOLF] All pending commands processed");
}

// C API implementations
extern "C"
{
    void wolfRuntimeAddCommand(WolfModId mod_id, const char *name, WolfConsoleCommandCallback callback, void *userdata, const char *description)
    {
        if (!name || !callback)
        {
            ::logError("[WOLF] AddCommand failed: invalid parameters");
            return;
        }

        ::logDebug("[WOLF] Runtime adding command: " + std::string(name));
        std::lock_guard<std::mutex> lock(g_CommandMutex);

        std::string cmdName(name);
        auto cmdInfo = std::make_unique<WolfCommandInfo>();
        cmdInfo->modId = mod_id;
        cmdInfo->callback = callback;
        cmdInfo->userdata = userdata;
        cmdInfo->description = description ? description : "";

        std::string cmdDescription = cmdInfo->description; // Copy before move
        g_Commands[cmdName] = std::move(cmdInfo);

        // Register with actual console system
        if (g_Console)
        {
            ::logInfo("[WOLF] Registering with console system: " + cmdName);
            g_Console->addCommand(
                cmdName,
                [cmdName](const std::vector<std::string> &args)
                {
                    std::lock_guard<std::mutex> innerLock(g_CommandMutex);
                    auto cmdIt = g_Commands.find(cmdName);
                    if (cmdIt != g_Commands.end())
                    {
                        // Convert std::vector<std::string> to C-style arrays
                        std::vector<const char *> argv;
                        for (const auto &arg : args)
                        {
                            argv.push_back(arg.c_str());
                        }

                        cmdIt->second->callback(static_cast<int>(argv.size()), argv.data(), cmdIt->second->userdata);
                    }
                },
                cmdDescription);
            ::logDebug("[WOLF] Console registration completed: " + cmdName);
        }
        else
        {
            ::logWarning("[WOLF] Console not available, deferring command registration: " + cmdName);
            g_PendingCommands.push_back(cmdName);
        }
    }

    void wolfRuntimeRemoveCommand(WolfModId mod_id, const char *name)
    {
        if (!name)
            return;

        std::lock_guard<std::mutex> lock(g_CommandMutex);

        std::string cmdName(name);
        auto it = g_Commands.find(cmdName);
        if (it != g_Commands.end() && it->second->modId == mod_id)
        {
            g_Commands.erase(it);

            // Remove from actual console system
            if (g_Console)
            {
                g_Console->removeCommand(cmdName);
            }
        }
    }

    void wolfRuntimeExecuteCommand(const char *command_line)
    {
        if (!command_line)
            return;

        if (g_Console)
        {
            g_Console->executeCommand(std::string(command_line));
        }
    }

    void wolfRuntimeConsolePrint(const char *message)
    {
        if (!message)
            return;

        if (g_Console)
        {
            g_Console->printToConsole(std::string(message));
        }
        else
        {
            ::logInfo(std::string(message));
        }
    }

    int wolfRuntimeIsConsoleVisible(void)
    {
        return g_Console ? (g_Console->IsVisible ? 1 : 0) : 0;
    }

} // extern "C"