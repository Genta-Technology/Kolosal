#pragma once

#include "imgui.h"
#include "ui/widgets.hpp"
#include "ui/chat/model_manager_modal.hpp"
#include "model/model_manager.hpp"
#include "model/server_state_manager.hpp"

#include <IconsCodicons.h>

class ServerLogViewer {
public:
    ServerLogViewer() {
        m_logBuffer = "Server logs will be displayed here.";
        m_lastLogUpdate = std::chrono::steady_clock::now();
    }

    ~ServerLogViewer() {
        // Make sure to stop the server on destruction
        if (ServerStateManager::getInstance().isServerRunning()) {
            Model::ModelManager::getInstance().stopServer();
        }
    }

    void render(const float sidebarWidth) {
        ImGuiIO& io = ImGui::GetIO();
        Model::ModelManager& modelManager = Model::ModelManager::getInstance();
        ServerStateManager& serverState = ServerStateManager::getInstance();

        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
        ImGui::SetNextWindowPos(ImVec2(0, Config::TITLE_BAR_HEIGHT), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x - sidebarWidth, io.DisplaySize.y - Config::TITLE_BAR_HEIGHT), ImGuiCond_Always);
        ImGui::Begin("Server Logs", nullptr, window_flags);
        ImGui::PopStyleVar();

        // Top bar with controls
        {
            // Start/Stop server button
            ButtonConfig serverButtonConfig;
            serverButtonConfig.id = "##server_toggle_button";

            if (serverState.isServerRunning()) {
                serverButtonConfig.label = "Stop Server";
                serverButtonConfig.icon = ICON_CI_DEBUG_STOP;
                serverButtonConfig.tooltip = "Stop the server";
            }
            else {
                serverButtonConfig.label = "Start Server";
                serverButtonConfig.icon = ICON_CI_RUN;
                serverButtonConfig.tooltip = "Start the server";
            }

            serverButtonConfig.size = ImVec2(150, 0);
            serverButtonConfig.alignment = Alignment::CENTER;
            serverButtonConfig.onClick = [this, &modelManager, &serverState]() {
                toggleServer(modelManager, serverState);
                };

            // Model selection button
            ButtonConfig selectModelButtonConfig;
            selectModelButtonConfig.id = "##server_select_model_button";
            selectModelButtonConfig.label =
                serverState.getCurrentModelName().value_or("Select Model");
            selectModelButtonConfig.tooltip =
                serverState.getCurrentModelName().value_or("Select Model");
            selectModelButtonConfig.icon = ICON_CI_SPARKLE;
            selectModelButtonConfig.size = ImVec2(180, 0);
            selectModelButtonConfig.alignment = Alignment::CENTER;
            selectModelButtonConfig.onClick = [this]() {
                m_modelManagerModalOpen = true;
                };

            if (serverState.isModelLoadInProgress()) {
                selectModelButtonConfig.label = "Loading Model...";
                serverButtonConfig.state = ButtonState::DISABLED;
            }

            if (serverState.isModelLoaded()) {
                selectModelButtonConfig.icon = ICON_CI_SPARKLE_FILLED;
            }
            else {
                serverButtonConfig.state = ButtonState::DISABLED; // Can't start server without model
            }

            std::vector<ButtonConfig> buttonConfigs = { serverButtonConfig, selectModelButtonConfig };

            // Add reload button if model params have changed
            if (serverState.haveModelParamsChanged() && serverState.isModelLoaded()) {
                ButtonConfig reloadModelButtonConfig;
                reloadModelButtonConfig.id = "##reload_model_button";
                reloadModelButtonConfig.icon = ICON_CI_REFRESH;
                reloadModelButtonConfig.tooltip = "Reload model with new parameters";
                reloadModelButtonConfig.size = ImVec2(24, 24);
                reloadModelButtonConfig.alignment = Alignment::CENTER;
				reloadModelButtonConfig.backgroundColor = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
                reloadModelButtonConfig.onClick = [this, &modelManager, &serverState]() {
					modelManager.switchModel(
						modelManager.getCurrentModelName().value(),
						modelManager.getCurrentVariantType()
					);
					serverState.resetModelParamsChanged();
                    };

                // Disable the reload button if server is running or model is loading
                if (serverState.isServerRunning() || serverState.isModelLoadInProgress()) {
                    reloadModelButtonConfig.state = ButtonState::DISABLED;
                }

                buttonConfigs.push_back(reloadModelButtonConfig);
            }

			Button::renderGroup(buttonConfigs, ImGui::GetCursorPosX(), ImGui::GetCursorPosY());

            // Show API endpoint info if server is running
            if (serverState.isServerRunning()) {
                ImGui::SameLine();

				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 40);

                ImGui::TextUnformatted("API Endpoint:");
                ImGui::SameLine();

                std::string endpoint = "http://localhost:" + serverState.getServerPortString() + "/v1/chat/completions";
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
                ImGui::TextUnformatted(endpoint.c_str());
                ImGui::PopStyleColor();

                ImGui::SameLine();
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2);
                ButtonConfig copyButtonConfig;
                copyButtonConfig.id = "##copy_endpoint_button";
                copyButtonConfig.icon = ICON_CI_COPY;
                copyButtonConfig.tooltip = "Copy endpoint to clipboard";
                copyButtonConfig.size = ImVec2(24, 24);
                copyButtonConfig.onClick = [endpoint]() {
                    ImGui::SetClipboardText(endpoint.c_str());
                    };

                Button::render(copyButtonConfig);
            }

            m_modelManagerModal.render(m_modelManagerModalOpen);
        }

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 12);

        // Update log buffer from kolosal::Logger
        updateLogBuffer();

        // Log display area
        {
            InputFieldConfig input_cfg(
                "##server_log_input",
                ImVec2(-FLT_MIN, -FLT_MIN),
                m_logBuffer,
                m_isLogFocused
            );

            input_cfg.frameRounding = 4.0f;
            input_cfg.flags = ImGuiInputTextFlags_ReadOnly;
            input_cfg.backgroundColor = ImVec4(0.2f, 0.2f, 0.2f, 0.5f);
            InputField::renderMultiline(input_cfg);

            // Auto-scroll to bottom
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f) {
                ImGui::SetScrollHereY(1.0f);
            }
        }

        ImGui::End();
    }

private:
    bool m_isLogFocused = false;
    std::string m_logBuffer;
    size_t m_lastLogIndex = 0;
    std::chrono::steady_clock::time_point m_lastLogUpdate;

    ModelManagerModal m_modelManagerModal;
    bool m_modelManagerModalOpen = false;

    void toggleServer(Model::ModelManager& modelManager, ServerStateManager& serverState) {
        if (serverState.isServerRunning()) {
            // Stop the server
            modelManager.stopServer();
            serverState.setServerRunning(false);
        }
        else {
            // Start the server
            if (serverState.isModelLoaded()) {
                if (modelManager.startServer(serverState.getServerPortString())) {
                    serverState.setServerRunning(true);
                    addToLogBuffer("Server started on port " + serverState.getServerPortString());
                }
                else {
                    addToLogBuffer("Failed to start server on port " + serverState.getServerPortString());
                }
            }
            else {
                addToLogBuffer("Error: Cannot start server without a loaded model");
            }
        }
    }

    void updateLogBuffer() {
        // Check if it's time to update (limit updates to reduce performance impact)
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastLogUpdate).count() < 100) {
            return;
        }
        m_lastLogUpdate = now;

        // Get logs from the kolosal::Logger
        const auto& logs = Logger::instance().getLogs();

        // If there are new logs, add them to our buffer
        if (logs.size() > m_lastLogIndex) {
            for (size_t i = m_lastLogIndex; i < logs.size(); i++) {
                const auto& entry = logs[i];
                std::string levelPrefix;

                switch (entry.level) {
                case LogLevel::SERVER_ERROR:
                    levelPrefix = "[ERROR] ";
                    break;
                case LogLevel::SERVER_WARNING:
                    levelPrefix = "[WARNING] ";
                    break;
                case LogLevel::SERVER_INFO:
                    levelPrefix = "[INFO] ";
                    break;
                case LogLevel::SERVER_DEBUG:
                    levelPrefix = "[DEBUG] ";
                    break;
                default:
                    levelPrefix = "[LOG] ";
                }

                addToLogBuffer(levelPrefix + entry.message);
            }

            m_lastLogIndex = logs.size();
        }
    }

    void addToLogBuffer(const std::string& message) {
        // Add timestamp
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::tm* tm = std::localtime(&time_t);

        char timestamp[32];
        std::strftime(timestamp, sizeof(timestamp), "[%H:%M:%S] ", tm);

        // Add to buffer with newline if not empty
        if (!m_logBuffer.empty() && m_logBuffer != "Server logs will be displayed here.") {
            m_logBuffer += "\n";
        }
        else if (m_logBuffer == "Server logs will be displayed here.") {
            m_logBuffer = ""; // Clear the initial message
        }

        m_logBuffer += std::string(timestamp) + message;
    }
};