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
        auto& serverState = ServerStateManager::getInstance();

        const float sliderWidth = m_sidebarWidth - 30;

        // n_ctx slider (context size) - using float for slider then converting back to int
        {
            int n_ctx = configManager.getContextSize();
            float n_ctx_float = static_cast<float>(n_ctx);
            Slider::render("##n_ctx", n_ctx_float, 1024.0f, 16384.0f, sliderWidth, "%.0f");
            int new_n_ctx = static_cast<int>(n_ctx_float);
            if (new_n_ctx != n_ctx) {
                configManager.setContextSize(new_n_ctx);
                configManager.saveConfig(); // Auto-save on change
                serverState.setModelParamsChanged(true); // Mark params as changed
            }
        }

        // n_keep slider (keep size) - using float for slider then converting back to int
        {
            int n_keep = configManager.getKeepSize();
            float n_keep_float = static_cast<float>(n_keep);
            Slider::render("##n_keep", n_keep_float, 0.0f, static_cast<float>(configManager.getContextSize()), sliderWidth, "%.0f");
            int new_n_keep = static_cast<int>(n_keep_float);
            if (new_n_keep != n_keep) {
                configManager.setKeepSize(new_n_keep);
                configManager.saveConfig(); // Auto-save on change
                serverState.setModelParamsChanged(true); // Mark params as changed
            }
        }

        // n_gpu_layers slider - using float for slider then converting back to int
        {
            int n_gpu_layers = configManager.getGpuLayers();
            float n_gpu_layers_float = static_cast<float>(n_gpu_layers);
            Slider::render("##n_gpu_layers", n_gpu_layers_float, 0.0f, 100.0f, sliderWidth, "%.0f");
            int new_n_gpu_layers = static_cast<int>(n_gpu_layers_float);
            if (new_n_gpu_layers != n_gpu_layers) {
                configManager.setGpuLayers(new_n_gpu_layers);
                configManager.saveConfig(); // Auto-save on change
                serverState.setModelParamsChanged(true); // Mark params as changed
            }
        }

        // use_mlock checkbox
        renderCheckbox("Memory Lock", "##use_mlock", configManager.getUseMlock(),
            [&configManager, &serverState](bool value) {
                configManager.setUseMlock(value);
                configManager.saveConfig();
                serverState.setModelParamsChanged(true); // Mark params as changed
            },
            "Locks memory to prevent swapping to disk");

        // use_mmap checkbox
        renderCheckbox("Memory Map", "##use_mmap", configManager.getUseMmap(),
            [&configManager, &serverState](bool value) {
                configManager.setUseMmap(value);
                configManager.saveConfig();
                serverState.setModelParamsChanged(true); // Mark params as changed
            },
            "Use memory mapping for model weights");

        // n_parallel input
        ImGui::Spacing();
        int n_parallel = configManager.getParallelCount();
        IntInputField::render("##n_parallel", n_parallel, sliderWidth);
        if (n_parallel != configManager.getParallelCount()) {
            configManager.setParallelCount(n_parallel);
            configManager.saveConfig();
            serverState.setModelParamsChanged(true); // Mark params as changed
        }

        // cont_batching checkbox
        renderCheckbox("Continuous Batching", "##cont_batching", configManager.getContinuousBatching(),
            [&configManager, &serverState](bool value) {
                configManager.setContinuousBatching(value);
                configManager.saveConfig();
                serverState.setModelParamsChanged(true); // Mark params as changed
            },
            "Enable continuous batching for better performance");

        // warmup checkbox
        renderCheckbox("Warmup", "##warmup", configManager.getWarmup(),
            [&configManager, &serverState](bool value) {
                configManager.setWarmup(value);
                configManager.saveConfig();
                serverState.setModelParamsChanged(true); // Mark params as changed
            },
            "Run model warmup at initialization");
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
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f);

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

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 8.0f);
        Label::render(labelConfig);

        ImGui::Spacing();
    }
};

class ServerSettingsComponent {
public:
    ServerSettingsComponent(float& sidebarWidth)
        : m_sidebarWidth(sidebarWidth)
    {
        m_serverSettingsLabel = createLabel("Server Settings", ICON_CI_SERVER);
    }

    void render() {
        auto& serverState = ServerStateManager::getInstance();
        const float sliderWidth = m_sidebarWidth - 30;

        // Server status indicator
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 5.0f);
        ImGui::TextUnformatted("Status:");
        ImGui::SameLine();

        if (serverState.isServerRunning()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
            ImGui::TextUnformatted("Running");
        }
        else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
            ImGui::TextUnformatted("Stopped");
        }
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        int port = serverState.getServerPort();

        // Make the port input read-only if server is running
        ImGui::BeginDisabled(serverState.isServerRunning());

        IntInputField::render("##server_port", port, sliderWidth);
        if (port != serverState.getServerPort() && port > 0 && port <= 65535) {
            serverState.setServerPort(port);
        }

        ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::Spacing();
    }

private:
    float& m_sidebarWidth;
    LabelConfig m_serverSettingsLabel;

    LabelConfig createLabel(const std::string& text, const std::string& icon) {
        LabelConfig label;
        label.id = "##" + text + "_label";
        label.label = text;
        label.icon = icon;
        label.size = ImVec2(Config::Icon::DEFAULT_FONT_SIZE, 0);
        label.fontType = FontsManager::BOLD;
        return label;
    }
};

class DeploymentSettingsSidebar {
public:
    DeploymentSettingsSidebar() :
        m_width(Config::DeploymentSettingsSidebar::SIDEBAR_WIDTH),
        m_modelLoaderSettingsComponent(m_width),
        m_serverSettingsComponent(m_width) {
    }

    void render() {
        ImGuiIO& io = ImGui::GetIO();
        const float sidebarHeight = io.DisplaySize.y - Config::TITLE_BAR_HEIGHT;

        // Right sidebar window
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - m_width, Config::TITLE_BAR_HEIGHT + 40), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(m_width, sidebarHeight), ImGuiCond_Always);
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(Config::DeploymentSettingsSidebar::MIN_SIDEBAR_WIDTH, sidebarHeight),
            ImVec2(Config::DeploymentSettingsSidebar::MAX_SIDEBAR_WIDTH, sidebarHeight)
        );

        ImGui::Begin("Deployment Settings", nullptr, DeploymentSettingsConstants::SidebarFlags);

        // Update the current sidebar width
        m_width = ImGui::GetWindowSize().x;

        // Render scrollable content area
        ImGui::BeginChild("##deployment_settings_content", ImVec2(0, 0), false, false);

        // Render server settings component first
        m_serverSettingsComponent.render();

        // Render model loader settings component
        m_modelLoaderSettingsComponent.render();

        ImGui::EndChild();

        ImGui::End();
    }

    float getWidth() const { return m_width; }

private:
    float m_width = 0.0F;
    ModelLoaderSettingsComponent m_modelLoaderSettingsComponent;
    ServerSettingsComponent m_serverSettingsComponent;
};