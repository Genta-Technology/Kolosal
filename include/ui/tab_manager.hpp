#pragma once

#include "IconsCodicons.h"

#include "ui/chat/chat_history_sidebar.hpp"
#include "ui/chat/preset_sidebar.hpp"
#include "ui/chat/chat_window.hpp"
#include "ui/server/server_logs.hpp"
#include "ui/server/deployment_settings.hpp"

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
    virtual const char* getTitle() const = 0;
    virtual const char* getIcon() const = 0;
};

// Update ChatTab to implement the new methods
class ChatTab : public ITab {
public:
    ChatTab()
        : chatHistorySidebar(), modelPresetSidebar(), chatWindow()
    {
    }

    void onActivate() override {}
    void onDeactivate() override {}

    void render() override {
        chatHistorySidebar.render();
        modelPresetSidebar.render();
        chatWindow.render(
            chatHistorySidebar.getSidebarWidth(),
            modelPresetSidebar.getSidebarWidth()
        );
    }

    // Return a title for the Chat tab
    const char* getTitle() const override { return "Chat"; }

    // Return the icon for the Chat tab
    const char* getIcon() const override { return ICON_CI_COMMENT_DISCUSSION; }

private:
    ChatHistorySidebar chatHistorySidebar;
    ModelPresetSidebar modelPresetSidebar;
    ChatWindow chatWindow;
};

class ServerTab : public ITab {
public:
	ServerTab() : serverLogViewer(), deploymentSettingsSidebar()
    {
    }

    void onActivate() override {}
    void onDeactivate() override {}

    void render() override {
        deploymentSettingsSidebar.render();
        serverLogViewer.render(deploymentSettingsSidebar.getWidth());
    }

    // Return a title for the Chat tab
    const char* getTitle() const override { return "Server"; }

    // Return the icon for the Chat tab
    const char* getIcon() const override { return ICON_CI_SERVER_PROCESS; }

private:
    ServerLogViewer serverLogViewer;
	DeploymentSettingsSidebar deploymentSettingsSidebar;
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

    ITab* getTab(size_t index) const { return tabs.at(index).get(); }
    const size_t getTabCount() const { return tabs.size(); }
    const size_t getCurrentActiveTabIndex() const { return activeTabIndex; };

private:
    std::vector<std::unique_ptr<ITab>> tabs;
    size_t activeTabIndex;
};