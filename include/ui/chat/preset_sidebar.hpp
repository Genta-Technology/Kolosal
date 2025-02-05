#pragma once

#include "imgui.h"
#include "model/preset_manager.hpp"
#include "ui/widgets.hpp"
#include "config.hpp"
#include "nfd.h"

class ModelPresetSidebar {
public:
    ModelPresetSidebar() {
        // Initialize reusable configurations
        {
			systemPromptLabel.id = "##systempromptlabel";
			systemPromptLabel.label = "System Prompt";
			systemPromptLabel.icon = ICON_CI_GEAR;
			systemPromptLabel.size = ImVec2(Config::Icon::DEFAULT_FONT_SIZE, 0);
			systemPromptLabel.fontSize = FontsManager::MD;
			systemPromptLabel.fontType = FontsManager::BOLD;
        }

        {
			modelSettingsLabel.id = "##modelsettings";
			modelSettingsLabel.label = "Model Settings";
			modelSettingsLabel.icon = ICON_CI_SETTINGS;
			modelSettingsLabel.size = ImVec2(Config::Icon::DEFAULT_FONT_SIZE, 0);
			modelSettingsLabel.fontSize = FontsManager::MD;
			modelSettingsLabel.fontType = FontsManager::BOLD;
		}

        {
			exportButtonConfig.id = "##export";
			exportButtonConfig.label = "Export as JSON";
			exportButtonConfig.size = ImVec2(0, 0);
			exportButtonConfig.alignment = Alignment::CENTER;
			exportButtonConfig.onClick = [this]() { exportPresets(); };
			exportButtonConfig.state = ButtonState::NORMAL;
			exportButtonConfig.fontSize = FontsManager::MD;
			exportButtonConfig.backgroundColor = Config::Color::SECONDARY;
			exportButtonConfig.hoverColor = Config::Color::PRIMARY;
			exportButtonConfig.activeColor = Config::Color::SECONDARY;
		}

        sidebarWidth = Config::ChatHistorySidebar::SIDEBAR_WIDTH;
    }

    void render() {
        ImGuiIO& io = ImGui::GetIO();
        const float sidebarHeight = io.DisplaySize.y - Config::TITLE_BAR_HEIGHT;

        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - sidebarWidth, Config::TITLE_BAR_HEIGHT), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(sidebarWidth, sidebarHeight), ImGuiCond_Always);
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(Config::ModelPresetSidebar::MIN_SIDEBAR_WIDTH, sidebarHeight),
            ImVec2(Config::ModelPresetSidebar::MAX_SIDEBAR_WIDTH, sidebarHeight));

        ImGui::Begin("Model Settings", nullptr, sidebarFlags);
        sidebarWidth = ImGui::GetWindowSize().x;

        renderPresetSelection();
        ImGui::Separator();
        renderSamplingSettings();
        ImGui::Separator();
        renderExportButton();

        ImGui::End();

        renderSaveAsDialog();
    }

private:
    float sidebarWidth;
    bool showSaveAsDialog = false;
    std::string newPresetName;
    bool focusSystemPrompt = true;
	bool focusNewPresetName = true;

    LabelConfig systemPromptLabel;
    LabelConfig modelSettingsLabel;
    ButtonConfig exportButtonConfig;

    static constexpr ImGuiWindowFlags sidebarFlags =
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoScrollbar;

    void renderPresetSelection() {
        LabelConfig presetLabel = {
            "##modelpresets",
            "Model Presets",
            ICON_CI_PACKAGE,
            ImVec2(Config::Icon::DEFAULT_FONT_SIZE, 0),
            0.0f,
            0.0f,
            FontsManager::BOLD
        };
        Label::render(presetLabel);

        ImGui::Spacing();
        ImGui::Spacing();

        const auto& presets = Model::PresetManager::getInstance().getPresets();
        presetNames.clear();
        for (const auto& preset : presets) {
            presetNames.push_back(preset.name.c_str());
        }

        const auto currentPresetOpt = Model::PresetManager::getInstance().getCurrentPreset();
        int currentIndex = currentPresetOpt ?
            static_cast<int>(Model::PresetManager::getInstance().getSortedPresetIndex(
                currentPresetOpt->get().name)) : 0;

        const float comboWidth = sidebarWidth - 54;
        if (ComboBox::render("##modelpresets", presetNames.data(),
            static_cast<int>(presetNames.size()), currentIndex, comboWidth)) {
            Model::PresetManager::getInstance().switchPreset(presetNames[currentIndex]);
        }

        renderDeleteButton(presets);
        renderSaveButtons();
    }

    void renderDeleteButton(const std::vector<Model::ModelPreset>& presets) {
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
            if (presets.size() > 1 && Model::PresetManager::getInstance().getCurrentPreset()) {
                const auto& name = Model::PresetManager::getInstance().getCurrentPreset()->get().name;
                Model::PresetManager::getInstance().deletePreset(name).get();
            }
            };
        deleteConfig.state = presets.size() <= 1 ? ButtonState::DISABLED : ButtonState::NORMAL;

        Button::render(deleteConfig);
    }

    void renderSaveButtons() {
        ImGui::Spacing();
        ImGui::Spacing();

        ButtonConfig saveConfig;
        saveConfig.id = "##save";
        saveConfig.label = "Save";
        saveConfig.size = ImVec2(sidebarWidth / 2 - 15, 0);
        saveConfig.onClick = [&]() {
            if (Model::PresetManager::getInstance().hasUnsavedChanges()) {
                Model::PresetManager::getInstance().saveCurrentPreset().get();
            }
            };

        ButtonConfig saveAsConfig;
        saveAsConfig.id = "##saveasnew";
        saveAsConfig.label = "Save as New";
        saveAsConfig.size = ImVec2(sidebarWidth / 2 - 15, 0);
        saveAsConfig.onClick = [&]() { showSaveAsDialog = true; };

        const auto hasChanges = Model::PresetManager::getInstance().hasUnsavedChanges();
        saveConfig.backgroundColor = hasChanges ? RGBAToImVec4(26, 95, 180, 255)
            : RGBAToImVec4(26, 95, 180, 128);
        saveConfig.hoverColor = RGBAToImVec4(53, 132, 228, 255);
        saveConfig.activeColor = RGBAToImVec4(26, 95, 180, 255);

        Button::renderGroup({ saveConfig, saveAsConfig }, 9, ImGui::GetCursorPosY(), 10);
    }

    void renderSamplingSettings() {
        auto currentPresetOpt = Model::PresetManager::getInstance().getCurrentPreset();
        if (!currentPresetOpt) return;
        auto& currentPreset = currentPresetOpt->get();

        Label::render(systemPromptLabel);
        ImGui::Spacing();
        ImGui::Spacing();

        InputFieldConfig inputConfig(
            "##systemprompt",
            ImVec2(sidebarWidth - 20, 100),
            currentPreset.systemPrompt,
            focusSystemPrompt
        );
        inputConfig.placeholderText = "Enter your system prompt here...";
        inputConfig.processInput = [&](const std::string& input) {
            currentPreset.systemPrompt = input;
            };
        InputField::renderMultiline(inputConfig);

        ImGui::Spacing();
        ImGui::Spacing();
        Label::render(modelSettingsLabel);
        ImGui::Spacing();
        ImGui::Spacing();

        const float sliderWidth = sidebarWidth - 30;
        Slider::render("##temperature", currentPreset.temperature, 0.0f, 1.0f, sliderWidth);
        Slider::render("##top_p", currentPreset.top_p, 0.0f, 1.0f, sliderWidth);
        Slider::render("##top_k", currentPreset.top_k, 0.0f, 100.0f, sliderWidth, "%.0f");
        IntInputField::render("##random_seed", currentPreset.random_seed, sliderWidth);

        ImGui::Spacing();
        ImGui::Spacing();

        Slider::render("##min_length", currentPreset.min_length, 0.0f, 4096.0f, sliderWidth, "%.0f");
        Slider::render("##max_new_tokens", currentPreset.max_new_tokens, 0.0f, 8192.0f, sliderWidth, "%.0f");
    }

    void renderSaveAsDialog() {
        if (!showSaveAsDialog) return;

        ModalConfig config{
            "Save Preset As",
            "Save As New Preset",
            ImVec2(300, 98),
            [&]() {
                if (newPresetName.empty() && Model::PresetManager::getInstance().getCurrentPreset()) {
                    newPresetName = Model::PresetManager::getInstance().getCurrentPreset()->get().name;
                }

                InputFieldConfig inputConfig(
                    "##newpresetname",
                    ImVec2(ImGui::GetWindowSize().x - 32.0f, 0),
                    newPresetName,
                    focusNewPresetName
                );
                inputConfig.placeholderText = "Enter new preset name...";
                inputConfig.flags = ImGuiInputTextFlags_EnterReturnsTrue;
                inputConfig.frameRounding = 5.0f;
                inputConfig.processInput = [&](const std::string& input) {
                    if (Model::PresetManager::getInstance().copyCurrentPresetAs(input).get()) {
                        Model::PresetManager::getInstance().switchPreset(input);
                        showSaveAsDialog = false;
                        newPresetName.clear();
                    }
                };

                InputField::render(inputConfig);
            },
            showSaveAsDialog
        };
        config.padding = ImVec2(16.0f, 8.0f);

        ModalWindow::render(config);
    }

    void renderExportButton() {
        ImGui::Spacing();
        ImGui::Spacing();

        exportButtonConfig.size = ImVec2(sidebarWidth - 20, 0);
        Button::render(exportButtonConfig);
    }

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

    std::vector<const char*> presetNames;
};