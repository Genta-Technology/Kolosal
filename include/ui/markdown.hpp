#ifndef MARKDOWN_HPP
#define MARKDOWN_HPP

#include "ui/fonts.hpp"

#include <imgui.h>
#include <imgui_md.h>

// If you need to open URLs in a platform-dependent way, include or forward declare here.
// For example, if you're using SDL:
// #include <SDL.h> 

// This class overrides the virtual hooks from imgui_md and uses your FontsManager
// to pick the right font based on markdown states such as heading level, bold, italic, etc.
class MarkdownRenderer : public imgui_md
{
public:
    MarkdownRenderer() = default;
    ~MarkdownRenderer() override = default;

protected:
    // Override how fonts are selected
    ImFont* get_font() const override
    {
        // A reference to your FontsManager singleton
        auto& fm = FontsManager::GetInstance();

        // If we are rendering a table header, you might want it bold:
        if (m_is_table_header)
        {
            // Return BOLD in Medium size, for example
            return fm.GetMarkdownFont(FontsManager::BOLD, FontsManager::MD);
        }

        // If we are inside a code block or inline code:
        if (m_is_code)
        {
            // Return code font in Medium size
            return fm.GetMarkdownFont(FontsManager::CODE, FontsManager::MD);
        }

        // If we have a heading level, we can choose bigger/smaller fonts accordingly:
        // For example, H1 -> XL, H2 -> LG, H3 -> MD, H4 -> SM
        if (m_hlevel >= 1 && m_hlevel <= 4)
        {
            switch (m_hlevel)
            {
            case 1:
                // e.g., BOLD in XL
                return fm.GetMarkdownFont(FontsManager::BOLD, FontsManager::XL);
            case 2:
                // e.g., BOLD in LG
                return fm.GetMarkdownFont(FontsManager::BOLD, FontsManager::LG);
            case 3:
                // e.g., BOLD in MD
                return fm.GetMarkdownFont(FontsManager::BOLD, FontsManager::MD);
            case 4:
            default:
                // e.g., BOLD in SM
                return fm.GetMarkdownFont(FontsManager::BOLD, FontsManager::SM);
            }
        }

        // If not a heading and not code, check if we have strong/em:
        // strong + em can map to BOLDITALIC if you prefer.
        if (m_is_strong && m_is_em)
        {
            return fm.GetMarkdownFont(FontsManager::BOLDITALIC, FontsManager::MD);
        }
        if (m_is_strong)
        {
            return fm.GetMarkdownFont(FontsManager::BOLD, FontsManager::MD);
        }
        if (m_is_em)
        {
            return fm.GetMarkdownFont(FontsManager::ITALIC, FontsManager::MD);
        }

        // Otherwise, just return regular MD font
        return fm.GetMarkdownFont(FontsManager::REGULAR, FontsManager::MD);
    }

    // Example for image rendering:
    bool get_image(image_info& nfo) const override
    {
        // Use `m_href` to decide which image to load. Here is a trivial placeholder:
        // If you want multiple images, parse m_href (e.g., "icon://something") and
        // pick a texture accordingly.
        nfo.texture_id = ImGui::GetIO().Fonts->TexID; // fallback: font texture
        nfo.size = ImVec2(64, 64);
        nfo.uv0 = ImVec2(0, 0);
        nfo.uv1 = ImVec2(1, 1);
        nfo.col_tint = ImVec4(1, 1, 1, 1);
        nfo.col_border = ImVec4(0, 0, 0, 0);
        return true;
    }

    // Example link opening (platform dependent):
    void open_url() const override
    {
        // If not an image, open the link. E.g., with SDL:
        // if (!m_is_image) SDL_OpenURL(m_href.c_str());
        // else { /* image clicked logic */ }
    }

    // (Optional) If you have special handling for <div class="...">, override here:
    void html_div(const std::string& dclass, bool enter) override
    {
        // Example toggling text color if <div class="red"> ...
        if (dclass == "red")
        {
            if (enter)
            {
                // For example, push color
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255));
            }
            else
            {
                // pop color
                ImGui::PopStyleColor();
            }
        }
    }
};

// -----------------------------------------------------------------------------------
// The single function you can call from anywhere in your code to render markdown text.
// For convenience, we keep a static instance of `MarkdownRenderer` around.
inline void RenderMarkdown(const char* text)
{
    static MarkdownRenderer s_renderer;
    if (!text || !*text)
        return;

    // This calls the underlying md4c-based parser (from imgui_md) and renders to ImGui
    s_renderer.print(text, text + std::strlen(text));
}

inline float ApproxMarkdownHeight(const char* text, float width)
{
	return MarkdownRenderer::ComputeMarkdownHeight(text, width);
}

#endif // MARKDOWN_HPP