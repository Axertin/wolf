#include "test_mocks.h"

// Forward declaration
enum class LogLevel
{
    Info = 0,
    Warning = 1,
    Error = 2,
    Debug = 3
};

// Mock Console implementation
void Console::addCommand(const std::string& name, std::function<void(const std::vector<std::string>&)> callback, const std::string& description)
{
    added_commands.push_back(name + ":" + description);
}

void Console::removeCommand(const std::string& name)
{
    removed_commands.push_back(name);
}

void Console::executeCommand(const std::string& command)
{
    executed_commands.push_back(command);
}

void Console::printToConsole(const std::string& message)
{
    printed_messages.push_back(message);
}

void Console::reset()
{
    added_commands.clear();
    removed_commands.clear();
    executed_commands.clear();
    printed_messages.clear();
    IsVisible = false;
}

// Mock Logger implementation
void Logger::log(const std::string& message, LogLevel level)
{
    // Route to our mock logging system
    switch (level)
    {
    case LogLevel::Info:
        mock_logging::logInfo(message);
        break;
    case LogLevel::Warning:
        mock_logging::logWarning(message);
        break;
    case LogLevel::Error:
        mock_logging::logError(message);
        break;
    case LogLevel::Debug:
        mock_logging::logDebug(message);
        break;
    }
}

// Mock logging capture (mock what the logging system calls)
namespace mock_logging 
{
    std::vector<std::string> captured_logs;
    
    void reset() 
    {
        captured_logs.clear();
    }
    
    void logInfo(const std::string& message) 
    {
        captured_logs.push_back("[INFO] " + message);
    }
    
    void logWarning(const std::string& message) 
    {
        captured_logs.push_back("[WARNING] " + message);
    }
    
    void logError(const std::string& message) 
    {
        captured_logs.push_back("[ERROR] " + message);
    }
    
    void logDebug(const std::string& message) 
    {
        captured_logs.push_back("[DEBUG] " + message);
    }
}

// Override global logging functions for testing (mock external dependencies)
void logInfo(const std::string& message) { mock_logging::logInfo(message); }
void logWarning(const std::string& message) { mock_logging::logWarning(message); }
void logError(const std::string& message) { mock_logging::logError(message); }
void logDebug(const std::string& message) { mock_logging::logDebug(message); }

// Global mock instances (to replace real globals)
static Console mock_console_instance;
static Logger mock_logger_instance;

Console* g_Console = &mock_console_instance;
Logger* g_Logger = &mock_logger_instance;