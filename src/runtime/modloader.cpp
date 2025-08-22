#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "logger.h"
#include "wolf_runtime_api.h"

using namespace wolf::runtime;

namespace fs = std::filesystem;
using namespace std::chrono_literals;

static std::vector<HMODULE> loadedMods;
static fs::path moddedTemp = fs::temp_directory_path() / "okami_modded_signal.txt";

bool IsModded()
{
    LPCSTR cmdLine = GetCommandLineA();
    if (cmdLine != nullptr && strstr(cmdLine, "-MODDED") != nullptr)
    {
        return true;
    }

    if (fs::exists(moddedTemp))
    {
        auto lastWrite = fs::last_write_time(moddedTemp);
        auto diff = decltype(lastWrite)::clock::now() - lastWrite;

        // If older than 30 seconds, assume not modded.
        if (diff < 30s)
        {
            return true;
        }
    }
    return false;
}

void LoadMods()
{
    static bool alreadyLoaded = false;
    if (alreadyLoaded || !IsModded())
        return;

    // Remove the temp file so that subsequent launch attempt in a short time span doesn't open modded
    fs::remove(moddedTemp);

    alreadyLoaded = true;

    logInfo("Discovering mods in 'mods' directory...");

    if (!fs::exists("mods"))
    {
        logInfo("No mods directory found, creating one");
        fs::create_directories("mods");
        return;
    }

    int loadedCount = 0;
    for (const auto &dll : fs::recursive_directory_iterator("mods"))
    {
        const auto &path = dll.path();
        if (path.extension().string() == ".dll")
        {
            logInfo("Loading mod: " + path.filename().string());
            HMODULE hMod = LoadLibraryW(path.c_str());
            if (hMod == nullptr)
            {
                std::string msg = "Failed to load mod: " + path.string();
                logError(msg);
                MessageBoxA(nullptr, msg.c_str(), "WOLF Runtime Error", MB_ICONERROR);
                continue;
            }

            // Look for the mod entry point
            typedef WolfModInterface(*wolfGetModInterfaceFunc)(WolfRuntimeAPI *runtime);
            FARPROC procAddr = GetProcAddress(hMod, "wolfGetModInterface");
            wolfGetModInterfaceFunc getModInterface = nullptr;
            if (procAddr != nullptr)
            {
                // Suppress function type cast warning - this is intentional for DLL loading
                #pragma warning(push)
                #pragma warning(disable: 4191) // unsafe conversion from FARPROC
                getModInterface = reinterpret_cast<wolfGetModInterfaceFunc>(procAddr);
                #pragma warning(pop)
            }
            if (getModInterface != nullptr)
            {
                // Create runtime API and pass it to the mod
                WolfRuntimeAPI *runtimeAPI = createRuntimeAPI();
                WolfModInterface modInterface = getModInterface(runtimeAPI);
                
                if (modInterface.getName != nullptr)
                {
                    logInfo("Mod registered successfully: " + path.filename().string());
                    
                    // Call early init if provided
                    if (modInterface.earlyGameInit != nullptr)
                    {
                        modInterface.earlyGameInit();
                    }
                }
                else
                {
                    logWarning("Mod returned invalid interface: " + path.filename().string());
                }
            }
            else
            {
                logInfo("Loaded library (no mod entry point): " + path.filename().string());
            }

            loadedMods.emplace_back(hMod);
            loadedCount++;
        }
    }

    logInfo("Successfully loaded " + std::to_string(loadedCount) + " mod(s)");
}

void UnloadMods()
{
    for (HMODULE hMod : loadedMods)
    {
        FreeLibrary(hMod);
    }
    loadedMods.clear();
}
