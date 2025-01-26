#ifndef MARKDOWN_HPP
#define MARKDOWN_HPP

#include "ui/fonts.hpp"

#include <imgui.h>
#include <imgui_md.h>


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

    bool get_image(image_info& nfo) const override
    {
        nfo.texture_id = ImGui::GetIO().Fonts->TexID; // fallback: font texture
        nfo.size = ImVec2(64, 64);
        nfo.uv0 = ImVec2(0, 0);
        nfo.uv1 = ImVec2(1, 1);
        nfo.col_tint = ImVec4(1, 1, 1, 1);
        nfo.col_border = ImVec4(0, 0, 0, 0);
        return true;
    }

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