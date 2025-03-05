#pragma once

#include <glad/glad.h>
#include <imgui.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "resource.h"

#include "tab_manager.hpp"
#include "widgets.hpp"

GLuint LoadTextureFromFile(const char* filename)
{
    int width, height, channels;
    unsigned char* data = stbi_load(filename, &width, &height, &channels, 4); // Force RGBA
    if (!data)
    {
        fprintf(stderr, "Failed to load texture: %s\n", filename);
        return 0;
    }

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    // Load texture data into OpenGL
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

    // Set texture parameters for scaling
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Optional: Prevent texture wrapping (clamp to edges)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);

    return texture;
}

void titleBar(void* handler, TabManager& tabManager)
{
#ifdef _WIN32
    // Cast the HWND
    HWND hwnd = static_cast<HWND>(handler);
#else
    // Cast the XID
    XID xid = static_cast<XID>(handler);
#endif

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* draw_list = ImGui::GetForegroundDrawList();

    // Title bar setup
    {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, Config::TITLE_BAR_HEIGHT)); // Adjust height as needed
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0)); // No padding
        ImGui::Begin("TitleBar", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBackground);
    }

    // Render the logo
    {
        static GLuint logoTexture = 0;
        static bool textureLoaded = false;

        if (!textureLoaded)
        {
            logoTexture = LoadTextureFromFile(KOLOSAL_LOGO_PATH);
            textureLoaded = true;
        }

        if (logoTexture)
        {
            const float logoWidth = 20.0F;
            ImGui::SetCursorPos(ImVec2(18, (Config::TITLE_BAR_HEIGHT - logoWidth) / 2)); // Position the logo (adjust as needed)
            ImGui::Image((ImTextureID)(uintptr_t)logoTexture, ImVec2(logoWidth, logoWidth)); // Adjust size as needed
            ImGui::SameLine();
        }
    }

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 16.0f);

    // Render a button for each available tab
    {
        std::vector<ButtonConfig> buttonConfigs;

        for (size_t i = 0; i < tabManager.getTabCount(); ++i)
        {
            ButtonConfig tabButtonConfig;
            tabButtonConfig.id = "##" + (std::string)tabManager.getTab(i)->getTitle();
            tabButtonConfig.icon = tabManager.getTab(i)->getIcon();
            tabButtonConfig.size = ImVec2(24, 0);
            tabButtonConfig.onClick = [i, &tabManager]() { tabManager.switchTab(i); };
            tabButtonConfig.tooltip = tabManager.getTab(i)->getTitle();
            if (tabManager.getCurrentActiveTabIndex() == i)
            {
                tabButtonConfig.state = ButtonState::ACTIVE;
            }
            else
            {
                tabButtonConfig.textColor = ImVec4(0.7f, 0.7f, 0.7f, 0.7f);
            }

            buttonConfigs.push_back(tabButtonConfig);
        }

        // Calculate background dimensions
        float buttonHeight = 16.0f;
        float totalWidth = buttonConfigs.size() * 24.0f + (buttonConfigs.size() - 2) * 10.0f + 6.0f;
        float padding = 6.0f;

        // Calculate background position and size
        ImVec2 pos = ImVec2(ImGui::GetCursorPosX(), ImGui::GetCursorPosY());
        ImVec2 size = ImVec2(totalWidth + padding * 2, buttonHeight + padding * 2);

        // Draw the background
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(
            ImVec2(pos.x - padding, pos.y - padding),
            ImVec2(pos.x + size.x, pos.y + size.y),
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.3f, 0.3f, 0.3f, 0.3f)),
            8.0f
        );

        // Render the buttons
        Button::renderGroup(buttonConfigs, pos.x, pos.y);

        ImGui::SameLine();
    }

    // Title Bar Buttons
    {
        float buttonWidth = 45.0f; // Adjust as needed
        float buttonHeight = Config::TITLE_BAR_HEIGHT; // Same as the title bar height
        float buttonSpacing = 0.0f; // No spacing
        float x = io.DisplaySize.x - buttonWidth * 3;
        float y = 0.0f;

        // Style variables for hover effects
        ImU32 hoverColor = IM_COL32(255, 255, 255, (int)(255 * 0.3f)); // Adjust alpha as needed
        ImU32 closeHoverColor = IM_COL32(232, 17, 35, (int)(255 * 0.5f)); // Red color for close button

        // Minimize button
        {
            ImGui::SetCursorPos(ImVec2(x, y));
            ImGui::PushID("MinimizeButton");
            if (ImGui::InvisibleButton("##MinimizeButton", ImVec2(buttonWidth, buttonHeight)))
            {
                // Handle minimize
                ShowWindow(hwnd, SW_MINIMIZE);
            }

            // Hover effect
            if (ImGui::IsItemHovered())
            {
                ImVec2 p_min = ImGui::GetItemRectMin();
                ImVec2 p_max = ImGui::GetItemRectMax();
                draw_list->AddRectFilled(p_min, p_max, hoverColor);
            }

            // Render minimize icon
            {
                const char* icon = ICON_CI_CHROME_MINIMIZE;
                ImVec2 iconPos = ImGui::GetItemRectMin();
                iconPos.x += ((buttonWidth - ImGui::CalcTextSize(icon).x) / 2.0f) - 4;
                iconPos.y += (buttonHeight - ImGui::CalcTextSize(icon).y) / 2.0f;

                // Select icon font
                ImGui::PushFont(FontsManager::GetInstance().GetIconFont());
                draw_list->AddText(iconPos, IM_COL32(255, 255, 255, 255), icon);
                ImGui::PopFont();
            }

            ImGui::PopID();

        } // Minimize button

        // Maximize/Restore button
        {
            x += buttonWidth + buttonSpacing;

            // Maximize/Restore button
            ImGui::SetCursorPos(ImVec2(x, y));
            ImGui::PushID("MaximizeButton");
            if (ImGui::InvisibleButton("##MaximizeButton", ImVec2(buttonWidth, buttonHeight)))
            {
                // Handle maximize/restore
                if (IsZoomed(hwnd))
                    ShowWindow(hwnd, SW_RESTORE);
                else
                    ShowWindow(hwnd, SW_MAXIMIZE);
            }

            // Hover effect
            if (ImGui::IsItemHovered())
            {
                ImVec2 p_min = ImGui::GetItemRectMin();
                ImVec2 p_max = ImGui::GetItemRectMax();
                draw_list->AddRectFilled(p_min, p_max, hoverColor);
            }

            // Render maximize or restore icon
            {
                const char* icon = IsZoomed(hwnd) ? ICON_CI_CHROME_RESTORE : ICON_CI_CHROME_MAXIMIZE;
                ImVec2 iconPos = ImGui::GetItemRectMin();
                iconPos.x += ((buttonWidth - ImGui::CalcTextSize(icon).x) / 2.0f) - 4;
                iconPos.y += (buttonHeight - ImGui::CalcTextSize(icon).y) / 2.0f;

                // Select icon font
                ImGui::PushFont(FontsManager::GetInstance().GetIconFont());
                draw_list->AddText(iconPos, IM_COL32(255, 255, 255, 255), icon);
                ImGui::PopFont();
            }

            ImGui::PopID();

        } // Maximize/Restore button

        // Close button
        {
            x += buttonWidth + buttonSpacing;

            ImGui::SetCursorPos(ImVec2(x, y));
            ImGui::PushID("CloseButton");
            if (ImGui::InvisibleButton("##CloseButton", ImVec2(buttonWidth, buttonHeight)))
            {
                // Handle close
                PostMessage(hwnd, WM_CLOSE, 0, 0);
            }

            // Hover effect
            if (ImGui::IsItemHovered())
            {
                ImVec2 p_min = ImGui::GetItemRectMin();
                ImVec2 p_max = ImGui::GetItemRectMax();
                draw_list->AddRectFilled(p_min, p_max, closeHoverColor);
            }

            // Render close icon
            {
                const char* icon = ICON_CI_CHROME_CLOSE;
                ImVec2 iconPos = ImGui::GetItemRectMin();
                iconPos.x += ((buttonWidth - ImGui::CalcTextSize(icon).x) / 2.0f) - 4;
                iconPos.y += (buttonHeight - ImGui::CalcTextSize(icon).y) / 2.0f;

                // Select icon font
                ImGui::PushFont(FontsManager::GetInstance().GetIconFont());
                draw_list->AddText(iconPos, IM_COL32(255, 255, 255, 255), icon);
                ImGui::PopFont();
            }

            ImGui::PopID();

        } // Close button

    } // Title Bar Buttons

    ImGui::End();
    ImGui::PopStyleVar(3);
}