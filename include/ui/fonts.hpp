#pragma once

#include "IconsCodicons.h"
#include "imgui_dx10_helpers.hpp"

#include <iostream>
#include <imgui.h>
#include <misc/freetype/imgui_freetype.h>
#include <array>
#include <algorithm>
#include <memory>
#include <unordered_map>

class FontsManager
{
public:
    static FontsManager& GetInstance()
    {
        static FontsManager instance;
        return instance;
    }

    enum FontType
    {
        REGULAR,
        BOLD,
        ITALIC,
        BOLDITALIC,
        CODE
    };

    enum IconType
    {
        CODICON
    };

    enum SizeLevel
    {
        SM = 0, // Small
        MD,     // Medium
        LG,     // Large
        XL,     // Extra Large
        SIZE_COUNT
    };

    // Use font with dynamic scaling based on size level
    void PushFont(const FontType style, SizeLevel sizeLevel = MD)
    {
        ImFont* font = GetBaseFont(style);
        if (font) {
            // Apply only the SIZE_MULTIPLIER scaling
            // The global scale factor is now handled by FontGlobalScale
            float scaleFactor = SIZE_MULTIPLIERS[sizeLevel];
            ImGui::PushFont(font);
            ImGui::PushFontSize(BASE_FONT_SIZE * scaleFactor);
        }
        else {
            ImGui::PushFont(ImGui::GetIO().FontDefault);
        }
    }

    // Pop font and its scaling
    void PopFont()
    {
        ImGui::PopFontSize();
        ImGui::PopFont();
    }

    // Use icon font with dynamic scaling
    void PushIconFont(const IconType style = CODICON, SizeLevel sizeLevel = MD)
    {
        ImFont* font = GetBaseIconFont(style);
        if (font) {
            // Apply only the SIZE_MULTIPLIER scaling
            // The global scale factor is now handled by FontGlobalScale
            float scaleFactor = SIZE_MULTIPLIERS[sizeLevel];
            ImGui::PushFont(font);
            ImGui::PushFontSize(BASE_FONT_SIZE * scaleFactor);
        }
        else {
            ImGui::PushFont(ImGui::GetIO().FontDefault);
        }
    }

    // Pop icon font and its scaling
    void PopIconFont()
    {
        ImGui::PopFontSize();
        ImGui::PopFont();
    }

    // For compatibility with legacy code that expects to receive an ImFont*
    ImFont* GetMarkdownFont(const FontType style, SizeLevel sizeLevel = MD) const
    {
        // Return the base font - dynamic sizing will be handled by PushFont/PushFontSize
        if (fonts.find(style) != fonts.end() && fonts.at(style) != nullptr) {
            return fonts.at(style);
        }

        // Fallback to regular font if available
        if (fonts.find(REGULAR) != fonts.end() && fonts.at(REGULAR) != nullptr) {
            return fonts.at(REGULAR);
        }

        // Last resort fallback to ImGui default font
        return ImGui::GetIO().Fonts->Fonts[0];
    }

    ImFont* GetIconFont(const IconType style = CODICON, SizeLevel sizeLevel = MD) const
    {
        if (iconFonts.find(style) != iconFonts.end() && iconFonts.at(style) != nullptr) {
            return iconFonts.at(style);
        }

        // Fallback to regular font if icon font not available
        return GetMarkdownFont(REGULAR, sizeLevel);
    }

    // Update fonts when DPI changes
    void UpdateForDpiChange(float newDpiScale)
    {
        if (currentDpiScale == newDpiScale) {
            return; // No change needed
        }

        currentDpiScale = newDpiScale;
        // Update global scale for all fonts
        UpdateGlobalFontScale();
    }

    // Adjust font size using zoom factor (for Ctrl+Scroll)
    void AdjustFontSize(float zoomDelta)
    {
        // Apply zoom delta with limits to prevent fonts from becoming too small or too large
        userZoomFactor = std::clamp(userZoomFactor + zoomDelta, MIN_ZOOM_FACTOR, MAX_ZOOM_FACTOR);

        // Update global scale for all fonts
        UpdateGlobalFontScale();
    }

    // Reset font size to default
    void ResetFontSize()
    {
        userZoomFactor = 1.0f;

        // Update global scale for all fonts
        UpdateGlobalFontScale();
    }

    // Get the current DPI scale factor
    float GetDpiScale() const { return currentDpiScale; }

    // Get the current user zoom factor
    float GetUserZoomFactor() const { return userZoomFactor; }

    // Get the combined scale factor (DPI * zoom)
    float GetTotalScaleFactor() const { return currentDpiScale * userZoomFactor; }

private:
    // Update the global font scale for all ImGui elements
    void UpdateGlobalFontScale()
    {
        float totalScale = GetTotalScaleFactor();
        ImGui::GetIO().FontGlobalScale = totalScale;
    }

    // Get base font without scaling
    ImFont* GetBaseFont(const FontType style) const
    {
        if (fonts.find(style) != fonts.end() && fonts.at(style) != nullptr) {
            return fonts.at(style);
        }

        // Fallback to regular font
        if (style != REGULAR && fonts.find(REGULAR) != fonts.end() && fonts.at(REGULAR) != nullptr) {
            return fonts.at(REGULAR);
        }

        // Last resort fallback to ImGui default font
        return ImGui::GetIO().Fonts->Fonts[0];
    }

    // Get base icon font without scaling
    ImFont* GetBaseIconFont(const IconType style) const
    {
        if (iconFonts.find(style) != iconFonts.end() && iconFonts.at(style) != nullptr) {
            return iconFonts.at(style);
        }

        // Fallback to regular font if icon font not available
        return GetBaseFont(REGULAR);
    }

    // Private constructor that initializes the font system
    FontsManager() :
        currentDpiScale(1.0f),
        userZoomFactor(1.0f)
    {
        // Get ImGui IO
        ImGuiIO& io = ImGui::GetIO();

        // Make sure the backend supports dynamic fonts
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

        // Add default font first (as fallback)
        io.Fonts->AddFontDefault();

        // Detect initial DPI scale
#ifdef _WIN32
        // Check if ImGui_ImplWin32_GetDpiScaleForHwnd is available
        HWND hwnd = GetActiveWindow();
        if (hwnd != NULL) {
            // Try to use the DPI aware value if available (Win10+)
            currentDpiScale = GetDpiScaleForWindow(hwnd);
        }
#endif

        // Load fonts
        LoadFonts(io);

        // Initialize global font scale
        UpdateGlobalFontScale();
    }

    // Delete copy constructor and assignment operator
    FontsManager(const FontsManager&) = delete;
    FontsManager& operator=(const FontsManager&) = delete;

    // Get DPI scale for a window (Windows-specific implementation)
#ifdef _WIN32
    float GetDpiScaleForWindow(HWND hwnd)
    {
        // Try to use GetDpiForWindow first (Windows 10+)
        HMODULE user32 = GetModuleHandleA("user32.dll");
        if (user32)
        {
            typedef UINT(WINAPI* GetDpiForWindowFunc)(HWND);
            GetDpiForWindowFunc getDpiForWindow =
                (GetDpiForWindowFunc)GetProcAddress(user32, "GetDpiForWindow");

            if (getDpiForWindow)
            {
                UINT dpi = getDpiForWindow(hwnd);
                if (dpi > 0) {
                    return dpi / 96.0f; // 96 is the default DPI
                }
            }
        }

        // Fallback to older method
        HDC hdc = GetDC(hwnd);
        if (hdc)
        {
            int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
            ReleaseDC(hwnd, hdc);
            return dpiX / 96.0f;
        }

        return 1.0f; // Default scale if detection fails
    }
#else
    float GetDpiScaleForWindow(void* hwnd) { return 1.0f; }
#endif

    // Font data storage - with dynamic fonts, we just store one instance of each font
    mutable std::unordered_map<int, ImFont*> fonts;        // Font style -> font object
    mutable std::unordered_map<int, ImFont*> iconFonts;    // Icon style -> font object

    // Scale factors
    float currentDpiScale;   // DPI-based scaling (system controlled)
    float userZoomFactor;    // User-controlled zoom level via Ctrl+Scroll

    // Zoom factor limits
    static constexpr float MIN_ZOOM_FACTOR = 0.5f;
    static constexpr float MAX_ZOOM_FACTOR = 2.5f;

    // Base font size (the reference size that will be scaled)
    static constexpr float BASE_FONT_SIZE = 16.0f;

    // Size multipliers for different size levels (same as before)
    static constexpr std::array<float, SizeLevel::SIZE_COUNT> SIZE_MULTIPLIERS = {
        0.875f, // SM (14px at standard DPI and BASE_FONT_SIZE=16)
        1.0f,   // MD (16px at standard DPI)
        1.5f,   // LG (24px at standard DPI)
        2.25f   // XL (36px at standard DPI)
    };

    void LoadFonts(ImGuiIO& io)
    {
        // Font paths
        const char* mdFontPaths[] = {
            IMGUI_FONT_PATH_INTER_REGULAR,
            IMGUI_FONT_PATH_INTER_BOLD,
            IMGUI_FONT_PATH_INTER_ITALIC,
            IMGUI_FONT_PATH_INTER_BOLDITALIC,
            IMGUI_FONT_PATH_FIRACODE_REGULAR
        };

        const char* iconFontPath = IMGUI_FONT_PATH_CODICON;
        const char* emojiFontPath = IMGUI_FONT_PATH_NOTO_EMOJI;

        // Load each font type once
        for (int fontType = REGULAR; fontType <= CODE; ++fontType)
        {
            ImFontConfig fontConfig;
            fontConfig.OversampleH = 2;
            fontConfig.OversampleV = 2;

            // Load the font - use the base size which will be scaled dynamically
            fonts[fontType] = io.Fonts->AddFontFromFileTTF(
                mdFontPaths[fontType],
                BASE_FONT_SIZE,
                &fontConfig
            );

            // If loading failed, log an error
            if (!fonts[fontType]) {
                std::cerr << "Failed to load font: " << mdFontPaths[fontType] << std::endl;
                continue;
            }

            if (!(fontType == REGULAR))
                continue;

            // Now merge Noto Emoji font with this font
            ImFontConfig emojiConfig;
            emojiConfig.MergeMode = true;
            emojiConfig.OversampleH = 1;
            emojiConfig.OversampleV = 1;
            emojiConfig.FontBuilderFlags |= ImGuiFreeTypeBuilderFlags_LoadColor;

            static ImWchar ranges[] = { 0x1F000, 0x1FFFF, 0 };

            // Merge the emoji font with the current font
            io.Fonts->AddFontFromFileTTF(
                emojiFontPath,
                BASE_FONT_SIZE,
                &emojiConfig,
                ranges
            );
        }

        // Load icon font - just once at the base size
        ImFontConfig icons_config;
        icons_config.PixelSnapH = true;
        icons_config.GlyphMinAdvanceX = BASE_FONT_SIZE; // Maintain consistent width

        // No need to specify glyph ranges with new dynamic font system, but 
        // we still need to do it for icon fonts as they use a specific range
        static const ImWchar icons_ranges[] = { ICON_MIN_CI, ICON_MAX_CI, 0 };

        iconFonts[CODICON] = io.Fonts->AddFontFromFileTTF(
            iconFontPath,
            BASE_FONT_SIZE,
            &icons_config
            //icons_ranges
        );

        if (!iconFonts[CODICON]) {
            std::cerr << "Failed to load icon font: " << iconFontPath << std::endl;
        }

        // Set the default font to regular
        if (fonts[REGULAR] != nullptr) {
            io.FontDefault = fonts[REGULAR];
        }
    }
};