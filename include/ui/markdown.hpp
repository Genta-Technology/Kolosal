#ifndef MARKDOWN_HPP
#define MARKDOWN_HPP

#include <imgui.h>
#include <imgui_md.h>

#include "ui/widgets.hpp"
#include "config.hpp"

class MarkdownRenderer : public imgui_md
{
public:
    MarkdownRenderer() = default;
    ~MarkdownRenderer() override = default;

    int chatCounter = 0;

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
                return fm.GetMarkdownFont(FontsManager::BOLD, FontsManager::LG);
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

    void BLOCK_CODE(const MD_BLOCK_CODE_DETAIL* d, bool e) override 
    {
        if (e) {
            // Push new code block with stable ID
			CodeBlock block;
			block.lang = std::string(d->lang.text, d->lang.size);
			block.content = "";
            block.render_id = chatCounter + (m_code_id++);
            m_code_stack.push_back(block);

            ImGui::PushFont(FontsManager::GetInstance().GetMarkdownFont(
                FontsManager::CODE, FontsManager::MD));

			m_is_code_block = true;
        }
        else {
            if (!m_code_stack.empty()) {
                CodeBlock& block = m_code_stack.back();

				// remove last newline
				if (!block.content.empty() && block.content.back() == '\n')
					block.content.pop_back();

                // Calculate height
                const float line_height  = ImGui::GetTextLineHeight();
                const int   line_count   = std::count(block.content.begin(), block.content.end(), '\n') + 2;
                const float total_height = line_height * line_count;

                // Setup styling
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 24);
                ImGui::PushStyleColor(ImGuiCol_ChildBg, Config::InputField::INPUT_FIELD_BG_COLOR);
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);

#ifdef DEBUG
                ImGui::TextUnformatted(std::to_string(block.render_id).c_str());
#endif

                // Use stable ID for child window
                ImGui::BeginChild(ImGui::GetID(("##code_content_" + std::to_string(block.render_id)).c_str()),
                    ImVec2(0, total_height + 36 + (!block.lang.empty() ? 4 : 0)), false,
                    ImGuiWindowFlags_NoScrollbar);

                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);

                // if lang is not empty, add a label with the language
                ImGui::Indent(4);
                LabelConfig label_cfg;
                label_cfg.label = block.lang.empty() ? "idk fr" : block.lang;
                label_cfg.fontType = FontsManager::ITALIC;
                label_cfg.fontSize = FontsManager::SM;
                // set color to a light gray
                label_cfg.color = ImVec4(0.7F, 0.7F, 0.7F, 1.0F);
                Label::render(label_cfg);

                ImGui::Unindent(4);

                ImGui::SameLine();

                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 4);

                // Copy button
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 56);
                ButtonConfig copy_cfg;
                copy_cfg.id = "##copy_" + std::to_string(block.render_id); // Stable ID
                copy_cfg.label = "copy";
                copy_cfg.size = ImVec2(48, 0);
				copy_cfg.fontSize = FontsManager::SM;
                copy_cfg.onClick = [content = block.content]() { // Capture by value
                    ImGui::SetClipboardText(content.c_str());
                    };
                Button::render(copy_cfg);

                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);

                // Input field
                bool focusInput = false;
                InputFieldConfig input_cfg(
                    ("##code_input_" + std::to_string(block.render_id)).c_str(),
                    ImVec2(-FLT_MIN, total_height + 4),
                    block.content,
                    focusInput
                );
                input_cfg.frameRounding = 4.0f;
                input_cfg.flags = ImGuiInputTextFlags_ReadOnly;
                InputField::renderMultiline(input_cfg);

                ImGui::EndChild();
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();

                m_code_stack.pop_back();
            }
            ImGui::PopFont();
            
			m_is_code_block = false;
        }
    }

    void SPAN_CODE(bool e) override
	{
		if (e) {
			ImGui::PushFont(FontsManager::GetInstance().GetMarkdownFont(
				FontsManager::CODE, FontsManager::MD));
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(180, 230, 180, 255)); // Greenish text
		}
		else {
            ImGui::PopStyleColor();
			ImGui::PopFont();
		}
	}
};

std::unordered_map<int, MarkdownRenderer> g_markdownRenderers;

inline void RenderMarkdown(const char* text, int id)
{
    if (!text || !*text)
        return;

	// if id in g_markdownRenderers, use it, otherwise create a new one
	if (g_markdownRenderers.find(id) == g_markdownRenderers.end())
	{
		MarkdownRenderer renderer;
		renderer.chatCounter = id * 100;
		g_markdownRenderers[id] = renderer;
	}

	MarkdownRenderer& renderer = g_markdownRenderers[id];
	renderer.print(text, text + std::strlen(text));
}

inline float ApproxMarkdownHeight(const char* text, float width)
{
	return MarkdownRenderer::ComputeMarkdownHeight(text, width);
}

#endif // MARKDOWN_HPP