/**
 * @file wolf_console.hpp
 * @brief WOLF Framework Console System
 */

#pragma once

#include <cstdarg>

#include "wolf_core.hpp"
#include "wolf_logging.hpp"

//==============================================================================
// CONSOLE SYSTEM
//==============================================================================

namespace wolf
{

/**
 * @brief Function signature for console command handlers
 * @param args Vector of command arguments (including command name at [0])
 */
using CommandHandler = std::function<void(const std::vector<std::string> &args)>;

namespace detail
{
/**
 * @brief Simple storage for console command callbacks
 */
inline std::vector<std::unique_ptr<CommandHandler>> command_callbacks;

/**
 * @brief Add a command handler
 * @param handler Handler to store
 * @return Pointer to stored handler for runtime registration
 */
inline CommandHandler *addCommandHandler(CommandHandler &&handler)
{
    auto stored_handler = std::make_unique<CommandHandler>(std::move(handler));
    CommandHandler *handler_ptr = stored_handler.get();
    command_callbacks.push_back(std::move(stored_handler));
    return handler_ptr;
}

/**
 * @brief Clear all command callbacks during shutdown
 */
inline void clearCommandCallbacks()
{
    command_callbacks.clear();
}

// C-style callback wrapper for commands
inline void __cdecl commandCallbackWrapper(int argc, const char **argv, void *userdata) noexcept
{
    auto *cb = static_cast<CommandHandler *>(userdata);
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i)
    {
        args.emplace_back(argv[i]);
    }
    (*cb)(args);
}
} // namespace detail

/**
 * @brief Register a console command
 * @param name Command name (case-sensitive)
 * @param handler Function to call when command is executed
 * @param description Help text for the command (optional)
 * @return True if command was successfully registered
 */
inline bool addCommand(const char *name, CommandHandler handler, const char *description = "") noexcept
{
    CommandHandler *callback_ptr = detail::addCommandHandler(std::move(handler));

    // Register cleanup handler to ensure callback storage is cleaned up
    static bool cleanup_registered = false;
    if (!cleanup_registered)
    {
        registerCleanupHandler([]() { detail::clearCommandCallbacks(); });
        cleanup_registered = true;
    }

    if (!detail::g_runtime)
        return false;

    detail::g_runtime->addCommand(detail::getCurrentModId(), name, detail::commandCallbackWrapper, callback_ptr, description);
    return true;
}

/**
 * @brief Unregister a console command
 * @param name Command name to remove
 * @return True if command was successfully removed
 */
inline bool removeCommand(const char *name) noexcept
{
    if (!detail::g_runtime)
        return false;
    detail::g_runtime->removeCommand(detail::getCurrentModId(), name);
    return true;
}

/**
 * @brief Execute a command line string
 * @param commandLine Full command with arguments
 * @return True if command was successfully executed
 */
inline bool executeCommand(const std::string &commandLine) noexcept
{
    if (!detail::g_runtime)
        return false;
    detail::g_runtime->executeCommand(commandLine.c_str());
    return true;
}

/**
 * @brief Print a message to the console
 * @param message Message to display
 * @return True if message was successfully printed
 */
inline bool consolePrint(const char *message) noexcept
{
    if (!detail::g_runtime)
        return false;
    detail::g_runtime->consolePrint(message);
    return true;
}

/**
 * @brief Print a formatted message to the console
 * @param format Printf-style format string
 * @param ... Format arguments
 * @return True if message was successfully printed
 */
inline bool consolePrintf(const char *format, ...) noexcept
{
    va_list args;
    va_start(args, format);

    // Format string for console output
    va_list args_copy;
    va_copy(args_copy, args);
    int buffer_size = vsnprintf(nullptr, 0, format, args_copy);
    va_end(args_copy);

    if (buffer_size <= 0)
    {
        va_end(args);
        return false;
    }

    std::string message(buffer_size, '\0');
    vsnprintf(&message[0], buffer_size + 1, format, args);
    va_end(args);

    if (!detail::g_runtime)
        return false;
    detail::g_runtime->consolePrint(message.c_str());
    return true;
}

/**
 * @brief Check if console window is visible
 * @return True if console is visible
 */
inline bool isConsoleVisible() noexcept
{
    if (detail::g_runtime)
    {
        return detail::g_runtime->isConsoleVisible() != 0;
    }
    return false;
}

} // namespace wolf
