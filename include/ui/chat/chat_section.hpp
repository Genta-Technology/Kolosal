#pragma once

#include "imgui.h"
#include "config.hpp"
#include "ui/widgets.hpp"
#include "chat/chat_manager.hpp"
#include "model/preset_manager.hpp"
#include "model/model_manager.hpp"

#include <iostream>
#include <inference.h>

inline void pushIDAndColors(const Chat::Message msg, int index)
{
    ImGui::PushID(index);

    // Set background color to #2f2f2f for user
    ImVec4 bgColor = ImVec4(
        Config::UserColor::COMPONENT,
        Config::UserColor::COMPONENT,
        Config::UserColor::COMPONENT,
        1.0F);

    // Set background color to transparent for assistant
    if (msg.role == "assistant")
    {
        bgColor = ImVec4(0.0F, 0.0F, 0.0F, 0.0F);
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, bgColor);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 1.0F, 1.0F, 1.0F)); // White text
}

inline auto calculateDimensions(const Chat::Message msg, float windowWidth) -> std::tuple<float, float, float>
{
    float bubbleWidth = windowWidth * Config::Bubble::WIDTH_RATIO;
    float bubblePadding = Config::Bubble::PADDING;
    float paddingX = windowWidth - bubbleWidth - Config::Bubble::RIGHT_PADDING;

    if (msg.role == "assistant")
    {
        bubbleWidth = windowWidth;
        paddingX = 0;
    }

    return {bubbleWidth, bubblePadding, paddingX};
}

inline void renderMessageContent(const Chat::Message msg, float bubbleWidth, float bubblePadding)
{
    ImGui::SetCursorPosX(bubblePadding);
    ImGui::SetCursorPosY(bubblePadding);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + bubbleWidth - (bubblePadding * 2));
    ImGui::TextWrapped("%s", msg.content.c_str());
    ImGui::PopTextWrapPos();
}

inline void renderTimestamp(const Chat::Message msg, float bubblePadding)
{
    // Set timestamp color to a lighter gray
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7F, 0.7F, 0.7F, 1.0F)); // Light gray for timestamp

    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ImGui::GetTextLineHeightWithSpacing() // Align timestamp at the bottom
                         - (bubblePadding - Config::Timing::TIMESTAMP_OFFSET_Y));
    ImGui::SetCursorPosX(bubblePadding); // Align timestamp to the left
    ImGui::TextWrapped("%s", timePointToString(msg.timestamp).c_str());

    ImGui::PopStyleColor(); // Restore original text color
}

inline void renderButtons(const Chat::Message msg, int index, float bubbleWidth, float bubblePadding)
{
    ImVec2 textSize = ImGui::CalcTextSize(msg.content.c_str(), nullptr, true, bubbleWidth - bubblePadding * 2);
    float buttonPosY = textSize.y + bubblePadding;

    if (msg.role == "user")
    {
        ButtonConfig copyButtonConfig;
        copyButtonConfig.id = "##copy" + std::to_string(index);
        copyButtonConfig.label = std::nullopt;
        copyButtonConfig.icon = ICON_CI_COPY;
        copyButtonConfig.size = ImVec2(Config::Button::WIDTH, 0);
        copyButtonConfig.onClick = [&msg]()
        {
            ImGui::SetClipboardText(msg.content.c_str());
            std::cout << "Copied message content to clipboard" << std::endl;
        };
        std::vector<ButtonConfig> userButtons = {copyButtonConfig};

        Button::renderGroup(
            userButtons,
            bubbleWidth - bubblePadding - Config::Button::WIDTH,
            buttonPosY);
    }
    else
    {
        ButtonConfig likeButtonConfig;
        likeButtonConfig.id = "##like" + std::to_string(index);
        likeButtonConfig.label = std::nullopt;
        likeButtonConfig.icon = ICON_CI_THUMBSUP;
        likeButtonConfig.size = ImVec2(Config::Button::WIDTH, 0);
        likeButtonConfig.onClick = [index]()
        {
            std::cout << "Like button clicked for message " << index << std::endl;
        };

        ButtonConfig dislikeButtonConfig;
        dislikeButtonConfig.id = "##dislike" + std::to_string(index);
        dislikeButtonConfig.label = std::nullopt;
        dislikeButtonConfig.icon = ICON_CI_THUMBSDOWN;
        dislikeButtonConfig.size = ImVec2(Config::Button::WIDTH, 0);
        dislikeButtonConfig.onClick = [index]()
        {
            std::cout << "Dislike button clicked for message " << index << std::endl;
        };

        std::vector<ButtonConfig> assistantButtons = {likeButtonConfig, dislikeButtonConfig};

        Button::renderGroup(
            assistantButtons,
            bubbleWidth - bubblePadding * 2 - 10 - (2 * Config::Button::WIDTH + Config::Button::SPACING),
            buttonPosY);
    }
}

inline void renderMessage(const Chat::Message &msg, int index, float contentWidth)
{
    pushIDAndColors(msg, index);
    float windowWidth = contentWidth;
    auto [bubbleWidth, bubblePadding, paddingX] = calculateDimensions(msg, windowWidth);

    ImVec2 textSize = ImGui::CalcTextSize(msg.content.c_str(), nullptr, true, bubbleWidth - bubblePadding * 2);
    float estimatedHeight = textSize.y + bubblePadding * 2 + ImGui::GetTextLineHeightWithSpacing();

    ImGui::SetCursorPosX(paddingX);

    if (msg.role == "user")
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, Config::InputField::CHILD_ROUNDING);
    }

    ImGui::BeginGroup();
    ImGui::BeginChild(
        ("MessageCard" + std::to_string(index)).c_str(),
        ImVec2(bubbleWidth, estimatedHeight),
        false,
        ImGuiWindowFlags_NoScrollbar);

    renderMessageContent(msg, bubbleWidth, bubblePadding);
    ImGui::Spacing();
    renderTimestamp(msg, bubblePadding);
    renderButtons(msg, index, bubbleWidth, bubblePadding);

    ImGui::EndChild();
    ImGui::EndGroup();

    if (msg.role == "user")
    {
        ImGui::PopStyleVar();
    }

    ImGui::PopStyleColor(2);
    ImGui::PopID();
    ImGui::Spacing();
}

inline void renderChatHistory(const Chat::ChatHistory chatHistory, float contentWidth)
{
    static size_t lastMessageCount = 0;
    size_t currentMessageCount = chatHistory.messages.size();

    // Check if new messages have been added
    bool newMessageAdded = currentMessageCount > lastMessageCount;

    // Save the scroll position before rendering
    float scrollY = ImGui::GetScrollY();
    float scrollMaxY = ImGui::GetScrollMaxY();
    bool isAtBottom = (scrollMaxY <= 0.0F) || (scrollY >= scrollMaxY - 1.0F);

    // Render messages
    const std::vector<Chat::Message> &messages = chatHistory.messages;
    for (size_t i = 0; i < messages.size(); ++i)
    {
        renderMessage(messages[i], static_cast<int>(i), contentWidth);
    }

    // If the user was at the bottom and new messages were added, scroll to bottom
    if (newMessageAdded && isAtBottom)
    {
        ImGui::SetScrollHereY(1.0F);
    }

    // Update the last message count
    lastMessageCount = currentMessageCount;
}

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

inline void renderModelManager(bool &openModal)
{
    ImVec2 windowSize = ImGui::GetWindowSize();
    const float targetWidth = windowSize.x;

    // Card constants
    const float cardWidth = 200;
    const float cardHeight = 200;
    const float cardSpacing = 10.0f;
    const float cardUnit = cardWidth + cardSpacing;
    const float paddingTotal = 2 * 16.0F; // 16.0F is the default padding value defined in the ModalWindow::render function

    // Calculate number of cards that fit within target width
    float availableWidth = targetWidth - paddingTotal;
    int numCards = static_cast<int>(availableWidth / cardUnit);

    // Calculate actual modal width
    float modalWidth = (numCards * cardUnit) + paddingTotal;

    // If we're too far below target width, add one more card
    if (targetWidth - modalWidth > cardUnit * 0.5f)
    {
        numCards++;
        modalWidth = (numCards * cardUnit) + paddingTotal;
    }

    ImVec2 modalSize = ImVec2(modalWidth, windowSize.y * 0.9F);

    ModalConfig modalConfig{
        "Model Manager",
        "Model Manager",
        modalSize,
        [numCards, cardSpacing, cardWidth, cardHeight, targetWidth]()
        {
            std::vector<Model::ModelData> models = Model::ModelManager::getInstance().getModels();
            static std::vector<std::string> modelVariants;
            if (modelVariants.empty())
            {
                for (int8_t i = 0; i < models.size(); i++)
                {
                    if (models[i].name == Model::ModelManager::getInstance().getCurrentModelName())
                    {
                        modelVariants.push_back(Model::ModelManager::getInstance().getCurrentVariantType());
                        continue;
                    }

                    modelVariants.push_back("4-bit Quantized");
                }
            }

            for (size_t i = 0; i < models.size(); i++)
            {
                // Start new row
                if (i % numCards == 0)
                {
                    ImGui::SetCursorPos(ImVec2(16.0F, ImGui::GetCursorPosY() + (i > 0 ? cardSpacing : 0)));
                }

                ImGui::BeginGroup();
                ImGui::PushStyleColor(ImGuiCol_ChildBg, RGBAToImVec4(26, 26, 26, 255));
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0F);

                ImGui::BeginChild(("ModelCard" + std::to_string(i)).c_str(),
                                  ImVec2(cardWidth, cardHeight), true);

                // Render author label
                LabelConfig modelAuthorLabel;
                modelAuthorLabel.id = "##modelAuthor" + std::to_string(i);
                modelAuthorLabel.label = "Meta";
                modelAuthorLabel.size = ImVec2(0, 0);
                modelAuthorLabel.fontType = FontsManager::ITALIC;
                modelAuthorLabel.alignment = Alignment::LEFT;
                modelAuthorLabel.fontSize = FontsManager::SM;
                Label::render(modelAuthorLabel);

                // Get the height of the author label
                float authorLabelHeight = ImGui::GetTextLineHeightWithSpacing();

                // Render model name label
                LabelConfig modelNameLabel;
                modelNameLabel.id = "##modelName" + std::to_string(i);
                modelNameLabel.label = models[i].name;
                modelNameLabel.size = ImVec2(0, 0);
                modelNameLabel.fontType = FontsManager::BOLD;
                modelNameLabel.alignment = Alignment::LEFT;
                Label::render(modelNameLabel);

                // Get the height of the model name label
                float modelNameLabelHeight = ImGui::GetTextLineHeightWithSpacing();

                // Calculate the total height of the labels
                float totalLabelHeight = authorLabelHeight + modelNameLabelHeight;

                // add left padding
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);

                // TODO: Check button rect size is bigger when no icon is present
                //       make sure to adjust the size of the button accordingly                
                ButtonConfig use4bitButton;
                use4bitButton.id = "##use4bit" + std::to_string(i);
                if (modelVariants[i] == "4-bit Quantized")
                {
                    use4bitButton.icon = ICON_CI_CHECK;
                }
                else
                {
                    // TODO's workarounds for the button size issue
                    use4bitButton.icon = ICON_CI_CLOSE;
                    use4bitButton.textColor = RGBAToImVec4(34, 34, 34, 255);
                }
                use4bitButton.fontSize = FontsManager::SM;
                use4bitButton.size = ImVec2(24, 0);
                use4bitButton.backgroundColor = RGBAToImVec4(34, 34, 34, 255);
                use4bitButton.onClick = [i, models]()
                {
                    if (modelVariants[i] == "4-bit Quantized")
                    {
                        modelVariants[i] = "Full Precision";
                    }
                    else
                    {
                        modelVariants[i] = "4-bit Quantized";
                    }
                };
                Button::render(use4bitButton);

                // Get the height of the quantization checkbox button
                float quantizationHeight = ImGui::GetTextLineHeightWithSpacing();

                ImGui::SameLine(0.0f, 4.0f);

                // Render labels
                LabelConfig quantizationLabel;
                quantizationLabel.id = "##quantization" + std::to_string(i);
                quantizationLabel.label = "Use 4-bit quantization";
                quantizationLabel.size = ImVec2(0, 0);
                quantizationLabel.fontType = FontsManager::REGULAR;
                quantizationLabel.fontSize = FontsManager::SM;
                quantizationLabel.alignment = Alignment::LEFT;
                float labelOffsetY = (use4bitButton.size.y - ImGui::GetTextLineHeight()) / 4.0f + 2.0f;
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + labelOffsetY);
                Label::render(quantizationLabel);

                // Render select button at the bottom of the card
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (cardHeight - totalLabelHeight - quantizationHeight * 3 - 10));

                bool isSelected = models[i].name == Model::ModelManager::getInstance().getCurrentModelName() &&
                                  modelVariants[i] == Model::ModelManager::getInstance().getCurrentVariantType();
                bool isDownloaded = Model::ModelManager::getInstance().isModelDownloaded(i, modelVariants[i]);

                ButtonConfig selectButton;
                selectButton.size = ImVec2(cardWidth - 18, 0);

                if (!isDownloaded)
                {
                    selectButton.id = "##download" + std::to_string(i);
                    selectButton.label = "Download";
                    selectButton.backgroundColor = RGBAToImVec4(26, 95, 180, 255);
                    selectButton.hoverColor = RGBAToImVec4(53, 132, 228, 255);
                    selectButton.activeColor = RGBAToImVec4(26, 95, 180, 255);
                    selectButton.icon = ICON_CI_CLOUD_DOWNLOAD;
                    selectButton.borderSize = 1.0F;

                    selectButton.onClick = [i, models]()
                    {
                        Model::ModelManager::getInstance().downloadModel(i, modelVariants[i]);
                    };

                    if (Model::ModelManager::getInstance().getModelDownloadProgress(i, modelVariants[i]) > 0.0)
                    {
                        selectButton.label = "Downloading";
                        selectButton.icon = ICON_CI_CLOUD_DOWNLOAD;
                        selectButton.state = ButtonState::DISABLED;

                        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - quantizationHeight - 6);

                        // Add a progress bar
                        ImGui::ProgressBar(
                            Model::ModelManager::getInstance().getModelDownloadProgress(i, modelVariants[i]) / 100.0,
                            ImVec2(cardWidth - 18, 0));
                    }
                }
                else
                {
                    selectButton.id = "##select" + std::to_string(i);
                    selectButton.label = isSelected ? "selected" : "select";
                    selectButton.backgroundColor = RGBAToImVec4(34, 34, 34, 255);
                    if (isSelected)
                    {
                        selectButton.icon = ICON_CI_PASS;
                        selectButton.borderColor = RGBAToImVec4(172, 131, 255, 255 / 4);
                        selectButton.borderSize = 1.0F;
                        selectButton.state = ButtonState::ACTIVE;
                    }

                    selectButton.onClick = [i, models]()
                    {
                        Model::ModelManager::getInstance().switchModel(
                            models[i].name,
                            modelVariants[i]
                        );
                    };
                }

                Button::render(selectButton);

                ImGui::EndChild();

                if (ImGui::IsItemHovered() || isSelected)
                {
                    ImVec2 min = ImGui::GetItemRectMin();
                    ImVec2 max = ImGui::GetItemRectMax();
                    ImU32 borderColor = IM_COL32(172, 131, 255, 255 / 2);
                    ImGui::GetWindowDrawList()->AddRect(
                        min, max, borderColor, 8.0f, 0, 1.0f);
                }

                ImGui::PopStyleVar();
                ImGui::PopStyleColor();

                ImGui::EndGroup();

                if ((i + 1) % numCards != 0 && i < models.size() - 1)
                {
                    ImGui::SameLine(0.0f, cardSpacing);
                }
            }
        },
        openModal};

    ModalWindow::render(modalConfig);
}

inline void renderChatFeatureButtons(const float startX = 0, const float startY = 0)
{
    static bool openModal = false;

    // Configure the button
    std::vector<ButtonConfig> buttons;

    std::string currentModelName = Model::ModelManager::getInstance()
        .getCurrentModelName().value_or("Select Model");

    ButtonConfig openModelManager;
    openModelManager.id = "##openModalButton";
    openModelManager.label = currentModelName;
    openModelManager.icon = ICON_CI_SPARKLE;
    openModelManager.size = ImVec2(128, 0);
    openModelManager.alignment = Alignment::LEFT;
    openModelManager.onClick = [&]()
    { openModal = true; };

    buttons.push_back(openModelManager);

    // Render the button using renderGroup
    Button::renderGroup(buttons, startX, startY);

    // Open the modal window if the button was clicked
    renderModelManager(openModal);
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
        // TODO: Implement assistant response through callback
        {
			Model::PresetManager& presetManager = Model::PresetManager::getInstance();
			Model::ModelManager& modelManager = Model::ModelManager::getInstance();

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
                completionParams.randomSeed     = presetManager.getCurrentPreset().value().get().random_seed;
                completionParams.maxNewTokens   = static_cast<int>(presetManager.getCurrentPreset().value().get().max_new_tokens);
                completionParams.minLength      = static_cast<int>(presetManager.getCurrentPreset().value().get().min_length);
                completionParams.temperature    = presetManager.getCurrentPreset().value().get().temperature;
                completionParams.topP           = presetManager.getCurrentPreset().value().get().top_p;
				// TODO: add top_k to the completion parameters
				// completionParams.topK        = presetManager.getCurrentPreset().value().get().top_k;
                completionParams.streaming      = true;
            }
            int jobId = modelManager.startChatCompletionJob(completionParams);

			// track the job ID in the chat manager
			chatManager.setCurrentJobId(jobId);
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
    renderChatHistory(Chat::ChatManager::getInstance().getCurrentChat().value(), contentWidth);

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