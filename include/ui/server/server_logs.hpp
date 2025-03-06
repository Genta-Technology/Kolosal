#pragma once

#include "imgui.h"
#include "ui/widgets.hpp"
#include "ui/chat/model_manager_modal.hpp"
#include "model/model_manager.hpp"

#include <IconsCodicons.h>

class ServerLogViewer {
public:
	ServerLogViewer() {
		m_logBuffer = "Server logs will be displayed here.";
	}

    void render(const float sidebarWidth) {
        ImGuiIO& io = ImGui::GetIO();
		Model::ModelManager& modelManager = Model::ModelManager::getInstance();

        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
        ImGui::SetNextWindowPos(ImVec2(0, Config::TITLE_BAR_HEIGHT), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x - sidebarWidth, io.DisplaySize.y - Config::TITLE_BAR_HEIGHT), ImGuiCond_Always);
        ImGui::Begin("Server Logs", nullptr, window_flags);
        ImGui::PopStyleVar();

        {
            ButtonConfig startServerButtonConfig;
            startServerButtonConfig.id = "##server_start_server_button";
            startServerButtonConfig.label = "Start Server";
            startServerButtonConfig.icon = ICON_CI_RUN;
            startServerButtonConfig.tooltip = "Start the server";
            startServerButtonConfig.size = ImVec2(128, 0);
            startServerButtonConfig.alignment = Alignment::LEFT;

			ButtonConfig selectModelButtonConfig;
			selectModelButtonConfig.id = "##server_select_model_button";
			selectModelButtonConfig.label = 
                modelManager.getCurrentModelName().value_or("Select Model");
			selectModelButtonConfig.tooltip = 
                modelManager.getCurrentModelName().value_or("Select Model");
			selectModelButtonConfig.icon = ICON_CI_SPARKLE;
			selectModelButtonConfig.size = ImVec2(128, 0);
            selectModelButtonConfig.alignment = Alignment::LEFT;
			selectModelButtonConfig.onClick = [this]() {
				m_modelManagerModalOpen = true;
				};

            if (modelManager.isLoadInProgress())
            {
                selectModelButtonConfig.label = "Loading Model...";
            }

            if (modelManager.isModelLoaded())
            {
                selectModelButtonConfig.icon = ICON_CI_SPARKLE_FILLED;
            }

			std::vector<ButtonConfig> buttons = { 
                startServerButtonConfig, selectModelButtonConfig };

			Button::renderGroup(buttons, ImGui::GetCursorPosX(), ImGui::GetCursorPosY());

            m_modelManagerModal.render(m_modelManagerModalOpen);
        }

		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 12);

        {
            InputFieldConfig input_cfg(
                "##server_log_input",
                ImVec2(-FLT_MIN, -FLT_MIN),
                m_logBuffer,
                m_isLogFocused
            );

            input_cfg.frameRounding = 4.0f;
            input_cfg.flags = ImGuiInputTextFlags_ReadOnly;
            input_cfg.backgroundColor = ImVec4(0.2f, 0.2f, 0.2f, 0.5f);
            InputField::renderMultiline(input_cfg);
        }

        ImGui::End();
    }

private:
    bool m_isLogFocused = false;
	std::string m_logBuffer;

	ModelManagerModal m_modelManagerModal;
	bool m_modelManagerModalOpen = false;
};
