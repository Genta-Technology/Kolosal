#pragma once

#include "imgui.h"
#include "config.hpp"
#include "chat_history.hpp"
#include "ui/widgets.hpp"
#include "ui/markdown.hpp"
#include "ui/chat/model_manager_modal.hpp"
#include "chat/chat_manager.hpp"
#include "model/preset_manager.hpp"
#include "model/model_manager.hpp"

#include <iostream>
#include <inference.h>
#include <vector>
#include <string>
#include <optional>
#include <functional>

class ClearChatModalComponent {
public:
    ClearChatModalComponent() : isOpen(false) {
        // Cache button configuration for the Cancel button.
        cancelButtonConfig.id = "##cancelClearChat";
        cancelButtonConfig.label = "Cancel";
        cancelButtonConfig.backgroundColor = RGBAToImVec4(34, 34, 34, 255);
        cancelButtonConfig.hoverColor = RGBAToImVec4(53, 132, 228, 255);
        cancelButtonConfig.activeColor = RGBAToImVec4(26, 95, 180, 255);
        cancelButtonConfig.textColor = RGBAToImVec4(255, 255, 255, 255);
        cancelButtonConfig.size = ImVec2(130, 0);

        // Cache button configuration for the Confirm button.
        confirmButtonConfig.id = "##confirmClearChat";
        confirmButtonConfig.label = "Confirm";
        confirmButtonConfig.backgroundColor = RGBAToImVec4(26, 95, 180, 255);
        confirmButtonConfig.hoverColor = RGBAToImVec4(53, 132, 228, 255);
        confirmButtonConfig.activeColor = RGBAToImVec4(26, 95, 180, 255);
        confirmButtonConfig.size = ImVec2(130, 0);
    }

    // Call this to trigger the modal to open.
    void open() { isOpen = true; }

    // Call this every frame to render the modal (if open).
    void render() {
        if (!isOpen)
            return;

        ModalConfig config{
            "Confirm Clear Chat",   // unique id and title
            "Confirm Clear Chat",
            ImVec2(300, 96),
            [this]() {
                std::vector<ButtonConfig> buttons;

                // Bind the Cancel action.
                cancelButtonConfig.onClick = [this]() { isOpen = false; };
                buttons.push_back(cancelButtonConfig);

                // Bind the Confirm action.
                confirmButtonConfig.onClick = [this]() {
                    Chat::ChatManager::getInstance().clearCurrentChat();
                    isOpen = false;
                };
                buttons.push_back(confirmButtonConfig);

                Button::renderGroup(buttons, 16, ImGui::GetCursorPosY() + 8);
            },
            isOpen  // pass in our cached state instead of an external bool
        };

        // Set some modal padding.
        config.padding = ImVec2(16.0F, 8.0F);
        ModalWindow::render(config);

        // Ensure that if the popup closes (e.g. by user dismiss), our state is kept in sync.
        if (!ImGui::IsPopupOpen(config.id.c_str()))
            isOpen = false;
    }

private:
    bool isOpen;
    ButtonConfig cancelButtonConfig;
    ButtonConfig confirmButtonConfig;
};

class ChatWindow {
public:
    ChatWindow() {
        renameButtonConfig.id = "##renameChat";
        renameButtonConfig.size = ImVec2(Config::CHAT_WINDOW_CONTENT_WIDTH, 30);
        renameButtonConfig.gap = 10.0F;
        renameButtonConfig.alignment = Alignment::CENTER;
        renameButtonConfig.hoverColor = ImVec4(0.1F, 0.1F, 0.1F, 0.5F);
        renameButtonConfig.onClick = [this]() { showRenameChatDialog = true; };

        openModelManagerConfig.id = "##openModalButton";
        openModelManagerConfig.icon = ICON_CI_SPARKLE;
        openModelManagerConfig.size = ImVec2(128, 0);
        openModelManagerConfig.alignment = Alignment::LEFT;
        openModelManagerConfig.onClick = [this]() { openModelSelectionModal = true; };

        clearChatButtonConfig.id = "##clearChatButton";
        clearChatButtonConfig.icon = ICON_CI_CLEAR_ALL;
        clearChatButtonConfig.size = ImVec2(24, 0);
        clearChatButtonConfig.alignment = Alignment::CENTER;
        clearChatButtonConfig.tooltip = "Clear Chat";
        clearChatButtonConfig.onClick = [this]() { clearChatModal.open(); };

        inputPlaceholderText = "Type a message and press Enter to send (Ctrl+Enter or Shift+Enter for new line)";
    }

    // Render the chat window. This method computes layout values and then renders
    // the cached widgets, updating only the dynamic properties.
    void render(float inputHeight, float leftSidebarWidth, float rightSidebarWidth) {
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 windowSize = ImVec2(io.DisplaySize.x - rightSidebarWidth - leftSidebarWidth,
            io.DisplaySize.y - Config::TITLE_BAR_HEIGHT);

        ImGui::SetNextWindowPos(ImVec2(leftSidebarWidth, Config::TITLE_BAR_HEIGHT), ImGuiCond_Always);
        ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);

        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToFrontOnFocus;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
        ImGui::Begin("Chatbot", nullptr, windowFlags);

        // Calculate centered content region.
        float availableWidth = ImGui::GetContentRegionAvail().x;
        float contentWidth = (availableWidth < Config::CHAT_WINDOW_CONTENT_WIDTH)
            ? availableWidth
            : Config::CHAT_WINDOW_CONTENT_WIDTH;
        float paddingX = (availableWidth - contentWidth) / 2.0F;
        if (paddingX > 0.0F)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + paddingX);

        // Update and render the rename button (its label is dynamic).
        renameButtonConfig.label = Chat::ChatManager::getInstance().getCurrentChatName();
        Button::render(renameButtonConfig);

        // Render the clear chat modal.
        clearChatModal.render();

        // Spacing between widgets.
        for (int i = 0; i < 4; ++i)
            ImGui::Spacing();

        if (paddingX > 0.0F)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + paddingX);

        // Render the chat history region.
        float availableHeight = ImGui::GetContentRegionAvail().y - inputHeight - Config::BOTTOM_MARGIN;
        ImGui::BeginChild("ChatHistoryRegion", ImVec2(contentWidth, availableHeight), false, ImGuiWindowFlags_NoScrollbar);
        if (auto chat = Chat::ChatManager::getInstance().getCurrentChat())
            chatHistoryRenderer.render(*chat, contentWidth);
        ImGui::EndChild();

        ImGui::Spacing();
        float inputFieldPaddingX = (availableWidth - contentWidth) / 2.0F;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + inputFieldPaddingX);

        renderInputField(inputHeight, contentWidth);

        ImGui::End();
        ImGui::PopStyleVar();
    }

private:
    // Render the row of buttons that allow the user to switch models or clear chat.
    void renderChatFeatureButtons(float baseX, float baseY) {
        // Update the open-model manager button’s label dynamically.
        openModelManagerConfig.label =
            Model::ModelManager::getInstance().getCurrentModelName().value_or("Select Model");

        std::vector<ButtonConfig> buttons = { openModelManagerConfig, clearChatButtonConfig };
        Button::renderGroup(buttons, baseX, baseY);

        // Render the model manager modal (its internal state controls visibility).
        modelManagerModal.render(openModelSelectionModal);
    }

    // Render the input field and then place the chat feature buttons in its vicinity.
    void renderInputField(const float inputHeight, const float inputWidth) {
        // Lambda to process the user's input.
        auto processInput = [this](const std::string& input) {
            auto& chatManager = Chat::ChatManager::getInstance();
            auto currentChat = chatManager.getCurrentChat();

            if (!currentChat.has_value()) {
                std::cerr << "[ChatSection] No chat selected. Cannot send message.\n";
                return;
            }

            if (!Model::ModelManager::getInstance().getCurrentModelName().has_value()) {
                std::cerr << "[ChatSection] No model selected. Cannot send message.\n";
                return;
            }

            // Append the user message.
            Chat::Message userMessage;
            userMessage.id = static_cast<int>(currentChat.value().messages.size()) + 1;
            userMessage.role = "user";
            userMessage.content = input;
            chatManager.addMessageToCurrentChat(userMessage);

            // Build the completion parameters.
            Model::PresetManager& presetManager = Model::PresetManager::getInstance();
            Model::ModelManager& modelManager = Model::ModelManager::getInstance();

            ChatCompletionParameters completionParams;
            completionParams.messages.push_back(
                { "system", presetManager.getCurrentPreset().value().get().systemPrompt.c_str() });
            for (const auto& msg : currentChat.value().messages) {
                completionParams.messages.push_back({ msg.role.c_str(), msg.content.c_str() });
            }
            completionParams.messages.push_back({ "user", input.c_str() });

            const auto& currentPreset = presetManager.getCurrentPreset().value().get();
            completionParams.randomSeed = currentPreset.random_seed;
            completionParams.maxNewTokens = static_cast<int>(currentPreset.max_new_tokens);
            completionParams.minLength = static_cast<int>(currentPreset.min_length);
            completionParams.temperature = currentPreset.temperature;
            completionParams.topP = currentPreset.top_p;
            completionParams.streaming = true;
            completionParams.kvCacheFilePath = chatManager.getCurrentKvChatPath(
                modelManager.getCurrentModelName().value(),
                modelManager.getCurrentVariantType()
            ).value().string();

            int jobId = modelManager.startChatCompletionJob(completionParams);
            if (!chatManager.setCurrentJobId(jobId)) {
                std::cerr << "[ChatSection] Failed to set the current job ID.\n";
            }
            };

        // Construct InputFieldConfig using its proper constructor.
        InputFieldConfig inputConfig(
            "##chatinput",
            ImVec2(inputWidth, inputHeight - Config::Font::DEFAULT_FONT_SIZE - 20),
            inputTextBuffer,
            focusInputField
        );

        // Set additional configuration parameters.
        inputConfig.placeholderText = inputPlaceholderText;
        inputConfig.flags = ImGuiInputTextFlags_EnterReturnsTrue |
            ImGuiInputTextFlags_CtrlEnterForNewLine |
            ImGuiInputTextFlags_ShiftEnterForNewLine;
        inputConfig.processInput = processInput;

        // Draw a background rectangle for the input field.
        ImVec2 screenPos = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(
            screenPos,
            ImVec2(screenPos.x + inputWidth, screenPos.y + inputHeight),
            ImGui::ColorConvertFloat4ToU32(Config::InputField::INPUT_FIELD_BG_COLOR),
            Config::InputField::FRAME_ROUNDING
        );

        ImGui::BeginGroup();
        InputField::renderMultiline(inputConfig);

        // Position and render the chat feature buttons.
        ImVec2 cursorPos = ImGui::GetCursorPos();
        renderChatFeatureButtons(cursorPos.x + 10, cursorPos.y);
        ImGui::EndGroup();

        // Ensure the text buffer remains at a fixed size.
        inputTextBuffer.resize(Config::InputField::TEXT_SIZE, '\0');
    }

private:
    // Cached widget configurations.
    ButtonConfig renameButtonConfig;
    ButtonConfig openModelManagerConfig;
    ButtonConfig clearChatButtonConfig;
    std::string inputPlaceholderText;

    // State variables.
    bool showRenameChatDialog = false;
    bool openModelSelectionModal = false;
    std::string inputTextBuffer = std::string(Config::InputField::TEXT_SIZE, '\0');
    bool focusInputField = true;

    // Child components.
    ModelManagerModal modelManagerModal;
    ClearChatModalComponent clearChatModal;
    ChatHistoryRenderer chatHistoryRenderer;
};