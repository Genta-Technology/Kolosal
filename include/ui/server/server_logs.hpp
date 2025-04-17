#pragma once

#include "imgui.h"
#include "ui/widgets.hpp"
#include "ui/model_manager_modal.hpp"
#include "ui/server/server_model_list.hpp"
#include "model/model_manager.hpp"
#include "model/server_state_manager.hpp"

#include <IconsCodicons.h>
#include <vector>
#include <string>
#include <functional>
#include <unordered_map>

class APIEndpointModal {
public:
    // Constructor initializes button configurations
    APIEndpointModal() {
        initializeButtonConfigs();
    }

    // Main render method for the modal
    void render(bool& openModal, const std::string& modelName) {
        ModalConfig config{
            "API Endpoints",
            "API Endpoints",
            ImVec2(512, 512),
            [this, modelName]() { renderContent(modelName); },
            openModal
        };
        config.padding = ImVec2(16.0f, 8.0f);
        ModalWindow::render(config);

        if (!ImGui::IsPopupOpen(config.id.c_str())) {
            openModal = false;
        }
    }

private:
    // Structure to hold endpoint information
    struct EndpointInfo {
        std::string method;        // "GET" or "POST"
        std::string path;          // e.g., "/models" or "/completions"
        std::string url;           // Full URL
        int codeIndex;             // Index in code examples array
        bool needsRequestBody;     // Whether this endpoint needs a request body
    };

    ButtonConfig m_getButtonConfig;
    ButtonConfig m_postButtonConfig;

    int  m_showCodeIndex = 0;   // Which endpoint is selected
    int  m_showCodeMethod = 0;   // 0=cURL, 1=OpenAI Python, 2=JSON
    bool m_focusCodeBlock = false;

    const float BUTTON_WIDTH = 64.0f;
    const float SPACING = 8.0f;
    const float INDENT = 16.0f;

    // Initialize button configurations
    void initializeButtonConfigs() {
        m_getButtonConfig.id = "##GET";
        m_getButtonConfig.label = "GET";
        m_getButtonConfig.backgroundColor = RGBAToImVec4(26, 95, 180, 128);
        m_getButtonConfig.hoverColor = RGBAToImVec4(26, 95, 180, 128);
        m_getButtonConfig.activeColor = RGBAToImVec4(26, 95, 180, 128);
        m_getButtonConfig.size = ImVec2(BUTTON_WIDTH, 0);
        m_getButtonConfig.textColor = RGBAToImVec4(255, 255, 255, 255);

        m_postButtonConfig.id = "##POST";
        m_postButtonConfig.label = "POST";
        m_postButtonConfig.backgroundColor = RGBAToImVec4(136, 212, 78, 128);
        m_postButtonConfig.hoverColor = RGBAToImVec4(136, 212, 78, 128);
        m_postButtonConfig.activeColor = RGBAToImVec4(136, 212, 78, 128);
        m_postButtonConfig.size = ImVec2(BUTTON_WIDTH, 0);
        m_postButtonConfig.textColor = RGBAToImVec4(255, 255, 255, 255);
    }

    // Main content renderer
    void renderContent(const std::string& modelName) {
        ImGui::BeginGroup();

        // Define all endpoints
        const std::vector<EndpointInfo> endpoints = {
            {"GET",  "models",              "http://localhost:8080/models",                0, false},
            {"POST", "completions",         "http://localhost:8080/completions",           1, true},
            {"POST", "v1_completions",      "http://localhost:8080/v1/completions",        2, true},
            {"POST", "chat_completions",    "http://localhost:8080/chat/completions",      3, true},
            {"POST", "v1_chat_completions", "http://localhost:8080/v1/chat/completions",   4, true}
        };

        // Render each endpoint
        for (size_t i = 0; i < endpoints.size(); i++) {
            renderEndpoint(endpoints[i]);

            if (i < endpoints.size() - 1) {
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + SPACING);
            }
        }

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + SPACING * 2);

        // Render code format selection tabs
        renderCodeFormatTabs();

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + SPACING);

        // Render code display area
        renderCodeDisplay(modelName);

        ImGui::EndGroup();
    }

    // Render a single endpoint button row
    void renderEndpoint(const EndpointInfo& endpoint) {
        ImGui::BeginGroup();

        // Render method button (GET/POST)
        ButtonConfig methodButtonConfig =
            endpoint.method == "GET" ? m_getButtonConfig : m_postButtonConfig;
        methodButtonConfig.id += endpoint.path;
        Button::render(methodButtonConfig);

        ImGui::SameLine();

        // Render URL button
        ButtonConfig urlButtonConfig;
        urlButtonConfig.id = "##" + endpoint.method + "_" + endpoint.path + "_url";
        urlButtonConfig.label = endpoint.url;
        urlButtonConfig.icon = ICON_CI_CODE;
        urlButtonConfig.textColor = RGBAToImVec4(200, 200, 238, 255);
        urlButtonConfig.size = ImVec2(ImGui::GetContentRegionAvail().x - BUTTON_WIDTH, 0);
        urlButtonConfig.alignment = Alignment::LEFT;
        urlButtonConfig.onClick = [this, index = endpoint.codeIndex]() {
            m_showCodeIndex = index;
            };

        if (m_showCodeIndex == endpoint.codeIndex) {
            urlButtonConfig.state = ButtonState::ACTIVE;
        }

        // Adjust position for better alignment
        float xIndent = endpoint.method == "GET" ? INDENT + 4.0f : INDENT;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + xIndent);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3);
        Button::render(urlButtonConfig);

        ImGui::SameLine();

        // Render copy button
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 42);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3);

        ButtonConfig copyButtonConfig;
        copyButtonConfig.id = "##copy_" + urlButtonConfig.id;
        copyButtonConfig.icon = ICON_CI_COPY;
        copyButtonConfig.size = ImVec2(24, 0);
        copyButtonConfig.onClick = [url = urlButtonConfig.label]() {
            ImGui::SetClipboardText(url.value().c_str());
            };
        Button::render(copyButtonConfig);

        ImGui::EndGroup();
    }

    // Render tabs for different code formats (cURL, OpenAI, JSON)
    void renderCodeFormatTabs() {
        // Define button configurations
        ButtonConfig curlButtonConfig;
        curlButtonConfig.id = "##curl";
        curlButtonConfig.label = "cURL";
        curlButtonConfig.size = ImVec2(BUTTON_WIDTH, 0);
        curlButtonConfig.onClick = [this]() { m_showCodeMethod = 0; };
        if (m_showCodeMethod == 0)
            curlButtonConfig.state = ButtonState::ACTIVE;

        ButtonConfig openaiButtonConfig;
        openaiButtonConfig.id = "##openai";
        openaiButtonConfig.label = "OpenAI";
        openaiButtonConfig.size = ImVec2(BUTTON_WIDTH, 0);
        openaiButtonConfig.onClick = [this]() { m_showCodeMethod = 1; };
        if (m_showCodeMethod == 1)
            openaiButtonConfig.state = ButtonState::ACTIVE;

        ButtonConfig jsonButtonConfig;
        jsonButtonConfig.id = "##json";
        jsonButtonConfig.label = "JSON";
        jsonButtonConfig.size = ImVec2(BUTTON_WIDTH, 0);
        jsonButtonConfig.onClick = [this]() { m_showCodeMethod = 2; };
        if (m_showCodeMethod == 2)
            jsonButtonConfig.state = ButtonState::ACTIVE;

        Button::renderGroup({ curlButtonConfig, openaiButtonConfig, jsonButtonConfig },
            ImGui::GetCursorPosX(), ImGui::GetCursorPosY());
    }

    // Render code example display area
    void renderCodeDisplay(const std::string& modelName) {
        auto codeExamples = getCodeExamples(modelName);

        InputFieldConfig input_cfg(
            "##code_input",
            ImVec2(ImGui::GetContentRegionAvail().x - 16, ImGui::GetContentRegionAvail().y - 8),
            codeExamples[m_showCodeIndex][m_showCodeMethod],
            m_focusCodeBlock
        );

        input_cfg.frameRounding = 4.0f;
        input_cfg.flags = ImGuiInputTextFlags_ReadOnly;
        InputField::renderMultiline(input_cfg);
    }

    // Get code examples for all endpoints and formats
    std::vector<std::vector<std::string>> getCodeExamples(const std::string& modelName) {
        return {
            {
                // GET /models
                "curl http://localhost:8080/models",
                "", // No OpenAI Python example
                ""  // No JSON example
            },
            {
                // POST /completions
                "curl http://localhost:8080/completions \\\n"
                "  -H \"Content-Type: application/json\" \\\n"
                "  -d '{\n"
                "        \"model\": \"" + modelName + "\",\n"
                "        \"prompt\": \"Once upon a time\",\n"
                "        \"temperature\": 1.0,\n"
                "        \"max_tokens\": 50,\n"
                "        \"stream\": false,\n"
                "        \"top_p\": 1.0,\n"
                "        \"n\": 1\n"
                "      }'",

            // OpenAI Python example
            "import openai\n\n"
            "# Set the base URL for the OpenAI API\n"
            "openai.api_base = 'http://localhost:8080'\n\n"
            "response = openai.Completion.create(\n"
            "    model='" + modelName + "',\n"
            "    prompt='Once upon a time',\n"
            "    temperature=1.0,\n"
            "    max_tokens=50\n"
            ")\n"
            "print(response)",

            // JSON payload example
            "{\n"
            "  \"model\": \"" + modelName + "\",\n"
            "  \"prompt\": \"Once upon a time\",\n"
            "  \"temperature\": 1.0,\n"
            "  \"max_tokens\": 50,\n"
            "  \"stream\": false,\n"
            "  \"top_p\": 1.0,\n"
            "  \"n\": 1\n"
            "}"
        },
        {
            // POST /v1/completions
            "curl http://localhost:8080/v1/completions \\\n"
            "  -H \"Content-Type: application/json\" \\\n"
            "  -d '{\n"
            "        \"model\": \"" + modelName + "\",\n"
            "        \"prompt\": \"Once upon a time\",\n"
            "        \"temperature\": 1.0,\n"
            "        \"max_tokens\": 50,\n"
            "        \"stream\": false,\n"
            "        \"top_p\": 1.0,\n"
            "        \"n\": 1\n"
            "      }'",

            // OpenAI Python example
            "import openai\n\n"
            "# Set the base URL for the OpenAI API\n"
            "openai.api_base = 'http://localhost:8080'\n\n"
            "response = openai.Completion.create(\n"
            "    model='" + modelName + "',\n"
            "    prompt='Once upon a time',\n"
            "    temperature=1.0,\n"
            "    max_tokens=50\n"
            ")\n"
            "print(response)",

            // JSON payload example
            "{\n"
            "  \"model\": \"" + modelName + "\",\n"
            "  \"prompt\": \"Once upon a time\",\n"
            "  \"temperature\": 1.0,\n"
            "  \"max_tokens\": 50,\n"
            "  \"stream\": false,\n"
            "  \"top_p\": 1.0,\n"
            "  \"n\": 1\n"
            "}"
        },
        {
            // POST /chat/completions
            "curl http://localhost:8080/chat/completions \\\n"
            "  -H \"Content-Type: application/json\" \\\n"
            "  -d '{\n"
            "        \"model\": \"" + modelName + "\",\n"
            "        \"messages\": [{\"role\": \"user\", \"content\": \"Hello, world!\"}],\n"
            "        \"temperature\": 1.0,\n"
            "        \"stream\": false,\n"
            "        \"top_p\": 1.0,\n"
            "        \"n\": 1\n"
            "      }'",

            // OpenAI Python example
            "import openai\n\n"
            "# Set the base URL for the OpenAI API\n"
            "openai.api_base = 'http://localhost:8080'\n\n"
            "response = openai.ChatCompletion.create(\n"
            "    model='" + modelName + "',\n"
            "    messages=[{'role': 'user', 'content': 'Hello, world!'}],\n"
            "    temperature=1.0\n"
            ")\n"
            "print(response)",

            // JSON payload example
            "{\n"
            "  \"model\": \"" + modelName + "\",\n"
            "  \"messages\": [\n"
            "    {\"role\": \"user\", \"content\": \"Hello, world!\"}\n"
            "  ],\n"
            "  \"temperature\": 1.0,\n"
            "  \"stream\": false,\n"
            "  \"top_p\": 1.0,\n"
            "  \"n\": 1\n"
            "}"
        },
        {
            // POST /v1/chat/completions
            "curl http://localhost:8080/v1/chat/completions \\\n"
            "  -H \"Content-Type: application/json\" \\\n"
            "  -d '{\n"
            "        \"model\": \"" + modelName + "\",\n"
            "        \"messages\": [{\"role\": \"user\", \"content\": \"Hello, world!\"}],\n"
            "        \"temperature\": 1.0,\n"
            "        \"stream\": false,\n"
            "        \"top_p\": 1.0,\n"
            "        \"n\": 1\n"
            "      }'",

            // OpenAI Python example
            "import openai\n\n"
            "# Set the base URL for the OpenAI API\n"
            "openai.api_base = 'http://localhost:8080'\n\n"
            "response = openai.ChatCompletion.create(\n"
            "    model='" + modelName + "',\n"
            "    messages=[{'role': 'user', 'content': 'Hello, world!'}],\n"
            "    temperature=1.0\n"
            ")\n"
            "print(response)",

            // JSON payload example
            "{\n"
            "  \"model\": \"" + modelName + "\",\n"
            "  \"messages\": [\n"
            "    {\"role\": \"user\", \"content\": \"Hello, world!\"}\n"
            "  ],\n"
            "  \"temperature\": 1.0,\n"
            "  \"stream\": false,\n"
            "  \"top_p\": 1.0,\n"
            "  \"n\": 1\n"
            "}"
        }
        };
    }
};

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
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x - sidebarWidth, io.DisplaySize.y - Config::TITLE_BAR_HEIGHT - Config::FOOTER_HEIGHT), ImGuiCond_Always);
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
            selectModelButtonConfig.label = "Load model";
            selectModelButtonConfig.tooltip = "Load model into server";
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

            if (serverState.isModelLoaded() || serverState.isServerRunning()) {
                selectModelButtonConfig.icon = ICON_CI_SPARKLE_FILLED;
            }
            else {
                serverButtonConfig.state = ButtonState::DISABLED; // Can't start server without model
            }

            std::vector<ButtonConfig> buttonConfigs = { serverButtonConfig, selectModelButtonConfig };

			Button::renderGroup(buttonConfigs, ImGui::GetCursorPosX(), ImGui::GetCursorPosY());

            // Show API endpoint info if server is running
            if (serverState.isServerRunning()) {
                ImGui::SameLine();

				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 40);
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 4);

                ButtonConfig openEndpointButtonConfig;
                openEndpointButtonConfig.id = "##open_endpoint";
                openEndpointButtonConfig.label = "API Endpoints Code";
                openEndpointButtonConfig.icon = ICON_CI_LINK;
                openEndpointButtonConfig.tooltip = "See API Endpoints List and Code Examples";
                openEndpointButtonConfig.size = ImVec2(180, 24);
                openEndpointButtonConfig.onClick = [this]() {
					m_apiEndpointModalOpen = true;
                    };

                Button::render(openEndpointButtonConfig);

                std::string modelName = "qwen2.5-0.5b";
                if (!modelManager.getModelNamesInServer().empty())
                    modelName = modelManager.getModelNamesInServer()[0];

                m_apiEndpointModal.render(m_apiEndpointModalOpen, modelName);
            }

            m_modelManagerModal.render(m_modelManagerModalOpen, false);
        }

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 12);

		// Server model list
        {
			m_serverModelList.render(300);
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

	APIEndpointModal m_apiEndpointModal;
	bool m_apiEndpointModalOpen = false;

	ServerModelList m_serverModelList;

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