#pragma once

#include "imgui.h"
#include "ui/widgets.hpp"
#include "model/model_manager.hpp"
#include "model/server_state_manager.hpp"

class ServerModelList {
public:
    void render(const float height = 120) {
        // Set border radius
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);

        // Custom scrollbar styling - hide background, only show handle
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

        ImGui::BeginChild("##server_model_list", ImVec2(0, height), true);
        ImGui::PopStyleVar();

        ImGui::Text("Loaded Models");
        ImGui::Separator();

        auto& modelManager  = Model::ModelManager::getInstance();
		auto& serverState   = ServerStateManager::getInstance();
        std::vector<std::string> modelInServerNames = modelManager.getModelNamesInServer();

        if (modelInServerNames.empty()) {
            ImGui::Text("No models loaded.");
        }
        else
        {
            // Create a child window for horizontal scrolling
            const float listHeight = height - ImGui::GetCursorPosY() - 20; // Account for title and padding
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8);

            // Set up horizontal scrolling region
            ImGui::BeginChild("##horizontal_scroll_region", ImVec2(ImGui::GetContentRegionAvail().x, listHeight), false,
                ImGuiWindowFlags_HorizontalScrollbar);

            // Start horizontal layout
            ImGui::BeginGroup();

            // Iterate through all models in the server
            for (const auto& modelId : modelInServerNames) {
				std::string modelName = modelId;
				std::string modelVariant = modelId;
				// get model name from modelId (modelId is in the format "modelName:variantName")
				if (modelName.find(':') != std::string::npos) {
					modelName    = modelName.substr(0, modelName.find(':'));
					modelVariant = modelId.substr(modelId.find(':') + 1);
				}

                // Get the model data
                const auto& modelData = modelManager.getModelLocked(modelName);
				if (!modelData) {
					continue;
				}

                // Model card - use fixed width for horizontal layout
                ImGui::PushStyleColor(ImGuiCol_ChildBg, RGBAToImVec4(26, 26, 26, 128));
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0F);

                // Start the card with fixed width but dynamic height
                ImGui::BeginChild(("##model_card_" + modelId).c_str(), ImVec2(200, listHeight), true);

                // author
                LabelConfig authorLabel;
                authorLabel.id = "##modelAuthor" + modelId;
                authorLabel.label = modelData->author;
                authorLabel.size = ImVec2(0, 0);
                authorLabel.fontType = FontsManager::ITALIC;
                authorLabel.fontSize = FontsManager::SM;
                authorLabel.alignment = Alignment::LEFT;
                Label::render(authorLabel);

                // name
				LabelConfig nameLabel;
				nameLabel.id = "##modelName" + modelId;
				nameLabel.label = modelData->name;
				nameLabel.size = ImVec2(0, 0);
				nameLabel.fontType = FontsManager::BOLD;
				nameLabel.fontSize = FontsManager::MD;
				nameLabel.alignment = Alignment::LEFT;
				Label::render(nameLabel);

                // Add reload button if model params have changed
                if (serverState.haveModelParamsChanged(modelId)) {
                    ButtonConfig reloadModelButtonConfig;
                    reloadModelButtonConfig.id = "##reload_model_button" + modelId;
                    reloadModelButtonConfig.icon = ICON_CI_REFRESH;
                    reloadModelButtonConfig.tooltip = "Reload model with new parameters";
                    reloadModelButtonConfig.size = ImVec2(24, 24);
                    reloadModelButtonConfig.alignment = Alignment::CENTER;
                    reloadModelButtonConfig.backgroundColor = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
                    reloadModelButtonConfig.onClick = [this, &modelName, &modelVariant, &modelId, &modelManager, &serverState]() {
						// unload, wait, then load the model again
						modelManager.reloadModel(modelName, modelVariant);
                        serverState.resetModelParamsChanged(modelId);
                        };

                    // Disable the reload button if server is running or model is loading
                    if (serverState.isServerRunning() || serverState.isModelLoadInProgress()) {
                        reloadModelButtonConfig.state = ButtonState::DISABLED;
                    }

                    ImGui::SameLine();
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 30);
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3);
                    Button::render(reloadModelButtonConfig);
                }

                // name id
                ButtonConfig nameButton;
                nameButton.id = "##modelNameId" + modelId;
                nameButton.label = modelId;
                nameButton.icon = ICON_CI_COPY;
                nameButton.size = ImVec2(ImGui::GetContentRegionAvail().x, 0);
                nameButton.fontType = FontsManager::BOLD;
                nameButton.fontSize = FontsManager::SM;
                nameButton.alignment = Alignment::LEFT;
                nameButton.onClick = [this, modelId]() {
                    // copy model name to clipboard
                    ImGui::SetClipboardText(modelId.c_str());
                    };
                // yellow terminal like text color
                nameButton.textColor = ImVec4(1.0f, 1.0f, 0.5f, 1.0f);
                nameButton.backgroundColor = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6);
                Button::render(nameButton);

				// Variant
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6);

                {
					ImGui::BeginGroup();
                    LabelConfig variantLabel;
                    variantLabel.id = "##modelVariantLabel" + modelId;
                    variantLabel.label = "Variant: ";
                    variantLabel.size = ImVec2(0, 0);
                    variantLabel.fontType = FontsManager::ITALIC;
                    variantLabel.fontSize = FontsManager::SM;
                    variantLabel.alignment = Alignment::LEFT;
                    Label::render(variantLabel);
                    ImGui::SameLine();

					LabelConfig variantValueLabel;
					variantValueLabel.id = "##modelVariantValue" + modelId;
					variantValueLabel.label = modelVariant;
					variantValueLabel.size = ImVec2(ImGui::GetContentRegionAvail().x, 0);
					variantValueLabel.fontSize = FontsManager::SM;
					variantValueLabel.alignment = Alignment::RIGHT;
					variantValueLabel.color = ImVec4(1.0f, 1.0f, 0.5f, 1.0f);
                    ImGui::SetCursorPos({
						ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(variantValueLabel.label.c_str()).x,
						ImGui::GetCursorPosY() - 3
                        });
					Label::render(variantValueLabel);
					ImGui::EndGroup();
                }

                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);

                if ((modelManager.isLoadInProgress() && modelId == modelManager.getCurrentOnLoadingModel()) ||
                    (modelManager.isUnloadInProgress() && modelId == modelManager.getCurrentOnUnloadingModel()))
                {
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetContentRegionAvail().y - 40);
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4);
                    ProgressBar::render(0, ImVec2(ImGui::GetContentRegionAvail().x - 6, 6));
                }

                // unload button
                ButtonConfig unloadButton;
                unloadButton.id = "##unload" + modelId;
                unloadButton.label = "Unload";
                unloadButton.size = ImVec2(ImGui::GetContentRegionAvail().x - 8, 0);
                unloadButton.onClick = [this, modelName, modelVariant, &modelManager]() {
                    // Unload the model from server
                    modelManager.removeModelFromServer(modelName, modelVariant);
                    // Unload the model
                    modelManager.unloadModel(modelName, modelVariant);
                    };
                unloadButton.backgroundColor = ImVec4(0.2F, 0.2F, 0.2F, 0.3F);

                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetContentRegionAvail().y - 30);
                Button::render(unloadButton);

                ImGui::EndChild();
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();

                // Add card to the same line with spacing
                ImGui::SameLine(0, 12);
            }

            ImGui::EndGroup();
            ImGui::EndChild(); // End horizontal scroll region
        }

        ImGui::EndChild(); // End server_model_list

        // Pop scrollbar styling
        ImGui::PopStyleColor();
    }

private:
    DeleteModelModalComponent m_deleteModal;
    bool m_deleteModalOpen = false;
};