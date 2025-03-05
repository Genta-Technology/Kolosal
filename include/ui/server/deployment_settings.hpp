#pragma once

#include "imgui.h"
#include "ui/widgets.hpp"
#include "model/model_loader_config_manager.hpp"

#include <IconsCodicons.h>
#include <string>
#include <functional>

namespace DeploymentSettingsConstants {
    constexpr ImGuiWindowFlags SidebarFlags =
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoScrollbar;
}

class ModelLoaderSettingsComponent {
public:
    // Takes sidebarWidth by reference to always reflect the current width
    ModelLoaderSettingsComponent(float& sidebarWidth)
        : m_sidebarWidth(sidebarWidth)
    {
        // Initialize labels
        m_contextSizeLabel = createLabel("Context Size", ICON_CI_BRACKET);
        m_gpuLayersLabel = createLabel("GPU Layers", ICON_CI_CHIP);
        m_systemSettingsLabel = createLabel("System Settings", ICON_CI_SERVER);
        m_optimizationLabel = createLabel("Optimization Settings", ICON_CI_DASHBOARD);
    }

    void render() {
        auto& configManager = Model::ModelLoaderConfigManager::getInstance();

        // Context Size Section
        ImGui::Spacing(); ImGui::Spacing();
        Label::render(m_contextSizeLabel);
        ImGui::Spacing(); ImGui::Spacing();

        const float sliderWidth = m_sidebarWidth - 30;

        // n_ctx slider (context size)
        int n_ctx = configManager.getContextSize();
        Slider::render("##n_ctx", *(float*)&n_ctx, 1024.0f, 16384.0f, sliderWidth, "%.0f");
        if (n_ctx != configManager.getContextSize()) {
            configManager.setContextSize(n_ctx);
        }

        // n_keep slider (keep size)
        int n_keep = configManager.getKeepSize();
        Slider::render("##n_keep", *(float*)&n_keep, 0.0f, (float)n_ctx, sliderWidth, "%.0f");
        if (n_keep != configManager.getKeepSize()) {
            configManager.setKeepSize(n_keep);
        }

        // GPU Section
        ImGui::Spacing(); ImGui::Spacing();
        Label::render(m_gpuLayersLabel);
        ImGui::Spacing(); ImGui::Spacing();

        // n_gpu_layers slider
        int n_gpu_layers = configManager.getGpuLayers();
        Slider::render("##n_gpu_layers", *(float*)&n_gpu_layers, 0.0f, 100.0f, sliderWidth, "%.0f");
        if (n_gpu_layers != configManager.getGpuLayers()) {
            configManager.setGpuLayers(n_gpu_layers);
        }

        // System Settings Section
        ImGui::Spacing(); ImGui::Spacing();
        Label::render(m_systemSettingsLabel);
        ImGui::Spacing(); ImGui::Spacing();

        // use_mlock checkbox
        renderCheckbox("Memory Lock", "##use_mlock", configManager.getUseMlock(),
            [&configManager](bool value) { configManager.setUseMlock(value); },
            "Locks memory to prevent swapping to disk");

        // use_mmap checkbox
        renderCheckbox("Memory Map", "##use_mmap", configManager.getUseMmap(),
            [&configManager](bool value) { configManager.setUseMmap(value); },
            "Use memory mapping for model weights");

        // n_parallel input
        ImGui::Spacing();
        int n_parallel = configManager.getParallelCount();
        IntInputField::render("##n_parallel", n_parallel, sliderWidth);
        if (n_parallel != configManager.getParallelCount()) {
            configManager.setParallelCount(n_parallel);
        }

        // Optimization Settings Section
        ImGui::Spacing(); ImGui::Spacing();
        Label::render(m_optimizationLabel);
        ImGui::Spacing(); ImGui::Spacing();

        // cont_batching checkbox
        renderCheckbox("Continuous Batching", "##cont_batching", configManager.getContinuousBatching(),
            [&configManager](bool value) { configManager.setContinuousBatching(value); },
            "Enable continuous batching for better performance");

        // warmup checkbox
        renderCheckbox("Warmup", "##warmup", configManager.getWarmup(),
            [&configManager](bool value) { configManager.setWarmup(value); },
            "Run model warmup at initialization");

        // Save and Reset Buttons
        ImGui::Spacing(); ImGui::Spacing(); ImGui::Spacing();
        renderSaveButtons();
    }

private:
    float& m_sidebarWidth;
    LabelConfig m_contextSizeLabel;
    LabelConfig m_gpuLayersLabel;
    LabelConfig m_systemSettingsLabel;
    LabelConfig m_optimizationLabel;

    LabelConfig createLabel(const std::string& text, const std::string& icon) {
        LabelConfig label;
        label.id = "##" + text + "_label";
        label.label = text;
        label.icon = icon;
        label.size = ImVec2(Config::Icon::DEFAULT_FONT_SIZE, 0);
        label.fontType = FontsManager::BOLD;
        return label;
    }

    void renderCheckbox(const std::string& label, const std::string& id, bool value, std::function<void(bool)> onChange, const std::string& tooltip = "") {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 5.0f);

        ButtonConfig btnConfig;
        btnConfig.id = id;
        btnConfig.icon = value ? ICON_CI_CHECK : ICON_CI_CLOSE;
        btnConfig.textColor = value ? ImVec4(1, 1, 1, 1) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
        btnConfig.fontSize = FontsManager::SM;
        btnConfig.size = ImVec2(24, 24);
        btnConfig.backgroundColor = value ? Config::Color::PRIMARY : RGBAToImVec4(60, 60, 60, 255);
        btnConfig.hoverColor = value ? RGBAToImVec4(53, 132, 228, 255) : RGBAToImVec4(80, 80, 80, 255);
        btnConfig.activeColor = value ? RGBAToImVec4(26, 95, 180, 255) : RGBAToImVec4(100, 100, 100, 255);
        btnConfig.onClick = [value, onChange]() {
            onChange(!value);
            };
        if (!tooltip.empty()) {
            btnConfig.tooltip = tooltip;
        }
        Button::render(btnConfig);

        ImGui::SameLine(0.0f, 8.0f);
        LabelConfig labelConfig;
        labelConfig.id = id + "_label";
        labelConfig.label = label;
        labelConfig.size = ImVec2(0, 0);
        labelConfig.fontType = FontsManager::REGULAR;
        labelConfig.fontSize = FontsManager::MD;
        labelConfig.alignment = Alignment::LEFT;
        Label::render(labelConfig);

        ImGui::Spacing();
    }

    void renderSaveButtons() {
        auto& configManager = Model::ModelLoaderConfigManager::getInstance();

        ButtonConfig saveConfig;
        saveConfig.id = "##save_config";
        saveConfig.label = "Save Configuration";
        saveConfig.size = ImVec2(m_sidebarWidth / 2 - 15, 0);
        saveConfig.onClick = [&]() {
            configManager.saveConfig();
            };
        saveConfig.backgroundColor = RGBAToImVec4(26, 95, 180, 255);
        saveConfig.hoverColor = RGBAToImVec4(53, 132, 228, 255);
        saveConfig.activeColor = RGBAToImVec4(26, 95, 180, 255);

        ButtonConfig resetConfig;
        resetConfig.id = "##reset_config";
        resetConfig.label = "Reset";
        resetConfig.size = ImVec2(m_sidebarWidth / 2 - 15, 0);
        resetConfig.onClick = [&]() {
            configManager.loadConfig();
            };
        resetConfig.backgroundColor = RGBAToImVec4(180, 26, 26, 255);
        resetConfig.hoverColor = RGBAToImVec4(228, 53, 53, 255);
        resetConfig.activeColor = RGBAToImVec4(180, 26, 26, 255);

        Button::renderGroup({ saveConfig, resetConfig }, 9, ImGui::GetCursorPosY(), 10);
    }
};

class DeploymentSettingsSidebar {
public:
    DeploymentSettingsSidebar() :
        m_width(Config::DeploymentSettingsSidebar::SIDEBAR_WIDTH),
        m_modelLoaderSettingsComponent(m_width) {
    }

    void render() {
        ImGuiIO& io = ImGui::GetIO();
        const float sidebarHeight = io.DisplaySize.y - Config::TITLE_BAR_HEIGHT;

        // Right sidebar window
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - m_width, Config::TITLE_BAR_HEIGHT), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(m_width, sidebarHeight), ImGuiCond_Always);
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(Config::DeploymentSettingsSidebar::MIN_SIDEBAR_WIDTH, sidebarHeight),
            ImVec2(Config::DeploymentSettingsSidebar::MAX_SIDEBAR_WIDTH, sidebarHeight)
        );

        ImGui::Begin("Deployment Settings", nullptr, DeploymentSettingsConstants::SidebarFlags);

        // Update the current sidebar width
        m_width = ImGui::GetWindowSize().x;

        // Render header
        renderHeader();
        ImGui::Separator();

        // Render scrollable content area
        ImGui::BeginChild("##deployment_settings_content", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

        // Render model loader settings component
        m_modelLoaderSettingsComponent.render();

        ImGui::EndChild();

        ImGui::End();
    }

    float getWidth() const { return m_width; }

private:
    float m_width = 0.0F;
    ModelLoaderSettingsComponent m_modelLoaderSettingsComponent;

    void renderHeader() {
        LabelConfig titleLabel;
        titleLabel.id = "##deployment_settings_title";
        titleLabel.label = "Deployment Settings";
        titleLabel.icon = ICON_CI_ROCKET;
        titleLabel.size = ImVec2(Config::Icon::DEFAULT_FONT_SIZE, 0);
        titleLabel.fontType = FontsManager::BOLD;
        titleLabel.fontSize = FontsManager::LG;
        titleLabel.alignment = Alignment::LEFT;

        ImGui::Spacing();
        Label::render(titleLabel);
        ImGui::Spacing();

        // Display current datetime
        LabelConfig datetimeLabel;
        datetimeLabel.id = "##deployment_settings_datetime";
        datetimeLabel.label = "2025-03-04 18:57:13 UTC | rifkybujana";
        datetimeLabel.size = ImVec2(0, 0);
        datetimeLabel.fontType = FontsManager::REGULAR;
        datetimeLabel.fontSize = FontsManager::SM;
        datetimeLabel.color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 5.0f);
        Label::render(datetimeLabel);
        ImGui::Spacing();
    }
};