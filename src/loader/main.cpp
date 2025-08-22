#include <windows.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

void createConsoleWindow()
{
    // Check if we already have a console (launched from command line)
    if (GetConsoleWindow() != nullptr)
        return;

    // Allocate a new console for this GUI application
    if (AllocConsole())
    {
        // Redirect stdout, stdin, stderr to the console
#ifdef _MSC_VER
        freopen_s(reinterpret_cast<FILE **>(stdout), "CONOUT$", "w", stdout);
        freopen_s(reinterpret_cast<FILE **>(stderr), "CONOUT$", "w", stderr);
        freopen_s(reinterpret_cast<FILE **>(stdin), "CONIN$", "r", stdin);
#else
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        freopen("CONIN$", "r", stdin);
#endif

        // Make cout, wcout, cin, wcin, wcerr, cerr, wclog and clog point to console as well
        std::ios::sync_with_stdio(true);

        // Set console title
        SetConsoleTitleA("WOLF Okami Loader");

        // Optional: Set console window size
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        COORD bufferSize = {100, 50};
        SetConsoleScreenBufferSize(hConsole, bufferSize);

        SMALL_RECT windowSize = {0, 0, 99, 30};
        SetConsoleWindowInfo(hConsole, TRUE, &windowSize);
    }
}

int error(const std::string &message)
{
    std::cerr << "Error: " << message << std::endl;
    std::cout << "Press Enter to exit..." << std::endl;
    std::cin.get();
    return 1;
}

std::string formatWindowsError(DWORD error)
{
    if (error == 0)
        return "Success";

    LPSTR messageBuffer = nullptr;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, nullptr, error, 0, reinterpret_cast<LPSTR>(&messageBuffer), 0, nullptr);

    std::string message(messageBuffer);
    LocalFree(messageBuffer);

    // Remove trailing newlines
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
        message.pop_back();

    return message;
}

void createModdedSignal()
{
    fs::path temp = fs::temp_directory_path() / "okami_modded_signal.txt";
    fs::remove(temp);
    std::ofstream{temp};
}

bool copyRuntime(const fs::path &gameDir)
{
    fs::path sourceDll = gameDir / "wolf" / "dinput8.dll";
    fs::path targetDll = gameDir / "dinput8.dll";

    if (!fs::exists(sourceDll))
    {
        std::cerr << "Error: Cannot find dinput8.dll (runtime) at: " << sourceDll.string() << std::endl;
        return false;
    }

    std::error_code ec;
    fs::copy_file(sourceDll, targetDll, fs::copy_options::overwrite_existing, ec);
    if (ec)
    {
        std::cerr << "Error: Failed to copy dinput8.dll to game directory: " << ec.message() << std::endl;
        return false;
    }

    std::cout << "Copied runtime (dinput8.dll) to game directory" << std::endl;
    return true;
}

void discoverMods(const fs::path &modsDir)
{
    if (!fs::exists(modsDir))
    {
        std::cout << "Creating mods directory..." << std::endl;
        fs::create_directories(modsDir);
        return;
    }

    std::vector<fs::path> foundMods;
    for (const auto &entry : fs::recursive_directory_iterator(modsDir))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".dll")
        {
            foundMods.push_back(entry.path());
        }
    }

    if (foundMods.empty())
    {
        std::cout << "No mods found in " << modsDir.string() << std::endl;
    }
    else
    {
        std::cout << "Found " << foundMods.size() << " mod(s):" << std::endl;
        for (const auto &mod : foundMods)
        {
            std::cout << "  - " << fs::relative(mod, modsDir).string() << std::endl;
        }
    }
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char *argv[])
{
    // Create console window if launched by double-clicking
    createConsoleWindow();

    std::cout << "WOLF Okami Loader Framework" << std::endl;
    std::cout << "=============================" << std::endl;

    // Get loader directory
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    fs::path loaderDir = fs::path(exePath).parent_path();

    // Build paths
    fs::path okamiExe = loaderDir / "okami.exe";
    fs::path modsDir = loaderDir / "mods";
    fs::path gameDir = loaderDir; // Assume game is in same directory as loader

    // Check if game exe exists
    if (!fs::exists(okamiExe))
    {
        return error(std::format("Cannot find okami.exe at: {}", okamiExe.string()));
    }

    // Discover and display available mods
    std::cout << std::endl << "Discovering mods..." << std::endl;
    discoverMods(modsDir);

    // Create modded signal for runtime detection
    createModdedSignal();

    // Launch the game
    std::cout << std::endl << "Launching Okami with WOLF runtime..." << std::endl;
    std::cout << "Attempting to launch: " << okamiExe << std::endl;

    // Check if okami.exe exists
    if (!fs::exists(okamiExe))
    {
        return error(std::format("okami.exe not found at: {}", okamiExe.string()));
    }

    // Launch the game
    std::cout << std::endl << "Launching Okami with WOLF runtime..." << std::endl;
    STARTUPINFOW si{};
    PROCESS_INFORMATION pi{};
    std::wstring wideOkamiExe = okamiExe;

    if (!CreateProcessW(wideOkamiExe.c_str(), nullptr, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
    {
        DWORD lastError = GetLastError();
        std::cout << "CreateProcessW failed with error code: " << lastError << std::endl;
        return error(std::format("Failed to launch okami.exe: {}", formatWindowsError(lastError)));
    }

    std::cout << "CreateProcessW succeeded! Process ID: " << pi.dwProcessId << std::endl;
    std::cout << "Game launched successfully! Waiting for game to initialize..." << std::endl;
    WaitForSingleObject(pi.hProcess, 10000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return 0;
}
