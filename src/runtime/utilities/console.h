#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "logger.h"
#include "window.h"

// Command system types
using CommandHandler = std::function<void(const std::vector<std::string> &args)>;

struct CommandInfo
{
    CommandHandler handler;
    std::string description;
};

// Console - Handles UI rendering and command execution
class Console : public Window
{
  public:
    Console();               // Default constructor uses global logger
    Console(Logger &logger); // Alternative constructor for custom logger
    ~Console();

    void toggleVisibility() override;
    void draw(int OuterWidth, int OuterHeight, float UIScale) override;

    // Command system
    void addCommand(const std::string &commandName, CommandHandler handler, const std::string &description = "");
    void removeCommand(const std::string &commandName);
    void executeCommand(const std::string &commandLine);
    void printToConsole(const std::string &message);

  private:
    void drawLogOutput();
    void drawCommandInput();
    void processCommand(const std::string &input);
    std::vector<std::string> parseCommandLine(const std::string &commandLine);

    Logger &logger_;
    bool autoScroll_;
    bool showTimestamps_;
    bool levelEnabled_[4];

    // Command system
    std::unordered_map<std::string, CommandInfo> commands_;
    char commandBuffer_[256];
    std::vector<std::string> commandHistory_;
    int historyIndex_;
};

// Global console access (managed by GUI system)
extern Console *g_Console;
