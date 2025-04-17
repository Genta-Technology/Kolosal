#include "config.hpp"

#include "window/window_factory.hpp"
#include "window/graphics_context_factory.hpp"

#include "ui/fonts.hpp"
#include "ui/title_bar.hpp"
#include "ui/tab_manager.hpp"
#include "ui/status_bar.hpp"

#include "chat/chat_manager.hpp"
#include "model/preset_manager.hpp"
#include "model/model_manager.hpp"
#include "model/model_loader_config_manager.hpp"

#include "nfd.h"

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx10.h>
#include <curl/curl.h>
#include <chrono>
#include <thread>
#include <memory>
#include <vector>
#include <exception>
#include <iostream>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

class ScopedCleanup
{
public:
    ~ScopedCleanup()
    {
        ImGui_ImplDX10_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        NFD_Quit();
    }
};

class WindowStateTransitionManager
{
public:
    WindowStateTransitionManager(Window& window)
        : window(window)
        , transitionProgress(0.0f)
        , easedProgress(0.0f)
        , isTransitioning(false)
        , targetActiveState(window.isActive())
        , previousActiveState(window.isActive()) {}

    void updateTransition()
    {
        bool currentActiveState = window.isActive();
        if (currentActiveState != previousActiveState)
        {
            isTransitioning = true;
            targetActiveState = currentActiveState;
            transitionStartTime = std::chrono::steady_clock::now();
        }
        previousActiveState = currentActiveState;

        if (isTransitioning)
        {
            float elapsedTime = std::chrono::duration<float>(std::chrono::steady_clock::now() - transitionStartTime).count();
            float progress = elapsedTime / Config::TRANSITION_DURATION;
            if (progress >= 1.0f)
            {
                progress = 1.0f;
                isTransitioning = false;
            }
            transitionProgress = targetActiveState ? progress : 1.0f - progress;
        }
        else
        {
            transitionProgress = targetActiveState ? 1.0f : 0.0f;
        }

        // Apply easing function
        easedProgress = transitionProgress * transitionProgress * (3.0f - 2.0f * transitionProgress);
    }

    float getTransitionProgress() const { return transitionProgress; }
    float getEasedProgress() const { return easedProgress; }

private:
    Window& window;
    float transitionProgress;
    float easedProgress;
    bool isTransitioning;
    bool targetActiveState;
    std::chrono::steady_clock::time_point transitionStartTime;
    bool previousActiveState;
};

void InitializeImGui(Window& window, DX10Context* dxContext)
{
    // Setup ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    // Enable power saving mode
    io.ConfigFlags |= ImGuiConfigFlags_EnablePowerSavingMode;

    // Set style
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = Config::WINDOW_CORNER_RADIUS;
    style.WindowBorderSize = 0.0f; // Disable ImGui's window border
    ImGui::StyleColorsDark();

    // Initialize font manager
    FontsManager::GetInstance();

    ImGui_ImplWin32_Init(window.getNativeHandle());
    ImGui_ImplDX10_Init(dxContext->getDevice());  // Change from OpenGL3 to DX10
}

void StartNewFrame() {
    ImGui_ImplDX10_NewFrame();  // Change from OpenGL3 to DX10
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void EnforceFrameRate(const std::chrono::time_point<std::chrono::high_resolution_clock>& frameStartTime)
{
    auto frameEndTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> frameDuration = frameEndTime - frameStartTime;
    double frameTime = frameDuration.count();

    if (frameTime < Config::TARGET_FRAME_TIME)
    {
        std::this_thread::sleep_for(std::chrono::duration<double>(Config::TARGET_FRAME_TIME - frameTime));
    }
}

void HandleException(const std::exception& e)
{
    ::MessageBoxA(nullptr, e.what(), "Unhandled Exception", MB_OK | MB_ICONERROR);
}

class Application
{
public:
    Application()
    {
        // Initialize the TabManager and add the ChatTab (other tabs can be added similarly)
        tabManager = std::make_unique<TabManager>();
        tabManager->addTab(std::make_unique<ChatTab>());
        tabManager->addTab(std::make_unique<ServerTab>());

        // Initialize the status bar
        statusBar = std::make_unique<StatusBar>();

        // Create and show the window
        window = WindowFactory::createWindow();
        window->createWindow(Config::WINDOW_WIDTH, Config::WINDOW_HEIGHT, Config::WINDOW_TITLE,
            tabManager->getTabCount() * 24.0f + (tabManager->getTabCount() - 2) * 10.0f + 6.0f + 12.0f);
        window->show();

        // Create and initialize the DirectX context
        dxContext = std::unique_ptr<DX10Context>(static_cast<DX10Context*>(
            GraphicContextFactory::createDirectXContext().release()));
        dxContext->initialize(window->getNativeHandle());

        // Set the DX context in the window
        static_cast<Win32Window*>(window.get())->setDXContext(dxContext.get());

        // Initialize cleanup (RAII)
        cleanup = std::make_unique<ScopedCleanup>();

        // Initialize ImGui
        InitializeImGui(*window, dxContext.get());

        // Initialize the chat, preset, and model managers
        Chat::initializeChatManager();
        Model::initializePresetManager();
        Model::initializeModelManager();
        Model::initializeModelLoaderConfigManager("model_loader_config.json");

        // Initialize Native File Dialog
        NFD_Init();

        // Get the initial window dimensions
        display_w = window->getWidth();
        display_h = window->getHeight();

        // Create the window state transition manager
        transitionManager = std::make_unique<WindowStateTransitionManager>(*window);
    }

    int run()
    {
        while (!window->shouldClose())
        {
            auto frameStartTime = std::chrono::high_resolution_clock::now();

            window->processEvents();

            // Skip rendering if the window is being moved
            // The movement is already being tracked in the DX10Context
            Win32Window* win32Window = static_cast<Win32Window*>(window.get());

            // Update window state transitions
            transitionManager->updateTransition();

            StartNewFrame();

            // Render the custom title bar
            titleBar(window->getNativeHandle(), *tabManager, dxContext.get());

            // Render the currently active tab (chat tab in this example)
            tabManager->renderCurrentTab();

            // Render the status bar
            statusBar->render();

            // Render ImGui
            ImGui::Render();

            // Check for window resizing and update viewport/swapchain accordingly
            int new_display_w = window->getWidth();
            int new_display_h = window->getHeight();
            if (new_display_w != display_w || new_display_h != display_h)
            {
                display_w = new_display_w;
                display_h = new_display_h;
                dxContext->resizeBuffers(display_w, display_h);
            }

            // Clear background with DirectX
            float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f }; // Transparent background
			ID3D10RenderTargetView* renderTargetView = dxContext->getRenderTargetView();
            dxContext->getDevice()->OMSetRenderTargets(1, &renderTargetView, nullptr);
            dxContext->getDevice()->ClearRenderTargetView(renderTargetView, clearColor);

            // Render the ImGui draw data using DirectX
            ImGui_ImplDX10_RenderDrawData(ImGui::GetDrawData());

            // Swap the buffers
            dxContext->swapBuffers();

            // Enforce the target frame rate
            EnforceFrameRate(frameStartTime);
        }

        return 0;
    }

private:
    std::unique_ptr<Window> window;
    std::unique_ptr<DX10Context> dxContext;
    std::unique_ptr<ScopedCleanup> cleanup;
    std::unique_ptr<WindowStateTransitionManager> transitionManager;
    std::unique_ptr<TabManager> tabManager;
    std::unique_ptr<StatusBar> statusBar;
    int display_w;
    int display_h;
};

void SetupDpiAwareness()
{
    // Enable Per-Monitor DPI awareness for newer Windows
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((void*)-4)
#endif

// Try to set the highest DPI awareness available
    HMODULE user32 = GetModuleHandleA("user32.dll");
    if (user32)
    {
        typedef BOOL(WINAPI* SetProcessDpiAwarenessContextFunc)(void*);
        SetProcessDpiAwarenessContextFunc setProcessDpiAwarenessContext =
            (SetProcessDpiAwarenessContextFunc)GetProcAddress(user32, "SetProcessDpiAwarenessContext");

        if (setProcessDpiAwarenessContext)
        {
            // Try Per-Monitor V2 first (Windows 10 1703+)
            if (!setProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
            {
                // Fall back to Per-Monitor (Windows 8.1+)
                setProcessDpiAwarenessContext((void*)-3); // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE
            }
        }
        else
        {
            // For Windows 8.1
            typedef HRESULT(WINAPI* SetProcessDpiAwarenessFunc)(int);
            SetProcessDpiAwarenessFunc setProcessDpiAwareness =
                (SetProcessDpiAwarenessFunc)GetProcAddress(user32, "SetProcessDpiAwareness");

            if (setProcessDpiAwareness)
            {
                // 2 = PROCESS_PER_MONITOR_DPI_AWARE
                setProcessDpiAwareness(2);
            }
            else
            {
                // For Windows Vista through 8
                typedef BOOL(WINAPI* SetProcessDPIAwareFunc)();
                SetProcessDPIAwareFunc setProcessDPIAware =
                    (SetProcessDPIAwareFunc)GetProcAddress(user32, "SetProcessDPIAware");

                if (setProcessDPIAware)
                {
                    setProcessDPIAware();
                }
            }
        }
    }
}

#ifdef DEBUG
int main()
{
    // Set up DPI awareness before creating any window
    SetupDpiAwareness();

    try
    {
        Application app;
        return app.run();
    }
    catch (const std::exception& e)
    {
        HandleException(e);
        return 1;
    }
}
#else
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // Set up DPI awareness before creating any window
    SetupDpiAwareness();

    try
    {
        Application app;
        return app.run();
    }
    catch (const std::exception& e)
    {
        HandleException(e);
        return 1;
    }
}
#endif