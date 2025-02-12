#pragma once

#include "imgui.h"
#include "config.hpp"
#include "ui/widgets.hpp"
#include "ui/markdown.hpp"
#include "chat/chat_manager.hpp"

#include <string>
#include <vector>
#include <unordered_map>

namespace ChatHistoryConstants {
    constexpr float MIN_SCROLL_DIFFERENCE = 1.0f;
    constexpr float THINK_LINE_THICKNESS = 1.0f;
    constexpr float THINK_LINE_PADDING = 8.0f;
    const ImU32 THINK_LINE_COLOR = IM_COL32(153, 153, 153, 153);
}

class ChatHistoryRenderer {
public:
    ChatHistoryRenderer()
    {
		thinkButtonBase.id = "##think";
		thinkButtonBase.icon = ICON_CI_CHEVRON_DOWN;
		thinkButtonBase.label = "Thoughts";
		thinkButtonBase.size = ImVec2(80, 0);
		thinkButtonBase.alignment = Alignment::LEFT;
		thinkButtonBase.backgroundColor = ImVec4(0.2f, 0.2f, 0.2f, 0.4f);
		thinkButtonBase.textColor = ImVec4(0.9f, 0.9f, 0.9f, 0.9f);

		copyButtonBase.id = "##copy";
		copyButtonBase.icon = ICON_CI_COPY;
		copyButtonBase.size = ImVec2(Config::Button::WIDTH, 0);
        copyButtonBase.tooltip = "Copy Text";

        regenerateButtonBase.id = "##regen";
        regenerateButtonBase.icon = ICON_CI_DEBUG_RERUN;
        regenerateButtonBase.size = ImVec2(Config::Button::WIDTH, 0);
        regenerateButtonBase.tooltip = "Regenerate Response";

		timestampColor = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
		thinkTextColor = ImVec4(0.7f, 0.7f, 0.7f, 0.7f);
        bubbleBgColorUser = {
            Config::UserColor::COMPONENT,
            Config::UserColor::COMPONENT,
            Config::UserColor::COMPONENT,
            1.0f
        };
		bubbleBgColorAssistant = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    void render(const Chat::ChatHistory& chatHistory, float contentWidth)
    {
        const size_t currentMessageCount = chatHistory.messages.size();
        const bool newMessageAdded = currentMessageCount > m_lastMessageCount;

        const float scrollY = ImGui::GetScrollY();
        const float scrollMaxY = ImGui::GetScrollMaxY();
        const bool atBottom = (scrollMaxY <= 0.0f) || (scrollY >= scrollMaxY - ChatHistoryConstants::MIN_SCROLL_DIFFERENCE);

        for (size_t i = 0; i < currentMessageCount; ++i) {
            renderMessage(chatHistory.messages[i], static_cast<int>(i), contentWidth);
        }

        if (newMessageAdded && atBottom) {
            ImGui::SetScrollHereY(1.0f);
        }

        m_lastMessageCount = currentMessageCount;
    }

private:
    struct MessageDimensions {
        float bubbleWidth;
        float bubblePadding;
        float paddingX;
    };

    std::vector<std::pair<bool, std::string>> parseThinkSegments(const std::string& content) const
    {
        std::vector<std::pair<bool, std::string>> segments;
        size_t current_pos = 0;
        const std::string open_tag = "<think>";
        const std::string close_tag = "</think>";

        while (current_pos < content.size()) {
            size_t think_start = content.find(open_tag, current_pos);
            if (think_start == std::string::npos) {
                std::string normal = content.substr(current_pos);
                if (!normal.empty()) {
                    segments.emplace_back(false, normal);
                }
                break;
            }

            // Add normal text before think tag
            if (think_start > current_pos) {
                segments.emplace_back(false, content.substr(current_pos, think_start - current_pos));
            }

            // Process think content
            size_t content_start = think_start + open_tag.size();
            size_t think_end = content.find(close_tag, content_start);

            if (think_end == std::string::npos) {
                segments.emplace_back(true, content.substr(content_start));
                break;
            }

            segments.emplace_back(true, content.substr(content_start, think_end - content_start));
            current_pos = think_end + close_tag.size();
        }

        return segments;
    }

    MessageDimensions calculateDimensions(const Chat::Message& msg, float windowWidth) const
    {
        MessageDimensions dim;
        dim.bubbleWidth = windowWidth * Config::Bubble::WIDTH_RATIO;
        dim.bubblePadding = Config::Bubble::PADDING;
        dim.paddingX = windowWidth - dim.bubbleWidth;

        if (msg.role == "assistant") {
            dim.bubbleWidth = windowWidth;
            dim.paddingX = 0;
        }

        return dim;
    }

    void renderMessageContent(const Chat::Message& msg, float bubbleWidth, float bubblePadding)
    {
        if (msg.role == "user") {
            ImGui::SetCursorPosX(bubblePadding);
            ImGui::TextWrapped("%s", msg.content.c_str());
            return;
        }

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 24);
        ImGui::BeginGroup();

        auto segments = parseThinkSegments(msg.content);
        for (size_t i = 0; i < segments.size(); ++i) {
            const auto& [isThink, text] = segments[i];

            if (isThink && text.find_first_not_of(" \t\n") == std::string::npos) {
                continue;
            }

            if (isThink) {
                const std::string uniqueID = msg.id + "_think_" + std::to_string(i);
                bool& showThink = m_thinkToggleStates.try_emplace(uniqueID, true).first->second;

                // Clone base config and set dynamic properties
                ButtonConfig thinkBtn = thinkButtonBase;
                thinkBtn.id = "##" + uniqueID;
                thinkBtn.icon = showThink ? ICON_CI_CHEVRON_DOWN : ICON_CI_CHEVRON_RIGHT;
                thinkBtn.onClick = [&showThink] { showThink = !showThink; };
				thinkBtn.fontSize = FontsManager::SM;

                ImGui::NewLine();
                Button::render(thinkBtn);

                if (showThink) {
                    const float availableWidth = bubbleWidth - 2 * bubblePadding;
                    const ImVec2 textSize = ImGui::CalcTextSize(text.c_str(), nullptr, false, availableWidth);
                    const float segmentHeight = textSize.y + 2 * bubblePadding;

                    const ImVec2 startPos = ImGui::GetCursorScreenPos();
                    ImDrawList* drawList = ImGui::GetWindowDrawList();

                    drawList->AddLine(
                        { startPos.x, startPos.y + 12 },
                        { startPos.x, startPos.y + 12 + segmentHeight },
                        ChatHistoryConstants::THINK_LINE_COLOR,
                        ChatHistoryConstants::THINK_LINE_THICKNESS
                    );

                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ChatHistoryConstants::THINK_LINE_THICKNESS + ChatHistoryConstants::THINK_LINE_PADDING);
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + availableWidth - ChatHistoryConstants::THINK_LINE_THICKNESS - ChatHistoryConstants::THINK_LINE_PADDING);

                    ImGui::PushStyleColor(ImGuiCol_Text, thinkTextColor);
                    ImGui::TextUnformatted(text.c_str());
                    ImGui::PopStyleColor();

                    ImGui::PopTextWrapPos();
                    ImGui::SetCursorScreenPos({ startPos.x, startPos.y + segmentHeight });
                    ImGui::Dummy({ 0, 5.0f });
                }
            }
            else {
                RenderMarkdown(text.c_str(), msg.id);
            }
        }

        ImGui::EndGroup();
    }

    void regenerateResponse(int index) {
        Model::ModelManager& modelManager = Model::ModelManager::getInstance();
        Chat::ChatManager& chatManager = Chat::ChatManager::getInstance();

        if (!modelManager.isModelLoaded())
		{
			std::cerr << "[ChatSection] No model loaded. Cannot regenerate response.\n";
			return;
		}

        // Stop current generation if running.
        if (modelManager.isCurrentlyGenerating()) {
            modelManager.stopJob(chatManager.getCurrentJobId());

            while (true)
            {
                if (!modelManager.isCurrentlyGenerating())
                    break;
            }
        }

        auto currentChatOpt = chatManager.getCurrentChat();
        if (!currentChatOpt.has_value()) {
            std::cerr << "[ChatSection] No chat selected. Cannot regenerate response.\n";
            return;
        }

        if (!modelManager.getCurrentModelName().has_value()) {
            std::cerr << "[ChatSection] No model selected. Cannot regenerate response.\n";
            return;
        }

        auto& currentChat = currentChatOpt.value();

        // Validate the provided index.
        if (index < 0 || index >= static_cast<int>(currentChat.messages.size())) {
            std::cerr << "[ChatSection] Invalid chat index (" << index << "). Cannot regenerate response.\n";
            return;
        }

        int userMessageIndex = -1;
        if (currentChat.messages[index].role == "user") {
            userMessageIndex = index;

            // Find the first assistant response after this user message.
            int targetAssistantIndex = -1;
            for (int i = index + 1; i < static_cast<int>(currentChat.messages.size()); i++) {
                if (currentChat.messages[i].role == "assistant") {
                    targetAssistantIndex = i;
                    break;
                }
            }
            if (targetAssistantIndex == -1) {
                std::cerr << "[ChatSection] No assistant response found after user message at index " << index << ".\n";
                return;
            }

            // Delete all messages from the first assistant response (targetAssistantIndex) to the end.
            // Deleting in reverse order avoids index shifting issues.
            for (int i = static_cast<int>(currentChat.messages.size()) - 1; i >= targetAssistantIndex; i--) {
                chatManager.deleteMessage(currentChat.name, i);
            }
        }
        else if (currentChat.messages[index].role == "assistant") {
            if (index - 1 < 0 || currentChat.messages[index - 1].role != "user") {
                std::cerr << "[ChatSection] Could not find an associated user message for assistant at index " << index << ".\n";
                return;
            }
            userMessageIndex = index - 1;

            // Delete all messages starting from this assistant response to the end.
            for (int i = static_cast<int>(currentChat.messages.size()) - 1; i >= index; i--) {
                chatManager.deleteMessage(currentChat.name, i);
            }
        }
        else {
            std::cerr << "[ChatSection] Message at index " << index << " is neither a user nor an assistant message. Cannot regenerate response.\n";
            return;
        }

        ChatCompletionParameters completionParams = modelManager.buildChatCompletionParameters(
            chatManager.getCurrentChat().value()
        );

        int jobId = modelManager.startChatCompletionJob(completionParams);
        if (!chatManager.setCurrentJobId(jobId)) {
            std::cerr << "[ChatSection] Failed to set the current job ID.\n";
        }
    }

    void renderMetadata(const Chat::Message& msg, int index, float bubbleWidth, float bubblePadding)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, timestampColor);

        // Timestamp
        ImGui::TextWrapped("%s", timePointToString(msg.timestamp).c_str());

        // TPS for assistant messages
        if (msg.role == "assistant") {
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10);
            ImGui::TextWrapped("TPS: %.2f", msg.tps);
        }

        // Copy button
        ImGui::SameLine();
        ImGui::SetCursorPosX(
            ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x -
            (msg.role == "assistant" ? 2 : 1) *
            Config::Button::WIDTH - bubblePadding
        );

        std::vector<ButtonConfig> helperButtons;

        if (msg.role == "assistant")
        {
            ButtonConfig regenBtn = regenerateButtonBase;
            regenBtn.id = "##regen" + std::to_string(index);
            regenBtn.onClick = [this, index]() {
                regenerateResponse(index - 1);
            };

            if (!Model::ModelManager::getInstance().isModelLoaded())
            {
				regenBtn.state = ButtonState::DISABLED;
				regenBtn.tooltip = "No model loaded";
            }

            helperButtons.push_back(regenBtn);
        }

        ButtonConfig copyBtn = copyButtonBase;
        copyBtn.id = "##copy" + std::to_string(index);
        copyBtn.onClick = [index] {
            if (auto chat = Chat::ChatManager::getInstance().getCurrentChat()) {
                ImGui::SetClipboardText(chat->messages[index].content.c_str());
            }
            };
        helperButtons.push_back(copyBtn);

        Button::renderGroup(helperButtons, ImGui::GetCursorPosX(), ImGui::GetCursorPosY());

        ImGui::PopStyleColor();
    }

    void renderMessage(const Chat::Message& msg, int index, float contentWidth)
    {
        const auto [bubbleWidth, bubblePadding, paddingX] = calculateDimensions(msg, contentWidth);

        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, Config::InputField::CHILD_ROUNDING);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, msg.role == "user"
            ? bubbleBgColorUser
            : bubbleBgColorAssistant);

        ImGui::SetCursorPosX(paddingX);

        if (msg.role == "user") {
            ImVec2 textSize = ImGui::CalcTextSize(msg.content.c_str(), nullptr, true, bubbleWidth - 2 * bubblePadding);
            float height = textSize.y + 2 * bubblePadding + ImGui::GetTextLineHeightWithSpacing() + 12;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { bubblePadding, bubblePadding });
            ImGui::BeginChild(("##Msg" + std::to_string(msg.id)).c_str(),
                { bubbleWidth, height },
                ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysUseWindowPadding);
            ImGui::PopStyleVar();

            renderMessageContent(msg, bubbleWidth - 2 * bubblePadding, bubblePadding);
            ImGui::Spacing();
            renderMetadata(msg, index, bubbleWidth, 0);

            ImGui::EndChild();
        }
        else {
            if (!msg.modelName.empty())
            {
                // get width of the button based on the msg.modelName
                float modelNameWidth = ImGui::CalcTextSize(msg.modelName.c_str()).x;

                ButtonConfig modelNameConfig;
                modelNameConfig.id = "##modelNameMessage" + std::to_string(index);
                modelNameConfig.label = msg.modelName;
                modelNameConfig.icon = ICON_CI_SPARKLE;
                modelNameConfig.size = ImVec2(modelNameWidth, 0);
                modelNameConfig.fontSize = FontsManager::SM;
                modelNameConfig.alignment = Alignment::LEFT;
                modelNameConfig.state = ButtonState::DISABLED;
                modelNameConfig.tooltip = msg.modelName;
                Button::render(modelNameConfig);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 12);
            }

            renderMessageContent(msg, bubbleWidth, bubblePadding);
            ImGui::Spacing();
            renderMetadata(msg, index, bubbleWidth, bubblePadding);
        }

        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        ImGui::Dummy({ 0, 20.0f });
    }

    ButtonConfig thinkButtonBase;
    ButtonConfig copyButtonBase;
    ButtonConfig regenerateButtonBase;

    ImVec4 timestampColor;
    ImVec4 thinkTextColor;
    ImVec4 bubbleBgColorUser;
    ImVec4 bubbleBgColorAssistant;

    size_t m_lastMessageCount = 0;
    std::unordered_map<std::string, bool> m_thinkToggleStates;
};