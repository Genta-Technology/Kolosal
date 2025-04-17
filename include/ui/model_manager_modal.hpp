#pragma once

#include "imgui.h"
#include "ui/widgets.hpp"
#include "ui/markdown.hpp"
#include "model/model_manager.hpp"
#include "model/gguf_reader.hpp"
#include "ui/fonts.hpp"
#include <string>
#include <vector>
#include <functional>
#include <algorithm>
#include <map>
#include <nfd.h>
#include <filesystem>
#include <regex>
#include <curl/curl.h>

namespace ModelManagerConstants {
    constexpr float cardWidth = 200.0f;
    constexpr float cardHeight = 220.0f;
    constexpr float cardSpacing = 10.0f;
    constexpr float padding = 16.0f;
    constexpr float modalVerticalScale = 0.9f;
    constexpr float sectionSpacing = 20.0f;
    constexpr float sectionHeaderHeight = 30.0f;
}

class AddCustomModelModalComponent {
public:
    AddCustomModelModalComponent() {
        // Add variant button
        ButtonConfig addVariantButton;
        addVariantButton.id = "##confirmAddVariant";
        addVariantButton.backgroundColor = RGBAToImVec4(26, 95, 180, 255);
        addVariantButton.hoverColor = RGBAToImVec4(53, 132, 228, 255);
        addVariantButton.activeColor = RGBAToImVec4(26, 95, 180, 255);
        addVariantButton.size = ImVec2(130, 0);
        addVariantButton.onClick = [this]() {
            if (validateVariantForm()) {
                Model::ModelVariant variant;
                variant.type = m_currentVariantName;

                // Determine if input is URL or local path
                bool isUrl = isUrlInput(m_currentVariantPath);

                if (isUrl) {
                    // If it's a URL, set downloadLink and generate a local path
                    variant.downloadLink = m_currentVariantPath;

                    // Extract filename from URL
                    std::string filename = getFilenameFromPath(m_currentVariantPath);

                    // Generate path in format: models/<model name>/<variant name>/<gguf file name>
                    variant.path = "models/" + m_modelName + "/" + m_currentVariantName + "/" + filename;

                    // For URL, mark as not downloaded yet
                    variant.isDownloaded = false;
                    variant.downloadProgress = 0.0;
                }
                else {
                    // If it's a local path, set path and leave downloadLink empty
                    variant.path = m_currentVariantPath;
                    variant.downloadLink = ""; // Empty for local files
                    variant.isDownloaded = true; // Already available locally
                    variant.downloadProgress = 100.0;
                }

                variant.lastSelected = 0;
                variant.size = getFileSize(m_currentVariantPath, isUrl);

                // Check if we're editing or adding a new variant
                if (!m_editingVariantName.empty()) {
                    // If the name changed, remove the old entry
                    if (m_editingVariantName != m_currentVariantName) {
                        m_variants.erase(m_editingVariantName);
                    }
                    // Add with new or same name
                    m_variants[m_currentVariantName] = variant;
                    m_editingVariantName.clear(); // Clear edit mode
                }
                else {
                    // Add new variant
                    m_variants[m_currentVariantName] = variant;
                }

                // Clear the form and collapse it
                m_currentVariantName.clear();
                m_currentVariantPath.clear();
                m_variantErrorMessage.clear();
                m_showVariantForm = false;

                // Reset focus for next time the variant form opens
                s_focusVariantName = true;
                s_focusVariantPath = false;
            }
            };

        variantButtons.push_back(addVariantButton);
    }

    void render(bool& openModal) {
        // Reset model added flag when modal opens
        if (openModal && !m_wasOpen) {
            m_modelAdded = false;
            s_focusAuthor = true;
            s_focusModelName = false;
            s_focusVariantName = false;
            s_focusVariantPath = false;
        }

        m_wasOpen = openModal;

        ModalConfig config{
            "Add Custom Model",
            "Add Custom Model",
            ImVec2(500, 550),
            [this]() {
                ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
                ImGui::BeginChild("##addCustomModelChild", ImVec2(0,
                    ImGui::GetContentRegionAvail().y - 42), false);
                renderMainForm();
                ImGui::EndChild();
                ImGui::PopStyleColor();

                ButtonConfig submitButton;
                submitButton.id = "##submitAddCustomModel";
                submitButton.label = "Submit";
                submitButton.backgroundColor = RGBAToImVec4(26, 95, 180, 255);
                submitButton.hoverColor = RGBAToImVec4(53, 132, 228, 255);
                submitButton.activeColor = RGBAToImVec4(26, 95, 180, 255);
                submitButton.size = ImVec2(ImGui::GetContentRegionAvail().x - 12.0F, 0);
                submitButton.onClick = [this]() {
                    if (validateMainForm()) {
                        if (submitCustomModel()) {
                            m_modelAdded = true;
                            ImGui::CloseCurrentPopup();
                        }
                        else {
                            m_errorMessage = "Failed to add custom model. Check the model file and try again.";
                        }
                    }
                };

                if (m_variants.empty()) {
                    submitButton.state = ButtonState::DISABLED;
                }

                ImGui::SetCursorPos(ImVec2(
                    ImGui::GetCursorPosX() + 6.0F,
                    ImGui::GetCursorPosY() + ImGui::GetContentRegionAvail().y - 30.0F
                ));
                Button::render(submitButton);
            },
            openModal
        };
        config.padding = ImVec2(16.0f, 16.0f);
        ModalWindow::render(config);

        if (!ImGui::IsPopupOpen(config.id.c_str())) {
            openModal = false;
            if (!m_modelAdded) {
                clearForm();
            }
        }
    }

    // Check if a model was successfully added in the last session
    bool wasModelAdded() const {
        return m_modelAdded;
    }

    // Reset the model added flag after handling it
    void resetModelAddedFlag() {
        m_modelAdded = false;
    }

private:
    // Main form data
    std::string m_authorName;
    std::string m_modelName;
    std::map<std::string, Model::ModelVariant> m_variants;
    std::string m_errorMessage;
    bool m_wasOpen = false;
    bool m_modelAdded = false;

    // Variant form data
    bool m_showVariantForm = false;
    std::string m_currentVariantName;
    std::string m_currentVariantPath;
    std::string m_variantErrorMessage;
    std::string m_editingVariantName;

    // Static focus control variables
    static bool s_focusAuthor;
    static bool s_focusModelName;
    static bool s_focusVariantName;
    static bool s_focusVariantPath;

    // Static counter for unique IDs
    static int s_idCounter;

    // Buttons
    std::vector<ButtonConfig> variantButtons;

	// GGUF reader
    GGUFMetadataReader m_ggufReader;

    // Check if input is a URL
    bool isUrlInput(const std::string& input) {
        // Simple regex to detect URLs
        static const std::regex urlPattern(
            R"(^(https?|ftp)://)"  // protocol
            R"([^\s/$.?#].[^\s]*$)",  // domain and path
            std::regex::icase
        );

        return std::regex_match(input, urlPattern);
    }

    // Extract filename from path or URL
    std::string getFilenameFromPath(const std::string& path) {
        // First try to use filesystem for local paths
        std::string filename;

        try {
            // For URLs, extract the last part of the path
            if (isUrlInput(path)) {
                // Find the last '/' character
                size_t lastSlash = path.find_last_of('/');
                if (lastSlash != std::string::npos && lastSlash < path.length() - 1) {
                    filename = path.substr(lastSlash + 1);

                    // Handle URL query parameters
                    size_t queryPos = filename.find('?');
                    if (queryPos != std::string::npos) {
                        filename = filename.substr(0, queryPos);
                    }
                }
                else {
                    // Fallback
                    filename = "model.gguf";
                }
            }
            else {
                // For local paths, use std::filesystem
                std::filesystem::path fsPath(path);
                filename = fsPath.filename().string();
            }
        }
        catch (...) {
            // Last resort fallback
            filename = "model.gguf";
        }

        // Ensure the filename has .gguf extension
        if (filename.length() < 5 ||
            filename.substr(filename.length() - 5) != ".gguf") {
            filename += ".gguf";
        }

        return filename;
    }

    // CURL callback for URL file size check
    static size_t headerCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
        size_t totalSize = size * nitems;
        std::string header(buffer, totalSize);

        // Convert header to lowercase for case-insensitive comparison
        std::transform(header.begin(), header.end(), header.begin(),
            [](unsigned char c) { return std::tolower(c); });

        // Check if this is the content-length header
        if (header.find("content-length:") == 0) {
            // Extract the size value
            std::string lengthStr = header.substr(15); // Skip "content-length:"
            // Trim whitespace
            lengthStr.erase(0, lengthStr.find_first_not_of(" \t\r\n"));
            lengthStr.erase(lengthStr.find_last_not_of(" \t\r\n") + 1);

            // Store the size in the userdata pointer
            if (!lengthStr.empty()) {
                try {
                    *(size_t*)userdata = std::stoull(lengthStr);
                }
                catch (...) {
                    // Conversion error, ignore
                }
            }
        }

        return totalSize;
    }

    // Get file size in GB from URL using a HEAD request
    float getUrlFileSize(const std::string& url) {
        size_t fileSizeBytes = 0;

        CURL* curl = curl_easy_init();
        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_NOBODY, 1L); // HEAD request
            curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
            curl_easy_setopt(curl, CURLOPT_HEADERDATA, &fileSizeBytes);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L); // Follow redirects
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L); // 10 second timeout

            CURLcode res = curl_easy_perform(curl);
            curl_easy_cleanup(curl);

            if (res != CURLE_OK) {
                // Failed to get size, return 0
                return 0.0f;
            }
        }

        // Convert bytes to GB
        float fileSizeGB = static_cast<float>(fileSizeBytes) / (1024.0f * 1024.0f * 1024.0f);
        return fileSizeGB;
    }

    // Get file size in GB from local path
    float getLocalFileSize(const std::string& path) {
        try {
            std::filesystem::path fsPath(path);
            if (std::filesystem::exists(fsPath) && std::filesystem::is_regular_file(fsPath)) {
                // Get size in bytes and convert to GB
                uintmax_t sizeInBytes = std::filesystem::file_size(fsPath);
                float sizeInGB = static_cast<float>(sizeInBytes) / (1024.0f * 1024.0f * 1024.0f);
                return sizeInGB;
            }
        }
        catch (...) {
            // If there's any error, return 0
        }
        return 0.0f;
    }

    // Get file size in GB from either URL or local path
    float getFileSize(const std::string& path, bool isUrl) {
        if (isUrl) {
            return getUrlFileSize(path);
        }
        else {
            return getLocalFileSize(path);
        }
    }

    // Generate a unique ID for UI elements
    std::string generateUniqueId(const std::string& prefix) {
        return prefix + std::to_string(s_idCounter++);
    }

    // Start editing a variant
    void startEditingVariant(const std::string& variantName) {
        m_editingVariantName = variantName;
        m_currentVariantName = variantName;

        const auto& variant = m_variants[variantName];
        // Use downloadLink if available, otherwise use path
        m_currentVariantPath = !variant.downloadLink.empty()
            ? variant.downloadLink
            : variant.path;

        m_showVariantForm = true;
        s_focusVariantName = true;
    }

    void renderMainForm() {
        // Display error message if any
        if (!m_errorMessage.empty()) {
            LabelConfig errorLabel;
            errorLabel.id = "##mainErrorMessage";
            errorLabel.label = m_errorMessage;
            errorLabel.size = ImVec2(0, 0);
            errorLabel.fontType = FontsManager::ITALIC;
            errorLabel.fontSize = FontsManager::SM;
            errorLabel.color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
            errorLabel.alignment = Alignment::LEFT;
            Label::render(errorLabel);
            ImGui::Spacing();
        }

        // Author input
        LabelConfig authorLabel;
        authorLabel.id = "##modelAuthorLabel";
        authorLabel.label = "Author";
        authorLabel.size = ImVec2(0, 0);
        authorLabel.fontType = FontsManager::REGULAR;
        authorLabel.fontSize = FontsManager::MD;
        authorLabel.alignment = Alignment::LEFT;
        Label::render(authorLabel);

        InputFieldConfig authorFieldConfig(
            "##modelAuthorInput",
            ImVec2(ImGui::GetContentRegionAvail().x - 12.0F, 32.0f),
            m_authorName,
            s_focusAuthor
        );
        // Reset focus flag after use
        s_focusAuthor = false;

        authorFieldConfig.placeholderText = "Enter author name";
        authorFieldConfig.backgroundColor = RGBAToImVec4(34, 34, 34, 255);
        authorFieldConfig.hoverColor = RGBAToImVec4(44, 44, 44, 255);
        authorFieldConfig.activeColor = RGBAToImVec4(54, 54, 54, 255);
        InputField::render(authorFieldConfig);
        ImGui::Spacing();
        ImGui::Spacing();

        // Model name input
        LabelConfig nameLabel;
        nameLabel.id = "##modelNameLabel";
        nameLabel.label = "Model Name";
        nameLabel.size = ImVec2(0, 0);
        nameLabel.fontType = FontsManager::REGULAR;
        nameLabel.fontSize = FontsManager::MD;
        nameLabel.alignment = Alignment::LEFT;
        Label::render(nameLabel);

        InputFieldConfig nameFieldConfig(
            "##modelNameInput",
            ImVec2(ImGui::GetContentRegionAvail().x - 12.0F, 32.0f),
            m_modelName,
            s_focusModelName
        );
        // Reset focus flag after use
        s_focusModelName = false;

        nameFieldConfig.placeholderText = "Enter model name";
        nameFieldConfig.backgroundColor = RGBAToImVec4(34, 34, 34, 255);
        nameFieldConfig.hoverColor = RGBAToImVec4(44, 44, 44, 255);
        nameFieldConfig.activeColor = RGBAToImVec4(54, 54, 54, 255);
        InputField::render(nameFieldConfig);
        ImGui::Spacing();
        ImGui::Spacing();

        // Variants section
        LabelConfig variantsLabel;
        variantsLabel.id = "##modelVariantsLabel";
        variantsLabel.label = "Variants:";
        variantsLabel.size = ImVec2(0, 0);
        variantsLabel.fontType = FontsManager::REGULAR;
        variantsLabel.fontSize = FontsManager::MD;
        variantsLabel.alignment = Alignment::LEFT;
        Label::render(variantsLabel);
        ImGui::Spacing();

        // Display existing variants in a scrollable area
        if (!m_variants.empty()) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, RGBAToImVec4(26, 26, 26, 255));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
            ImGui::BeginChild("##variantsList", ImVec2(ImGui::GetContentRegionAvail().x, 180), true);

            int variantIdx = 0;
            for (auto& [variantName, variant] : m_variants) {
                std::string variantId = "variant_" + std::to_string(variantIdx++);
                ImGui::PushID(variantId.c_str());

                // Variant section with border
                ImGui::BeginGroup();
                ImGui::PushStyleColor(ImGuiCol_ChildBg, RGBAToImVec4(34, 34, 34, 255));
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
                ImGui::BeginChild(("##variantItem_" + variantId).c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 100), true);

                // Variant name - bold
                LabelConfig variantNameLabel;
                variantNameLabel.id = "##variant_name_" + variantId;
                variantNameLabel.label = "Variant: " + variantName;
                variantNameLabel.fontType = FontsManager::BOLD;
                variantNameLabel.fontSize = FontsManager::MD;
                Label::render(variantNameLabel);

                // Show path or URL info
                std::string locationInfo;
                if (!variant.downloadLink.empty()) {
                    locationInfo = "URL: " + variant.downloadLink;

                    // Also show the local path where it will be downloaded
                    LabelConfig variantPathLabel;
                    variantPathLabel.id = "##variant_download_path_" + variantId;
                    variantPathLabel.label = "Download path: " + variant.path;
                    variantPathLabel.fontType = FontsManager::ITALIC;
                    variantPathLabel.fontSize = FontsManager::SM;
                    Label::render(variantPathLabel);
                }
                else {
                    locationInfo = "Path: " + variant.path;
                }

                // Display the path/URL info
                LabelConfig variantPathLabel;
                variantPathLabel.id = "##variant_path_" + variantId;
                variantPathLabel.label = locationInfo;
                variantPathLabel.fontType = FontsManager::REGULAR;
                variantPathLabel.fontSize = FontsManager::SM;
                Label::render(variantPathLabel);

                // Edit button - at right side
                ImGui::SetCursorPos(ImVec2(ImGui::GetContentRegionAvail().x - 48, 10));
                ButtonConfig editVariantBtn;
                editVariantBtn.id = "##editVariant_" + variantId;
                editVariantBtn.icon = ICON_CI_EDIT;
                editVariantBtn.size = ImVec2(24, 24);
                editVariantBtn.tooltip = "Edit variant";
                editVariantBtn.onClick = [this, variantName]() {
                    startEditingVariant(variantName);
                    };
                Button::render(editVariantBtn);

                // Delete button - small, at the right side
                ImGui::SetCursorPos(ImVec2(ImGui::GetContentRegionAvail().x - 18, 10));
                ButtonConfig deleteVariantBtn;
                deleteVariantBtn.id = "##deleteVariant_" + variantId;
                deleteVariantBtn.icon = ICON_CI_TRASH;
                deleteVariantBtn.hoverColor = RGBAToImVec4(220, 70, 70, 255);
                deleteVariantBtn.size = ImVec2(24, 24);
                deleteVariantBtn.tooltip = "Delete variant";
                deleteVariantBtn.onClick = [this, variantName]() {
                    // If we're currently editing this variant, cancel editing
                    if (m_editingVariantName == variantName) {
                        m_editingVariantName.clear();
                        m_currentVariantName.clear();
                        m_currentVariantPath.clear();
                        m_showVariantForm = false;
                    }
                    m_variants.erase(variantName);
                    };
                Button::render(deleteVariantBtn);

                ImGui::EndChild();
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
                ImGui::EndGroup();

                ImGui::PopID();
                ImGui::Spacing();
            }

            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
        }
        else {
            LabelConfig noVariantsLabel;
            noVariantsLabel.id = "##noVariants";
            noVariantsLabel.label = "No variants added. Click 'Add New Variant' button below.";
            noVariantsLabel.fontType = FontsManager::ITALIC;
            noVariantsLabel.fontSize = FontsManager::SM;
            noVariantsLabel.color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
            Label::render(noVariantsLabel);
            ImGui::Spacing();
        }

        // Collapsible "Add New Variant" section
        ImGui::Spacing();

        // Determine button label based on whether we're editing or adding
        std::string buttonLabel = "Add New Variant";
        if (m_showVariantForm) {
            buttonLabel = m_editingVariantName.empty() ? "Cancel Adding Variant" : "Cancel Editing Variant";
        }

        ButtonConfig toggleVariantFormButton;
        toggleVariantFormButton.id = "##toggleAddNewVariant";
        toggleVariantFormButton.label = buttonLabel;
        toggleVariantFormButton.icon = m_showVariantForm ? ICON_CI_CLOSE : ICON_CI_PLUS;
        toggleVariantFormButton.alignment = Alignment::LEFT;
        toggleVariantFormButton.size = ImVec2(
            ImGui::CalcTextSize(buttonLabel.c_str()).x + /*padding + icon size*/ 40.0f, 32.0f);
        toggleVariantFormButton.onClick = [this]() {
            if (m_showVariantForm) {
                // Cancel editing/adding
                m_showVariantForm = false;
                m_currentVariantName.clear();
                m_currentVariantPath.clear();
                m_variantErrorMessage.clear();
                m_editingVariantName.clear();
            }
            else {
                // Start adding new
                m_showVariantForm = true;
                m_editingVariantName.clear();
                m_currentVariantName.clear();
                m_currentVariantPath.clear();
                s_focusVariantName = true;
                s_focusVariantPath = false;
            }
            };
        Button::render(toggleVariantFormButton);

        // Render the collapsible variant form if it's visible
        if (m_showVariantForm) {
            ImGui::Spacing();

            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0F);

            ImGui::PushStyleColor(ImGuiCol_ChildBg, RGBAToImVec4(30, 30, 30, 255));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);
            ImGui::BeginChild("##variantFormSection", ImVec2(ImGui::GetContentRegionAvail().x, 256), true);

            // Title - changes based on whether we're editing or adding
            LabelConfig variantFormLabel;
            variantFormLabel.id = "##addVariantTitle";
            variantFormLabel.label = m_editingVariantName.empty() ? "Add New Variant" : "Edit Variant";
            variantFormLabel.fontType = FontsManager::BOLD;
            variantFormLabel.fontSize = FontsManager::MD;
            variantFormLabel.alignment = Alignment::LEFT;
            Label::render(variantFormLabel);
            ImGui::Spacing();

            // Display error message if any
            if (!m_variantErrorMessage.empty()) {
                LabelConfig errorLabel;
                errorLabel.id = "##variantErrorMessage";
                errorLabel.label = m_variantErrorMessage;
                errorLabel.size = ImVec2(0, 0);
                errorLabel.fontType = FontsManager::ITALIC;
                errorLabel.fontSize = FontsManager::SM;
                errorLabel.color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
                errorLabel.alignment = Alignment::LEFT;
                Label::render(errorLabel);
                ImGui::Spacing();
            }

            // Variant Name
            LabelConfig variantNameLabel;
            variantNameLabel.id = "##variantNameLabel";
            variantNameLabel.label = "Variant Name";
            variantNameLabel.size = ImVec2(0, 0);
            variantNameLabel.fontType = FontsManager::REGULAR;
            variantNameLabel.fontSize = FontsManager::MD;
            variantNameLabel.alignment = Alignment::LEFT;
            Label::render(variantNameLabel);

            InputFieldConfig variantNameField(
                "##variantNameInput",
                ImVec2(ImGui::GetContentRegionAvail().x, 32.0f),
                m_currentVariantName,
                s_focusVariantName
            );
            // Reset focus flag after use
            s_focusVariantName = false;

            variantNameField.placeholderText = "e.g., q4_0, f16, etc.";
            variantNameField.backgroundColor = RGBAToImVec4(34, 34, 34, 255);
            variantNameField.hoverColor = RGBAToImVec4(44, 44, 44, 255);
            variantNameField.activeColor = RGBAToImVec4(54, 54, 54, 255);
            InputField::render(variantNameField);
            ImGui::Spacing();

            // Path / URL
            LabelConfig variantPathLabel;
            variantPathLabel.id = "##variantPathLabel";
            variantPathLabel.label = "Path / URL to GGUF";
            variantPathLabel.size = ImVec2(0, 0);
            variantPathLabel.fontType = FontsManager::REGULAR;
            variantPathLabel.fontSize = FontsManager::MD;
            variantPathLabel.alignment = Alignment::LEFT;
            Label::render(variantPathLabel);

            // Add info about URL vs path handling
            LabelConfig pathInfoLabel;
            pathInfoLabel.id = "##pathInfoLabel";
            pathInfoLabel.label = "Enter a URL (https://) to download or a local file path";
            pathInfoLabel.fontType = FontsManager::ITALIC;
            pathInfoLabel.fontSize = FontsManager::SM;
            pathInfoLabel.color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
            Label::render(pathInfoLabel);

            InputFieldConfig variantPathField(
                "##variantPathInput",
                ImVec2(ImGui::GetContentRegionAvail().x - 48, 32.0f),
                m_currentVariantPath,
                s_focusVariantPath
            );
            // Reset focus flag after use
            s_focusVariantPath = false;

            variantPathField.placeholderText = "Enter path or URL to the model file";
            variantPathField.backgroundColor = RGBAToImVec4(34, 34, 34, 255);
            variantPathField.hoverColor = RGBAToImVec4(44, 44, 44, 255);
            variantPathField.activeColor = RGBAToImVec4(54, 54, 54, 255);
            InputField::render(variantPathField);

            ImGui::SameLine();
            ButtonConfig browseButton;
            browseButton.id = "##browseVariantPath";
            browseButton.icon = ICON_CI_FOLDER;
            browseButton.size = ImVec2(38, 38);
            browseButton.onClick = [this]() {
                openFileDialog();
                };
            Button::render(browseButton);

            ImGui::Spacing();

            // Update Add/Update variant button
            ButtonConfig actionButton = variantButtons[0]; // Get our base Add Variant button
            actionButton.id = "##" + std::string(m_editingVariantName.empty() ? "addVariant" : "updateVariant");
            actionButton.label = m_editingVariantName.empty() ? "Add Variant" : "Update Variant";
            actionButton.size = ImVec2(ImGui::GetContentRegionAvail().x, 0);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 16.0F);
            Button::render(actionButton);

            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
        }
    }

    void openFileDialog() {
        nfdu8char_t* outPath = nullptr;
        nfdu8filteritem_t filters[1] = { {"GGUF Models", "gguf"} };

        nfdopendialogu8args_t args{};
        args.filterList = filters;
        args.filterCount = 1;

        nfdresult_t result = NFD_OpenDialogU8_With(&outPath, &args);

        if (result == NFD_OKAY) {
            m_currentVariantPath = (const char*)outPath;
            NFD_FreePathU8(outPath);
            s_focusVariantPath = true;
        }
        else if (result == NFD_ERROR) {
            m_variantErrorMessage = "Error opening file dialog: ";
            m_variantErrorMessage += NFD_GetError();
        }
    }

    bool validateMainForm() {
        m_errorMessage.clear();

        if (m_authorName.empty()) {
            m_errorMessage = "Error: Author name cannot be empty";
            s_focusAuthor = true;
            return false;
        }

        if (m_modelName.empty()) {
            m_errorMessage = "Error: Model name cannot be empty";
            s_focusModelName = true;
            return false;
        }

        if (m_variants.empty()) {
            m_errorMessage = "Error: You must add at least one variant";
            return false;
        }

        return true;
    }

    bool validateVariantForm() {
        m_variantErrorMessage.clear();

        if (m_currentVariantName.empty()) {
            m_variantErrorMessage = "Error: Variant name cannot be empty";
            s_focusVariantName = true;
            return false;
        }

        if (m_currentVariantPath.empty()) {
            m_variantErrorMessage = "Error: Path/URL cannot be empty";
            s_focusVariantPath = true;
            return false;
        }

        // Check if variant already exists (only if we're adding a new one or changing the name)
        if (m_currentVariantName != m_editingVariantName &&
            m_variants.find(m_currentVariantName) != m_variants.end()) {
            m_variantErrorMessage = "Error: A variant with this name already exists";
            s_focusVariantName = true;
            return false;
        }

        return true;
    }

    bool submitCustomModel() {
        // Create a new ModelData instance
        Model::ModelData modelData;
        modelData.name = m_modelName;
        modelData.author = m_authorName;
        modelData.variants = m_variants;

        std::optional<GGUFModelParams> metadata;
		for (const auto& [variantName, variant] : m_variants) {
			if (!variant.downloadLink.empty()) {
				metadata = m_ggufReader.readModelParams(variant.downloadLink, false);
				break;
			}
            else {
				metadata = m_ggufReader.readModelParams(variant.path, false);
				break;
            }
		}

		if (!metadata.has_value()) {
            m_errorMessage = "Error: Failed to read model metadata";
			return false;
		}

        modelData.hidden_size     = metadata->hidden_size;
        modelData.attention_heads = metadata->attention_heads;
        modelData.hidden_layers   = metadata->hidden_layers;
        modelData.kv_heads        = metadata->kv_heads;

        // Call ModelManager to add the custom model
        if (!Model::ModelManager::getInstance().addCustomModel(modelData)) {
            m_errorMessage = "Error: Failed to add custom model. The model may already exist.";
			return false;
        }

        // Clear the form
        clearForm();

        return true;
    }

    void clearForm() {
        m_authorName.clear();
        m_modelName.clear();
        m_variants.clear();
        m_errorMessage.clear();
        m_showVariantForm = false;
        m_currentVariantName.clear();
        m_currentVariantPath.clear();
        m_variantErrorMessage.clear();
        m_editingVariantName.clear();

        // Reset focus flags
        s_focusAuthor = true;
        s_focusModelName = false;
        s_focusVariantName = false;
        s_focusVariantPath = false;
    }
};

// Initialize static members
bool AddCustomModelModalComponent::s_focusAuthor        = true;
bool AddCustomModelModalComponent::s_focusModelName     = false;
bool AddCustomModelModalComponent::s_focusVariantName   = false;
bool AddCustomModelModalComponent::s_focusVariantPath   = false;
int  AddCustomModelModalComponent::s_idCounter          = 0;

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
    ModelCardRenderer(const int index, const Model::ModelData& modelData,
        std::function<void(int, const std::string&)> onDeleteRequested, std::string id = "", bool allowSwitching = true)
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

		m_allowSwitching = allowSwitching;
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
			std::string loadingModel = manager.getCurrentOnLoadingModel();
			std::string unloadingModel = manager.getCurrentOnUnloadingModel();

			// get model name only from modelName:variant format on loading/unloading model
			if (loadingModel.find(':') != std::string::npos) {
				loadingModel = loadingModel.substr(0, loadingModel.find(':'));
			}
			if (unloadingModel.find(':') != std::string::npos) {
				unloadingModel = unloadingModel.substr(0, unloadingModel.find(':'));
			}

            bool isLoadingSelected = manager.isLoadInProgress() && m_model.name == loadingModel;
			bool isUnloading = manager.isUnloadInProgress() && m_model.name == unloadingModel;

            // Configure button label and base state
            if (isLoadingSelected || isUnloading) {
                selectButton.label = isLoadingSelected ? "Loading Model..." : "Unloading Model...";
                selectButton.state = ButtonState::DISABLED;
                selectButton.icon = ""; // Clear any existing icon
                selectButton.borderSize = 0.0f; // Remove border
            }
            else {
                if (m_allowSwitching) {
                    selectButton.label = isSelected ? "Selected" : "Select";
                }
                else {
                    selectButton.label = manager.isModelInServer(m_model.name, manager.getCurrentVariantForModel(m_model.name))
                        ? "Unload" : "Load Model";
                }
            }

            // Base styling (applies to all states)
            selectButton.backgroundColor = RGBAToImVec4(34, 34, 34, 255);

            // Disabled state for non-selected loading
            if (!isSelected && manager.isLoadInProgress()) {
                selectButton.state = ButtonState::DISABLED;
            }

            // Common properties
            selectButton.onClick = [this, &manager]() {
                std::string variant = manager.getCurrentVariantForModel(m_model.name);

                if (m_allowSwitching)
                {
                    manager.switchModel(m_model.name, variant);
                }
                else
                {
                    if (manager.isModelInServer(m_model.name, variant))
                    {
                        if (manager.unloadModel(m_model.name, variant))
                            manager.removeModelFromServer(m_model.name, variant);
                    }
                    else
                    {
						if (manager.loadModelIntoEngine(m_model.name, variant))
							manager.addModelToServer(m_model.name, variant);
                    }
                }
                };
            selectButton.size = ImVec2(ModelManagerConstants::cardWidth - 18 - 5 - 24, 0);

            // Selected state styling (only if not loading)
            if (isSelected && !isLoadingSelected && m_allowSwitching) {
                selectButton.borderColor = RGBAToImVec4(172, 131, 255, 255 / 4);
                selectButton.borderSize = 1.0f;
                selectButton.state = ButtonState::NORMAL;
                selectButton.tooltip = "Click to unload model from memory";
                selectButton.onClick = [this, &manager]() {
                    manager.unloadModel(m_model.name, manager.getCurrentVariantForModel(m_model.name));
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

            if (isSelected && manager.isLoadInProgress())
                deleteButton.state = ButtonState::DISABLED;
            else
                deleteButton.state = ButtonState::NORMAL;

            if (manager.isModelLoaded(m_model.name, manager.getCurrentVariantForModel(m_model.name)))
            {
                deleteButton.icon = ICON_CI_ARROW_UP;
				deleteButton.tooltip = "Click to unload model";
				deleteButton.onClick = [this, &manager]() {
					std::cout << "[ModelManagerModal] Unloading model from delete button: " << m_model.name << "\n";
					manager.unloadModel(m_model.name, manager.getCurrentVariantForModel(m_model.name));
					};
			}
			else
			{
				deleteButton.icon = ICON_CI_TRASH;
				deleteButton.tooltip = "Click to delete model";
                deleteButton.onClick = [this, &manager]() {
                    std::string currentVariant = manager.getCurrentVariantForModel(m_model.name);
                    m_onDeleteRequested(m_index, currentVariant);
                    };
			}

            Button::render(deleteButton);
        }

        ImGui::EndChild();
        if (ImGui::IsItemHovered() || (isSelected && m_allowSwitching)) {
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
	bool m_allowSwitching;

    void renderHeader() {
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);
        Label::render(authorLabel);

		ImGui::SameLine();

        float modelMemoryRequirement = 0;
		float kvramMemoryRequirement = 0;

		Model::ModelManager& manager = Model::ModelManager::getInstance();
		bool hasEnoughMemory = manager.hasEnoughMemoryForModel(m_model.name, 
            modelMemoryRequirement, kvramMemoryRequirement);

        ButtonConfig memorySufficientButton;
        memorySufficientButton.id = "##memorySufficient" + std::to_string(m_index) + m_id;
        memorySufficientButton.icon = ICON_CI_PASS_FILLED;
        memorySufficientButton.size = ImVec2(24, 0);
        memorySufficientButton.tooltip = "Model is compatible\n\nmodel: "
            + std::to_string(static_cast<int>(modelMemoryRequirement)) + " MB\nkv cache: "
            + std::to_string(static_cast<int>(kvramMemoryRequirement)) + " MB";

        if (!hasEnoughMemory)
        {
            memorySufficientButton.icon = ICON_CI_WARNING;
            memorySufficientButton.tooltip = "Not enough memory available\n\nmodel: "
                + std::to_string(static_cast<int>(modelMemoryRequirement)) + " MB\nkv cache: "
                + std::to_string(static_cast<int>(kvramMemoryRequirement)) + " MB";
        }

		// place it to the top right corner of the card
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 8);
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 24 - 2);

        Button::render(memorySufficientButton);

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
        const float variantAreaHeight = 80.0f;

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
    bool hasSufficientMemory;

    bool operator<(const SortableModel& other) const {
        return name < other.name;
    }
};

class ModelManagerModal {
public:
    ModelManagerModal() : m_searchText(""), m_shouldFocusSearch(false), m_showSufficientMemoryOnly(false) {}

    void render(bool& showDialog, bool allowSwitching = true) {
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

        // Check if a model was added through the custom model form
        if (m_addCustomModelModal.wasModelAdded()) {
            needsUpdate = true;
            m_addCustomModelModal.resetModelAddedFlag();
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

        auto renderCards = [numCards, this, &manager, allowSwitching]() {
            const auto& models = manager.getModels();

            // Render search field at the top
            renderSearchField();
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12.0F);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 12.0F);

            ButtonConfig addCustomModelBtn;
            addCustomModelBtn.id = "##addCustomModel";
            addCustomModelBtn.label = "Add Custom Model";
            addCustomModelBtn.icon = ICON_CI_PLUS;
			addCustomModelBtn.backgroundColor = ImVec4(0.3, 0.3, 0.3, 0.3);
			addCustomModelBtn.hoverColor = ImVec4(0.2, 0.2, 0.2, 0.2);
            addCustomModelBtn.size = ImVec2(180, 32.0f);
            addCustomModelBtn.onClick = [this]() {
                m_addCustomModelModalOpen = true;
                };
            Button::render(addCustomModelBtn);
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 12.0F);

            if (m_addCustomModelModalOpen) {
                m_addCustomModelModal.render(m_addCustomModelModalOpen);
            }

            if (m_deleteModalOpen) {
                m_deleteModal.render(m_deleteModalOpen);
            }

            LabelConfig downloadedSectionLabel;
            downloadedSectionLabel.id = "##downloadedModelsHeader";
            downloadedSectionLabel.label = "Downloaded Models";
            downloadedSectionLabel.size = ImVec2(0, 0);
            downloadedSectionLabel.fontSize = FontsManager::LG;
            downloadedSectionLabel.alignment = Alignment::LEFT;

            ImGui::SetCursorPos(ImVec2(ModelManagerConstants::padding, ImGui::GetCursorPosY()));
            Label::render(downloadedSectionLabel);

            // Add the "Show models with sufficient memory only" checkbox using custom widget
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - 32);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);

            LabelConfig memoryFilterLabel;
            memoryFilterLabel.id = "##memoryFilterCheckbox_label";
            memoryFilterLabel.label = "Show compatible model only";
            memoryFilterLabel.size = ImVec2(0, 0);
            memoryFilterLabel.fontType = FontsManager::REGULAR;
            memoryFilterLabel.fontSize = FontsManager::MD;
            memoryFilterLabel.alignment = Alignment::LEFT;

            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 4.0F);

            Label::render(memoryFilterLabel);

            ImGui::SameLine();

            ButtonConfig memoryFilterBtn;
            memoryFilterBtn.id = "##memoryFilterCheckbox";
            memoryFilterBtn.icon = m_showSufficientMemoryOnly ? ICON_CI_CHECK : ICON_CI_CLOSE;
            memoryFilterBtn.textColor = m_showSufficientMemoryOnly ? ImVec4(1, 1, 1, 1) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
            memoryFilterBtn.fontSize = FontsManager::SM;
            memoryFilterBtn.size = ImVec2(24, 24);
            memoryFilterBtn.backgroundColor = m_showSufficientMemoryOnly ? Config::Color::PRIMARY : RGBAToImVec4(60, 60, 60, 255);
            memoryFilterBtn.hoverColor = m_showSufficientMemoryOnly ? RGBAToImVec4(53, 132, 228, 255) : RGBAToImVec4(80, 80, 80, 255);
            memoryFilterBtn.activeColor = m_showSufficientMemoryOnly ? RGBAToImVec4(26, 95, 180, 255) : RGBAToImVec4(100, 100, 100, 255);
            memoryFilterBtn.tooltip = "Only show models that can run with your available memory";
            memoryFilterBtn.onClick = [this]() {
                m_showSufficientMemoryOnly = !m_showSufficientMemoryOnly;
                filterModels(); // Reapply filters when checkbox changes
                };
            Button::render(memoryFilterBtn);

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
                            }, "downloaded", allowSwitching);
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

            // Render "Available Models" section header with custom checkbox
            LabelConfig availableSectionLabel;
            availableSectionLabel.id = "##availableModelsHeader";
            availableSectionLabel.label = "Available Models";
            availableSectionLabel.size = ImVec2(0, 0);
            availableSectionLabel.fontSize = FontsManager::LG;
            availableSectionLabel.alignment = Alignment::LEFT;

            ImGui::SetCursorPosX(ModelManagerConstants::padding);
            Label::render(availableSectionLabel);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);

            // Check if we have any available models that match the search and filters
            if (m_filteredModels.empty()) {
                LabelConfig noModelsLabel;
                noModelsLabel.id = "##noAvailableModels";
                if (!m_searchText.empty()) {
                    noModelsLabel.label = "No models match your search. Try a different search term.";
                }
                else if (m_showSufficientMemoryOnly) {
                    noModelsLabel.label = "No models with high compatibility found. Try disabling the compatibility filter.";
                }
                else {
                    noModelsLabel.label = "No models available.";
                }
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

        if (m_needsUpdateAfterDelete && !m_deleteModalOpen) {
            updateSortedModels();
            filterModels(); // Apply search filter after updating models
            m_needsUpdateAfterDelete = false;
        }

        if (!ImGui::IsPopupOpen(config.id.c_str())) {
            showDialog = false;
        }
    }

private:
    DeleteModelModalComponent m_deleteModal;
    bool m_deleteModalOpen = false;
    bool m_wasShowing = false;
    bool m_needsUpdateAfterDelete = false;
    size_t m_lastModelCount = 0;
    std::unordered_set<std::string> m_lastDownloadedStatus;
    std::vector<SortableModel> m_sortedModels;
    std::vector<SortableModel> m_filteredModels;

    // Search related variables
    std::string m_searchText;
    bool m_shouldFocusSearch;

    // Memory filter checkbox state
    bool m_showSufficientMemoryOnly;


    AddCustomModelModalComponent m_addCustomModelModal;
    bool m_addCustomModelModalOpen = false;

    void updateSortedModels() {
        auto& manager = Model::ModelManager::getInstance();
        const auto& models = manager.getModels();

        // Clear and rebuild the sorted model list
        m_sortedModels.clear();
        m_sortedModels.reserve(models.size());

        for (size_t i = 0; i < models.size(); ++i) {
            // Check memory sufficiency status
            float modelMemoryRequirement = 0;
            float kvramMemoryRequirement = 0;
            bool hasSufficientMemory = manager.hasEnoughMemoryForModel(
                models[i].name, modelMemoryRequirement, kvramMemoryRequirement);

            // Store the index, name, and memory status
            m_sortedModels.push_back({
                static_cast<int>(i),
                models[i].name,
                hasSufficientMemory
                });
        }

        // Sort models alphabetically by name
        std::sort(m_sortedModels.begin(), m_sortedModels.end());

        // Initialize filtered models with all models when sort is updated
        filterModels();
    }

    // Filter models based on search text and memory filter
    void filterModels() {
        m_filteredModels.clear();
        auto& manager = Model::ModelManager::getInstance();
        const auto& models = manager.getModels();

        // Convert search text to lowercase for case-insensitive comparison
        std::string searchLower = m_searchText;
        std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(),
            [](unsigned char c) { return std::tolower(c); });

        // Filter models based on name OR author containing the search text
        // AND the memory sufficiency if that filter is enabled
        for (const auto& model : m_sortedModels) {
            // Skip models that don't have sufficient memory if filter is enabled
            if (m_showSufficientMemoryOnly && !model.hasSufficientMemory) {
                continue;
            }

            // If search text is empty, include the model (it already passed the memory filter)
            if (searchLower.empty()) {
                m_filteredModels.push_back(model);
                continue;
            }

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