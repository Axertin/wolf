#include "logger.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <format>
#include <iomanip>
#include <memory>
#include <sstream>

// Global logger instance
Logger *g_Logger = nullptr;

// std::chrono::current_zone() requires _LIBCPP_HAS_TIME_ZONE_DATABASE in libc++,
// which llvm-mingw ships disabled. Fall back to localtime_s/_r in that case.
static std::string formatLocalTime(const char *fmt)
{
    auto now = std::chrono::system_clock::now();
#if defined(_LIBCPP_VERSION) && (!defined(_LIBCPP_HAS_TIME_ZONE_DATABASE) || _LIBCPP_HAS_TIME_ZONE_DATABASE == 0)
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#  ifdef _WIN32
    localtime_s(&tm, &t);
#  else
    localtime_r(&t, &tm);
#  endif
    char buf[64];
    std::strftime(buf, sizeof(buf), fmt, &tm);
    (void)now;
    return buf;
#else
    auto local_time = std::chrono::current_zone()->to_local(now);
    if (std::string_view(fmt) == "%H:%M:%S")
        return std::format("{:%H:%M:%S}", local_time);
    return std::format("{:%Y%m%d_%H%M%S}", local_time);
#endif
}

LogEntry::LogEntry(const std::string &msg, LogLevel lvl) : message(msg), level(lvl)
{
    timestamp = formatLocalTime("%H:%M:%S");
}

ImVec4 LogEntry::getColor() const
{
    switch (level)
    {
    case LogLevel::Info:
        return ImVec4(1.0f, 1.0f, 1.0f, 1.0f); // White
    case LogLevel::Warning:
        return ImVec4(1.0f, 1.0f, 0.0f, 1.0f); // Yellow
    case LogLevel::Error:
        return ImVec4(1.0f, 0.0f, 0.0f, 1.0f); // Red
    case LogLevel::Debug:
        return ImVec4(0.5f, 0.5f, 0.5f, 1.0f); // Gray
    default:
        return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    }
}

const char *LogEntry::getLevelString() const
{
    switch (level)
    {
    case LogLevel::Info:
        return "[INFO] ";
    case LogLevel::Warning:
        return "[WARN] ";
    case LogLevel::Error:
        return "[ERROR]";
    case LogLevel::Debug:
        return "[DEBUG]";
    default:
        return "[UNKNOWN]";
    }
}

StreamCapture::StreamCapture(std::ostream &stream, std::function<void(const std::string &, LogLevel)> callback, LogLevel level)
    : stream_(stream), callback_(callback), level_(level)
{
    old_buf_ = stream.rdbuf(this);
}

StreamCapture::~StreamCapture()
{
    stream_.rdbuf(old_buf_);
}

int StreamCapture::overflow(int c)
{
    if (c != EOF)
    {
        buffer_ += static_cast<char>(c);
        if (c == '\n')
        {
            if (!buffer_.empty() && buffer_.back() == '\n')
            {
                buffer_.pop_back(); // Remove newline for cleaner display
            }
            if (!buffer_.empty())
            {
                callback_(buffer_, level_);
                buffer_.clear();
            }
        }
    }
    return c;
}

// Logger Implementation - Handles data storage and logging

Logger::Logger()
{
    initializeLogFile();
    setupStreamCapture();
}

Logger::~Logger()
{
    cleanupStreamCapture();
    if (logFile_.is_open())
    {
        logFile_.close();
    }
}

void Logger::log(const std::string &message, LogLevel level)
{
    addLogEntry(message, level);
}

void Logger::logInfo(const std::string &message)
{
    log(message, LogLevel::Info);
}

void Logger::logWarning(const std::string &message)
{
    log(message, LogLevel::Warning);
}

void Logger::logError(const std::string &message)
{
    log(message, LogLevel::Error);
}

void Logger::logDebug(const std::string &message)
{
    log(message, LogLevel::Debug);
}

void Logger::clear()
{
    std::lock_guard<std::mutex> lock(logMutex_);
    logEntries_.clear();
}

const std::vector<LogEntry> &Logger::getLogEntries() const
{
    return logEntries_;
}

std::mutex &Logger::getLogMutex() const
{
    return logMutex_;
}

void Logger::initializeLogFile()
{
    // Create logs directory if it doesn't exist
    std::filesystem::create_directories("logs");

    std::string filename = "logs/okami_log_" + formatLocalTime("%Y%m%d_%H%M%S") + ".txt";

    logFile_.open(filename, std::ios::out | std::ios::app);
    if (logFile_.is_open())
    {
        logFile_ << "=== Console Log Started ===" << std::endl;
    }
}

void Logger::setupStreamCapture()
{
    auto captureCallback = [this](const std::string &msg, LogLevel level) { this->addLogEntry(msg, level); };

    stdoutCapture_.reset();
    stderrCapture_.reset();

    stdoutCapture_.reset(new StreamCapture(std::cout, captureCallback, LogLevel::Debug));
    stderrCapture_.reset(new StreamCapture(std::cerr, captureCallback, LogLevel::Error));
}

void Logger::cleanupStreamCapture()
{
    stdoutCapture_.reset();
    stderrCapture_.reset();
}

void Logger::addLogEntry(const std::string &message, LogLevel level)
{
    std::lock_guard<std::mutex> lock(logMutex_);

    logEntries_.emplace_back(message, level);

    // Write to file regardless of window visibility
    if (logFile_.is_open())
    {
        logFile_ << "[" << logEntries_.back().timestamp << "] " << logEntries_.back().getLevelString() << " " << message << std::endl;
        logFile_.flush();
    }

    // Keep log size manageable (trim old entries)
    if (logEntries_.size() > 10000)
    {
        logEntries_.erase(logEntries_.begin(), logEntries_.begin() + 1000);
    }
}

// Global Functions - Use Logger directly

void logMessage(const std::string &message, LogLevel level)
{
    if (g_Logger != nullptr)
    {
        g_Logger->log(message, level);
    }
}

void logInfo(const std::string &message)
{
    if (g_Logger != nullptr)
    {
        g_Logger->logInfo(message);
    }
}

void logWarning(const std::string &message)
{
    if (g_Logger != nullptr)
    {
        g_Logger->logWarning(message);
    }
}

void logError(const std::string &message)
{
    if (g_Logger != nullptr)
    {
        g_Logger->logError(message);
    }
}

void logDebug(const std::string &message)
{
    if (g_Logger != nullptr)
    {
        g_Logger->logDebug(message);
    }
}

// Initialization Functions

void initializeLogger()
{
    if (g_Logger == nullptr)
    {
        g_Logger = new Logger();
    }
}

void shutdownLogger()
{
    delete g_Logger;
    g_Logger = nullptr;
}
