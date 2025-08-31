/**
 * @file wolf_logging.hpp
 * @brief WOLF Framework Logging System
 */

#pragma once

#include <cstdarg>

#include "wolf_core.hpp"

//==============================================================================
// LOGGING
//==============================================================================

namespace wolf
{

/**
 * @brief Log message severity levels
 */
enum class LogLevel
{
    Info = 0,    ///< General information
    Warning = 1, ///< Warning conditions
    Error = 2,   ///< Error conditions
    Debug = 3    ///< Debug information
};

namespace detail
{
// Convert C++ log level to C enum - use the constants directly to avoid enum conflicts
inline int toCLogLevel(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Info:
        return 0; // WOLF_LOG_INFO
    case LogLevel::Warning:
        return 1; // WOLF_LOG_WARNING
    case LogLevel::Error:
        return 2; // WOLF_LOG_ERROR
    case LogLevel::Debug:
        return 3; // WOLF_LOG_DEBUG
    default:
        return 0; // WOLF_LOG_INFO
    }
}

// Helper for printf-style formatting (used by logging system)
inline std::string formatLogString(const char *format, va_list args)
{
    va_list args_copy;
    va_copy(args_copy, args);
    int buffer_size = vsnprintf(nullptr, 0, format, args_copy);
    va_end(args_copy);

    if (buffer_size <= 0)
        return std::string(format);

    std::string result(buffer_size, '\0');
    vsnprintf(&result[0], buffer_size + 1, format, args);
    return result;
}
} // namespace detail

/**
 * @brief Log an info message with optional printf-style formatting
 * @param format Format string (can be a simple message or printf-style format)
 * @param ... Format arguments (optional)
 */
inline void logInfo(const char *format, ...) noexcept
{
    if (!detail::g_runtime)
        return;

    va_list args;
    va_start(args, format);
    std::string message = detail::formatLogString(format, args);
    va_end(args);

    detail::g_runtime->log(detail::getCurrentModId(), static_cast<WolfLogLevel>(0), message.c_str());
}

/**
 * @brief Log a warning message with optional printf-style formatting
 * @param format Format string (can be a simple message or printf-style format)
 * @param ... Format arguments (optional)
 */
inline void logWarning(const char *format, ...) noexcept
{
    if (!detail::g_runtime)
        return;

    va_list args;
    va_start(args, format);
    std::string message = detail::formatLogString(format, args);
    va_end(args);

    detail::g_runtime->log(detail::getCurrentModId(), static_cast<WolfLogLevel>(1), message.c_str());
}

/**
 * @brief Log an error message with optional printf-style formatting
 * @param format Format string (can be a simple message or printf-style format)
 * @param ... Format arguments (optional)
 */
inline void logError(const char *format, ...) noexcept
{
    if (!detail::g_runtime)
        return;

    va_list args;
    va_start(args, format);
    std::string message = detail::formatLogString(format, args);
    va_end(args);

    detail::g_runtime->log(detail::getCurrentModId(), static_cast<WolfLogLevel>(2), message.c_str());
}

/**
 * @brief Log a debug message with optional printf-style formatting
 * @param format Format string (can be a simple message or printf-style format)
 * @param ... Format arguments (optional)
 */
inline void logDebug(const char *format, ...) noexcept
{
    if (!detail::g_runtime)
        return;

    va_list args;
    va_start(args, format);
    std::string message = detail::formatLogString(format, args);
    va_end(args);

    detail::g_runtime->log(detail::getCurrentModId(), static_cast<WolfLogLevel>(3), message.c_str());
}

/**
 * @brief Set a prefix for all log messages from this mod
 * @param prefix Prefix string (e.g., "[MyMod]")
 * @return True if prefix was successfully set
 */
inline bool setLogPrefix(const char *prefix) noexcept
{
    if (!detail::g_runtime)
        return false;
    detail::g_runtime->setLogPrefix(detail::getCurrentModId(), prefix);
    return true;
}

} // namespace wolf
