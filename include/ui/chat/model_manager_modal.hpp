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
	DeleteModelModalComponent(int index, const std::string& variant)
		: m_index(index), m_variant(variant)
	{
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
		confirmButton.onClick = [index, variant]() {
			Model::ModelManager::getInstance().deleteDownloadedModel(index, variant);
			ImGui::CloseCurrentPopup();
			};

		buttons.push_back(cancelButton);
		buttons.push_back(confirmButton);
	}

    void render(bool& openModal) {
        ModalConfig config{
            "Confirm Delete Model", // Title
            "Confirm Delete Model", // Identifier
            ImVec2(300, 96),        // Size
            [this]() {
                Button::renderGroup(buttons, 16, ImGui::GetCursorPosY() + 8);
            },
            openModal // External open flag.
        };
        config.padding = ImVec2(16.0f, 8.0f);
        ModalWindow::render(config);
        if (!ImGui::IsPopupOpen(config.id.c_str()))
            openModal = false;
    }

private:
	std::string m_variant;
	int m_index;

    // widget configs
    std::vector<ButtonConfig> buttons;
};

class ModelCardRenderer {
public:
    ModelCardRenderer(int index, const Model::ModelData& modelData)
        : m_index(index), m_model(modelData)
    {
		selectButton.id = "##select" + std::to_string(m_index);
        selectButton.size = ImVec2(ModelManagerConstants::cardWidth - 18, 0);

        deleteButton.id = "##delete" + std::to_string(m_index);
        deleteButton.size = ImVec2(24, 0);
        deleteButton.backgroundColor = RGBAToImVec4(200, 50, 50, 255);
        deleteButton.hoverColor = RGBAToImVec4(220, 70, 70, 255);
        deleteButton.activeColor = RGBAToImVec4(200, 50, 50, 255);
        deleteButton.icon = ICON_CI_TRASH;

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
        // Retrieve the current variant from the model manager.
        auto& manager = Model::ModelManager::getInstance();
        std::string currentVariant = manager.getCurrentVariantForModel(m_model.name);

        // Begin a group with custom background and rounding.
        ImGui::BeginGroup();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, RGBAToImVec4(26, 26, 26, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);

        std::string childName = "ModelCard" + std::to_string(m_index);
        ImGui::BeginChild(childName.c_str(), ImVec2(ModelManagerConstants::cardWidth, ModelManagerConstants::cardHeight), true);

        renderHeader();
        ImGui::Spacing();
        renderVariantOptions(currentVariant);

        // Position the extra button row near the bottom of the card.
        ImGui::SetCursorPosY(ModelManagerConstants::cardHeight - 35);

        // Determine state: selected and downloaded.
        bool isSelected = (m_model.name == manager.getCurrentModelName() &&
            currentVariant == manager.getCurrentVariantType());
        bool isDownloaded = Model::ModelManager::getInstance().isModelDownloaded(m_index, currentVariant);

        if (!isDownloaded)
        {
            // Render Download button.
            selectButton.label = "Download";
            selectButton.backgroundColor = RGBAToImVec4(26, 95, 180, 255);
            selectButton.hoverColor = RGBAToImVec4(53, 132, 228, 255);
            selectButton.activeColor = RGBAToImVec4(26, 95, 180, 255);
            selectButton.icon = ICON_CI_CLOUD_DOWNLOAD;
            selectButton.borderSize = 1.0f;
            selectButton.onClick = [this, currentVariant]() {
                // Commit the current variant before downloading.
                std::string variant = Model::ModelManager::getInstance().getCurrentVariantForModel(m_model.name);
                Model::ModelManager::getInstance().setPreferredVariant(m_model.name, variant);
                Model::ModelManager::getInstance().downloadModel(m_index, variant);
                };

            // If download is already in progress, change to a Cancel button.
            double progress = Model::ModelManager::getInstance().getModelDownloadProgress(m_index, currentVariant);
            if (progress > 0.0)
            {
                selectButton.label = "Cancel";
                selectButton.backgroundColor = RGBAToImVec4(200, 50, 50, 255);
                selectButton.hoverColor = RGBAToImVec4(220, 70, 70, 255);
                selectButton.activeColor = RGBAToImVec4(200, 50, 50, 255);
                selectButton.icon = ICON_CI_CLOSE;
                selectButton.onClick = [this, currentVariant]() {
                    Model::ModelManager::getInstance().cancelDownload(m_index, currentVariant);
                    };

                // Move cursor up a bit before drawing progress bar.
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 24);
                ImGui::ProgressBar(static_cast<float>(progress) / 100.0f,
                    ImVec2(ModelManagerConstants::cardWidth - 18, 0));
            }
        }
        else
        {
            // Render Select button if model is already downloaded.
            selectButton.label = isSelected ? "selected" : "select";
            selectButton.backgroundColor = RGBAToImVec4(34, 34, 34, 255);
            if (isSelected)
            {
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 4);

                selectButton.icon = ICON_CI_PASS;
                selectButton.borderColor = RGBAToImVec4(172, 131, 255, 255 / 4);
                selectButton.borderSize = 1.0f;
                selectButton.state = ButtonState::ACTIVE;
            }
            selectButton.onClick = [this]() {
                std::string variant = Model::ModelManager::getInstance().getCurrentVariantForModel(m_model.name);
                Model::ModelManager::getInstance().switchModel(m_model.name, variant);
                };
            // Reduce width to leave space for a delete button.
            selectButton.size = ImVec2(ModelManagerConstants::cardWidth - 18 - 5 - 24, 0);
        }
        Button::render(selectButton);

        // Render Delete button if model is downloaded.
        static bool openModelDeleteModal = false;
        if (isDownloaded)
        {
            ImGui::SameLine();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 24 - 2);

            deleteButton.onClick = []() { openModelDeleteModal = true; };
            Button::render(deleteButton);
        }

		DeleteModelModalComponent deleteModelModal(m_index, currentVariant);
		deleteModelModal.render(openModelDeleteModal);

        // End Child and Group.
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

    // Render model author and model name.
    void renderHeader() {
        Label::render(authorLabel);
        Label::render(nameLabel);
    }

    // Render all variant-selection buttons.
    void renderVariantOptions(const std::string& currentVariant) {
        // Helper lambda to render a variant button with its label.
        auto renderVariant = [this, &currentVariant](const std::string& variant,
            const std::string& labelText) {
                ButtonConfig btnConfig;
                btnConfig.id = "##" + variant + std::to_string(m_index);
                btnConfig.icon = (currentVariant == variant) ? ICON_CI_CHECK : ICON_CI_CLOSE;
                if (currentVariant != variant) {
                    btnConfig.textColor = RGBAToImVec4(34, 34, 34, 255);
                }
                btnConfig.fontSize = FontsManager::SM;
                btnConfig.size = ImVec2(24, 0);
                btnConfig.backgroundColor = RGBAToImVec4(34, 34, 34, 255);
                btnConfig.onClick = [variant, this]() {
                    Model::ModelManager::getInstance().setPreferredVariant(m_model.name, variant);
                    };
                Button::render(btnConfig);

                // Add a small spacing between button and label.
                ImGui::SameLine(0.0f, 4.0f);

                LabelConfig variantLabel;
                variantLabel.id = "##" + variant + "Label" + std::to_string(m_index);
                variantLabel.label = labelText;
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

    // Button configs
    ButtonConfig deleteButton;
	ButtonConfig selectButton;
	ButtonConfig variantButton;

	// Label configs
    LabelConfig nameLabel;
	LabelConfig authorLabel;
	LabelConfig variantLabel;
};

class ModelManagerModal {
public:
    // The render method follows the same pattern as SaveAsDialogComponent.
    // The external boolean (showDialog) controls whether the modal is open.
    void render(bool& showDialog) {
        // Get the current window size (or fallback to main viewport).
        ImVec2 windowSize = ImGui::GetWindowSize();
        if (windowSize.x == 0)
            windowSize = ImGui::GetMainViewport()->Size;
        const float targetWidth = windowSize.x;
        float availableWidth = targetWidth - (2 * ModelManagerConstants::padding);

        // Calculate how many cards fit in one row.
        int numCards = static_cast<int>(availableWidth / (ModelManagerConstants::cardWidth + ModelManagerConstants::cardSpacing));
        float modalWidth = (numCards * (ModelManagerConstants::cardWidth + ModelManagerConstants::cardSpacing)) + (2 * ModelManagerConstants::padding);
        if (targetWidth - modalWidth > (ModelManagerConstants::cardWidth + ModelManagerConstants::cardSpacing) * 0.5f) {
            ++numCards;
            modalWidth = (numCards * (ModelManagerConstants::cardWidth + ModelManagerConstants::cardSpacing)) + (2 * ModelManagerConstants::padding);
        }
        ImVec2 modalSize = ImVec2(modalWidth, windowSize.y * ModelManagerConstants::modalVerticalScale);

        // Lambda that renders the grid of model cards.
        auto renderCards = [numCards](void) {
            auto& manager = Model::ModelManager::getInstance();
            const std::vector<Model::ModelData>& models = manager.getModels();
            for (size_t i = 0; i < models.size(); ++i) {
                // Start a new row.
                if (i % numCards == 0) {
                    ImGui::SetCursorPos(ImVec2(ModelManagerConstants::padding,
                        ImGui::GetCursorPosY() + (i > 0 ? ModelManagerConstants::cardSpacing : 0)));
                }
                ModelCardRenderer card(static_cast<int>(i), models[i]);
                card.render();
                // Add horizontal spacing if this is not the last card in the row.
                if ((i + 1) % numCards != 0 && i < models.size() - 1) {
                    ImGui::SameLine(0.0f, ModelManagerConstants::cardSpacing);
                }
            }
            };

        // Set up the modal configuration.
        ModalConfig config{
            "Model Manager",     // Title
            "Model Manager",     // Identifier
            modalSize,
            renderCards,         // Content renderer (grid of cards)
            showDialog           // External open flag.
        };
        config.padding = ImVec2(ModelManagerConstants::padding, 8.0f);

        // Render the modal.
        ModalWindow::render(config);

        // If the popup is no longer open, reset the open flag.
        if (!ImGui::IsPopupOpen(config.id.c_str()))
            showDialog = false;
    }
};