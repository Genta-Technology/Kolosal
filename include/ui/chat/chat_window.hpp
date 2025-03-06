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

class RenameChatModalComponent {
public:
    RenameChatModalComponent() : isOpen(false) {
        // Cache the new chat name buffer
        newChatName = std::string(Config::InputField::TEXT_SIZE, '\0');
    }

    // Call this to trigger the modal to open
    void open() {
        isOpen = true;
        focusInput = true;

        // Initialize with current chat name
        auto currentChatName = Chat::ChatManager::getInstance().getCurrentChatName();
        if (currentChatName.has_value()) {
            newChatName = currentChatName.value();
        }
    }

    // Call this every frame to render the modal (if open)
    void render() {
        if (!isOpen)
            return;

        ModalConfig config{
            "Rename Chat",    // unique id and title
            "Rename Chat",
            ImVec2(300, 98),
            [this]() {
                // Update input configuration
                InputFieldConfig inputConfig(
                    "##newchatname",
                    ImVec2(ImGui::GetWindowSize().x - 32.0F, 0),
                    newChatName,
                    focusInput
                );
                inputConfig.flags = ImGuiInputTextFlags_EnterReturnsTrue;
                inputConfig.frameRounding = 5.0F;

                // Set up the input processing callback
                inputConfig.processInput = [this](const std::string& input) {
                    Chat::ChatManager::getInstance().renameCurrentChat(input);
                    isOpen = false;
                    newChatName.clear();
                };

                // Render the input field
                InputField::render(inputConfig);

                // Reset focus flag after first render
                if (focusInput)
                    focusInput = false;
            },
            isOpen  // pass in our cached state instead of an external bool
        };

        // Set modal padding
        config.padding = ImVec2(16.0F, 8.0F);
        ModalWindow::render(config);

        // Ensure that if the popup closes (e.g. by user dismiss), our state is kept in sync
        if (!ImGui::IsPopupOpen(config.id.c_str())) {
            isOpen = false;
            newChatName.clear();
        }
    }

private:
    bool isOpen;
    bool focusInput;
    std::string newChatName;
};

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
        renameButtonConfig.onClick = [this]() { renameChatModal.open(); };

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

        sendButtonConfig.id = "##sendButton";
        sendButtonConfig.icon = ICON_CI_SEND;
        sendButtonConfig.size = ImVec2(24, 0);
        sendButtonConfig.alignment = Alignment::CENTER;
        sendButtonConfig.tooltip = "Send Message";

        inputPlaceholderText = "Type a message and press Enter to send (Ctrl+Enter or Shift+Enter for new line)";
    }

    // Render the chat window. This method computes layout values and then renders
    // the cached widgets, updating only the dynamic properties.
    void render(float leftSidebarWidth, float rightSidebarWidth) {
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

		// Render the rename chat modal.
        renameChatModal.render();

        // Spacing between widgets.
        for (int i = 0; i < 4; ++i)
            ImGui::Spacing();

        if (paddingX > 0.0F)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + paddingX);

        // Render the chat history region.
        float availableHeight = ImGui::GetContentRegionAvail().y - m_inputHeight - Config::BOTTOM_MARGIN;
        ImGui::BeginChild("ChatHistoryRegion", ImVec2(contentWidth, availableHeight), false, ImGuiWindowFlags_NoScrollbar);
        if (auto chat = Chat::ChatManager::getInstance().getCurrentChat())
            chatHistoryRenderer.render(*chat, contentWidth);
        ImGui::EndChild();

        ImGui::Spacing();
        float inputFieldPaddingX = (availableWidth - contentWidth) / 2.0F;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + inputFieldPaddingX);

        renderInputField(contentWidth);

        ImGui::End();
        ImGui::PopStyleVar();
    }

private:
    static void chatStreamingCallback(const std::string& partialOutput, const float tps, const int jobId, const bool isFinished) {
        auto& chatManager = Chat::ChatManager::getInstance();
        auto& modelManager = Model::ModelManager::getInstance();
        std::string chatName = chatManager.getChatNameByJobId(jobId);

        if (isFinished) modelManager.setModelGenerationInProgress(false);

        auto chatOpt = chatManager.getChat(chatName);
        if (chatOpt) {
            Chat::ChatHistory chat = chatOpt.value();
            if (!chat.messages.empty() && chat.messages.back().role == "assistant") {
                // Append to existing assistant message
                chat.messages.back().content = partialOutput;
                chat.messages.back().tps = tps;
                chatManager.updateChat(chatName, chat);
            }
            else {
                // Create new assistant message
                Chat::Message assistantMsg;
                assistantMsg.id = static_cast<int>(chat.messages.size()) + 1;
                assistantMsg.role = "assistant";
                assistantMsg.content = partialOutput;
                assistantMsg.tps = tps;
                assistantMsg.modelName = modelManager.getCurrentModelName().value_or("idk") + " | "
                    + modelManager.getCurrentVariantType();
                chatManager.addMessage(chatName, assistantMsg);
            }
        }
    }

    // Render the row of buttons that allow the user to switch models or clear chat.
    void renderChatFeatureButtons(float baseX, float baseY) {
		Model::ModelManager& modelManager = Model::ModelManager::getInstance();

        // Update the open-model manager button’s label dynamically.
        openModelManagerConfig.label =
            modelManager.getCurrentModelName().value_or("Select Model");
		openModelManagerConfig.tooltip =
			modelManager.getCurrentModelName().value_or("Select Model");

        if (modelManager.isLoadInProgress())
        {
            openModelManagerConfig.label = "Loading Model...";
        }

        if (modelManager.isModelLoaded())
        {
			openModelManagerConfig.icon = ICON_CI_SPARKLE_FILLED;
        }

        std::vector<ButtonConfig> buttons = { openModelManagerConfig, clearChatButtonConfig };
        Button::renderGroup(buttons, baseX, baseY);

        // Render the model manager modal (its internal state controls visibility).
        modelManagerModal.render(openModelSelectionModal);
    }

    void handleUserMessage(const std::string& message) {
        auto& chatManager = Chat::ChatManager::getInstance();
        auto currentChatOpt = chatManager.getCurrentChat();
        if (!currentChatOpt.has_value()) {
            std::cerr << "[ChatSection] No chat selected. Cannot send message.\n";
            return;
        }

        if (!Model::ModelManager::getInstance().getCurrentModelName().has_value()) {
            std::cerr << "[ChatSection] No model selected. Cannot send message.\n";
            return;
        }

        auto& currentChat = currentChatOpt.value();

        // Append the user message.
        Chat::Message userMessage;
        userMessage.id = static_cast<int>(currentChat.messages.size()) + 1;
        userMessage.role = "user";
        userMessage.content = message;
        chatManager.addMessageToCurrentChat(userMessage);

        // Build the completion parameters.
        ChatCompletionParameters completionParams = Model::ModelManager::getInstance().
            buildChatCompletionParameters(currentChat, message);

        auto& modelManager = Model::ModelManager::getInstance();
        int jobId = modelManager.startChatCompletionJob(completionParams, chatStreamingCallback);
        if (!chatManager.setCurrentJobId(jobId)) {
            std::cerr << "[ChatSection] Failed to set the current job ID.\n";
        }

        modelManager.setModelGenerationInProgress(true);
    }

    InputFieldConfig createInputFieldConfig(
        const float inputWidth,
        std::function<void(const std::string&)> processInput
    ) {
        InputFieldConfig config(
            "##chatinput",
            ImVec2(inputWidth, m_inputHeight - Config::Font::DEFAULT_FONT_SIZE - 20),
            inputTextBuffer,
            focusInputField
        );
        config.placeholderText = inputPlaceholderText;
        // Default flags (may be adjusted later based on generation state).
        config.flags = ImGuiInputTextFlags_CtrlEnterForNewLine | ImGuiInputTextFlags_ShiftEnterForNewLine;
        config.processInput = processInput;
        return config;
    }

    void configureSendButton(InputFieldConfig& inputConfig) {
        auto& modelManager = Model::ModelManager::getInstance();

        if (!modelManager.isCurrentlyGenerating()) {
            // Enable the input field to process an Enter key.
            inputConfig.flags = ImGuiInputTextFlags_EnterReturnsTrue |
                ImGuiInputTextFlags_CtrlEnterForNewLine |
                ImGuiInputTextFlags_ShiftEnterForNewLine;
            inputConfig.processInput = [this](const std::string& input) {
                handleUserMessage(input);
                };

            // Configure the send button for starting generation.
            sendButtonConfig.icon = ICON_CI_SEND;
            sendButtonConfig.tooltip = "Start generation";
            sendButtonConfig.onClick = [this]() {
                handleUserMessage(inputTextBuffer);
                };

            sendButtonConfig.state = (strlen(inputTextBuffer.data()) == 0)
                ? ButtonState::DISABLED
                : ButtonState::NORMAL;
        }
        else {
            // When a job is running, disable input processing.
            inputConfig.flags = ImGuiInputTextFlags_CtrlEnterForNewLine |
                ImGuiInputTextFlags_ShiftEnterForNewLine;
            inputConfig.processInput = nullptr;

            // Configure the send button for stopping generation.
            sendButtonConfig.icon = ICON_CI_DEBUG_STOP;
            sendButtonConfig.tooltip = "Stop generation";
            sendButtonConfig.onClick = []() {
                Model::ModelManager::getInstance().stopJob(
                    Chat::ChatManager::getInstance().getCurrentJobId()
                );
                };
            sendButtonConfig.state = ButtonState::NORMAL;
        }

		// Disable the send button and input processing if no model is loaded.
		if (!modelManager.isModelLoaded()) {
            inputConfig.flags = ImGuiInputTextFlags_CtrlEnterForNewLine |
                ImGuiInputTextFlags_ShiftEnterForNewLine;
            inputConfig.processInput = nullptr;

			sendButtonConfig.state = ButtonState::DISABLED;
		}
    }

    void drawInputFieldBackground(const float width, const float height) {
        ImVec2 screenPos = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(
            screenPos,
            ImVec2(screenPos.x + width, screenPos.y + height),
            ImGui::ColorConvertFloat4ToU32(Config::InputField::INPUT_FIELD_BG_COLOR),
            Config::InputField::FRAME_ROUNDING
        );
    }

    void renderInputField(const float inputWidth) {
        auto processInput = [this](const std::string& input) {
            handleUserMessage(input);
            };

        InputFieldConfig inputConfig = createInputFieldConfig(inputWidth, processInput);

        configureSendButton(inputConfig);

        drawInputFieldBackground(inputWidth, m_inputHeight);

        ImGui::BeginGroup();
        InputField::renderMultiline(inputConfig);

        ImVec2 cursorPos = ImGui::GetCursorPos();
        renderChatFeatureButtons(cursorPos.x + 10, cursorPos.y);

        ImGui::SameLine();
        ImGui::SetCursorPosX(
            ImGui::GetContentRegionAvail().x +
            openModelManagerConfig.size.x +
            clearChatButtonConfig.size.x
        );
        Button::render(sendButtonConfig);
        ImGui::EndGroup();

        inputTextBuffer.resize(Config::InputField::TEXT_SIZE, '\0');
    }

private:
    // Cached widget configurations.
    ButtonConfig renameButtonConfig;
    ButtonConfig openModelManagerConfig;
    ButtonConfig clearChatButtonConfig;
    ButtonConfig sendButtonConfig;
    std::string inputPlaceholderText;

    // State variables.
    bool showRenameChatDialog = false;
    bool openModelSelectionModal = false;
    std::string inputTextBuffer = std::string(Config::InputField::TEXT_SIZE, '\0');
    bool focusInputField = true;
    float m_inputHeight = Config::INPUT_HEIGHT;

    // Child components.
    ModelManagerModal modelManagerModal;
    RenameChatModalComponent renameChatModal;
    ClearChatModalComponent clearChatModal;
    ChatHistoryRenderer chatHistoryRenderer;
};