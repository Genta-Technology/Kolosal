#pragma once

#include "imgui.h"
#include "ui/widgets.hpp"
#include "ui/markdown.hpp"
#include "model/model_manager.hpp"
#include "ui/fonts.hpp"
#include <string>
#include <vector>
#include <functional>

namespace ModelManagerConstants {
    constexpr float cardWidth = 200.0f;
    constexpr float cardHeight = 220.0f;
    constexpr float cardSpacing = 10.0f;
    constexpr float padding = 16.0f;
    constexpr float modalVerticalScale = 0.9f;
}

class DeleteModelModalComponent {
public:
    DeleteModelModalComponent() {
        ButtonConfig cancelButton;
        cancelButton.id = "##cancelDeleteModel";
        cancelButton.label = "Cancel";
        cancelButton.backgroundColor = RGBAToImVec4(34, 34, 34, 255);
        cancelButton.hoverColor = RGBAToImVec4(53, 132, 228, 255);
        cancelButton.activeColor = RGBAToImVec4(26, 95, 180, 255);
        cancelButton.textColor = RGBAToImVec4(255, 255, 255, 255);
        cancelButton.size = ImVec2(130, 0);
        cancelButton.onClick = []() { ImGui::CloseCurrentPopup(); };

        ButtonConfig confirmButton;
        confirmButton.id = "##confirmDeleteModel";
        confirmButton.label = "Confirm";
        confirmButton.backgroundColor = RGBAToImVec4(26, 95, 180, 255);
        confirmButton.hoverColor = RGBAToImVec4(53, 132, 228, 255);
        confirmButton.activeColor = RGBAToImVec4(26, 95, 180, 255);
        confirmButton.size = ImVec2(130, 0);
        confirmButton.onClick = [this]() {
            if (m_index != -1 && !m_variant.empty()) {
                Model::ModelManager::getInstance().deleteDownloadedModel(m_index, m_variant);
                ImGui::CloseCurrentPopup();
            }
            };

        buttons.push_back(cancelButton);
        buttons.push_back(confirmButton);
    }

    void setModel(int index, const std::string& variant) {
        m_index = index;
        m_variant = variant;
    }

    void render(bool& openModal) {
        if (m_index == -1 || m_variant.empty()) {
            openModal = false;
            return;
        }

        ModalConfig config{
            "Confirm Delete Model",
            "Confirm Delete Model",
            ImVec2(300, 96),
            [this]() {
                Button::renderGroup(buttons, 16, ImGui::GetCursorPosY() + 8);
            },
            openModal
        };
        config.padding = ImVec2(16.0f, 8.0f);
        ModalWindow::render(config);

        if (!ImGui::IsPopupOpen(config.id.c_str())) {
            openModal = false;
            m_index = -1;
            m_variant.clear();
        }
    }

private:
    int m_index = -1;
    std::string m_variant;
    std::vector<ButtonConfig> buttons;
};

class ModelCardRenderer {
public:
    ModelCardRenderer(int index, const Model::ModelData& modelData,
        std::function<void(int, const std::string&)> onDeleteRequested)
        : m_index(index), m_model(modelData), m_onDeleteRequested(onDeleteRequested)
    {
        selectButton.id = "##select" + std::to_string(m_index);
        selectButton.size = ImVec2(ModelManagerConstants::cardWidth - 18, 0);

        deleteButton.id = "##delete" + std::to_string(m_index);
        deleteButton.size = ImVec2(24, 0);
        deleteButton.backgroundColor = RGBAToImVec4(200, 50, 50, 255);
        deleteButton.hoverColor = RGBAToImVec4(220, 70, 70, 255);
        deleteButton.activeColor = RGBAToImVec4(200, 50, 50, 255);
        deleteButton.icon = ICON_CI_TRASH;
        deleteButton.onClick = [this]() {
            std::string currentVariant = Model::ModelManager::getInstance().getCurrentVariantForModel(m_model.name);
            m_onDeleteRequested(m_index, currentVariant);
            };

        authorLabel.id = "##modelAuthor" + std::to_string(m_index);
        authorLabel.label = m_model.author;
        authorLabel.size = ImVec2(0, 0);
        authorLabel.fontType = FontsManager::ITALIC;
        authorLabel.fontSize = FontsManager::SM;
        authorLabel.alignment = Alignment::LEFT;

        nameLabel.id = "##modelName" + std::to_string(m_index);
        nameLabel.label = m_model.name;
        nameLabel.size = ImVec2(0, 0);
        nameLabel.fontType = FontsManager::BOLD;
        nameLabel.fontSize = FontsManager::MD;
        nameLabel.alignment = Alignment::LEFT;
    }

    void render() {
        auto& manager = Model::ModelManager::getInstance();
        std::string currentVariant = manager.getCurrentVariantForModel(m_model.name);

        ImGui::BeginGroup();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, RGBAToImVec4(26, 26, 26, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);

        std::string childName = "ModelCard" + std::to_string(m_index);
        ImGui::BeginChild(childName.c_str(), ImVec2(ModelManagerConstants::cardWidth, ModelManagerConstants::cardHeight), true);

        renderHeader();
        ImGui::Spacing();
        renderVariantOptions(currentVariant);

        ImGui::SetCursorPosY(ModelManagerConstants::cardHeight - 35);

        bool isSelected = (m_model.name == manager.getCurrentModelName() &&
            currentVariant == manager.getCurrentVariantType());
        bool isDownloaded = manager.isModelDownloaded(m_index, currentVariant);

        if (!isDownloaded) {
            double progress = manager.getModelDownloadProgress(m_index, currentVariant);
            if (progress > 0.0) {
                selectButton.label = "Cancel";
                selectButton.backgroundColor = RGBAToImVec4(200, 50, 50, 255);
                selectButton.hoverColor = RGBAToImVec4(220, 70, 70, 255);
                selectButton.activeColor = RGBAToImVec4(200, 50, 50, 255);
                selectButton.icon = ICON_CI_CLOSE;
                selectButton.onClick = [this, currentVariant]() {
                    Model::ModelManager::getInstance().cancelDownload(m_index, currentVariant);
                    };

                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 12);
                float fraction = static_cast<float>(progress) / 100.0f;
				ProgressBar::render(fraction, ImVec2(ModelManagerConstants::cardWidth - 18, 6));
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
            }
            else {
                selectButton.label = "Download";
                selectButton.backgroundColor = RGBAToImVec4(26, 95, 180, 255);
                selectButton.hoverColor = RGBAToImVec4(53, 132, 228, 255);
                selectButton.activeColor = RGBAToImVec4(26, 95, 180, 255);
                selectButton.icon = ICON_CI_CLOUD_DOWNLOAD;
                selectButton.borderSize = 1.0f;
                selectButton.onClick = [this, currentVariant]() {
                    Model::ModelManager::getInstance().setPreferredVariant(m_model.name, currentVariant);
                    Model::ModelManager::getInstance().downloadModel(m_index, currentVariant);
                    };
            }
        }
        else {
            bool isLoadingSelected = isSelected && Model::ModelManager::getInstance().isLoadInProgress();
			bool isUnloading = isSelected && Model::ModelManager::getInstance().isUnloadInProgress();

            // Configure button label and base state
            if (isLoadingSelected || isUnloading) {
                selectButton.label = isLoadingSelected ? "Loading Model..." : "Unloading Model...";
                selectButton.state = ButtonState::DISABLED;
                selectButton.icon = ""; // Clear any existing icon
                selectButton.borderSize = 0.0f; // Remove border
            }
            else {
                selectButton.label = isSelected ? "Selected" : "Select";
            }

            // Base styling (applies to all states)
            selectButton.backgroundColor = RGBAToImVec4(34, 34, 34, 255);

            // Disabled state for non-selected loading
            if (!isSelected && Model::ModelManager::getInstance().isLoadInProgress()) {
                selectButton.state = ButtonState::DISABLED;
            }

            // Common properties
            selectButton.onClick = [this]() {
                std::string variant = Model::ModelManager::getInstance().getCurrentVariantForModel(m_model.name);
                Model::ModelManager::getInstance().switchModel(m_model.name, variant);
                };
            selectButton.size = ImVec2(ModelManagerConstants::cardWidth - 18 - 5 - 24, 0);

            // Selected state styling (only if not loading)
            if (isSelected && !isLoadingSelected) {
                selectButton.icon = ICON_CI_DEBUG_DISCONNECT;
                selectButton.borderColor = RGBAToImVec4(172, 131, 255, 255 / 4);
                selectButton.borderSize = 1.0f;
                selectButton.state = ButtonState::NORMAL;
				selectButton.tooltip = "Click to unload model from memory";
                selectButton.onClick = [this]() {
                    Model::ModelManager::getInstance().unloadModel();
                    };
            }

            // Add progress bar if in loading-selected state
            if (isLoadingSelected || isUnloading) {
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 12);
                ProgressBar::render(0, ImVec2(ModelManagerConstants::cardWidth - 18, 6));
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
            }
        }

        Button::render(selectButton);

        if (isDownloaded) {
            ImGui::SameLine();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 24 - 2);

            if (isSelected && Model::ModelManager::getInstance().isLoadInProgress())
				deleteButton.state = ButtonState::DISABLED;
			else
				deleteButton.state = ButtonState::NORMAL;

            Button::render(deleteButton);
        }

        ImGui::EndChild();
        if (ImGui::IsItemHovered() || isSelected) {
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            ImU32 borderColor = IM_COL32(172, 131, 255, 255 / 2);
            ImGui::GetWindowDrawList()->AddRect(min, max, borderColor, 8.0f, 0, 1.0f);
        }

        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::EndGroup();
    }

private:
    int m_index;
    const Model::ModelData& m_model;
    std::function<void(int, const std::string&)> m_onDeleteRequested;

    void renderHeader() {
        Label::render(authorLabel);
        Label::render(nameLabel);
    }

    void renderVariantOptions(const std::string& currentVariant) {
        auto renderVariant = [this, &currentVariant](const std::string& variant, const std::string& label) {
            ButtonConfig btnConfig;
            btnConfig.id = "##" + variant + std::to_string(m_index);
            btnConfig.icon = (currentVariant == variant) ? ICON_CI_CHECK : ICON_CI_CLOSE;
            btnConfig.textColor = (currentVariant != variant) ? RGBAToImVec4(34, 34, 34, 255) : ImVec4(1, 1, 1, 1);
            btnConfig.fontSize = FontsManager::SM;
            btnConfig.size = ImVec2(24, 0);
            btnConfig.backgroundColor = RGBAToImVec4(34, 34, 34, 255);
            btnConfig.onClick = [variant, this]() {
                Model::ModelManager::getInstance().setPreferredVariant(m_model.name, variant);
                };
            Button::render(btnConfig);

            ImGui::SameLine(0.0f, 4.0f);
            LabelConfig variantLabel;
            variantLabel.id = "##" + variant + "Label" + std::to_string(m_index);
            variantLabel.label = label;
            variantLabel.size = ImVec2(0, 0);
            variantLabel.fontType = FontsManager::REGULAR;
            variantLabel.fontSize = FontsManager::SM;
            variantLabel.alignment = Alignment::LEFT;
            Label::render(variantLabel);
            };

        renderVariant("Full Precision", "Use Full Precision");
        ImGui::Spacing();
        renderVariant("8-bit Quantized", "Use 8-bit quantization");
        ImGui::Spacing();
        renderVariant("4-bit Quantized", "Use 4-bit quantization");
    }

    ButtonConfig deleteButton;
    ButtonConfig selectButton;
    LabelConfig nameLabel;
    LabelConfig authorLabel;
};

// TODO: Fix the nested modal
// when i tried to make the delete modal rendered on top of the model modal, it simply
// either didn't show up at all, or the model modal closed, and the entire application
// freezed. I tried to fix it, but I couldn't find a solution. I'm leaving it as is for now.
// Time wasted: 18 hours.
class ModelManagerModal {
public:
    ModelManagerModal() = default;

    void render(bool& showDialog) {
        ImVec2 windowSize = ImGui::GetWindowSize();
        if (windowSize.x == 0) windowSize = ImGui::GetMainViewport()->Size;
        const float targetWidth = windowSize.x;
        float availableWidth = targetWidth - (2 * ModelManagerConstants::padding);

        int numCards = static_cast<int>(availableWidth / (ModelManagerConstants::cardWidth + ModelManagerConstants::cardSpacing));
        float modalWidth = (numCards * (ModelManagerConstants::cardWidth + ModelManagerConstants::cardSpacing)) + (2 * ModelManagerConstants::padding);
        if (targetWidth - modalWidth > (ModelManagerConstants::cardWidth + ModelManagerConstants::cardSpacing) * 0.5f) {
            ++numCards;
            modalWidth = (numCards * (ModelManagerConstants::cardWidth + ModelManagerConstants::cardSpacing)) + (2 * ModelManagerConstants::padding);
        }
        ImVec2 modalSize = ImVec2(modalWidth, windowSize.y * ModelManagerConstants::modalVerticalScale);

        auto renderCards = [numCards, this]() {
            auto& manager = Model::ModelManager::getInstance();
            const auto& models = manager.getModels();
            for (size_t i = 0; i < models.size(); ++i) {
                if (i % numCards == 0) {
                    ImGui::SetCursorPos(ImVec2(ModelManagerConstants::padding,
                        ImGui::GetCursorPosY() + (i > 0 ? ModelManagerConstants::cardSpacing : 0)));
                }
                ModelCardRenderer card(static_cast<int>(i), models[i],
                    [this](int index, const std::string& variant) {
                        m_deleteModal.setModel(index, variant);
                        m_deleteModalOpen = true;
                    });
                card.render();
                if ((i + 1) % numCards != 0 && i < models.size() - 1) {
                    ImGui::SameLine(0.0f, ModelManagerConstants::cardSpacing);
                }
            }
            };

        ModalConfig config{
            "Model Manager",
            "Model Manager",
            modalSize,
            renderCards,
            showDialog
        };
        config.padding = ImVec2(ModelManagerConstants::padding, 8.0f);
        ModalWindow::render(config);

        // Render the delete modal if it’s open.
        if (m_deleteModalOpen) {
            m_deleteModal.render(m_deleteModalOpen);
        }

        if (m_wasDeleteModalOpen && !m_deleteModalOpen) {
            showDialog = true;
            ImGui::OpenPopup(config.id.c_str());
        }
        m_wasDeleteModalOpen = m_deleteModalOpen;

        if (!ImGui::IsPopupOpen(config.id.c_str())) {
            showDialog = false;
        }
    }

private:
    DeleteModelModalComponent m_deleteModal;
    bool m_deleteModalOpen = false;

    // This flag tracks if the delete modal was open on the previous frame.
    bool m_wasDeleteModalOpen = false;
};