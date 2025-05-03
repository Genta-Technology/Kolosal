#pragma once

#include "imgui.h"
#include "config.hpp"
#include "ui/widgets.hpp"
#include "ui/markdown.hpp"
#include "chat/chat_manager.hpp"
#include "agent/tool.hpp"
#include "agent/tool_manager.hpp"
#include "mcp_sse_client.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <regex>

namespace ChatHistoryConstants {
    constexpr float MIN_SCROLL_DIFFERENCE = 1.0f;
    constexpr float THINK_LINE_THICKNESS = 1.0f;
    constexpr float THINK_LINE_PADDING = 8.0f;
    const ImU32 THINK_LINE_COLOR = IM_COL32(153, 153, 153, 153);
}

struct GroupedMessage {
    Chat::Message mainMessage;
    std::vector<Chat::Message> toolMessages;
    int originalIndex;
};

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

        // Tool styles
        toolCallButtonBase.id = "##toolCall";
        toolCallButtonBase.icon = ICON_CI_TOOLS;
        // We'll set label and size dynamically for each tool
        toolCallButtonBase.alignment = Alignment::LEFT;
        toolCallButtonBase.backgroundColor = ImVec4(0.2f, 0.3f, 0.2f, 0.4f);
        toolCallButtonBase.textColor = ImVec4(0.9f, 0.9f, 0.9f, 0.9f);

        timestampColor = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
        thinkTextColor = ImVec4(0.7f, 0.7f, 0.7f, 0.7f);
        toolMessageColor = ImVec4(0.6f, 0.8f, 0.6f, 0.9f);
        toolNameColor = ImVec4(0.4f, 0.7f, 0.4f, 1.0f);
        toolParamNameColor = ImVec4(0.7f, 0.7f, 0.4f, 1.0f);
        toolParamValueColor = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
        toolOutputColor = ImVec4(0.5f, 0.8f, 0.5f, 1.0f);
        bubbleBgColorUser = {
            Config::UserColor::COMPONENT,
            Config::UserColor::COMPONENT,
            Config::UserColor::COMPONENT,
            1.0f
        };
        bubbleBgColorAssistant = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        toolCallBgColor = ImVec4(0.15f, 0.2f, 0.15f, 0.5f);
    }

    void render(const Chat::ChatHistory& chatHistory, float contentWidth, float& paddingX)
    {
        const size_t currentMessageCount = chatHistory.messages.size();
        const bool newMessageAdded = currentMessageCount > m_lastMessageCount;

        const float scrollY = ImGui::GetScrollY();
        const float scrollMaxY = ImGui::GetScrollMaxY();
        const bool atBottom = (scrollMaxY <= 0.0f) || (scrollY >= scrollMaxY - ChatHistoryConstants::MIN_SCROLL_DIFFERENCE);

        // Group messages before rendering
        std::vector<GroupedMessage> groupedMessages = groupMessages(chatHistory);

        for (const auto& group : groupedMessages) {
            renderGroupedMessage(group, contentWidth, paddingX);
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

    // Match tool results to tool calls
    struct ToolInfo {
        Chat::ToolCall toolCall;
        std::string resultOutput;
        bool hasResult;
        bool isInCodeBlock;
        size_t codeBlockStartIndex;
    };

    // Structure for tracking code block state
    struct CodeBlockInfo {
        bool isInCodeBlock;
        size_t startIndex;
        size_t endIndex;
        std::string language;
    };

    std::vector<GroupedMessage> groupMessages(const Chat::ChatHistory& chatHistory) {
        std::vector<GroupedMessage> grouped;

        for (size_t i = 0; i < chatHistory.messages.size(); ++i) {
            const auto& msg = chatHistory.messages[i];

            // If this is a user message, create a new group
            if (msg.role == "user") {
                GroupedMessage group;
                group.mainMessage = msg;
                group.originalIndex = static_cast<int>(i);
                grouped.push_back(group);
                continue;
            }

            // If this is an assistant message
            if (msg.role == "assistant") {
                // Start a new group
                GroupedMessage group;
                group.mainMessage = msg;
                group.originalIndex = static_cast<int>(i);

                // Check if there are any subsequent tool messages to group with this assistant message
                size_t j = i + 1;
                while (j < chatHistory.messages.size() && chatHistory.messages[j].role == "tool") {
                    group.toolMessages.push_back(chatHistory.messages[j]);
                    j++;
                }

                grouped.push_back(group);
                i = j - 1; // Skip the tool messages we just processed
                continue;
            }

            // If this is a tool message
            if (msg.role == "tool") {
                // If there's a previous assistant message, add this tool message to that group
                if (!grouped.empty() && grouped.back().mainMessage.role == "assistant") {
                    grouped.back().toolMessages.push_back(msg);
                }
                else {
                    // If there's no previous assistant message, create a group with this tool message as main
                    GroupedMessage group;
                    group.mainMessage = msg; // Treat tool as main message if there's no assistant
                    group.originalIndex = static_cast<int>(i);
                    grouped.push_back(group);
                }
                continue;
            }

            // Any other role types just get their own group
            GroupedMessage group;
            group.mainMessage = msg;
            group.originalIndex = static_cast<int>(i);
            grouped.push_back(group);
        }

        return grouped;
    }

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

    // Helper function to detect if a position is inside a code block
    std::vector<CodeBlockInfo> findCodeBlocks(const std::string& content) {
        std::vector<CodeBlockInfo> codeBlocks;
        std::regex codeBlockStart(R"(```(\w*)(?:\n|$))");  // Match ```lang or just ```

        std::string::const_iterator searchStart(content.cbegin());
        std::smatch match;
        bool inCodeBlock = false;
        CodeBlockInfo currentBlock;

        while (std::regex_search(searchStart, content.cend(), match, codeBlockStart)) {
            size_t matchPos = std::distance(content.cbegin(), match[0].first);

            if (!inCodeBlock) {
                // Start of a new code block
                currentBlock.isInCodeBlock = true;
                currentBlock.startIndex = matchPos;
                currentBlock.language = match[1].str();
                inCodeBlock = true;
            }
            else {
                // End of a code block
                currentBlock.endIndex = matchPos + match[0].length();
                codeBlocks.push_back(currentBlock);
                inCodeBlock = false;
            }

            searchStart = match[0].second;
        }

        // Handle unclosed code block
        if (inCodeBlock) {
            currentBlock.endIndex = content.length();
            codeBlocks.push_back(currentBlock);
        }

        return codeBlocks;
    }

    // Find which code block contains a position, if any
    CodeBlockInfo* findContainingCodeBlock(size_t position, std::vector<CodeBlockInfo>& codeBlocks) {
        for (auto& block : codeBlocks) {
            if (position > block.startIndex && position < block.endIndex) {
                return &block;
            }
        }
        return nullptr;
    }

    // Helper function to extract visible content by removing tool call sections
    std::string getVisibleContent(const std::string& content, const std::vector<Chat::ToolCall>& toolCalls) {
        if (toolCalls.empty()) {
            return content;
        }

        std::string visibleContent;
        size_t lastEnd = 0;

        // Find all code blocks in the content
        std::vector<CodeBlockInfo> codeBlocks = findCodeBlocks(content);

        // Sort tool calls by start_index to process them in order
        std::vector<Chat::ToolCall> sortedToolCalls = toolCalls;
        std::sort(sortedToolCalls.begin(), sortedToolCalls.end(),
            [](const Chat::ToolCall& a, const Chat::ToolCall& b) {
                return a.start_index < b.start_index;
            });

        // Check if any tool calls are in code blocks
        for (auto& toolCall : sortedToolCalls) {
            CodeBlockInfo* codeBlock = findContainingCodeBlock(toolCall.start_index, codeBlocks);
            if (codeBlock) {
                // If this tool call is in a code block, stop at the start of the code block
                if (codeBlock->startIndex > lastEnd) {
                    visibleContent += content.substr(lastEnd, codeBlock->startIndex - lastEnd);
                }

                // Don't render anything after this code block tool call
                return visibleContent;
            }
        }

        // If no tool calls are in code blocks, process normally
        for (const auto& toolCall : sortedToolCalls) {
            // Add text before this tool call
            if (toolCall.start_index > lastEnd && toolCall.start_index < content.size()) {
                visibleContent += content.substr(lastEnd, toolCall.start_index - lastEnd);
            }

            // Skip the tool call text
            lastEnd = toolCall.end_index;
        }

        // Add any remaining text after the last tool call
        if (lastEnd < content.size()) {
            visibleContent += content.substr(lastEnd);
        }

        return visibleContent;
    }

    // Parse tool results from tool messages and match them with tool calls
    std::vector<ToolInfo> matchToolCallsWithResults(const std::vector<Chat::ToolCall>& toolCalls,
        const std::vector<Chat::Message>& toolMessages,
        const std::string& content) {
        std::vector<ToolInfo> toolInfos;

        // Get all code blocks from the content
        std::vector<CodeBlockInfo> codeBlocks = findCodeBlocks(content);

        // First convert tool calls to ToolInfo objects
        for (const auto& toolCall : toolCalls) {
            ToolInfo info;
            info.toolCall = toolCall;
            info.hasResult = false;
            info.codeBlockStartIndex = 0;

            CodeBlockInfo* codeBlock = findContainingCodeBlock(toolCall.start_index, codeBlocks);
            if (codeBlock) {
                info.isInCodeBlock = true;
                info.codeBlockStartIndex = codeBlock->startIndex;
            }
            else {
                info.isInCodeBlock = false;
            }

            toolInfos.push_back(info);
        }

        // Then try to match results from tool messages
        for (const auto& toolMsg : toolMessages) {
            try {
                json toolData = json::parse(toolMsg.content);
                if (!toolData.contains("tool_results") || !toolData["tool_results"].is_array()) {
                    continue;
                }

                auto results = toolData["tool_results"];
                for (const auto& result : results) {
                    if (!result.contains("tool_name") || !result.contains("output")) {
                        continue;
                    }

                    std::string toolName = result["tool_name"].get<std::string>();
                    std::string output = result["output"].get<std::string>();

                    // Find matching tool call
                    for (auto& toolInfo : toolInfos) {
                        if (toolInfo.toolCall.func_name == toolName && !toolInfo.hasResult) {
                            toolInfo.resultOutput = output;
                            toolInfo.hasResult = true;
                            break;
                        }
                    }
                }
            }
            catch (const std::exception& e) {
                // Skip non-JSON tool messages
                continue;
            }
        }

        return toolInfos;
    }

    void renderTools(const std::vector<Chat::ToolCall>& toolCalls,
        const std::vector<Chat::Message>& toolMessages,
        const std::string& msgId,
        const std::string& content,
        float availableWidth) {
        if (toolCalls.empty()) return;

        // Match tool calls with their corresponding results
        std::vector<ToolInfo> toolInfos = matchToolCallsWithResults(toolCalls, toolMessages, content);

        ImGui::Dummy({ 0, 5.0f });

        // Render each tool call with its result in a collapsible section
        for (size_t i = 0; i < toolInfos.size(); i++) {
            const auto& toolInfo = toolInfos[i];
            const std::string uniqueID = msgId + "_tool_" + std::to_string(i);
            bool& showToolCall = m_toolCallToggleStates.try_emplace(uniqueID, true).first->second;

            // Clone base config and set dynamic properties
            ButtonConfig toolCallBtn = toolCallButtonBase;
            toolCallBtn.id = "##" + uniqueID;
            toolCallBtn.label = toolInfo.toolCall.func_name;
            toolCallBtn.icon = showToolCall ? ICON_CI_CHEVRON_DOWN : ICON_CI_CHEVRON_RIGHT;
            toolCallBtn.onClick = [&showToolCall] { showToolCall = !showToolCall; };
            toolCallBtn.fontSize = FontsManager::SM;

            // Calculate button width based on text content - add some padding
            float textWidth = ImGui::CalcTextSize((ICON_CI_TOOLS " " + toolInfo.toolCall.func_name).c_str()).x;
            toolCallBtn.size = ImVec2(textWidth + 24.0f, 0);

            ImGui::NewLine();
            Button::render(toolCallBtn);

            if (showToolCall) {
                ImGui::Dummy({ 0, 5.0f });

                // Container for the tool call and result
                ImGui::PushStyleColor(ImGuiCol_ChildBg, toolCallBgColor);
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
                ImGui::BeginChild(("##toolcalls_container_" + uniqueID).c_str(),
                    { ImGui::GetContentRegionAvail().x, 0 },
                    ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Border);

                // Parameters section
                ImGui::Indent(10.0f);
                ImGui::TextUnformatted("Parameters:");

                ImGui::Indent(10.0f);
                for (const auto& [paramName, paramValue] : toolInfo.toolCall.params) {
                    ImGui::PushStyleColor(ImGuiCol_Text, toolParamNameColor);
                    ImGui::TextUnformatted(paramName.c_str());
                    ImGui::SameLine();
                    ImGui::TextUnformatted(": ");
                    ImGui::PopStyleColor();

                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Text, toolParamValueColor);
                    ImGui::TextWrapped("%s", paramValue.c_str());
                    ImGui::PopStyleColor();
                }
                ImGui::Unindent(10.0f);

                // Output section - show either from toolCall or from matched result
                std::string output;
                if (!toolInfo.toolCall.output.empty()) {
                    output = toolInfo.toolCall.output;
                }
                else if (toolInfo.hasResult) {
                    output = toolInfo.resultOutput;
                }

                if (!output.empty()) {
                    ImGui::TextUnformatted("Output:");
                    ImGui::Indent(10.0f);

                    ImGui::PushStyleColor(ImGuiCol_Text, toolOutputColor);
                    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + availableWidth - 40.0f);
                    ImGui::TextUnformatted(output.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::PopStyleColor();

                    ImGui::Unindent(10.0f);
                }
                ImGui::Unindent(10.0f);

                ImGui::EndChild();
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
            }

            // Small spacing between tool calls
            ImGui::Dummy({ 0, 5.0f });
        }
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

    void renderMessageContent(const GroupedMessage& group, float bubbleWidth, float bubblePadding, float& paddingX)
    {
        const Chat::Message& msg = group.mainMessage;

        if (msg.role == "user") {
            ImGui::SetCursorPosX(bubblePadding);
            ImGui::TextWrapped("%s", msg.content.c_str());
            return;
        }

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 24);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + paddingX);
        ImGui::BeginChild("##assistant_message_" + msg.id, { bubbleWidth, 0 }, ImGuiChildFlags_AutoResizeY);
        ImGui::BeginGroup();

        // For assistant messages, filter out tool call sections before rendering
        if (msg.role == "assistant" && !msg.toolCalls.empty()) {
            // Get visible content by removing tool call sections
            std::string visibleContent = getVisibleContent(msg.content, msg.toolCalls);

            // Parse think segments in the filtered content
            auto segments = parseThinkSegments(visibleContent);

            // Render the filtered content
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

            // Render tool calls and their results
            const float availableWidth = bubbleWidth - 2 * bubblePadding;
            renderTools(msg.toolCalls, group.toolMessages, std::to_string(msg.id), msg.content, availableWidth);
        }
        else {
            // For messages without tool calls, render normally
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
        }

        ImGui::EndGroup();
        ImGui::EndChild();
    }

    static void chatStreamingCallback(const std::string& partialOutput, const float tps, const int jobId, bool& isFinished) {
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

                // Check for tool calls in the partial output
                if (Chat::ToolManager::containsToolCall(partialOutput)) {
                    // Extract tool calls
                    std::vector<Chat::ToolCall> toolCalls = Chat::ToolManager::extractToolCalls(partialOutput);
                    chat.messages.back().toolCalls = toolCalls;
                }

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

                // Check for tool calls in the new message
                if (Chat::ToolManager::containsToolCall(partialOutput)) {
                    assistantMsg.toolCalls = Chat::ToolManager::extractToolCalls(partialOutput);
                }

                chatManager.addMessage(chatName, assistantMsg);
            }
        }
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
            modelManager.stopJob(chatManager.getCurrentJobId(), modelManager.getCurrentModelName().value(), modelManager.getCurrentVariantType());

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

        int jobId = modelManager.startChatCompletionJob(completionParams, chatStreamingCallback,
            modelManager.getCurrentModelName().value(), modelManager.getCurrentVariantType());
        if (!chatManager.setCurrentJobId(jobId)) {
            std::cerr << "[ChatSection] Failed to set the current job ID.\n";
        }

        modelManager.setModelGenerationInProgress(true);
    }

    void renderMetadata(const GroupedMessage& group, float bubbleWidth, float bubblePadding, float& paddingX)
    {
        const Chat::Message& msg = group.mainMessage;
        ImGui::PushStyleColor(ImGuiCol_Text, timestampColor);
        if (msg.role == "assistant" || msg.role == "tool")
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + paddingX);

        float cursorX = ImGui::GetCursorPosX();

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
            cursorX + bubbleWidth -
            2 * Config::Button::WIDTH - bubblePadding
        );

        std::vector<ButtonConfig> helperButtons;

        if (msg.role == "assistant")
        {
            ButtonConfig regenBtn = regenerateButtonBase;
            regenBtn.id = "##regen" + std::to_string(group.originalIndex);
            regenBtn.onClick = [this, index = group.originalIndex]() {
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
        copyBtn.id = "##copy" + std::to_string(group.originalIndex);
        copyBtn.onClick = [this, group] {
            const Chat::Message& msg = group.mainMessage;

            // For assistant messages with tool calls, construct a clean version of the content
            std::string cleanContent;
            if (msg.role == "assistant" && !msg.toolCalls.empty()) {
                // Get the visible content without the tool call sections
                cleanContent = getVisibleContent(msg.content, msg.toolCalls);
            }
            else {
                cleanContent = msg.content;
            }

            // Create a combined string of main message and tool messages
            std::string combinedContent = cleanContent;

            // Add tool call information if available
            if (!group.mainMessage.toolCalls.empty()) {
                combinedContent += "\n\nTool Calls:";
                for (const auto& toolCall : group.mainMessage.toolCalls) {
                    combinedContent += "\n- " + toolCall.func_name + ":";
                    for (const auto& [paramName, paramValue] : toolCall.params) {
                        combinedContent += "\n  " + paramName + ": " + paramValue;
                    }
                    if (!toolCall.output.empty()) {
                        combinedContent += "\n  Output: " + toolCall.output;
                    }
                }
            }

            // Add tool results
            for (const auto& toolMsg : group.toolMessages) {
                try {
                    json toolData = json::parse(toolMsg.content);
                    if (toolData.contains("tool_results") && toolData["tool_results"].is_array()) {
                        combinedContent += "\n\nTool Results:";
                        for (const auto& result : toolData["tool_results"]) {
                            combinedContent += "\n- " + result["tool_name"].get<std::string>() + ":";
                            combinedContent += "\n  " + result["output"].get<std::string>();
                        }
                    }
                    else {
                        combinedContent += "\n\n" + toolMsg.content;
                    }
                }
                catch (...) {
                    combinedContent += "\n\n" + toolMsg.content;
                }
            }

            ImGui::SetClipboardText(combinedContent.c_str());
            };
        helperButtons.push_back(copyBtn);

        Button::renderGroup(helperButtons, ImGui::GetCursorPosX(), ImGui::GetCursorPosY());

        ImGui::PopStyleColor();
    }

    void renderGroupedMessage(const GroupedMessage& group, float contentWidth, float& paddingX)
    {
        const auto& msg = group.mainMessage;
        const auto [bubbleWidth, bubblePadding, msgPaddingX] = calculateDimensions(msg, contentWidth);

        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, Config::InputField::CHILD_ROUNDING);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, msg.role == "user"
            ? bubbleBgColorUser
            : bubbleBgColorAssistant);

        ImGui::SetCursorPosX(msgPaddingX + paddingX);

        if (msg.role == "user") {
            // Calculate height for user message
            ImVec2 textSize = ImGui::CalcTextSize(msg.content.c_str(), nullptr, true, bubbleWidth - 2 * bubblePadding);
            float height = textSize.y + 2 * bubblePadding + ImGui::GetTextLineHeightWithSpacing() + 12;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { bubblePadding, bubblePadding });
            ImGui::BeginChild(("##Msg" + std::to_string(msg.id)).c_str(),
                { bubbleWidth, height },
                ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysUseWindowPadding);
            ImGui::PopStyleVar();

            renderMessageContent(group, bubbleWidth - 2 * bubblePadding, bubblePadding, paddingX);
            ImGui::Spacing();
            renderMetadata(group, bubbleWidth, 0, paddingX);

            ImGui::EndChild();
        }
        else {
            // Display model name for assistant messages
            if (msg.role == "assistant" && !msg.modelName.empty())
            {
                // get width of the button based on the msg.modelName
                float modelNameWidth = ImGui::CalcTextSize(msg.modelName.c_str()).x;

                ButtonConfig modelNameConfig;
                modelNameConfig.id = "##modelNameMessage" + std::to_string(group.originalIndex);
                modelNameConfig.label = msg.modelName;
                modelNameConfig.icon = ICON_CI_SPARKLE;
                modelNameConfig.size = ImVec2(modelNameWidth + 24.0F, 0);
                modelNameConfig.fontSize = FontsManager::SM;
                modelNameConfig.alignment = Alignment::LEFT;
                modelNameConfig.state = ButtonState::DISABLED;
                modelNameConfig.tooltip = msg.modelName;
                Button::render(modelNameConfig);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 12);
            }

            renderMessageContent(group, bubbleWidth, bubblePadding, paddingX);
            ImGui::Spacing();
            renderMetadata(group, bubbleWidth, bubblePadding, paddingX);
        }

        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        ImGui::Dummy({ 0, 20.0f });
    }

    ButtonConfig thinkButtonBase;
    ButtonConfig copyButtonBase;
    ButtonConfig regenerateButtonBase;
    ButtonConfig toolCallButtonBase;

    ImVec4 timestampColor;
    ImVec4 thinkTextColor;
    ImVec4 toolMessageColor;
    ImVec4 toolNameColor;
    ImVec4 toolParamNameColor;
    ImVec4 toolParamValueColor;
    ImVec4 toolOutputColor;
    ImVec4 bubbleBgColorUser;
    ImVec4 bubbleBgColorAssistant;
    ImVec4 toolCallBgColor;

    size_t m_lastMessageCount = 0;
    std::unordered_map<std::string, bool> m_thinkToggleStates;
    std::unordered_map<std::string, bool> m_toolCallToggleStates;
};