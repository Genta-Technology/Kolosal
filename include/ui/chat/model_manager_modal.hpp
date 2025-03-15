#pragma once

#include "imgui.h"
#include "ui/widgets.hpp"
#include "ui/markdown.hpp"
#include "model/model_manager.hpp"
#include "ui/fonts.hpp"
#include <string>
#include <vector>
#include <functional>
#include <algorithm>

namespace ModelManagerConstants {
    constexpr float cardWidth = 200.0f;
    constexpr float cardHeight = 220.0f;
    constexpr float cardSpacing = 10.0f;
    constexpr float padding = 16.0f;
    constexpr float modalVerticalScale = 0.9f;
    constexpr float sectionSpacing = 20.0f;
    constexpr float sectionHeaderHeight = 30.0f;
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
        std::function<void(int, const std::string&)> onDeleteRequested, std::string id = "")
        : m_index(index), m_model(modelData), m_onDeleteRequested(onDeleteRequested), m_id(id)
    {
        selectButton.id = "##select" + std::to_string(m_index) + m_id;
        selectButton.size = ImVec2(ModelManagerConstants::cardWidth - 18, 0);

        deleteButton.id = "##delete" + std::to_string(m_index) + m_id;
        deleteButton.size = ImVec2(24, 0);
        deleteButton.backgroundColor = RGBAToImVec4(200, 50, 50, 255);
        deleteButton.hoverColor = RGBAToImVec4(220, 70, 70, 255);
        deleteButton.activeColor = RGBAToImVec4(200, 50, 50, 255);
        deleteButton.icon = ICON_CI_TRASH;
        deleteButton.onClick = [this]() {
            std::string currentVariant = Model::ModelManager::getInstance().getCurrentVariantForModel(m_model.name);
            m_onDeleteRequested(m_index, currentVariant);
            };

        authorLabel.id = "##modelAuthor" + std::to_string(m_index) + m_id;
        authorLabel.label = m_model.author;
        authorLabel.size = ImVec2(0, 0);
        authorLabel.fontType = FontsManager::ITALIC;
        authorLabel.fontSize = FontsManager::SM;
        authorLabel.alignment = Alignment::LEFT;

        nameLabel.id = "##modelName" + std::to_string(m_index) + m_id;
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

        std::string childName = "ModelCard" + std::to_string(m_index) + m_id;
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
    std::string m_id;
    const Model::ModelData& m_model;
    std::function<void(int, const std::string&)> m_onDeleteRequested;

    void renderHeader() {
        Label::render(authorLabel);
        Label::render(nameLabel);
    }

    void renderVariantOptions(const std::string& currentVariant) {
        LabelConfig variantLabel;
        variantLabel.id = "##variantLabel" + std::to_string(m_index);
        variantLabel.label = "Model Variants";
        variantLabel.size = ImVec2(0, 0);
        variantLabel.fontType = FontsManager::REGULAR;
        variantLabel.fontSize = FontsManager::SM;
        variantLabel.alignment = Alignment::LEFT;
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);
        Label::render(variantLabel);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);

        // Calculate the height for the scrollable area
        // Card height minus header space minus button space at bottom
        const float variantAreaHeight = 100.0f; // Adjust this value based on your layout needs

        // Create a scrollable child window for variants
        ImGui::BeginChild(("##VariantScroll" + std::to_string(m_index)).c_str(),
            ImVec2(ModelManagerConstants::cardWidth - 18, variantAreaHeight),
            false);

        // Helper function to render a single variant option
        auto renderVariant = [this, &currentVariant](const std::string& variant) {
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
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4);
            Button::render(btnConfig);

            ImGui::SameLine(0.0f, 4.0f);
            LabelConfig variantLabel;
            variantLabel.id = "##" + variant + "Label" + std::to_string(m_index);
            variantLabel.label = variant;
            variantLabel.size = ImVec2(0, 0);
            variantLabel.fontType = FontsManager::REGULAR;
            variantLabel.fontSize = FontsManager::SM;
            variantLabel.alignment = Alignment::LEFT;
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 6);
            Label::render(variantLabel);
            };

        // Iterate through all variants in the model
        for (const auto& [variant, variantData] : m_model.variants) {
            // For each variant, render a button
            renderVariant(variant);
            ImGui::Spacing();
        }

        // End the scrollable area
        ImGui::EndChild();
    }

    ButtonConfig deleteButton;
    ButtonConfig selectButton;
    LabelConfig nameLabel;
    LabelConfig authorLabel;
};

struct SortableModel {
    int index;
    std::string name;

    bool operator<(const SortableModel& other) const {
        return name < other.name;
    }
};

// TODO: Fix the nested modal
// when i tried to make the delete modal rendered on top of the model modal, it simply
// either didn't show up at all, or the model modal closed, and the entire application
// freezed. I tried to fix it, but I couldn't find a solution. I'm leaving it as is for now.
// Time wasted: 18 hours.
class ModelManagerModal {
public:
    ModelManagerModal() : m_searchText(""), m_shouldFocusSearch(false) {}

    void render(bool& showDialog) {
        auto& manager = Model::ModelManager::getInstance();

        // Update sorted models when:
        // - The modal is opened for the first time
        // - A model is downloaded, deleted, or its status changed
        bool needsUpdate = false;

        if (showDialog && !m_wasShowing) {
            // Modal just opened - refresh the model list
            needsUpdate = true;
            // Focus the search field when the modal is opened
            m_shouldFocusSearch = true;
        }

        // Check for changes in download status
        const auto& models = manager.getModels();
        if (models.size() != m_lastModelCount) {
            // The model count changed
            needsUpdate = true;
        }

        // Check for changes in downloaded status
        if (!needsUpdate) {
            std::unordered_set<std::string> currentDownloaded;

            for (size_t i = 0; i < models.size(); ++i) {
                // Check if ANY variant is downloaded instead of just the current one
                if (manager.isAnyVariantDownloaded(static_cast<int>(i))) {
                    currentDownloaded.insert(models[i].name); // Don't need to add variant to the key
                }
            }

            if (currentDownloaded != m_lastDownloadedStatus) {
                needsUpdate = true;
                m_lastDownloadedStatus = std::move(currentDownloaded);
            }
        }

        if (needsUpdate) {
            updateSortedModels();
            m_lastModelCount = models.size();
            filterModels(); // Apply the current search filter to the updated models
        }

        m_wasShowing = showDialog;

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

            // Render search field at the top
            renderSearchField();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ModelManagerConstants::sectionSpacing);

            LabelConfig downloadedSectionLabel;
            downloadedSectionLabel.id = "##downloadedModelsHeader";
            downloadedSectionLabel.label = "Downloaded Models";
            downloadedSectionLabel.size = ImVec2(0, 0);
            downloadedSectionLabel.fontSize = FontsManager::LG;
            downloadedSectionLabel.alignment = Alignment::LEFT;

            ImGui::SetCursorPos(ImVec2(ModelManagerConstants::padding, ImGui::GetCursorPosY()));
            Label::render(downloadedSectionLabel);

            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);

            // Count downloaded models and check if we have any
            bool hasDownloadedModels = false;
            int downloadedCardCount = 0;

            // First pass to check if we have any downloaded models
            for (const auto& sortableModel : m_filteredModels) {
                // Check if ANY variant is downloaded instead of just current variant
                if (manager.isAnyVariantDownloaded(sortableModel.index)) {
                    hasDownloadedModels = true;
                    break;
                }
            }

            // Render downloaded models
            if (hasDownloadedModels) {
                for (const auto& sortableModel : m_filteredModels) {
                    // Check if ANY variant is downloaded instead of just current variant
                    if (manager.isAnyVariantDownloaded(sortableModel.index)) {
                        if (downloadedCardCount % numCards == 0) {
                            ImGui::SetCursorPos(ImVec2(ModelManagerConstants::padding,
                                ImGui::GetCursorPosY() + (downloadedCardCount > 0 ? ModelManagerConstants::cardSpacing : 0)));
                        }

                        ModelCardRenderer card(sortableModel.index, models[sortableModel.index],
                            [this](int index, const std::string& variant) {
                                m_deleteModal.setModel(index, variant);
                                m_deleteModalOpen = true;
                            }, "downloaded");
                        card.render();

                        if ((downloadedCardCount + 1) % numCards != 0) {
                            ImGui::SameLine(0.0f, ModelManagerConstants::cardSpacing);
                        }

                        downloadedCardCount++;
                    }
                }

                // Add spacing before the next section
                if (downloadedCardCount % numCards != 0) {
                    ImGui::NewLine();
                }
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ModelManagerConstants::sectionSpacing);
            }
            else {
                // Show a message if no downloaded models
                LabelConfig noModelsLabel;
                noModelsLabel.id = "##noDownloadedModels";
                noModelsLabel.label = m_searchText.empty() ?
                    "No downloaded models yet. Download models from the section below." :
                    "No downloaded models match your search. Try a different search term.";
                noModelsLabel.size = ImVec2(0, 0);
                noModelsLabel.fontType = FontsManager::ITALIC;
                noModelsLabel.fontSize = FontsManager::MD;
                noModelsLabel.alignment = Alignment::LEFT;

                ImGui::SetCursorPosX(ModelManagerConstants::padding);
                Label::render(noModelsLabel);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ModelManagerConstants::sectionSpacing);
            }

            // Separator between sections
            ImGui::SetCursorPosX(ModelManagerConstants::padding);
            ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
            ImGui::Separator();
            ImGui::PopStyleColor();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);

            // Render "Available Models" section header
            LabelConfig availableSectionLabel;
            availableSectionLabel.id = "##availableModelsHeader";
            availableSectionLabel.label = "Available Models";
            availableSectionLabel.size = ImVec2(0, 0);
            availableSectionLabel.fontSize = FontsManager::LG;
            availableSectionLabel.alignment = Alignment::LEFT;

            ImGui::SetCursorPosX(ModelManagerConstants::padding);
            Label::render(availableSectionLabel);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);

            // Check if we have any available models that match the search
            if (m_filteredModels.empty() && !m_searchText.empty()) {
                LabelConfig noModelsLabel;
                noModelsLabel.id = "##noAvailableModels";
                noModelsLabel.label = "No models match your search. Try a different search term.";
                noModelsLabel.size = ImVec2(0, 0);
                noModelsLabel.fontType = FontsManager::ITALIC;
                noModelsLabel.fontSize = FontsManager::MD;
                noModelsLabel.alignment = Alignment::LEFT;

                ImGui::SetCursorPosX(ModelManagerConstants::padding);
                Label::render(noModelsLabel);
            }
            else {
                // Render all models (available for download)
                for (size_t i = 0; i < m_filteredModels.size(); ++i) {
                    if (i % numCards == 0) {
                        ImGui::SetCursorPos(ImVec2(ModelManagerConstants::padding,
                            ImGui::GetCursorPosY() + (i > 0 ? ModelManagerConstants::cardSpacing : 0)));
                    }

                    ModelCardRenderer card(m_filteredModels[i].index, models[m_filteredModels[i].index],
                        [this](int index, const std::string& variant) {
                            m_deleteModal.setModel(index, variant);
                            m_deleteModalOpen = true;
                        });
                    card.render();

                    if ((i + 1) % numCards != 0 && i < m_filteredModels.size() - 1) {
                        ImGui::SameLine(0.0f, ModelManagerConstants::cardSpacing);
                    }
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

        // Render the delete modal if it's open.
        if (m_deleteModalOpen) {
            m_deleteModal.render(m_deleteModalOpen);

            // Mark for update on next frame after deletion
            if (!m_deleteModalOpen && m_wasDeleteModalOpen) {
                m_needsUpdateAfterDelete = true;
            }
        }

        if (m_wasDeleteModalOpen && !m_deleteModalOpen) {
            showDialog = true;
            ImGui::OpenPopup(config.id.c_str());
        }

        if (m_needsUpdateAfterDelete && !m_deleteModalOpen) {
            updateSortedModels();
            filterModels(); // Apply search filter after updating models
            m_needsUpdateAfterDelete = false;
        }

        m_wasDeleteModalOpen = m_deleteModalOpen;

        if (!ImGui::IsPopupOpen(config.id.c_str())) {
            showDialog = false;
        }
    }

private:
    DeleteModelModalComponent m_deleteModal;
    bool m_deleteModalOpen = false;
    bool m_wasDeleteModalOpen = false;
    bool m_wasShowing = false;
    bool m_needsUpdateAfterDelete = false;
    size_t m_lastModelCount = 0;
    std::unordered_set<std::string> m_lastDownloadedStatus;
    std::vector<SortableModel> m_sortedModels;
    std::vector<SortableModel> m_filteredModels;  // New: Filtered list of models based on search

    // Search related variables
    std::string m_searchText;
    bool m_shouldFocusSearch;

    void updateSortedModels() {
        auto& manager = Model::ModelManager::getInstance();
        const auto& models = manager.getModels();

        // Clear and rebuild the sorted model list
        m_sortedModels.clear();
        m_sortedModels.reserve(models.size());

        for (size_t i = 0; i < models.size(); ++i) {
            // Store the index and name directly, avoiding storing pointers
            m_sortedModels.push_back({ static_cast<int>(i), models[i].name });
        }

        // Sort models alphabetically by name
        std::sort(m_sortedModels.begin(), m_sortedModels.end());

        // Initialize filtered models with all models when sort is updated
        filterModels();
    }

    // Filter models based on search text
    void filterModels() {
        m_filteredModels.clear();
        auto& manager = Model::ModelManager::getInstance();
        const auto& models = manager.getModels();

        if (m_searchText.empty()) {
            // If no search term, show all models
            m_filteredModels = m_sortedModels;
            return;
        }

        // Convert search text to lowercase for case-insensitive comparison
        std::string searchLower = m_searchText;
        std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(),
            [](unsigned char c) { return std::tolower(c); });

        // Filter models based on name OR author containing the search text
        for (const auto& model : m_sortedModels) {
            // Get the model data using the stored index
            const auto& modelData = models[model.index];

            // Convert name and author to lowercase for case-insensitive comparison
            std::string nameLower = modelData.name;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(),
                [](unsigned char c) { return std::tolower(c); });

            std::string authorLower = modelData.author;
            std::transform(authorLower.begin(), authorLower.end(), authorLower.begin(),
                [](unsigned char c) { return std::tolower(c); });

            // Add model to filtered results if either name OR author contains the search text
            if (nameLower.find(searchLower) != std::string::npos ||
                authorLower.find(searchLower) != std::string::npos) {
                m_filteredModels.push_back(model);
            }
        }
    }

    // New method: Render search field
    void renderSearchField() {
        ImGui::SetCursorPosX(ModelManagerConstants::padding);

        // Create and configure search input field
        InputFieldConfig searchConfig(
            "##modelSearch",
            ImVec2(ImGui::GetContentRegionAvail().x, 32.0f),
            m_searchText,
            m_shouldFocusSearch
        );
        searchConfig.placeholderText = "Search models...";
        searchConfig.processInput = [this](const std::string& text) {
            // No need to handle submission specifically as we'll filter on every change
            };

        // Style the search field
        searchConfig.backgroundColor = RGBAToImVec4(34, 34, 34, 255);
        searchConfig.hoverColor = RGBAToImVec4(44, 44, 44, 255);
        searchConfig.activeColor = RGBAToImVec4(54, 54, 54, 255);

        // Render the search field
        InputField::render(searchConfig);

        // Filter models whenever search text changes
        static std::string lastSearch;
        if (lastSearch != m_searchText) {
            lastSearch = m_searchText;
            filterModels();
        }
    }
};