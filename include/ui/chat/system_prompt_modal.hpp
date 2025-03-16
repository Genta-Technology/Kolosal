#pragma once

#include "imgui.h"
#include "model/preset_manager.hpp"
#include "ui/widgets.hpp"
#include "config.hpp"
#include <string>

class SystemPromptModalComponent {
public:
    SystemPromptModalComponent(float& sidebarWidth)
        : m_sidebarWidth(sidebarWidth)
    {
    }

    void render(bool& showDialog, std::string& sharedSystemPromptBuffer, bool& focusEditor) {
        // Always render the modal so that it stays open if already open.
        ModalConfig config{
            "Edit System Prompt",     // Title
            "System Prompt Editor",   // Identifier
            ImVec2(600, 400),         // Larger size for text editing
            [&]() {
                // Get the current preset
                auto currentPresetOpt = Model::PresetManager::getInstance().getCurrentPreset();
                if (!currentPresetOpt) return;

                // Create a multiline text input for the system prompt using the shared buffer
                InputFieldConfig inputConfig(
                    "##systempromptmodal",
                    ImVec2(ImGui::GetWindowSize().x - 32.0f, ImGui::GetWindowSize().y - 64),
                    sharedSystemPromptBuffer,
                    focusEditor
                );
                inputConfig.placeholderText = "Enter your system prompt here...";
                inputConfig.flags = ImGuiInputTextFlags_AllowTabInput;
                inputConfig.processInput = [&](const std::string& input) {
                    // Update shared buffer and preset directly
                    sharedSystemPromptBuffer = input;
                    if (currentPresetOpt) {
                        currentPresetOpt->get().systemPrompt = input;
                    }
                };

                // Render the multiline input
                InputField::renderMultiline(inputConfig);
            },
            showDialog // Initial open flag passed in.
        };

        // Set modal padding
        config.padding = ImVec2(16.0f, 8.0f);
        ModalWindow::render(config);

        // If the popup is no longer open, ensure showDialog remains false.
        if (!ImGui::IsPopupOpen(config.id.c_str()))
            showDialog = false;
    }

private:
    float& m_sidebarWidth;
};