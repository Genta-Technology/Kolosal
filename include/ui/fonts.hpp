#pragma once

#include "IconsCodicons.h"
#include "imgui_dx10_helpers.hpp"

#include <iostream>
#include <imgui.h>
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

    ImFont* GetMarkdownFont(const FontType style, SizeLevel sizeLevel = MD) const
    {
        // Get the scaled font or fallback to regular if not available
        if (scaledFonts.find(style) != scaledFonts.end() &&
            scaledFonts.at(style)[sizeLevel] != nullptr) {
            return scaledFonts.at(style)[sizeLevel];
        }

        // Fallback to regular MD font if available
        if (scaledFonts.find(REGULAR) != scaledFonts.end() &&
            scaledFonts.at(REGULAR)[MD] != nullptr) {
            return scaledFonts.at(REGULAR)[MD];
        }

        // Last resort fallback to ImGui default font
        return ImGui::GetIO().Fonts->Fonts[0];
    }

    ImFont* GetIconFont(const IconType style = CODICON, SizeLevel sizeLevel = MD) const
    {
        if (scaledIconFonts.find(style) != scaledIconFonts.end() &&
            scaledIconFonts.at(style)[sizeLevel] != nullptr) {
            return scaledIconFonts.at(style)[sizeLevel];
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
        pendingRebuild = true;
    }

    // Adjust font size using zoom factor (for Ctrl+Scroll)
    void AdjustFontSize(float zoomDelta)
    {
        // Apply zoom delta with limits to prevent fonts from becoming too small or too large
        float newZoom = std::clamp(userZoomFactor + zoomDelta, MIN_ZOOM_FACTOR, MAX_ZOOM_FACTOR);

        // Only mark for rebuild if the change is significant
        if (std::abs(newZoom - userZoomFactor) > 0.05f) {
            userZoomFactor = newZoom;
            pendingRebuild = true;
        }
    }

    // Reset font size to default
    void ResetFontSize()
    {
        if (userZoomFactor != 1.0f) {
            userZoomFactor = 1.0f;
            pendingRebuild = true;
        }
    }

    // Process any pending font rebuilds - call this at a safe point in your render loop
    void ProcessPendingFontRebuild()
    {
        if (pendingRebuild) {
            RebuildFonts();
            pendingRebuild = false;
        }
    }

    // Get the current DPI scale factor
    float GetDpiScale() const { return currentDpiScale; }

    // Get the current user zoom factor
    float GetUserZoomFactor() const { return userZoomFactor; }

    // Get the combined scale factor (DPI * zoom)
    float GetTotalScaleFactor() const { return currentDpiScale * userZoomFactor; }

private:
    // Private constructor that initializes the font system
    FontsManager() :
        currentDpiScale(1.0f),
        userZoomFactor(1.0f),
        pendingRebuild(false)
    {
        // Get ImGui IO
        ImGuiIO& io = ImGui::GetIO();

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

        // Load fonts with the current DPI scale
        LoadFonts(io);
    }

    // Rebuild fonts with current scale factors
    void RebuildFonts()
    {
        ImGuiIO& io = ImGui::GetIO();

        // Tell ImGui_ImplDX10 that we're about to rebuild fonts
        // We need ImGui_ImplDX10_InvalidateDeviceObjects to be called
        // before clearing the font atlas to release DirectX resources
        extern void ImGui_ImplDX10_InvalidateDeviceObjects();
        ImGui_ImplDX10_InvalidateDeviceObjects();

        // Now it's safe to clear the font atlas
        io.Fonts->Clear();

        // Set default font first so it's at index 0
        io.Fonts->AddFontDefault();

        // Reload all fonts with the current scale settings
        LoadFonts(io);

        // Build the font atlas - this creates the font texture data
        io.Fonts->Build();

        // Now tell ImGui_ImplDX10 to recreate DirectX resources
        extern bool ImGui_ImplDX10_CreateDeviceObjects();
        ImGui_ImplDX10_CreateDeviceObjects();
    }

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

    // Delete copy constructor and assignment operator
    FontsManager(const FontsManager&) = delete;
    FontsManager& operator=(const FontsManager&) = delete;

    // Font data storage
    using FontSizeArray = std::array<ImFont*, SizeLevel::SIZE_COUNT>;
    mutable std::unordered_map<int, FontSizeArray> scaledFonts;     // Font style -> array of scaled fonts
    mutable std::unordered_map<int, FontSizeArray> scaledIconFonts; // Icon style -> array of scaled fonts

    // Scale factors
    float currentDpiScale;   // DPI-based scaling (system controlled)
    float userZoomFactor;    // User-controlled zoom level via Ctrl+Scroll
    bool pendingRebuild;     // Flag to indicate fonts need rebuilding

    // Zoom factor limits
    static constexpr float MIN_ZOOM_FACTOR = 0.5f;
    static constexpr float MAX_ZOOM_FACTOR = 2.5f;

    // Base font sizes (these will be multiplied by DPI scale and size factors)
    static constexpr float BASE_FONT_SIZE = 16.0f;

    // Size multipliers for different size levels
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

        // Clear existing font mappings
        scaledFonts.clear();
        scaledIconFonts.clear();

        // Calculate total scale factor (DPI * user zoom)
        float totalScale = currentDpiScale * userZoomFactor;

        // For each font type (regular, bold, etc.), load master font and create references
        for (int fontType = REGULAR; fontType <= CODE; ++fontType)
        {
            // Create a storage array for this font type
            FontSizeArray& fontArray = scaledFonts[fontType];

            // Load fonts at each size level with appropriate parameters
            for (int8_t sizeLevel = SizeLevel::SM; sizeLevel < SizeLevel::SIZE_COUNT; ++sizeLevel)
            {
                // Calculate final font size based on base size, total scale, and size multiplier
                float fontSize = BASE_FONT_SIZE * totalScale * SIZE_MULTIPLIERS[sizeLevel];

                // Configure improved font quality based on size and total scale
                ImFontConfig fontConfig;
                fontConfig.OversampleH = fontSize < 20.0f ? 2 : 1;
                fontConfig.OversampleV = fontSize < 20.0f ? 2 : 1;
                fontConfig.PixelSnapH = fontSize < 20.0f ? false : true;

                // For high DPI or large zoom, increase oversampling
                if (totalScale > 1.5f) {
                    fontConfig.OversampleH = 3;
                    fontConfig.OversampleV = 3;
                }

                // Adjust rasterizing based on scale
                if (totalScale > 2.0f) {
                    fontConfig.RasterizerMultiply = 0.9f;
                }
                else if (totalScale > 1.0f) {
                    fontConfig.RasterizerMultiply = 1.1f;
                }

                // Load the font with the calculated size and quality settings
                fontArray[sizeLevel] = io.Fonts->AddFontFromFileTTF(
                    mdFontPaths[fontType],
                    fontSize,
                    &fontConfig
                );

                // If loading failed, log an error
                if (!fontArray[sizeLevel]) {
                    std::cerr << "Failed to load font: " << mdFontPaths[fontType]
                        << " at size " << fontSize << std::endl;
                }
            }
        }

        // Load icon fonts with similar approach
        FontSizeArray& iconArray = scaledIconFonts[CODICON];
        for (int8_t sizeLevel = SizeLevel::SM; sizeLevel < SizeLevel::SIZE_COUNT; ++sizeLevel)
        {
            float fontSize = BASE_FONT_SIZE * totalScale * SIZE_MULTIPLIERS[sizeLevel];

            ImFontConfig icons_config;
            icons_config.MergeMode = false;
            icons_config.PixelSnapH = true;
            icons_config.GlyphMinAdvanceX = fontSize;

            if (totalScale > 1.5f) {
                icons_config.OversampleH = 3;
                icons_config.OversampleV = 3;
            }
            else {
                icons_config.OversampleH = 2;
                icons_config.OversampleV = 2;
            }

            // Load the icon font
            static const ImWchar icons_ranges[] = { ICON_MIN_CI, ICON_MAX_CI, 0 };
            iconArray[sizeLevel] = io.Fonts->AddFontFromFileTTF(
                iconFontPath,
                fontSize,
                &icons_config,
                icons_ranges
            );

            if (!iconArray[sizeLevel]) {
                std::cerr << "Failed to load icon font: " << iconFontPath
                    << " at size " << fontSize << std::endl;
            }
        }

        // Set the default font to regular medium size
        if (scaledFonts[REGULAR][SizeLevel::MD] != nullptr) {
            io.FontDefault = scaledFonts[REGULAR][SizeLevel::MD];
        }
    }
};