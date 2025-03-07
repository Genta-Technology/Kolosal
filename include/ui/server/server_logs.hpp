#pragma once

#include "imgui.h"
#include "ui/widgets.hpp"
#include "ui/chat/model_manager_modal.hpp"
#include "model/model_manager.hpp"

#include <IconsCodicons.h>

class ServerLogViewer {
public:
    ServerLogViewer() {
        m_logBuffer = "Server logs will be displayed here.";
        m_serverPort = "8080"; // Default port
        m_serverRunning = false;
        m_lastLogUpdate = std::chrono::steady_clock::now();
    }

    ~ServerLogViewer() {
        // Make sure to stop the server on destruction
        if (m_serverRunning) {
            Model::ModelManager::getInstance().stopServer();
        }
    }

    void render(const float sidebarWidth) {
        ImGuiIO& io = ImGui::GetIO();
        Model::ModelManager& modelManager = Model::ModelManager::getInstance();

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
            // Server status indicator
            ImGui::TextUnformatted("Status:");
            ImGui::SameLine();

            if (m_serverRunning) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
                ImGui::TextUnformatted("Running");
            }
            else {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
                ImGui::TextUnformatted("Stopped");
            }
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 20);

            // Port input field
            ImGui::TextUnformatted("Port:");
            ImGui::SameLine();

            //InputFieldConfig portConfig;
            //portConfig.id = "##server_port";
            //portConfig.size = ImVec2(100, 0);
            //portConfig.text = m_serverPort;
            //portConfig.onTextChanged = [this](const std::string& text) {
            //    m_serverPort = text;
            //    };
            //portConfig.readOnly = m_serverRunning; // Can't change port while server is running
            //InputField::render(portConfig);

            ImGui::SameLine(0, 20);

            // Start/Stop server button
            ButtonConfig serverButtonConfig;
            serverButtonConfig.id = "##server_toggle_button";

            if (m_serverRunning) {
                serverButtonConfig.label = "Stop Server";
                serverButtonConfig.icon = ICON_CI_DEBUG_STOP;
                serverButtonConfig.tooltip = "Stop the server";
                serverButtonConfig.backgroundColor = ImVec4(0.8f, 0.2f, 0.2f, 1.0f);
            }
            else {
                serverButtonConfig.label = "Start Server";
                serverButtonConfig.icon = ICON_CI_RUN;
                serverButtonConfig.tooltip = "Start the server";
                serverButtonConfig.backgroundColor = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
            }

            serverButtonConfig.size = ImVec2(150, 0);
            serverButtonConfig.alignment = Alignment::CENTER;
            serverButtonConfig.onClick = [this, &modelManager]() {
                toggleServer(modelManager);
                };

            // Model selection button
            ButtonConfig selectModelButtonConfig;
            selectModelButtonConfig.id = "##server_select_model_button";
            selectModelButtonConfig.label =
                modelManager.getCurrentModelName().value_or("Select Model");
            selectModelButtonConfig.tooltip =
                modelManager.getCurrentModelName().value_or("Select Model");
            selectModelButtonConfig.icon = ICON_CI_SPARKLE;
            selectModelButtonConfig.size = ImVec2(180, 0);
            selectModelButtonConfig.alignment = Alignment::CENTER;
            selectModelButtonConfig.onClick = [this]() {
                m_modelManagerModalOpen = true;
                };

            if (modelManager.isLoadInProgress()) {
                selectModelButtonConfig.label = "Loading Model...";
                serverButtonConfig.state = ButtonState::DISABLED;
            }

            if (modelManager.isModelLoaded()) {
                selectModelButtonConfig.icon = ICON_CI_SPARKLE_FILLED;
            }
            else {
                serverButtonConfig.state = ButtonState::DISABLED; // Can't start server without model
            }

            Button::render(serverButtonConfig);
            ImGui::SameLine(0, 10);
            Button::render(selectModelButtonConfig);

            // Show API endpoint info if server is running
            if (m_serverRunning) {
                ImGui::SameLine(0, 20);
                ImGui::TextUnformatted("API Endpoint:");
                ImGui::SameLine();

                std::string endpoint = "http://localhost:" + m_serverPort + "/v1/chat/completions";
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
                ImGui::TextUnformatted(endpoint.c_str());
                ImGui::PopStyleColor();
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
    std::string m_serverPort;
    bool m_serverRunning;
    size_t m_lastLogIndex = 0;
    std::chrono::steady_clock::time_point m_lastLogUpdate;

    ModelManagerModal m_modelManagerModal;
    bool m_modelManagerModalOpen = false;

    void toggleServer(Model::ModelManager& modelManager) {
        if (m_serverRunning) {
            // Stop the server
            modelManager.stopServer();
            m_serverRunning = false;
        }
        else {
            // Start the server
            if (modelManager.isModelLoaded()) {
                if (modelManager.startServer(m_serverPort)) {
                    m_serverRunning = true;
                    addToLogBuffer("Server started on port " + m_serverPort);
                }
                else {
                    addToLogBuffer("Failed to start server on port " + m_serverPort);
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