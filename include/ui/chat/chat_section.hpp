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

inline void renderRenameChatDialog(bool &showRenameChatDialog)
{
    static std::string newChatName;
    ModalConfig modalConfig
    {
        "Rename Chat",
        "Rename Chat",
        ImVec2(300, 98),
        [&]()
        {
            static bool focusNewChatName = true;
            if (newChatName.empty())
            {
                auto currentChatName = Chat::ChatManager::getInstance().getCurrentChatName();
                if (currentChatName.has_value())
                {
                    newChatName = currentChatName.value();
                }
            }

            // Input field
            auto processInput = [](const std::string &input)
            {
                Chat::ChatManager::getInstance().renameCurrentChat(input);
                ImGui::CloseCurrentPopup();
                newChatName.clear();
            };

            InputFieldConfig inputConfig(
                "##newchatname",
                ImVec2(ImGui::GetWindowSize().x - 32.0F, 0),
                newChatName,
                focusNewChatName);
            inputConfig.flags = ImGuiInputTextFlags_EnterReturnsTrue;
            inputConfig.processInput = processInput;
            inputConfig.frameRounding = 5.0F;
            InputField::render(inputConfig);
        },
        showRenameChatDialog
    };
    modalConfig.padding = ImVec2(16.0F, 8.0F);

    ModalWindow::render(modalConfig);
}

// TODO: refactor to be reusable
inline void renderClearChatModal(bool& openModal)
{
	ModalConfig modalConfig
	{
		"Confirm Clear Chat",
		"Confirm Clear Chat",
		ImVec2(300, 96),
		[&]()
		{
			// Render the buttons
			std::vector<ButtonConfig> buttons;
            
            ButtonConfig cancelButton;
            cancelButton.id = "##cancelClearChat";
            cancelButton.label = "Cancel";
            cancelButton.backgroundColor = RGBAToImVec4(34, 34, 34, 255);
            cancelButton.hoverColor = RGBAToImVec4(53, 132, 228, 255);
            cancelButton.activeColor = RGBAToImVec4(26, 95, 180, 255);
            cancelButton.textColor = RGBAToImVec4(255, 255, 255, 255);
			cancelButton.size = ImVec2(130, 0);
            cancelButton.onClick = []()
                {
                    ImGui::CloseCurrentPopup();
                };

			buttons.push_back(cancelButton);

			ButtonConfig confirmButton;
			confirmButton.id = "##confirmClearChat";
			confirmButton.label = "Confirm";
			confirmButton.backgroundColor = RGBAToImVec4(26, 95, 180, 255);
			confirmButton.hoverColor = RGBAToImVec4(53, 132, 228, 255);
			confirmButton.activeColor = RGBAToImVec4(26, 95, 180, 255);
			confirmButton.size = ImVec2(130, 0);
			confirmButton.onClick = []()
				{
					Chat::ChatManager::getInstance().clearCurrentChat();
					ImGui::CloseCurrentPopup();
				};

			buttons.push_back(confirmButton);

			Button::renderGroup(buttons, 16, ImGui::GetCursorPosY() + 8);
		},
		openModal
	};
    modalConfig.padding = ImVec2(16.0F, 8.0F);
	ModalWindow::render(modalConfig);
}

inline void renderChatFeatureButtons(const float startX = 0, const float startY = 0)
{
    static ModelManagerModal modelManagerModal;
    static bool openModelSelectionModal = false;
    static bool openClearChatModal = false;

    // Configure the buttons.
    std::vector<ButtonConfig> buttons;

    std::string currentModelName = Model::ModelManager::getInstance()
        .getCurrentModelName().value_or("Select Model");

    ButtonConfig openModelManager;
    openModelManager.id = "##openModalButton";
    openModelManager.label = currentModelName;
    openModelManager.icon = ICON_CI_SPARKLE;
    openModelManager.size = ImVec2(128, 0);
    openModelManager.alignment = Alignment::LEFT;
    // Simply set the external flag to true when clicked.
    openModelManager.onClick = [&]() {
        openModelSelectionModal = true;
        };

    buttons.push_back(openModelManager);

    ButtonConfig clearChatButton;
    clearChatButton.id = "##clearChatButton";
    clearChatButton.icon = ICON_CI_CLEAR_ALL;
    clearChatButton.size = ImVec2(24, 0);
    clearChatButton.alignment = Alignment::CENTER;
    clearChatButton.onClick = []() {
        openClearChatModal = true;
        };
    clearChatButton.tooltip = "Clear Chat";

    buttons.push_back(clearChatButton);

    // Render the buttons using renderGroup.
    Button::renderGroup(buttons, startX, startY);

    // Render the modals by passing the external flag.
    modelManagerModal.render(openModelSelectionModal);
    renderClearChatModal(openClearChatModal);
}

inline void renderInputField(const float inputHeight, const float inputWidth)
{
    static std::string inputTextBuffer(Config::InputField::TEXT_SIZE, '\0');
    static bool focusInputField = true;

    // Define the input size
    ImVec2 inputSize = ImVec2(inputWidth, inputHeight);

    // Define a lambda to process the submitted input
    auto processInput = [&](const std::string &input)
    {
        auto &chatManager = Chat::ChatManager::getInstance();
        auto currentChat = chatManager.getCurrentChat();

        // Check if we have a current chat
        if (!currentChat.has_value())
        {
			std::cerr << "[ChatSection] No chat selected. Cannot send message.\n";
			return;
        }

		// Check if any model is selected
		if (!Model::ModelManager::getInstance().getCurrentModelName().has_value())
		{
			std::cerr << "[ChatSection] No model selected. Cannot send message.\n";
			return;
		}

        // Handle user message
        {
            Chat::Message userMessage;
            userMessage.id = static_cast<int>(currentChat.value().messages.size()) + 1;
            userMessage.role = "user";
            userMessage.content = input;

            // Add message directly to current chat
            chatManager.addMessageToCurrentChat(userMessage);
        }

        // Handle assistant response
        {
			Model::PresetManager& presetManager = Model::PresetManager::getInstance();
			Model::ModelManager&  modelManager  = Model::ModelManager::getInstance();

			// Prepare completion parameters
            ChatCompletionParameters completionParams;
            {
                // push system prompt
                completionParams.messages.push_back(
                    { "system", presetManager.getCurrentPreset().value().get().systemPrompt.c_str()});
                for (const auto& msg : currentChat.value().messages)
                {
                    completionParams.messages.push_back({ msg.role.c_str(), msg.content.c_str()});
                }
                // push user new message
                completionParams.messages.push_back({ "user", input.c_str()});

				// set the preset parameters
                completionParams.randomSeed         = presetManager.getCurrentPreset().value().get().random_seed;
                completionParams.maxNewTokens       = static_cast<int>(presetManager.getCurrentPreset().value().get().max_new_tokens);
                completionParams.minLength          = static_cast<int>(presetManager.getCurrentPreset().value().get().min_length);
                completionParams.temperature        = presetManager.getCurrentPreset().value().get().temperature;
                completionParams.topP               = presetManager.getCurrentPreset().value().get().top_p;
				// TODO: add top_k to the completion parameters
				// completionParams.topK            = presetManager.getCurrentPreset().value().get().top_k;
                completionParams.streaming          = true;
				completionParams.kvCacheFilePath    = chatManager.getCurrentKvChatPath(
                    modelManager.getCurrentModelName().value(),
					modelManager.getCurrentVariantType()
                ).value().string();
            }
            int jobId = modelManager.startChatCompletionJob(completionParams);

			// track the job ID in the chat manager
            if (!chatManager.setCurrentJobId(jobId))
            {
				std::cerr << "[ChatSection] Failed to set the current job ID.\n";
            }
        }
    };

    // input field settings
    InputFieldConfig inputConfig(
        "##chatinput",                                                           // ID
        ImVec2(inputSize.x, inputSize.y - Config::Font::DEFAULT_FONT_SIZE - 20), // Size (excluding button height)
        inputTextBuffer,                                                         // Input text buffer
        focusInputField);                                                        // Focus
    {
        inputConfig.placeholderText = "Type a message and press Enter to send (Ctrl+Enter or Shift+Enter for new line)";
        inputConfig.flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine | ImGuiInputTextFlags_ShiftEnterForNewLine;
        inputConfig.processInput = processInput;
    }

    // Set background color and create child window
    ImVec2 screenPos = ImGui::GetCursorScreenPos();
    ImDrawList *drawList = ImGui::GetWindowDrawList();

    // Draw background rectangle
    drawList->AddRectFilled(
        screenPos,
        ImVec2(screenPos.x + inputWidth, screenPos.y + inputHeight),
        ImGui::ColorConvertFloat4ToU32(Config::InputField::INPUT_FIELD_BG_COLOR),
        Config::InputField::FRAME_ROUNDING // corner rounding
    );

    ImGui::BeginGroup();

    // Render the input field
    InputField::renderMultiline(inputConfig);

    {
        // Calculate position for feature buttons
        // Get current cursor position for relative positioning
        ImVec2 cursorPos = ImGui::GetCursorPos();
        float buttonX = cursorPos.x + 10;
        float buttonY = cursorPos.y;

        // Render the feature buttons
        renderChatFeatureButtons(buttonX, buttonY);
    }

    ImGui::EndGroup();
}

inline void renderChatWindow(float inputHeight, float leftSidebarWidth, float rightSidebarWidth)
{
    ImGuiIO &imguiIO = ImGui::GetIO();

    // Calculate the size of the chat window based on the sidebar width
    ImVec2 windowSize = ImVec2(imguiIO.DisplaySize.x - rightSidebarWidth - leftSidebarWidth, imguiIO.DisplaySize.y - Config::TITLE_BAR_HEIGHT);

    // Set window to cover the remaining display area
    ImGui::SetNextWindowPos(ImVec2(leftSidebarWidth, Config::TITLE_BAR_HEIGHT), ImGuiCond_Always);
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar |
                                   ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus;
    // Remove window border
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);

    ImGui::Begin("Chatbot", nullptr, windowFlags);

    // Calculate available width for content
    float availableWidth = ImGui::GetContentRegionAvail().x;
    float contentWidth = (availableWidth < Config::CHAT_WINDOW_CONTENT_WIDTH) ? availableWidth : Config::CHAT_WINDOW_CONTENT_WIDTH;
    float paddingX = (availableWidth - contentWidth) / 2.0F;
    float renameButtonWidth = contentWidth;
    static bool showRenameChatDialog = false;

    // Center the rename button horizontally
    if (paddingX > 0.0F)
    {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + paddingX);
    }

    // Render the rename button
    ButtonConfig renameButtonConfig;
    renameButtonConfig.id = "##renameChat";
    renameButtonConfig.label = Chat::ChatManager::getInstance().getCurrentChatName();
    renameButtonConfig.size = ImVec2(renameButtonWidth, 30);
    renameButtonConfig.gap = 10.0F;
    renameButtonConfig.onClick = []()
    {
        showRenameChatDialog = true;
    };
    renameButtonConfig.alignment = Alignment::CENTER;
    renameButtonConfig.hoverColor = ImVec4(0.1F, 0.1F, 0.1F, 0.5F);
    Button::render(renameButtonConfig);

    // Render the rename chat dialog
    renderRenameChatDialog(showRenameChatDialog);

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Spacing();

    // Center the content horizontally
    if (paddingX > 0.0F)
    {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + paddingX);
    }

    // Begin the main scrolling region for the chat history
    float availableHeight = ImGui::GetContentRegionAvail().y - inputHeight - Config::BOTTOM_MARGIN;
    ImGui::BeginChild("ChatHistoryRegion", ImVec2(contentWidth, availableHeight), false, ImGuiWindowFlags_NoScrollbar);

    // Render chat history
	static ChatHistoryRenderer chatHistoryRenderer;
    if (auto chat = Chat::ChatManager::getInstance().getCurrentChat()) {
        chatHistoryRenderer.render(*chat, contentWidth);
    }

    ImGui::EndChild(); // End of ChatHistoryRegion

    // Add some spacing or separator if needed
    ImGui::Spacing();

    // Center the input field horizontally by calculating left padding
    float inputFieldPaddingX = (availableWidth - contentWidth) / 2.0F;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + inputFieldPaddingX);

    // Render the input field at the bottom, centered
    renderInputField(inputHeight, contentWidth);

    ImGui::End(); // End of Chatbot window

    // Restore the window border size
    ImGui::PopStyleVar();
}