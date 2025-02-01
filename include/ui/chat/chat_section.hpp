#pragma once

#include "imgui.h"
#include "config.hpp"
#include "ui/widgets.hpp"
#include "ui/markdown.hpp"
#include "chat/chat_manager.hpp"
#include "model/preset_manager.hpp"
#include "model/model_manager.hpp"

#include <iostream>
#include <inference.h>

inline auto parseThinkSegments(const std::string& content) -> std::vector<std::pair<bool, std::string>>
{
    std::vector<std::pair<bool, std::string>> segments;
    size_t current_pos = 0;
    const std::string open_tag = "<think>";
    const std::string close_tag = "</think>";

    while (current_pos < content.size()) {
        size_t think_start = content.find(open_tag, current_pos);
        if (think_start == std::string::npos) {
            // Add remaining text as a normal segment
            std::string normal = content.substr(current_pos);
            if (!normal.empty()) {
                segments.emplace_back(false, normal);
            }
            break;
        }
        else {
            // Add text before <think> as a normal segment
            std::string normal_part = content.substr(current_pos, think_start - current_pos);
            if (!normal_part.empty()) {
                segments.emplace_back(false, normal_part);
            }
            // Process the think content
            size_t content_start = think_start + open_tag.size();
            size_t think_end = content.find(close_tag, content_start);
            if (think_end == std::string::npos) {
                // No closing tag; take the rest as think content
                std::string think_part = content.substr(content_start);
                segments.emplace_back(true, think_part);
                current_pos = content.size();
            }
            else {
                // Extract think content and move past the closing tag
                std::string think_part = content.substr(content_start, think_end - content_start);
                segments.emplace_back(true, think_part);
                current_pos = think_end + close_tag.size();
            }
        }
    }

    return segments;
}

inline auto calculateDimensions(const Chat::Message msg, float windowWidth) -> std::tuple<float, float, float>
{
    float bubbleWidth = windowWidth * Config::Bubble::WIDTH_RATIO;
    float bubblePadding = Config::Bubble::PADDING;
    float paddingX = windowWidth - bubbleWidth;

    if (msg.role == "assistant")
    {
        bubbleWidth = windowWidth;
        paddingX = 0;
    }

    return {bubbleWidth, bubblePadding, paddingX};
}

inline void renderMessageContent(const Chat::Message msg, float bubbleWidth, float bubblePadding)
{
    if (msg.role == "user")
    {
        ImGui::SetCursorPosX(bubblePadding);
        ImGui::SetCursorPosY(bubblePadding);
        ImGui::TextWrapped("%s", msg.content.c_str());
    }
    else
    {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 24);
        ImGui::BeginGroup();

        // Parse and render segments
        auto segments = parseThinkSegments(msg.content);
        for (int i = 0; i < segments.size(); i++) {
            bool isThink = segments[i].first;
            const std::string& text = segments[i].second;

            // Skip rendering if this is a "think" segment with no (or only whitespace) text.
            if (isThink && text.find_first_not_of(" \t\n") == std::string::npos) {
                continue;
            }

            if (isThink)
            {
                const std::string uniqueID =
                    msg.id + "_thinkSegment_" + std::to_string(i);

                if (!Chat::gThinkToggleMap.count(uniqueID))
                {
                    Chat::gThinkToggleMap[uniqueID] = true;
                }

                // Grab the current toggle state
                bool& showThink = Chat::gThinkToggleMap[uniqueID];

                // Some vertical spacing before the "think" segment
                ImGui::NewLine();

                // Give the toggle button a unique label or ID
				ButtonConfig thinkButtonConfig;
				thinkButtonConfig.id = "##" + uniqueID;
				thinkButtonConfig.icon = showThink ? ICON_CI_CHEVRON_DOWN : ICON_CI_CHEVRON_RIGHT;
				thinkButtonConfig.label = "Thoughts";
				thinkButtonConfig.size = ImVec2(100, 0);
				thinkButtonConfig.alignment = Alignment::LEFT;
				thinkButtonConfig.backgroundColor = ImVec4(0.2F, 0.2F, 0.2F, 0.4F);
				thinkButtonConfig.textColor = ImVec4(0.9F, 0.9F, 0.9F, 0.9F);
				thinkButtonConfig.onClick = [&showThink]() {
					showThink = !showThink;
					};
				Button::render(thinkButtonConfig);

                if (showThink)
                {
                    // Measure text size so we know how tall the line should be
                    float availableWidth = bubbleWidth - 2.0f * bubblePadding;
                    ImVec2 textSize = ImGui::CalcTextSize(text.c_str(), nullptr, false, availableWidth);

                    // We’ll add some extra padding to the top/bottom around the text
                    float segmentHeight = textSize.y + bubblePadding * 2.0f;

                    // Store the current screen position (top-left) for line drawing
                    ImVec2 startPos = ImGui::GetCursorScreenPos();
                    startPos.y += 12;

                    // Draw the vertical line
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    float lineThickness = 1.0f;                  // thickness of the line
                    float linePadding = 8.0f;                  // space between line and text
                    ImU32 lineColor = IM_COL32(153, 153, 153, 153); // grey color

                    drawList->AddLine(
                        ImVec2(startPos.x, startPos.y),
                        ImVec2(startPos.x, startPos.y + segmentHeight),
                        lineColor,
                        lineThickness
                    );

                    // Move cursor to the right of the line + padding
                    // so that the text lines up neatly
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + lineThickness + linePadding);

                    // Enable wrapping at the remaining available width
                    float wrapWidth = availableWidth - (lineThickness + linePadding);
                    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + wrapWidth);

                    // Render the "think" text in grey
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 0.7f));
                    ImGui::TextUnformatted(text.c_str());
                    ImGui::PopStyleColor();

                    ImGui::PopTextWrapPos();

                    // Move the cursor to the bottom of this "think" segment
                    // so subsequent items appear below
                    ImGui::SetCursorScreenPos(ImVec2(startPos.x, startPos.y + segmentHeight));

                    // A bit of spacing after the block
                    ImGui::Dummy(ImVec2(0, 5.0f));
                }
            }
            else
            {
                // Normal markdown content (unchanged)
                RenderMarkdown(text.c_str());
            }

        }

        ImGui::EndGroup();
    }
}

inline void renderTimestamp(const Chat::Message msg, float bubblePadding)
{
    // Set timestamp color to a lighter gray
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7F, 0.7F, 0.7F, 1.0F)); // Light gray for timestamp

    ImGui::TextWrapped("%s", timePointToString(msg.timestamp).c_str());

    ImGui::PopStyleColor(); // Restore original text color
}

inline void renderTps(const Chat::Message msg, float bubblePadding)
{
	// Set timestamp color to a lighter gray
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7F, 0.7F, 0.7F, 1.0F)); // Light gray for timestamp
	ImGui::TextWrapped("TPS: %.2f", msg.tps);
	ImGui::PopStyleColor(); // Restore original text color
}

inline void renderButtons(const Chat::Message msg, int index, float bubbleWidth, float bubblePadding)
{
	ImGui::SetCursorPosX(
		ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - Config::Button::WIDTH - bubblePadding
    );

    ButtonConfig copyButtonConfig;
    copyButtonConfig.id = "##copy" + std::to_string(index);
    copyButtonConfig.label = std::nullopt;
    copyButtonConfig.icon = ICON_CI_COPY;
    copyButtonConfig.size = ImVec2(Config::Button::WIDTH, 0);
    copyButtonConfig.onClick = [&index]()
        {
			Chat::ChatHistory chatHistory = Chat::ChatManager::getInstance().getCurrentChat().value();
			const Chat::Message& msg = chatHistory.messages[index];
            ImGui::SetClipboardText(msg.content.c_str());
        };
	Button::render(copyButtonConfig);
}

inline void renderMessage(const Chat::Message& msg, int index, float contentWidth)
{
    float windowWidth = contentWidth;
    auto [bubbleWidth, bubblePadding, paddingX] = calculateDimensions(msg, windowWidth);

    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, Config::InputField::CHILD_ROUNDING);
    ImVec4 bgColor = ImVec4( // Default to user color
        Config::UserColor::COMPONENT,
        Config::UserColor::COMPONENT,
        Config::UserColor::COMPONENT,
        1.0F);

    if (msg.role == "assistant")
    {
        bgColor = ImVec4(0.0F, 0.0F, 0.0F, 0.0F); // Transparent for assistant
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, bgColor);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 1.0F, 1.0F, 1.0F));

    // Add frame on user message
    {
        if (msg.role == "user")
        {
            ImVec2 textSize = ImGui::CalcTextSize(msg.content.c_str(), nullptr, true, bubbleWidth - bubblePadding * 2);
            float estimatedHeight = textSize.y + bubblePadding * 2 + ImGui::GetTextLineHeightWithSpacing() + 12;

            // User message frame with background and border
            ImGui::SetCursorPosX(paddingX);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(bubblePadding, bubblePadding));
            ImGui::BeginChild(("##UsrMessage" + std::to_string(msg.id)).c_str(),
                ImVec2(bubbleWidth, estimatedHeight),
                ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysUseWindowPadding);
            ImGui::PopStyleVar();

            // Calculate content width accounting for padding
            const float contentWidthInside = bubbleWidth - 2 * bubblePadding;
            renderMessageContent(msg, contentWidthInside, bubblePadding);
            ImGui::Spacing();

            // Render timestamp and buttons
            renderTimestamp(msg, 0);
            ImGui::SameLine();
            renderButtons(msg, index, contentWidthInside, 0);

            ImGui::EndChild();
        }
        else
        {
            // Assistant message (existing layout)
            ImGui::SetCursorPosX(paddingX);
            renderMessageContent(msg, bubbleWidth, bubblePadding);
            ImGui::Spacing();
            renderTimestamp(msg, bubblePadding);
            ImGui::SameLine();
			// add some spacing between messages
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10);
			renderTps(msg, bubblePadding);
            ImGui::SameLine();
            renderButtons(msg, index, bubbleWidth, bubblePadding);
        }
    }

    ImGui::PopStyleVar(); // ChildRounding
    ImGui::PopStyleColor(2);

    ImGui::Dummy(ImVec2(0, 20.0f)); // Add some spacing between messages
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

inline void renderDeleteModelModal(bool& openModal, int index, std::string variant)
{
    ModalConfig modalConfig
    {
        "Confirm Delete Model",
        "Confirm Delete Model",
        ImVec2(300, 96),
        [&]()
        {
            // Render the buttons
            std::vector<ButtonConfig> buttons;

            ButtonConfig cancelButton;
            cancelButton.id = "##cancelDeleteModel";
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
            confirmButton.id = "##confirmDeleteModel";
            confirmButton.label = "Confirm";
            confirmButton.backgroundColor = RGBAToImVec4(26, 95, 180, 255);
            confirmButton.hoverColor = RGBAToImVec4(53, 132, 228, 255);
            confirmButton.activeColor = RGBAToImVec4(26, 95, 180, 255);
            confirmButton.size = ImVec2(130, 0);
            confirmButton.onClick = [index, variant]()
                {
                    Model::ModelManager::getInstance().deleteDownloadedModel(index, variant);
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

inline void renderModelManager(bool& openModal)
{
    ImVec2 windowSize = ImGui::GetWindowSize();
    const float targetWidth = windowSize.x;

    // Card constants
    const float cardWidth = 200;
    const float cardHeight = 220;
    const float cardSpacing = 10.0f;
    const float cardUnit = cardWidth + cardSpacing;
    const float paddingTotal = 2 * 16.0F; // 16.0F is the default padding value

    // Calculate number of cards that fit within target width
    float availableWidth = targetWidth - paddingTotal;
    int numCards = static_cast<int>(availableWidth / cardUnit);

    // Calculate actual modal width
    float modalWidth = (numCards * cardUnit) + paddingTotal;
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

            for (size_t i = 0; i < models.size(); i++)
            {
                // Get the current variant for THIS model directly from the manager
                std::string currentVariant = Model::ModelManager::getInstance()
                    .getCurrentVariantForModel(models[i].name);

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
                modelAuthorLabel.label = models[i].author;
                modelAuthorLabel.size = ImVec2(0, 0);
                modelAuthorLabel.fontType = FontsManager::ITALIC;
                modelAuthorLabel.alignment = Alignment::LEFT;
                modelAuthorLabel.fontSize = FontsManager::SM;
                Label::render(modelAuthorLabel);

                float authorLabelHeight = ImGui::GetTextLineHeightWithSpacing();

                // Render model name label
                LabelConfig modelNameLabel;
                modelNameLabel.id = "##modelName" + std::to_string(i);
                modelNameLabel.label = models[i].name;
                modelNameLabel.size = ImVec2(0, 0);
                modelNameLabel.fontType = FontsManager::BOLD;
                modelNameLabel.alignment = Alignment::LEFT;
                Label::render(modelNameLabel);

                float modelNameLabelHeight = ImGui::GetTextLineHeightWithSpacing();
                float totalLabelHeight = authorLabelHeight + modelNameLabelHeight;

                // Add some padding.
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);

                // ---------------- Variant Buttons ----------------
                // Full Precision button:
                ButtonConfig fullPrecisionButton;
                fullPrecisionButton.id = "##useFullPrecision" + std::to_string(i);
                if (currentVariant == "Full Precision")
                {
                    fullPrecisionButton.icon = ICON_CI_CHECK;
                }
                else
                {
                    fullPrecisionButton.icon = ICON_CI_CLOSE;
                    fullPrecisionButton.textColor = RGBAToImVec4(34, 34, 34, 255);
                }
                fullPrecisionButton.fontSize = FontsManager::SM;
                fullPrecisionButton.size = ImVec2(24, 0);
                fullPrecisionButton.backgroundColor = RGBAToImVec4(34, 34, 34, 255);
                fullPrecisionButton.onClick = [i, models, &currentVariant]()
                {
					currentVariant = "Full Precision";
                    Model::ModelManager::getInstance().setPreferredVariant(models[i].name, "Full Precision");
                };
                Button::render(fullPrecisionButton);

                float fullPrecisionHeight = ImGui::GetTextLineHeightWithSpacing();
                ImGui::SameLine(0.0f, 4.0f);

                // Full Precision label:
                LabelConfig fullPrecisionLabel;
                fullPrecisionLabel.id = "##fullprecision" + std::to_string(i);
                fullPrecisionLabel.label = "Use Full Precision";
                fullPrecisionLabel.size = ImVec2(0, 0);
                fullPrecisionLabel.fontType = FontsManager::REGULAR;
                fullPrecisionLabel.fontSize = FontsManager::SM;
                fullPrecisionLabel.alignment = Alignment::LEFT;
                float fullPrecisionLabelOffsetY = (fullPrecisionButton.size.y - ImGui::GetTextLineHeight()) / 4.0f + 2.0f;
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + fullPrecisionLabelOffsetY);
                Label::render(fullPrecisionLabel);

                // 8-bit Quantized button:
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);

                ButtonConfig use8bitButton;
                use8bitButton.id = "##use8bit" + std::to_string(i);
                if (currentVariant == "8-bit Quantized")
                {
                    use8bitButton.icon = ICON_CI_CHECK;
                }
                else
                {
                    use8bitButton.icon = ICON_CI_CLOSE;
                    use8bitButton.textColor = RGBAToImVec4(34, 34, 34, 255);
                }
                use8bitButton.fontSize = FontsManager::SM;
                use8bitButton.size = ImVec2(24, 0);
                use8bitButton.backgroundColor = RGBAToImVec4(34, 34, 34, 255);
                use8bitButton.onClick = [i, models, &currentVariant]()
                {
					currentVariant = "8-bit Quantized";
                    Model::ModelManager::getInstance().setPreferredVariant(models[i].name, "8-bit Quantized");
                };
                Button::render(use8bitButton);

                float _8bitquantizationHeight = ImGui::GetTextLineHeightWithSpacing();
                ImGui::SameLine(0.0f, 4.0f);

                // 8-bit Quantized label:
                LabelConfig _8BitQuantizationLabel;
                _8BitQuantizationLabel.id = "##8bitquantization" + std::to_string(i);
                _8BitQuantizationLabel.label = "Use 8-bit quantization";
                _8BitQuantizationLabel.size = ImVec2(0, 0);
                _8BitQuantizationLabel.fontType = FontsManager::REGULAR;
                _8BitQuantizationLabel.fontSize = FontsManager::SM;
                _8BitQuantizationLabel.alignment = Alignment::LEFT;
                float _8BitLabelOffsetY = (use8bitButton.size.y - ImGui::GetTextLineHeight()) / 4.0f + 2.0f;
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + _8BitLabelOffsetY);
                Label::render(_8BitQuantizationLabel);

                // 4-bit Quantized button:
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);

                ButtonConfig use4bitButton;
                use4bitButton.id = "##use4bit" + std::to_string(i);
                if (currentVariant == "4-bit Quantized")
                {
                    use4bitButton.icon = ICON_CI_CHECK;
                }
                else
                {
                    use4bitButton.icon = ICON_CI_CLOSE;
                    use4bitButton.textColor = RGBAToImVec4(34, 34, 34, 255);
                }
                use4bitButton.fontSize = FontsManager::SM;
                use4bitButton.size = ImVec2(24, 0);
                use4bitButton.backgroundColor = RGBAToImVec4(34, 34, 34, 255);
                use4bitButton.onClick = [i, models, &currentVariant]()
                {
					currentVariant = "4-bit Quantized";
                    Model::ModelManager::getInstance().setPreferredVariant(models[i].name, "4-bit Quantized");
                };
                Button::render(use4bitButton);

                float _4BitQantizationHeight = ImGui::GetTextLineHeightWithSpacing();
                ImGui::SameLine(0.0f, 4.0f);

                // 4-bit Quantized label:
                LabelConfig _4BitQuantizationLabel;
                _4BitQuantizationLabel.id = "##quantization" + std::to_string(i);
                _4BitQuantizationLabel.label = "Use 4-bit quantization";
                _4BitQuantizationLabel.size = ImVec2(0, 0);
                _4BitQuantizationLabel.fontType = FontsManager::REGULAR;
                _4BitQuantizationLabel.fontSize = FontsManager::SM;
                _4BitQuantizationLabel.alignment = Alignment::LEFT;
                float labelOffsetY = (use4bitButton.size.y - ImGui::GetTextLineHeight()) / 4.0f + 2.0f;
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + labelOffsetY);
                Label::render(_4BitQuantizationLabel);
                // ----------------------------------------------------

                // Render select/download button at the bottom.
                ImGui::SetCursorPosY(cardHeight - 35);

                bool isSelected = models[i].name == Model::ModelManager::getInstance().getCurrentModelName() &&
                                  currentVariant == Model::ModelManager::getInstance().getCurrentVariantType();
                bool isDownloaded = Model::ModelManager::getInstance().isModelDownloaded(i, currentVariant);

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
                        // Commit the current variant before downloading.
                        std::string variant = Model::ModelManager::getInstance().getCurrentVariantForModel(models[i].name);
                        Model::ModelManager::getInstance().setPreferredVariant(models[i].name, variant);
                        Model::ModelManager::getInstance().downloadModel(i, variant);
                    };

                    if (Model::ModelManager::getInstance().getModelDownloadProgress(i, currentVariant) > 0.0)
                    {
						selectButton.id = "##cancel" + std::to_string(i);
                        selectButton.label = "Cancel";
                        selectButton.backgroundColor = RGBAToImVec4(200, 50, 50, 255);
                        selectButton.hoverColor = RGBAToImVec4(220, 70, 70, 255);
                        selectButton.activeColor = RGBAToImVec4(200, 50, 50, 255);
                        selectButton.icon = ICON_CI_CLOSE;
                        selectButton.onClick = [i, currentVariant]()
                            {
                                Model::ModelManager::getInstance().cancelDownload(i, currentVariant);
                            };
                        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - _4BitQantizationHeight - 6);
                        ImGui::ProgressBar(
                            Model::ModelManager::getInstance().getModelDownloadProgress(i, currentVariant) / 100.0,
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
                        std::string variant = Model::ModelManager::getInstance().getCurrentVariantForModel(models[i].name);
                        Model::ModelManager::getInstance().switchModel(models[i].name, variant);
                    };
                    selectButton.size = ImVec2(cardWidth - 18 /*gap*/ - 5 /*delete button size*/ - 24, 0);
                }

                Button::render(selectButton);

				static bool openModelDeleteModal = false;

                if (isDownloaded)
                {
                    ImGui::SameLine();
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2);
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 24 - 2);

                    // Render the "Delete" button on the same row.
                    ButtonConfig deleteButton;
                    deleteButton.id = "##delete" + std::to_string(i);
                    deleteButton.size = ImVec2(24, 0);
                    deleteButton.backgroundColor = RGBAToImVec4(200, 50, 50, 255);
                    deleteButton.hoverColor = RGBAToImVec4(220, 70, 70, 255);
                    deleteButton.activeColor = RGBAToImVec4(200, 50, 50, 255);
                    deleteButton.icon = ICON_CI_TRASH; // Use your trash icon.
                    deleteButton.onClick = []()
                        {
							openModelDeleteModal = true;
                        };
                    Button::render(deleteButton);
                }

				renderDeleteModelModal(openModelDeleteModal, i, currentVariant);

                ImGui::EndChild();

                if (ImGui::IsItemHovered() || isSelected)
                {
                    ImVec2 min = ImGui::GetItemRectMin();
                    ImVec2 max = ImGui::GetItemRectMax();
                    ImU32 borderColor = IM_COL32(172, 131, 255, 255 / 2);
                    ImGui::GetWindowDrawList()->AddRect(min, max, borderColor, 8.0f, 0, 1.0f);
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
        openModal
    };

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
    static bool openModelSelectionModal = false;
	static bool openClearChatModal      = false;

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
    { openModelSelectionModal = true; };

    buttons.push_back(openModelManager);

	ButtonConfig clearChatButton;
	clearChatButton.id = "##clearChatButton";
	clearChatButton.icon = ICON_CI_CLEAR_ALL;
	clearChatButton.size = ImVec2(24, 0);
	clearChatButton.alignment = Alignment::CENTER;
	clearChatButton.onClick = []()
		{
			openClearChatModal = true;
		};
	clearChatButton.tooltip = "Clear Chat";

	buttons.push_back(clearChatButton);

    // Render the button using renderGroup
    Button::renderGroup(buttons, startX, startY);

    // Open the modal window if the button was clicked
    renderModelManager(openModelSelectionModal);
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
        // TODO: Implement assistant response through callback
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