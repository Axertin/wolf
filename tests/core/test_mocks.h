#pragma once

#include <string>
#include <vector>
#include <functional>

// Mock Console class
class Console 
{
public:
    bool IsVisible = false;
    std::vector<std::string> added_commands;
    std::vector<std::string> removed_commands;
    std::vector<std::string> executed_commands;
    std::vector<std::string> printed_messages;
    
    void addCommand(const std::string& name, std::function<void(const std::vector<std::string>&)> callback, const std::string& description);
    void removeCommand(const std::string& name);
    void executeCommand(const std::string& command);
    void printToConsole(const std::string& message);
    void reset();
};

// Mock Logger class
enum class LogLevel;
class Logger 
{
public:
    void log(const std::string& message, LogLevel level);
};

// Shared mock logging functionality for core tests
namespace mock_logging 
{
    extern std::vector<std::string> captured_logs;
    
    void reset();
    void logInfo(const std::string& message);
    void logWarning(const std::string& message);
    void logError(const std::string& message);
    void logDebug(const std::string& message);
}

// Global logging function overrides (mock external dependencies)
void logInfo(const std::string& message);
void logWarning(const std::string& message);
void logError(const std::string& message);
void logDebug(const std::string& message);

// Global mock instances (to replace real globals)
extern Console* g_Console;
extern Logger* g_Logger;