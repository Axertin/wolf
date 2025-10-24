#include "gui.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>

#include "../core/gui_system.h"
#include "../core/mod_lifecycle.h"
#include "../wolf_runtime_api.h"
#include "console.h"
#include "logger.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX // Prevent Windows.h min/max macros
#endif
#include <algorithm>
#include <chrono>
#include <thread>

#include <d3d11.h>
#include <dxgi.h>
#include <imgui_impl_win32.h>

#include <MinHook.h>
#endif

#ifdef _WIN32

static ID3D11Device *device = nullptr;
static ID3D11DeviceContext *context = nullptr;
static HWND hwnd = nullptr;
static ID3D11RenderTargetView *rtv = nullptr;
static void *D3D11PresentFnPtr = nullptr;
static void *D3D11ResizeBuffersFnPtr = nullptr;

static bool GuiIsVisible = true;
static bool MouseIsReleased = false;
static unsigned long LastToggleTime = 0;
static const unsigned long DEBOUNCE_MS = 200;

static bool homePressed = false;
static bool endPressed = false;
static bool tildePressed = false;

static std::vector<std::unique_ptr<Window>> Windows;
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

/**
 * @brief Rebuild font atlas for the given resolution
 * @param width Window width
 * @param height Window height
 *
 * This rebuilds the font atlas at the appropriate pixel size for the current resolution,
 * rather than scaling pre-rasterized fonts which causes blurriness.
 */
static void rebuildFontAtlasForResolution(int width, int height)
{
    const int BaseWidth = 1920;
    const int BaseHeight = 1080;
    const float BaseFontSize = 13.0f; // ImGui's default font size

    float widthScale = static_cast<float>(width) / BaseWidth;
    float heightScale = static_cast<float>(height) / BaseHeight;
    float uiScale = std::min(widthScale, heightScale);

    // Calculate the font size we want to rasterize at
    float scaledFontSize = BaseFontSize * uiScale;

    ImGuiIO &io = ImGui::GetIO();

    logDebug("[WOLF] Rebuilding font atlas: resolution=%dx%d, scale=%.2f, fontsize=%.1fpx", width, height, uiScale, scaledFontSize);

    // Clear existing fonts
    io.Fonts->Clear();

    // Configure font to be rasterized at the scaled size
    ImFontConfig fontConfig;
    fontConfig.SizePixels = scaledFontSize;
    fontConfig.OversampleH = 3; // Better quality at different sizes
    fontConfig.OversampleV = 3;

    // Add default font at the scaled pixel size
    ImFont *font = io.Fonts->AddFontDefault(&fontConfig);

    if (!font)
    {
        logError("[WOLF] Failed to add default font!");
        return;
    }

    // Build the new font atlas
    bool buildSuccess = io.Fonts->Build();
    if (!buildSuccess)
    {
        logError("[WOLF] Failed to build font atlas!");
        return;
    }

    // Recreate device objects to upload the new font texture
    // This needs to be done for Wolf + all mod contexts
    wolf::runtime::internal::recreateImGuiDeviceObjects();

    logInfo("[WOLF] Font atlas rebuilt successfully for %dx%d", width, height);
}

/**
 * @brief Window procedure hook for handling Win32 messages and ImGui input
 *
 * Intercepts Win32 messages for ImGui processing and implements cross-DLL
 * character input forwarding. When ImGui's Win32 backend fails to handle
 * WM_CHAR messages (due to cross-DLL context issues), this function manually
 * forwards character events to mod contexts.
 *
 * @param Handle Window handle
 * @param Msg Windows message ID
 * @param WParam Message parameter
 * @param LParam Message parameter
 * @return Message handling result
 */
static WNDPROC oWndProc = nullptr;
LRESULT WINAPI onWndProc(HWND Handle, UINT Msg, WPARAM WParam, LPARAM LParam)
{
    bool handled = false;
    if (GuiIsVisible)
    {
        handled = ImGui_ImplWin32_WndProcHandler(Handle, Msg, WParam, LParam);

        // Handle character input for cross-DLL mod support
        // ImGui Win32 backend doesn't properly handle WM_CHAR in cross-DLL scenarios,
        // so we manually forward character events to mod contexts when needed
        if (Msg == WM_CHAR && !handled)
        {
            ImGuiIO &io = ImGui::GetIO();
            if (io.WantCaptureKeyboard && WParam > 0 && WParam < 0x10000)
            {
                ImWchar character = static_cast<ImWchar>(WParam);
                wolf::runtime::internal::forwardCharacterToModContexts(character);
                handled = true;
            }
        }
    }

    if (handled)
        return true;

    // Check if any mod wants to capture input (will be updated in next frame after input forwarding)
    bool anyModWantsInput = wolf::runtime::internal::anyModWantsInput();
    ImGuiIO &io = ImGui::GetIO();

    // If any mod wants input or Wolf's GUI wants mouse, keep cursor free
    if ((io.WantCaptureMouse || anyModWantsInput) && MouseIsReleased)
    {
        // Keep cursor free
        ClipCursor(nullptr);
        ShowCursor(TRUE);
        return true;
    }

    // Prevent game from recapturing mouse if any mods want input
    if (Msg == WM_LBUTTONDOWN && MouseIsReleased && anyModWantsInput)
    {
        return true; // Block game from recapturing
    }

    // Only allow game to recapture mouse if no mods want input and Wolf GUI doesn't want it
    if (Msg == WM_LBUTTONDOWN && MouseIsReleased && !io.WantCaptureMouse && !anyModWantsInput)
    {
        while (ShowCursor(FALSE) > -1)
            ;
        MouseIsReleased = false;
    }

    return oWndProc ? oWndProc(Handle, Msg, WParam, LParam) : DefWindowProc(Handle, Msg, WParam, LParam);
}

/**
 * @brief Try to initialize ImGUI
 */
bool guiTryInit(IDXGISwapChain *pSwapChain)
{
    logInfo("[WOLF] Initializing ImGui");
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();

    DXGI_SWAP_CHAIN_DESC desc;
    pSwapChain->GetDesc(&desc);
    hwnd = desc.OutputWindow;

    if (!hwnd || !IsWindow(hwnd))
    {
        logError("[WOLF] Invalid HWND from swapchain!");
        return false;
    }

    if (FAILED(pSwapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void **>(&device))))
    {
        logError("[WOLF] Failed to get render device!");
        return false;
    }

    oWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&onWndProc)));

    device->GetImmediateContext(&context);

    ImGui::StyleColorsDark();

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    Windows.push_back(std::make_unique<Console>());

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(device, context);

    // Get initial window size and build font atlas at appropriate resolution
    RECT rect;
    GetClientRect(hwnd, &rect);
    int initialWidth = rect.right - rect.left;
    int initialHeight = rect.bottom - rect.top;
    rebuildFontAtlasForResolution(initialWidth, initialHeight);

    // Create initial render target view
    ID3D11Texture2D *backBuffer = nullptr;
    if (SUCCEEDED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&backBuffer))))
    {
        device->CreateRenderTargetView(backBuffer, nullptr, &rtv);
        backBuffer->Release();
    }
    else
    {
        logError("[WOLF] Failed to get initial back buffer!");
        return false;
    }

    return true;
}

/**
 * @brief Render ImGUI frame
 */
void guiRenderFrame(IDXGISwapChain *pSwapChain)
{
    if (!ImGui::GetCurrentContext())
    {
        logError("[WOLF] ImGui context missing!");
        return;
    }

    RECT rect;
    GetClientRect(hwnd, &rect);
    int WindowWidth = rect.right - rect.left;
    int WindowHeight = rect.bottom - rect.top;

    const int BaseWidth = 1920;
    const int BaseHeight = 1080;
    float WidthScale = static_cast<float>(WindowWidth) / BaseWidth;
    float HeightScale = static_cast<float>(WindowHeight) / BaseHeight;
    float UIScale = std::min(WidthScale, HeightScale);

    // Force keyboard capture when mods are present to ensure character events are processed
    bool hasModContexts = wolf::runtime::internal::hasModContexts();
    if (hasModContexts)
    {
        ImGui::SetNextFrameWantCaptureKeyboard(true);
    }

    ImGui_ImplWin32_NewFrame();
    ImGui_ImplDX11_NewFrame();
    ImGui::NewFrame();

    // Draw GUI only if visible
    if (GuiIsVisible)
    {
        for (auto &Window : Windows)
        {
            Window->draw(WindowWidth, WindowHeight, UIScale);
        }
    }

    // Forward Wolf's input state to all mod contexts
    wolf::runtime::internal::forwardInputToModContexts();

    ImGui::Render();

    // Render mod GUI windows after Wolf's GUI but before backend rendering
    wolf::runtime::internal::renderModGuiWindows(pSwapChain);

    // Set render target and render
    context->OMSetRenderTargets(1, &rtv, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    // Render all collected mod draw data
    wolf::runtime::internal::renderCollectedModDrawData();
}

/**
 * @brief Hook for DX11 Present() Function
 */
typedef HRESULT(__stdcall *PresentFn)(IDXGISwapChain *pSwapChain, UINT SyncInterval, UINT Flags);
static PresentFn oPresent = nullptr;
HRESULT __stdcall onRenderPresent(IDXGISwapChain *pSwapChain, UINT SyncInterval, UINT Flags)
{
    static bool initialized = false;
    static int framesAfterInit = -1;

    if (!initialized && guiTryInit(pSwapChain))
    {
        initialized = true;
        framesAfterInit = 0;
    }

    // Call late game init one frame after GUI initialization
    if (initialized && framesAfterInit == 1)
    {
        wolf::runtime::internal::callLateGameInit();
        framesAfterInit = -1; // Prevent multiple calls
    }
    else if (framesAfterInit >= 0)
    {
        framesAfterInit++;
    }

    // Handle toggle keys
    unsigned long currentTime = GetTickCount();
    if (currentTime - LastToggleTime > DEBOUNCE_MS)
    {
        // Check HOME key
        bool homeDown = (GetAsyncKeyState(VK_HOME) & 0x8000) != 0;
        if (homeDown && !homePressed)
        {
            GuiIsVisible = !GuiIsVisible;
            LastToggleTime = currentTime;
            logDebug("[WOLF] GUI visibility toggled: %s", GuiIsVisible ? "ON" : "OFF");
        }
        homePressed = homeDown;

        // Check END key - release/capture mouse
        bool endDown = (GetAsyncKeyState(VK_END) & 0x8000) != 0;
        if (endDown && !endPressed)
        {
            if (!MouseIsReleased)
            {
                // Release the mouse
                ClipCursor(nullptr);
                ShowCursor(TRUE);
                MouseIsReleased = true;
                logDebug("[WOLF] Mouse released for ImGui");
            }
            else
            {
                // Re-capture the mouse
                while (ShowCursor(FALSE) > -1)
                    ;
                MouseIsReleased = false;
                logDebug("[WOLF] Mouse captured by game");
            }
            LastToggleTime = currentTime;
        }
        endPressed = endDown;

        // Also just release the mouse if some other keys are pressed like Win or Alt
        if (GetAsyncKeyState(VK_LWIN) || GetAsyncKeyState(VK_LMENU))
        {
            MouseIsReleased = true;
        }

        // Check the key to the left of `1` and above `tab` - Toggle console window
        UINT vk = MapVirtualKey(0x29, MAPVK_VSC_TO_VK);
        bool tildeDown = (GetAsyncKeyState(vk) & 0x8000) != 0;
        if (tildeDown && !tildePressed)
        {
            Windows[0]->toggleVisibility();
            LastToggleTime = currentTime;
            logDebug("[WOLF] Console visibility toggled: %s", Windows[0]->IsVisible ? "ON" : "OFF");
        }
        tildePressed = tildeDown;
    }

    // Keep cursor visible when released
    if (MouseIsReleased)
    {
        ClipCursor(nullptr);
        ShowCursor(TRUE);
    }

    // Render GUI
    if (initialized)
    {
        guiRenderFrame(pSwapChain);
    }

    return oPresent(pSwapChain, SyncInterval, Flags);
}

void guiCleanup()
{
    if (!ImGui::GetCurrentContext())
        return;

    // Clear global console pointer
    g_Console = nullptr;

    if (rtv)
    {
        rtv->Release();
        rtv = nullptr;
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

/**
 * @brief Hook for DX11 ResizeBuffers() Function
 */
typedef HRESULT(__stdcall *ResizeBuffersFn)(IDXGISwapChain *pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
static ResizeBuffersFn oResizeBuffers = nullptr;
HRESULT __stdcall onResizeBuffers(IDXGISwapChain *pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    logDebug("[WOLF] ResizeBuffers called: %dx%d", Width, Height);

    // Release old render target view before resizing
    if (rtv)
    {
        rtv->Release();
        rtv = nullptr;
    }

    // Call original ResizeBuffers
    HRESULT result = oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);

    if (SUCCEEDED(result))
    {
        // Get new back buffer and create new RTV
        ID3D11Texture2D *backBuffer = nullptr;
        if (SUCCEEDED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&backBuffer))))
        {
            device->CreateRenderTargetView(backBuffer, nullptr, &rtv);
            backBuffer->Release();

            // Rebuild font atlas at the new resolution for crisp text rendering
            rebuildFontAtlasForResolution(Width, Height);

            logDebug("[WOLF] Successfully recreated render resources after resize");
        }
        else
        {
            logError("[WOLF] Failed to get back buffer after resize");
        }
    }
    else
    {
        logError("[WOLF] ResizeBuffers failed with HRESULT: 0x%X", result);
    }

    return result;
}

void getPresentFunctionPtr()
{
    DXGI_SWAP_CHAIN_DESC SwapChainDesc = {};
    SwapChainDesc.BufferCount = 1;
    SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    SwapChainDesc.OutputWindow = GetForegroundWindow();
    SwapChainDesc.SampleDesc.Count = 1;
    SwapChainDesc.Windowed = TRUE;
    SwapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    SwapChainDesc.BufferDesc.Width = 2;
    SwapChainDesc.BufferDesc.Height = 2;
    SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    ID3D11Device *pDevice = nullptr;
    ID3D11DeviceContext *pContext = nullptr;
    IDXGISwapChain *pSwapChain = nullptr;

    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &SwapChainDesc, &pSwapChain,
                                             &pDevice, nullptr, &pContext)))
    {
        logError("[WOLF] Failed to initialize dummy D3D11 device!");
    }

    void **vtable = *reinterpret_cast<void ***>(pSwapChain);
    D3D11PresentFnPtr = vtable[8];
    D3D11ResizeBuffersFnPtr = vtable[13];

    pSwapChain->Release();
    pDevice->Release();
    pContext->Release();
}

ID3D11Device *guiGetD3D11Device()
{
    return device;
}

ID3D11DeviceContext *guiGetD3D11DeviceContext()
{
    return context;
}

void guiRenderDrawData(void *drawData)
{
    if (!drawData)
        return;

    ImDrawData *imDrawData = static_cast<ImDrawData *>(drawData);
    if (imDrawData && imDrawData->Valid && imDrawData->CmdListsCount > 0)
    {
        ImGui_ImplDX11_RenderDrawData(imDrawData);
    }
}

void guiInitHooks()
{
    std::thread(
        []
        {
            while (D3D11PresentFnPtr == nullptr)
            {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                getPresentFunctionPtr();
            }
            MH_CreateHook(D3D11PresentFnPtr, reinterpret_cast<LPVOID>(&onRenderPresent), reinterpret_cast<LPVOID *>(&oPresent));
            MH_EnableHook(D3D11PresentFnPtr);

            MH_CreateHook(D3D11ResizeBuffersFnPtr, reinterpret_cast<LPVOID>(&onResizeBuffers), reinterpret_cast<LPVOID *>(&oResizeBuffers));
            MH_EnableHook(D3D11ResizeBuffersFnPtr);
        })
        .detach();
}

#else // If not _WIN32

void guiInitHooks()
{
    logWarning("[WOLF] Not built for WIN32, no GUI is available.");
}

#endif
