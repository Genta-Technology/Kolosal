#include "config.hpp"

#include "window/window_factory.hpp"
#include "window/graphics_context_factory.hpp"
#include "window/gradient_background.hpp"

#include "ui/fonts.hpp"
#include "ui/title_bar.hpp"
#include "ui/tab_manager.hpp"
#include "ui/chat/chat_history_sidebar.hpp"
#include "ui/chat/chat_window.hpp"
#include "ui/chat/preset_sidebar.hpp"

#include "chat/chat_manager.hpp"
#include "model/preset_manager.hpp"
#include "model/model_manager.hpp"

#include "nfd.h"

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_opengl3.h>
#include <curl/curl.h>
#include <chrono>
#include <thread>
#include <memory>
#include <vector>
#include <exception>
#include <iostream>

class ScopedCleanup
{
public:
    ~ScopedCleanup()
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        GradientBackground::CleanUp();

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

void InitializeImGui(Window& window)
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
    ImGui_ImplOpenGL3_Init("#version 330");
}

void InitializeGradientBackground(int display_w, int display_h)
{
    GradientBackground::generateGradientTexture(display_w, display_h);
    g_shaderProgram = GradientBackground::createShaderProgram(g_quadVertexShaderSource, g_quadFragmentShaderSource);
    GradientBackground::setupFullScreenQuad();
}

void StartNewFrame() {
    ImGui_ImplOpenGL3_NewFrame();
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
        // Create and show the window
        window = WindowFactory::createWindow();
        window->createWindow(Config::WINDOW_WIDTH, Config::WINDOW_HEIGHT, Config::WINDOW_TITLE);
        window->show();

        // Create and initialize the OpenGL context
        openglContext = GraphicContextFactory::createOpenGLContext();
        openglContext->initialize(window->getNativeHandle());

        // Initialize cleanup (RAII)
        cleanup = std::make_unique<ScopedCleanup>();

        // Initialize ImGui
        InitializeImGui(*window);

        // Initialize the chat, preset, and model managers
        Chat::initializeChatManager();
        Model::initializePresetManager();
        Model::initializeModelManager();

        // Initialize Native File Dialog
        NFD_Init();

        // Get the initial window dimensions
        display_w = window->getWidth();
        display_h = window->getHeight();

        // Initialize gradient background
        InitializeGradientBackground(display_w, display_h);

        // Create the window state transition manager
        transitionManager = std::make_unique<WindowStateTransitionManager>(*window);

        // Initialize the TabManager and add the ChatTab (other tabs can be added similarly)
        tabManager = std::make_unique<TabManager>();
        tabManager->addTab(std::make_unique<ChatTab>());
    }

    int run()
    {
        while (!window->shouldClose())
        {
            auto frameStartTime = std::chrono::high_resolution_clock::now();

            window->processEvents();

            // Update window state transitions
            transitionManager->updateTransition();

            StartNewFrame();

            // Render the custom title bar
            titleBar(window->getNativeHandle());

            // Render the currently active tab (chat tab in this example)
            tabManager->renderCurrentTab();

            // Render ImGui
            ImGui::Render();

            // Check for window resizing and update viewport/gradient texture accordingly
            int new_display_w = window->getWidth();
            int new_display_h = window->getHeight();
            if (new_display_w != display_w || new_display_h != display_h)
            {
                display_w = new_display_w;
                display_h = new_display_h;
                GradientBackground::generateGradientTexture(display_w, display_h);
                glViewport(0, 0, display_w, display_h);
            }

            // Render the gradient background with transition effects
            GradientBackground::renderGradientBackground(
                display_w,
                display_h,
                transitionManager->getTransitionProgress(),
                transitionManager->getEasedProgress()
            );

            // Render the ImGui draw data using OpenGL
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            // Swap the buffers
            openglContext->swapBuffers();

            // Enforce the target frame rate
            EnforceFrameRate(frameStartTime);
        }

        return 0;
    }

private:
    std::unique_ptr<Window> window;
    std::unique_ptr<GraphicsContext> openglContext;
    std::unique_ptr<ScopedCleanup> cleanup;
    std::unique_ptr<WindowStateTransitionManager> transitionManager;
    std::unique_ptr<TabManager> tabManager;
    int display_w;
    int display_h;
};

#ifdef DEBUG
int main()
#else
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#endif
{
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