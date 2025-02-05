#pragma once

#include "imgui.h"
#include "config.hpp"
#include "ui/widgets.hpp"
#include "chat/chat_manager.hpp"

class ChatHistorySidebar {
public:
    ChatHistorySidebar() {
        // Initialize reusable configurations
        {
            createNewChatButtonConfig.id = "##createNewChat";
            createNewChatButtonConfig.label = "";
            createNewChatButtonConfig.icon = ICON_CI_ADD;
            createNewChatButtonConfig.size = ImVec2(24, 24);
            createNewChatButtonConfig.alignment = Alignment::CENTER;
            createNewChatButtonConfig.onClick = []() { Chat::ChatManager::getInstance().createNewChat(Chat::ChatManager::getDefaultChatName()); };
        }

        {
			baseChatButtonConfig.id = "";
			baseChatButtonConfig.label = "";
			baseChatButtonConfig.icon = ICON_CI_COMMENT;
			baseChatButtonConfig.size = ImVec2(0, 0);
			baseChatButtonConfig.alignment = Alignment::LEFT;
			baseChatButtonConfig.onClick = nullptr;
			baseChatButtonConfig.state = ButtonState::NORMAL;
			baseChatButtonConfig.fontSize = FontsManager::MD;
        }

        {
			baseDeleteButtonConfig.id = "";
			baseDeleteButtonConfig.label = "";
			baseDeleteButtonConfig.icon = ICON_CI_TRASH;
			baseDeleteButtonConfig.size = ImVec2(24, 0);
			baseDeleteButtonConfig.alignment = Alignment::CENTER;
			baseDeleteButtonConfig.onClick = nullptr;
			baseDeleteButtonConfig.state = ButtonState::NORMAL;
			baseDeleteButtonConfig.fontSize = FontsManager::MD;
			baseDeleteButtonConfig.tooltip = "Delete Chat";
		}

		{
			recentsLabelConfig.id = "##chathistory";
			recentsLabelConfig.label = "Recents";
			recentsLabelConfig.icon = ICON_CI_COMMENT;
			recentsLabelConfig.size = ImVec2(Config::Icon::DEFAULT_FONT_SIZE, 0);
			recentsLabelConfig.fontSize = FontsManager::MD;
			recentsLabelConfig.fontType = FontsManager::BOLD;
		}

        sidebarWidth = Config::ChatHistorySidebar::SIDEBAR_WIDTH;
    }

    void render() {
        ImGuiIO& io = ImGui::GetIO();
        const float sidebarHeight = io.DisplaySize.y - Config::TITLE_BAR_HEIGHT;

        ImGui::SetNextWindowPos(ImVec2(0, Config::TITLE_BAR_HEIGHT), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(sidebarWidth, sidebarHeight), ImGuiCond_Always);
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(Config::ChatHistorySidebar::MIN_SIDEBAR_WIDTH, sidebarHeight),
            ImVec2(Config::ChatHistorySidebar::MAX_SIDEBAR_WIDTH, sidebarHeight));

        ImGui::Begin("Chat History", nullptr, sidebarFlags);
        sidebarWidth = ImGui::GetWindowSize().x;

        renderHeader();
        renderChatList(sidebarHeight);

        ImGui::End();
    }

private:
    float sidebarWidth;
    ButtonConfig createNewChatButtonConfig;
    ButtonConfig baseChatButtonConfig;
    ButtonConfig baseDeleteButtonConfig;
    LabelConfig recentsLabelConfig;

    static constexpr ImGuiWindowFlags sidebarFlags =
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoScrollbar;

    void renderHeader() {
        Label::render(recentsLabelConfig);

        // Position create new chat button
        const ImVec2 labelSize = ImGui::CalcTextSize(recentsLabelConfig.label.c_str());
        const float buttonHeight = 24.0f;
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 22);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ((labelSize.y - buttonHeight) / 2.0f));

        Button::render(createNewChatButtonConfig);
        ImGui::Spacing();
    }

    void renderChatList(float sidebarHeight) {
        const auto& chatManager = Chat::ChatManager::getInstance();
        const auto chats = chatManager.getChats(); // Get copy to ensure iteration safety
        const auto currentChatName = chatManager.getCurrentChatName();

        const ImVec2 contentArea(sidebarWidth, sidebarHeight - ImGui::GetCursorPosY());
        ImGui::BeginChild("ChatHistoryButtons", contentArea, false, ImGuiWindowFlags_NoScrollbar);

        for (const auto& chat : chats) {
            renderChatButton(chat, contentArea, currentChatName);
            renderDeleteButton(chat, contentArea);
            ImGui::Spacing();
        }

        ImGui::EndChild();
    }

    void renderChatButton(const Chat::ChatHistory& chat, const ImVec2& contentArea,
        const std::optional<std::string>& currentChatName) {
        ButtonConfig config = baseChatButtonConfig;
        config.id = "##chat" + std::to_string(chat.id);
        config.label = chat.name;
        config.size = ImVec2(contentArea.x - 44, 0);
        config.state = (currentChatName && *currentChatName == chat.name)
            ? ButtonState::ACTIVE : ButtonState::NORMAL;
        config.onClick = [chatName = chat.name]() {
            Chat::ChatManager::getInstance().switchToChat(chatName);
            };

        // Format last modified time
        std::time_t time = static_cast<std::time_t>(chat.lastModified);
        char timeStr[26];
        ctime_s(timeStr, sizeof(timeStr), &time);
        config.tooltip = "Last modified: " + std::string(timeStr);

        Button::render(config);
    }

    void renderDeleteButton(const Chat::ChatHistory& chat, const ImVec2& contentArea) {
        ImGui::SameLine(contentArea.x - 38);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3);

        ButtonConfig config = baseDeleteButtonConfig;
        config.id = "##delete" + std::to_string(chat.id);
        config.onClick = [chatName = chat.name]() {
            Chat::ChatManager::getInstance().deleteChat(chatName);
            };

        Button::render(config);
    }
};