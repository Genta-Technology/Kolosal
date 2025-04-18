#pragma once

#include <imgui.h>
#include <d3d10_1.h>
#include <d3d10.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "resource.h"

#include "tab_manager.hpp"
#include "widgets.hpp"
#include "window/dx10_context.hpp"

// DirectX texture loading function
ID3D10ShaderResourceView* LoadTextureFromFile(const char* filename, ID3D10Device* device)
{
    // Load from disk into a raw RGBA buffer
    int width, height, channels;
    unsigned char* data = stbi_load(filename, &width, &height, &channels, 4); // Force RGBA
    if (!data)
    {
        fprintf(stderr, "Failed to load texture: %s\n", filename);
        return nullptr;
    }

    // Create texture
    ID3D10Texture2D* pTexture = nullptr;
    D3D10_TEXTURE2D_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D10_USAGE_DEFAULT;
    desc.BindFlags = D3D10_BIND_SHADER_RESOURCE;

    D3D10_SUBRESOURCE_DATA subResource;
    subResource.pSysMem = data;
    subResource.SysMemPitch = width * 4;
    subResource.SysMemSlicePitch = 0;

    device->CreateTexture2D(&desc, &subResource, &pTexture);

    // Create texture view
    ID3D10ShaderResourceView* textureView = nullptr;
    if (pTexture) {
        D3D10_SHADER_RESOURCE_VIEW_DESC srvDesc;
        ZeroMemory(&srvDesc, sizeof(srvDesc));
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D10_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = desc.MipLevels;
        srvDesc.Texture2D.MostDetailedMip = 0;

        device->CreateShaderResourceView(pTexture, &srvDesc, &textureView);
        pTexture->Release(); // Release texture as we only need the view
    }

    stbi_image_free(data);
    return textureView;
}

// Updated function signature to include DX10Context parameter
void titleBar(void* handler, TabManager& tabManager, DX10Context* dxContext)
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
        static ID3D10ShaderResourceView* logoTexture = nullptr;
        static bool textureLoaded = false;

        if (!textureLoaded && dxContext) // Use the passed dxContext instead of global
        {
            // Get the DirectX device from the context
            ID3D10Device* device = dxContext->getDevice();
            logoTexture = LoadTextureFromFile(KOLOSAL_LOGO_PATH, device);
            textureLoaded = true;
        }

        if (logoTexture)
        {
            const float logoWidth = 20.0F;
            ImGui::SetCursorPos(ImVec2(18, (Config::TITLE_BAR_HEIGHT - logoWidth) / 2)); // Position the logo (adjust as needed)
            ImGui::Image((ImTextureID)logoTexture, ImVec2(logoWidth, logoWidth)); // Adjust size as needed
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
                FontsManager::GetInstance().PushIconFont();
                draw_list->AddText(iconPos, IM_COL32(255, 255, 255, 255), icon);
				FontsManager::GetInstance().PopIconFont();
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
				FontsManager::GetInstance().PushIconFont();
                draw_list->AddText(iconPos, IM_COL32(255, 255, 255, 255), icon);
                FontsManager::GetInstance().PopIconFont();
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
				FontsManager::GetInstance().PushIconFont();
                draw_list->AddText(iconPos, IM_COL32(255, 255, 255, 255), icon);
                FontsManager::GetInstance().PopIconFont();
            }

            ImGui::PopID();

        } // Close button

    } // Title Bar Buttons

    ImGui::End();
    ImGui::PopStyleVar(3);
}