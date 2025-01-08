#include "config.hpp"

#include "window/window_factory.hpp"
#include "window/graphics_context_factory.hpp"
#include "window/gradient_background.hpp"

#include "ui/fonts.hpp"
#include "ui/title_bar.hpp"
#include "ui/chat/chat_history_sidebar.hpp"
#include "ui/chat/chat_section.hpp"
#include "ui/chat/preset_sidebar.hpp"

#include "chat/chat_manager.hpp"
#include "model/preset_manager.hpp"
#include "model/model_manager.hpp"

#include "nfd.h"

#include <iostream>
#include <chrono>
#include <thread>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_opengl3.h>
#include <curl/curl.h>

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
        , isTransitioning(false)
        , targetActiveState(window.isActive())
        , previousActiveState(window.isActive()) {
    }

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
    ImGuiIO& imguiIO = ImGui::GetIO();

    // Enable power saving mode
    imguiIO.ConfigFlags |= ImGuiConfigFlags_EnablePowerSavingMode;

    // Set the rounding and border size for the window
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = Config::WINDOW_CORNER_RADIUS;
    style.WindowBorderSize = 0.0f;               // Disable ImGui's window border
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

void renderPlayground(float& chatHistorySidebarWidth, float& modelPresetSidebarWidth)
{
    renderChatHistorySidebar(chatHistorySidebarWidth);
    renderModelPresetSidebar(modelPresetSidebarWidth);
    renderChatWindow(Config::INPUT_HEIGHT, chatHistorySidebarWidth, modelPresetSidebarWidth);
}

void StartNewFrame() {
    // Start the ImGui frame
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

#ifdef DEBUG
    int main()
#else
    int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#endif
{
    try 
    {
        // Create the window
        auto window = WindowFactory::createWindow();
        window->createWindow(Config::WINDOW_WIDTH, Config::WINDOW_HEIGHT, Config::WINDOW_TITLE);
        window->show();

        // Create the OpenGL context
        auto openglContext = GraphicContextFactory::createOpenGLContext();
        openglContext->initialize(window->getNativeHandle());

        // Initialize cleanup using RAII
        ScopedCleanup cleanup;

        // Initialize ImGui
        InitializeImGui(*window);

        // Initialize the chat manager, preset manager, and model manager
        Chat::initializeChatManager();
        Model::initializePresetManager();
        Model::initializeModelManager();

		// TODO: encapsulate to have a cleaner code
        Model::ModelManager::getInstance().setStreamingCallback(
            [](const std::string& partialOutput, const int jobId) {
                // Grab the current chat by the tracked jobId:
                auto& chatManager = Chat::ChatManager::getInstance();
				std::string chatName = chatManager.getChatNameByJobId(jobId);

                // Append partial output to the *last assistant message*,
                // or create a new one if the last role isn't "assistant."
                Chat::ChatHistory chat = chatManager.getChat(chatName).value();
                if (!chat.messages.empty() && chat.messages.back().role == "assistant")
                {
                    // Append to the existing assistant message
                    chat.messages.back().content = partialOutput;
					chatManager.updateChat(chatName, chat);
                }
                else
                {
                    // If, for some reason, we don't have an assistant message to update:
                    // create a new message with role "assistant"
                    Chat::Message assistantMsg;
                    assistantMsg.id = static_cast<int>(chat.messages.size()) + 1;
                    assistantMsg.role = "assistant";
                    assistantMsg.content = partialOutput;
					chatManager.addMessage(chatName, assistantMsg);
                }
            }
        );

        // Initialize NFD (Native File Dialog)
        NFD_Init();

        // Get initial window size
        int display_w = window->getWidth();
        int display_h = window->getHeight();

        // Initialize gradient background
        InitializeGradientBackground(display_w, display_h);

        // Create window state transition manager
        WindowStateTransitionManager transitionManager(*window);

        // Initialize sidebar widths
        float chatHistorySidebarWidth = Config::ChatHistorySidebar::SIDEBAR_WIDTH;
        float modelPresetSidebarWidth = Config::ModelPresetSidebar::SIDEBAR_WIDTH;

        // Enter the main loop
        while (!window->shouldClose()) 
        {
            auto frameStartTime = std::chrono::high_resolution_clock::now();

            window->processEvents();

            // Update window state transition
            transitionManager.updateTransition();

            StartNewFrame();

            // Render title bar
            titleBar(window->getNativeHandle());

			// Render the chat section
            renderPlayground(chatHistorySidebarWidth, modelPresetSidebarWidth);

            // Render the ImGui frame
            ImGui::Render();

            // Get updated window size
            int new_display_w = window->getWidth();
            int new_display_h = window->getHeight();

            if (new_display_w != display_w || new_display_h != display_h) 
            {
                display_w = new_display_w;
                display_h = new_display_h;
                GradientBackground::generateGradientTexture(display_w, display_h);
                glViewport(0, 0, display_w, display_h);
            }

            GradientBackground::renderGradientBackground(
                display_w,
                display_h,
                transitionManager.getTransitionProgress(),
                transitionManager.getEasedProgress()
            );

            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            openglContext->swapBuffers();

            EnforceFrameRate(frameStartTime);
        }

        return 0;
    }
    catch (const std::exception& e) {
        HandleException(e);
        return 1;
    }
}