#include "console.h"

#include <mutex>
#include <stdexcept>

#include <imgui.h>

#include "../core/console_system.h"
#include "logger.h"

// Console Implementation - Handles UI rendering only

Console::Console() : Window("Console"), logger_(*g_Logger), autoScroll_(true), showTimestamps_(true), historyIndex_(-1)
{
    // Ensure logger is initialized
    if (g_Logger == nullptr)
    {
        throw std::runtime_error("Console created before Logger initialization. Call initializeLogger() first.");
    }

    // Initialize level filters - all enabled by default
    for (int i = 0; i < 4; ++i)
    {
        levelEnabled_[i] = true;
    }

    // Initialize command buffer
    commandBuffer_[0] = '\0';

    // Register default commands
    addCommand(
        "help",
        [this](const std::vector<std::string> &)
        {
            printToConsole("Available commands:");
            for (const auto &[cmdName, info] : commands_)
            {
                printToConsole("  " + cmdName + " - " + info.description);
            }
        },
        "Show available commands");

    addCommand("clear", [this](const std::vector<std::string> &) { logger_.clear(); }, "Clear console output");

    // Set global console pointer
    g_Console = this;

    // Process any commands that were registered before console was ready
    processPendingCommands();
}

Console::Console(Logger &logger) : Window("Console"), logger_(logger), autoScroll_(true), showTimestamps_(true), historyIndex_(-1)
{
    // Initialize level filters - all enabled by default
    for (int i = 0; i < 4; ++i)
    {
        levelEnabled_[i] = true;
    }

    // Initialize command buffer
    commandBuffer_[0] = '\0';

    // Set global console pointer
    g_Console = this;

    // Process any commands that were registered before console was ready
    processPendingCommands();
}

Console::~Console()
{
    // Clear global console pointer
    g_Console = nullptr;
}

void Console::toggleVisibility()
{
    IsVisible = !IsVisible;
}

void Console::draw([[maybe_unused]] int OuterWidth, [[maybe_unused]] int OuterHeight, float UIScale)
{
    if (!IsVisible)
        return;

    ImGui::SetNextWindowSize(ImVec2(800 * UIScale, 400 * UIScale), ImGuiCond_FirstUseEver);

    if (ImGui::Begin(name.c_str(), &IsVisible))
    {
        // Simple toolbar
        if (ImGui::Button("Clear"))
        {
            logger_.clear();
        }

        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &autoScroll_);

        ImGui::SameLine();
        ImGui::Checkbox("Timestamps", &showTimestamps_);

        // Log level filters
        ImGui::SameLine();
        ImGui::Text("| Show: ");
        ImGui::SameLine();
        ImGui::Checkbox("Info", &levelEnabled_[0]);
        ImGui::SameLine();
        ImGui::Checkbox("Warning", &levelEnabled_[1]);
        ImGui::SameLine();
        ImGui::Checkbox("Error", &levelEnabled_[2]);
        ImGui::SameLine();
        ImGui::Checkbox("Debug", &levelEnabled_[3]);

        ImGui::Separator();

        drawLogOutput();
        drawCommandInput();
    }
    ImGui::End();
}

void Console::drawLogOutput()
{
    // Reserve space for the command input section at the bottom
    const float footerHeightToReserve = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing(); // Separator + "Command:" + input
    if (ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footerHeightToReserve), false, ImGuiWindowFlags_HorizontalScrollbar))
    {
        std::lock_guard<std::mutex> lock(logger_.getLogMutex());
        const auto &logEntries = logger_.getLogEntries();

        for (const auto &entry : logEntries)
        {
            // Skip filtered out levels
            if (!levelEnabled_[static_cast<int>(entry.level)])
            {
                continue;
            }

            // Apply color based on log level
            ImGui::PushStyleColor(ImGuiCol_Text, entry.getColor());

            // Format display text
            std::string displayText;
            if (showTimestamps_)
            {
                displayText = "[" + entry.timestamp + "] " + entry.getLevelString() + " " + entry.message;
            }
            else
            {
                displayText = std::string(entry.getLevelString()) + " " + entry.message;
            }

            ImGui::TextUnformatted(displayText.c_str());
            ImGui::PopStyleColor();
        }

        // Auto-scroll to bottom if enabled and we're near the bottom
        if (autoScroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();
}

void Console::drawCommandInput()
{
    ImGui::Separator();
    ImGui::Text("Command:");
    ImGui::SameLine();

    ImGui::PushItemWidth(-1);
    if (ImGui::InputText("##CommandInput", commandBuffer_, sizeof(commandBuffer_), ImGuiInputTextFlags_EnterReturnsTrue))
    {
        if (strlen(commandBuffer_) > 0)
        {
            processCommand(commandBuffer_);
            commandBuffer_[0] = '\0';
            historyIndex_ = -1;
        }
    }
    ImGui::PopItemWidth();
}

void Console::addCommand(const std::string &commandName, CommandHandler handler, const std::string &description)
{
    commands_[commandName] = {handler, description};
}

void Console::removeCommand(const std::string &commandName)
{
    commands_.erase(commandName);
}

void Console::executeCommand(const std::string &commandLine)
{
    processCommand(commandLine);
}

void Console::printToConsole(const std::string &message)
{
    logger_.logInfo(message);
}

void Console::processCommand(const std::string &input)
{
    // Add to history
    commandHistory_.push_back(input);

    // Echo command
    printToConsole("> " + input);

    // Parse command
    auto args = parseCommandLine(input);
    if (args.empty())
        return;

    std::string command = args[0];
    auto it = commands_.find(command);

    if (it != commands_.end())
    {
        try
        {
            it->second.handler(args);
        }
        catch (const std::exception &e)
        {
            printToConsole("Error executing command: " + std::string(e.what()));
        }
    }
    else
    {
        printToConsole("Unknown command: " + command + ". Type 'help' for available commands.");
    }
}

std::vector<std::string> Console::parseCommandLine(const std::string &commandLine)
{
    std::vector<std::string> args;
    std::string current;
    bool inQuotes = false;

    for (char c : commandLine)
    {
        if (c == '"')
        {
            inQuotes = !inQuotes;
        }
        else if (c == ' ' && !inQuotes)
        {
            if (!current.empty())
            {
                args.push_back(current);
                current.clear();
            }
        }
        else
        {
            current += c;
        }
    }

    if (!current.empty())
    {
        args.push_back(current);
    }

    return args;
}

// Global console instance (set by GUI system)
Console *g_Console = nullptr;
