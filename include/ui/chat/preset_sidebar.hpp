#pragma once

#include "imgui.h"
#include "model/preset_manager.hpp"
#include "ui/widgets.hpp"
#include "config.hpp"
#include "nfd.h"
#include <filesystem>
#include <string>
#include <vector>
#include <functional>

class PresetSelectionComponent {
public:
    // m_sidebarWidth is taken by reference so it always reflects the current width.
    PresetSelectionComponent(float& sidebarWidth)
        : m_sidebarWidth(sidebarWidth)
    {
    }

    void render() {
        renderPresetLabel();
        ImGui::Spacing();
        ImGui::Spacing();

        updatePresetNames();
        int currentIndex = getCurrentPresetIndex();

        const float comboWidth = m_sidebarWidth - 54;
        if (ComboBox::render("##modelpresets", m_presetNames.data(),
            static_cast<int>(m_presetNames.size()), currentIndex, comboWidth))
        {
            // When a preset is selected, switch to it.
            Model::PresetManager::getInstance().switchPreset(m_presetNames[currentIndex]);
        }

        renderDeleteButton();
        renderSaveButtons();
    }

    // Callback used to request a "Save As" dialog.
    std::function<void()> m_onSaveAsRequested;

private:
    float& m_sidebarWidth;
    // Store preset names in a vector of strings so that the c_str() pointers remain valid.
    std::vector<std::string> m_presetNameStorage;
    std::vector<const char*> m_presetNames;

    void renderPresetLabel() {
        LabelConfig presetLabel{
            "##modelpresets_label",             // id
            "Model Presets",                    // label
            ICON_CI_PACKAGE,                    // icon
            ImVec2(Config::Icon::DEFAULT_FONT_SIZE, 0),
            0.0f,
            0.0f,
            FontsManager::BOLD
        };
        Label::render(presetLabel);
    }

    // Refresh the preset name storage and build an array of c_str pointers.
    void updatePresetNames() {
        const auto& presets = Model::PresetManager::getInstance().getPresets();
        m_presetNameStorage.clear();

        // Copy preset names and sort them alphabetically
        for (const auto& preset : presets) {
            m_presetNameStorage.push_back(preset.name);
        }
        // Sort the names to match the sorted order used by getSortedPresetIndex
        std::sort(m_presetNameStorage.begin(), m_presetNameStorage.end());

        // Update m_presetNames with sorted names
        m_presetNames.clear();
        for (const auto& name : m_presetNameStorage) {
            m_presetNames.push_back(name.c_str());
        }
    }

    int getCurrentPresetIndex() {
        auto currentPresetOpt = Model::PresetManager::getInstance().getCurrentPreset();
        if (currentPresetOpt) {
            const std::string& currentName = currentPresetOpt->get().name;
            // Find the index in the sorted m_presetNameStorage
            auto it = std::lower_bound(m_presetNameStorage.begin(), m_presetNameStorage.end(), currentName);
            if (it != m_presetNameStorage.end() && *it == currentName) {
                return static_cast<int>(std::distance(m_presetNameStorage.begin(), it));
            }
        }
        return 0;
    }

    void renderDeleteButton() {
        auto& manager = Model::PresetManager::getInstance();
        const auto& presets = manager.getPresets();
        ImGui::SameLine();
        ButtonConfig deleteConfig;
        deleteConfig.id = "##delete";
        deleteConfig.icon = ICON_CI_TRASH;
        deleteConfig.size = ImVec2(24, 0);
        deleteConfig.alignment = Alignment::CENTER;
        deleteConfig.backgroundColor = Config::Color::TRANSPARENT_COL;
        deleteConfig.hoverColor = RGBAToImVec4(191, 88, 86, 255);
        deleteConfig.activeColor = RGBAToImVec4(165, 29, 45, 255);
        deleteConfig.onClick = [&]() {
            if (presets.size() > 1 && manager.getCurrentPreset()) {
                // Get the index of the current preset in the sorted list
                int curIndex = static_cast<int>(manager.getSortedPresetIndex(manager.getCurrentPreset()->get().name));
                std::string currentPresetName = manager.getCurrentPreset()->get().name;

                // Delete the current preset.
                if (manager.deletePreset(currentPresetName).get()) {
                    // Get the updated preset list.
                    const auto& updatedPresets = manager.getPresets();
                    if (!updatedPresets.empty()) {
                        // Pick the previous preset if possible, else the first preset.
                        int newIndex = curIndex > 0 ? curIndex - 1 : 0;
                        manager.switchPreset(updatedPresets[newIndex].name);
                    }
                }
            }
            };
        deleteConfig.state = (presets.size() <= 1) ? ButtonState::DISABLED : ButtonState::NORMAL;
        Button::render(deleteConfig);
    }

    void renderSaveButtons() {
        ImGui::Spacing();
        ImGui::Spacing();
        ButtonConfig saveConfig;
        saveConfig.id = "##save";
        saveConfig.label = "Save";
        saveConfig.size = ImVec2(m_sidebarWidth / 2 - 15, 0);
        saveConfig.onClick = [&]() {
            if (Model::PresetManager::getInstance().hasUnsavedChanges()) {
                Model::PresetManager::getInstance().saveCurrentPreset().get();
            }
            };

        ButtonConfig saveAsConfig;
        saveAsConfig.id = "##saveasnew";
        saveAsConfig.label = "Save as New";
        saveAsConfig.size = ImVec2(m_sidebarWidth / 2 - 15, 0);
        // When the save-as button is clicked, invoke the callback.
        saveAsConfig.onClick = [&]() { m_onSaveAsRequested(); };

        const bool hasChanges = Model::PresetManager::getInstance().hasUnsavedChanges();
        saveConfig.backgroundColor = hasChanges ? RGBAToImVec4(26, 95, 180, 255)
            : RGBAToImVec4(26, 95, 180, 128);
        saveConfig.hoverColor = RGBAToImVec4(53, 132, 228, 255);
        saveConfig.activeColor = RGBAToImVec4(26, 95, 180, 255);
        Button::renderGroup({ saveConfig, saveAsConfig }, 9, ImGui::GetCursorPosY(), 10);

		ImGui::Spacing(); ImGui::Spacing();
    }
};

class SamplingSettingsComponent {
public:
    // Takes sidebarWidth by reference.
    SamplingSettingsComponent(float& sidebarWidth, bool& focusSystemPrompt)
        : m_sidebarWidth(sidebarWidth), m_focusSystemPrompt(focusSystemPrompt)
    {
    }

    void render() {
        auto currentPresetOpt = Model::PresetManager::getInstance().getCurrentPreset();
        if (!currentPresetOpt) return;
        auto& currentPreset = currentPresetOpt->get();

        // Render the system prompt label and multi-line input field.
		ImGui::Spacing(); ImGui::Spacing();
        Label::render(m_systemPromptLabel);
        ImGui::Spacing();
        ImGui::Spacing();
        InputFieldConfig inputConfig(
            "##systemprompt",
            ImVec2(m_sidebarWidth - 20, 100),
            currentPreset.systemPrompt,
            m_focusSystemPrompt
        );
        inputConfig.placeholderText = "Enter your system prompt here...";
        inputConfig.processInput = [&](const std::string& input) {
            currentPreset.systemPrompt = input;
            };
        InputField::renderMultiline(inputConfig);

        // Render the model settings label and sampling sliders/inputs.
        ImGui::Spacing();
        ImGui::Spacing();
        Label::render(m_modelSettingsLabel);
        ImGui::Spacing();
        ImGui::Spacing();
        const float sliderWidth = m_sidebarWidth - 30;
        Slider::render("##temperature", currentPreset.temperature, 0.0f, 1.0f, sliderWidth);
        Slider::render("##top_p", currentPreset.top_p, 0.0f, 1.0f, sliderWidth);
        Slider::render("##top_k", currentPreset.top_k, 0.0f, 100.0f, sliderWidth, "%.0f");
        IntInputField::render("##random_seed", currentPreset.random_seed, sliderWidth);
        ImGui::Spacing();
        ImGui::Spacing();
        Slider::render("##min_length", currentPreset.min_length, 0.0f, 4096.0f, sliderWidth, "%.0f");
        Slider::render("##max_new_tokens", currentPreset.max_new_tokens, 0.0f, 8192.0f, sliderWidth, "%.0f");
    }

    // Optional setters to override default labels.
    void setSystemPromptLabel(const LabelConfig& label) { m_systemPromptLabel = label; }
    void setModelSettingsLabel(const LabelConfig& label) { m_modelSettingsLabel = label; }

private:
    float& m_sidebarWidth;
    bool& m_focusSystemPrompt;
    LabelConfig m_systemPromptLabel{
        "##systempromptlabel",
        "System Prompt",
        ICON_CI_GEAR,
        ImVec2(Config::Icon::DEFAULT_FONT_SIZE, 0),
        0.0f,
        0.0f,
        FontsManager::BOLD
    };
    LabelConfig m_modelSettingsLabel{
        "##modelsettings",
        "Model Settings",
        ICON_CI_SETTINGS,
        ImVec2(Config::Icon::DEFAULT_FONT_SIZE, 0),
        0.0f,
        0.0f,
        FontsManager::BOLD
    };
};

class SaveAsDialogComponent {
public:
    // Takes sidebarWidth by reference.
    SaveAsDialogComponent(float& sidebarWidth, bool& focusNewPresetName)
        : m_sidebarWidth(sidebarWidth), m_focusNewPresetName(focusNewPresetName)
    {
    }

    void render(bool& showDialog, std::string& newPresetName) {
        // Always render the modal so that it stays open if already open.
        ModalConfig config{
            "Save Preset As",           // Title
            "Save As New Preset",       // Identifier
            ImVec2(300, 98),
            [&]() {
                // If no new preset name is provided, default to the current preset name.
                if (newPresetName.empty() && Model::PresetManager::getInstance().getCurrentPreset()) {
                    newPresetName = Model::PresetManager::getInstance().getCurrentPreset()->get().name;
                }
                // Set up the input field configuration
                InputFieldConfig inputConfig(
                    "##newpresetname",
                    ImVec2(ImGui::GetWindowSize().x - 32.0f, 0),
                    newPresetName,
                    m_focusNewPresetName
                );
                inputConfig.placeholderText = "Enter new preset name...";
                inputConfig.flags = ImGuiInputTextFlags_EnterReturnsTrue;
                inputConfig.frameRounding = 5.0f;

                // Process the input when the user hits Enter.
                inputConfig.processInput = [&](const std::string& input) {
                    if (!input.empty() &&
                        Model::PresetManager::getInstance().copyCurrentPresetAs(input).get())
                    {
                        Model::PresetManager::getInstance().switchPreset(input);
                        showDialog = false;
                        newPresetName.clear();
						ImGui::CloseCurrentPopup();
                    }
                };

                // Render the input field.
                InputField::render(inputConfig);
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
    bool& m_focusNewPresetName;
};

class ExportButtonComponent {
public:
    // Takes sidebarWidth by reference.
    ExportButtonComponent(float& sidebarWidth)
        : m_sidebarWidth(sidebarWidth)
    {
        m_exportConfig.id = "##export";
        m_exportConfig.label = "Export as JSON";
        m_exportConfig.size = ImVec2(0, 0);
        m_exportConfig.alignment = Alignment::CENTER;
        m_exportConfig.onClick = [this]() { exportPresets(); };
        m_exportConfig.state = ButtonState::NORMAL;
        m_exportConfig.fontSize = FontsManager::MD;
        m_exportConfig.backgroundColor = Config::Color::SECONDARY;
        m_exportConfig.hoverColor = Config::Color::PRIMARY;
        m_exportConfig.activeColor = Config::Color::SECONDARY;
    }

    void render() {
        ImGui::Spacing();
        ImGui::Spacing();
        m_exportConfig.size = ImVec2(m_sidebarWidth - 20, 0);
        Button::render(m_exportConfig);
    }

private:
    float& m_sidebarWidth;
    ButtonConfig m_exportConfig;

    void exportPresets() {
        nfdu8char_t* outPath = nullptr;
        nfdu8filteritem_t filters[1] = { {"JSON Files", "json"} };
        nfdsavedialogu8args_t args{};
        args.filterList = filters;
        args.filterCount = 1;
        if (NFD_SaveDialogU8_With(&outPath, &args) == NFD_OKAY) {
            std::filesystem::path savePath(outPath);
            NFD_FreePathU8(outPath);
            if (savePath.extension() != ".json") {
                savePath += ".json";
            }
            Model::PresetManager::getInstance().saveCurrentPresetToPath(savePath).get();
        }
    }
};

class ModelPresetSidebar {
public:
    ModelPresetSidebar()
        : m_sidebarWidth(Config::ChatHistorySidebar::SIDEBAR_WIDTH),
        m_presetSelectionComponent(m_sidebarWidth),
        m_samplingSettingsComponent(m_sidebarWidth, m_focusSystemPrompt),
        m_saveAsDialogComponent(m_sidebarWidth, m_focusNewPresetName),
        m_exportButtonComponent(m_sidebarWidth)
    {
        // Set up the callback for "Save as New" so that it shows the modal.
        m_presetSelectionComponent.m_onSaveAsRequested = [this]() { m_showSaveAsDialog = true; };
    }

    void render() {
        ImGuiIO& io = ImGui::GetIO();
        const float sidebarHeight = io.DisplaySize.y - Config::TITLE_BAR_HEIGHT;

        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - m_sidebarWidth, Config::TITLE_BAR_HEIGHT), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(m_sidebarWidth, sidebarHeight), ImGuiCond_Always);
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(Config::ModelPresetSidebar::MIN_SIDEBAR_WIDTH, sidebarHeight),
            ImVec2(Config::ModelPresetSidebar::MAX_SIDEBAR_WIDTH, sidebarHeight)
        );

        ImGui::Begin("Model Settings", nullptr,
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoScrollbar);
        // Update m_sidebarWidth so that all components see the new size.
        m_sidebarWidth = ImGui::GetWindowSize().x;

        m_presetSelectionComponent.render();
        ImGui::Separator();
        m_samplingSettingsComponent.render();
        ImGui::Separator();
        m_exportButtonComponent.render();

        ImGui::End();

        m_saveAsDialogComponent.render(m_showSaveAsDialog, m_newPresetName);
    }

	float getSidebarWidth() const {
		return m_sidebarWidth;
	}

private:
    float m_sidebarWidth;
    bool m_showSaveAsDialog = false;
    std::string m_newPresetName;
    bool m_focusSystemPrompt = true;
    bool m_focusNewPresetName = true;

    PresetSelectionComponent m_presetSelectionComponent;
    SamplingSettingsComponent m_samplingSettingsComponent;
    SaveAsDialogComponent m_saveAsDialogComponent;
    ExportButtonComponent m_exportButtonComponent;
};