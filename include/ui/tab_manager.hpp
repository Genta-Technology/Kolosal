#pragma once

#include "ui/chat/chat_history_sidebar.hpp"
#include "ui/chat/preset_sidebar.hpp"
#include "ui/chat/chat_window.hpp"

#include "chat/chat_manager.hpp"

#include "model/model_manager.hpp"

#include <memory>
#include <vector>

class ITab {
public:
    virtual ~ITab() = default;
    virtual void render() = 0;
    virtual void onActivate() = 0;
    virtual void onDeactivate() = 0;
};

// Update ChatTab to implement the new methods
class ChatTab : public ITab {
public:
    ChatTab()
        : chatHistorySidebar(), modelPresetSidebar(), chatWindow()
    {
    }

    void onActivate() override {
		Model::ModelManager& modelManager = Model::ModelManager::getInstance();

        modelManager.setStreamingCallback(
            [&modelManager](const std::string& partialOutput, const float tps, const int jobId) {
                auto& chatManager = Chat::ChatManager::getInstance();
                std::string chatName = chatManager.getChatNameByJobId(jobId);

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
        );
    }

    void onDeactivate() override {
        Model::ModelManager::getInstance().setStreamingCallback(nullptr);
    }

    void render() override {
        chatHistorySidebar.render();
        modelPresetSidebar.render();
        chatWindow.render(
            chatHistorySidebar.getSidebarWidth(),
            modelPresetSidebar.getSidebarWidth()
        );
    }

private:
    ChatHistorySidebar chatHistorySidebar;
    ModelPresetSidebar modelPresetSidebar;
    ChatWindow chatWindow;
};

// Update TabManager to handle tab activation/deactivation
class TabManager {
public:
    TabManager() : activeTabIndex(0) {}

    void addTab(std::unique_ptr<ITab> tab) {
        if (tabs.empty()) {
            // Activate the first tab when it's added
            tab->onActivate();
        }
        tabs.push_back(std::move(tab));
    }

    void switchTab(size_t index) {
        if (index < tabs.size() && index != activeTabIndex) {
            // Deactivate current tab
            if (activeTabIndex < tabs.size()) {
                tabs[activeTabIndex]->onDeactivate();
            }
            // Activate new tab
            activeTabIndex = index;
            tabs[activeTabIndex]->onActivate();
        }
    }

    void renderCurrentTab() {
        if (!tabs.empty() && activeTabIndex < tabs.size()) {
            tabs[activeTabIndex]->render();
        }
    }

private:
    std::vector<std::unique_ptr<ITab>> tabs;
    size_t activeTabIndex;
};