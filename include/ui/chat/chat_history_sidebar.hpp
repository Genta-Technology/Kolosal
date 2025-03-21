#pragma once

#include "imgui.h"
#include "config.hpp"
#include "ui/widgets.hpp"
#include "chat/chat_manager.hpp"
#include "model/model_manager.hpp"

#include <optional>
#include <string>
#include <vector>
#include <ctime>

namespace ChatSidebarConstants {
    constexpr ImGuiWindowFlags SidebarFlags =
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoScrollbar;
}

class ChatHeaderComponent {
public:
    ChatHeaderComponent(const ButtonConfig& createNewChatConfig, const LabelConfig& recentsLabelConfig)
        : m_createNewChatConfig(createNewChatConfig), m_recentsLabelConfig(recentsLabelConfig)
    {
    }

    void render() {
        // Render the header label.
        Label::render(m_recentsLabelConfig);

        // Position and render the create-new-chat button.
        const ImVec2 labelSize = ImGui::CalcTextSize(m_recentsLabelConfig.label.c_str());
        constexpr float buttonHeight = 24.0f;
        // Place the button at the far right of the header.
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 22);
        // Vertically center the button with the label.
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ((labelSize.y - buttonHeight) / 2.0f));
        Button::render(m_createNewChatConfig);
        ImGui::Spacing();
    }

private:
    ButtonConfig m_createNewChatConfig;
    LabelConfig  m_recentsLabelConfig;
};

class ChatListComponent {
public:
    ChatListComponent(const ButtonConfig& baseChatButtonConfig, const ButtonConfig& baseDeleteButtonConfig)
        : m_baseChatButtonConfig(baseChatButtonConfig), m_baseDeleteButtonConfig(baseDeleteButtonConfig)
    {
    }

    void render(float sidebarWidth, float availableHeight) {
        auto& chatManager = Chat::ChatManager::getInstance();
        const auto chats = chatManager.getChats();  // Get a copy for safe iteration.
        const auto currentChatName = chatManager.getCurrentChatName();

        const ImVec2 contentArea(sidebarWidth, availableHeight);
        ImGui::BeginChild("ChatHistoryButtons", contentArea, false, ImGuiWindowFlags_NoScrollbar);

        for (const auto& chat : chats) {
            renderChatButton(chat, contentArea, currentChatName);
            renderDeleteButton(chat, contentArea);
            ImGui::Spacing();
        }

        ImGui::EndChild();
    }

private:
    ButtonConfig m_baseChatButtonConfig;
    ButtonConfig m_baseDeleteButtonConfig;

    void renderChatButton(const Chat::ChatHistory& chat, const ImVec2& contentArea,
        const std::optional<std::string>& currentChatName) {
        ButtonConfig config = m_baseChatButtonConfig;
        config.id = "##chat" + std::to_string(chat.id);
        config.label = chat.name;
        // Leave room for the delete button.
        config.size = ImVec2(contentArea.x - 44, 0);
        config.state = (currentChatName && *currentChatName == chat.name)
            ? ButtonState::ACTIVE : ButtonState::NORMAL;
        config.onClick = [chatName = chat.name]() {
            Chat::ChatManager::getInstance().switchToChat(chatName);
            };

        // Format the last modified time as a tooltip.
        std::time_t time = static_cast<std::time_t>(chat.lastModified);
        char timeStr[26];
        ctime_s(timeStr, sizeof(timeStr), &time);
        config.tooltip = "Last modified: " + std::string(timeStr);

        Button::render(config);
    }

    void renderDeleteButton(const Chat::ChatHistory& chat, const ImVec2& contentArea) {
        // Position the delete button on the right of the chat button.
        ImGui::SameLine(contentArea.x - 38);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3);

        ButtonConfig config = m_baseDeleteButtonConfig;
        config.id = "##delete" + std::to_string(chat.id);
        config.onClick = [chatName = chat.name]() {
            Chat::ChatManager::getInstance().deleteChat(chatName);
            };

        Button::render(config);
    }
};

class ChatHistorySidebar {
public:
    ChatHistorySidebar()
        : m_sidebarWidth(Config::ChatHistorySidebar::SIDEBAR_WIDTH),
        m_chatHeaderComponent(initCreateNewChatConfig(), initRecentsLabelConfig()),
        m_chatListComponent(initBaseChatButtonConfig(), initBaseDeleteButtonConfig())
    {
    }

    void render() {
        ImGuiIO& io = ImGui::GetIO();
        const float sidebarHeight = io.DisplaySize.y - Config::TITLE_BAR_HEIGHT - Config::FOOTER_HEIGHT;

        // Set up the sidebar window.
        ImGui::SetNextWindowPos(ImVec2(0, Config::TITLE_BAR_HEIGHT), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(m_sidebarWidth, sidebarHeight), ImGuiCond_Always);
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(Config::ChatHistorySidebar::MIN_SIDEBAR_WIDTH, sidebarHeight),
            ImVec2(Config::ChatHistorySidebar::MAX_SIDEBAR_WIDTH, sidebarHeight)
        );

        ImGui::Begin("Chat History", nullptr, ChatSidebarConstants::SidebarFlags);
        // Update the current sidebar width.
        m_sidebarWidth = ImGui::GetWindowSize().x;

        m_chatHeaderComponent.render();
        float availableHeight = sidebarHeight - ImGui::GetCursorPosY();
        m_chatListComponent.render(m_sidebarWidth, availableHeight);

        ImGui::End();
    }

	float getSidebarWidth() const {
		return m_sidebarWidth;
	}

private:
    float m_sidebarWidth;
    ChatHeaderComponent m_chatHeaderComponent;
    ChatListComponent m_chatListComponent;

    // Helper functions to initialize base configurations.
    static ButtonConfig initCreateNewChatConfig() {
        ButtonConfig config;
        config.id = "##createNewChat";
        config.label = "";
        config.icon = ICON_CI_ADD;
        config.size = ImVec2(24, 24);
        config.alignment = Alignment::CENTER;
        config.onClick = []() {
            Chat::ChatManager::getInstance().createNewChat(Chat::ChatManager::getDefaultChatName());
            };
        return config;
    }

    static ButtonConfig initBaseChatButtonConfig() {
        ButtonConfig config;
        config.id = "";
        config.label = "";
        config.icon = ICON_CI_COMMENT;
        config.size = ImVec2(0, 0);
        config.alignment = Alignment::LEFT;
        config.onClick = nullptr;
        config.state = ButtonState::NORMAL;
        config.fontSize = FontsManager::MD;
        return config;
    }

    static ButtonConfig initBaseDeleteButtonConfig() {
        ButtonConfig config;
        config.id = "";
        config.label = "";
        config.icon = ICON_CI_TRASH;
        config.size = ImVec2(24, 0);
        config.alignment = Alignment::CENTER;
        config.onClick = nullptr;
        config.state = ButtonState::NORMAL;
        config.fontSize = FontsManager::MD;
        config.tooltip = "Delete Chat";
        return config;
    }

    static LabelConfig initRecentsLabelConfig() {
        LabelConfig config;
        config.id = "##chathistory";
        config.label = "Recents";
        config.icon = ICON_CI_COMMENT;
        config.size = ImVec2(Config::Icon::DEFAULT_FONT_SIZE, 0);
        config.fontSize = FontsManager::MD;
        config.fontType = FontsManager::BOLD;
        return config;
    }
};