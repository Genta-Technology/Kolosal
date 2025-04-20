#ifndef MARKDOWN_SELECTABLE_HPP
#define MARKDOWN_SELECTABLE_HPP

#include <imgui.h>
#include <imgui_md.h>
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#elif defined(__APPLE__)
#include <cstdlib>
#else // Linux and other Unix-like systems
#include <cstdlib>
#endif

#include "ui/widgets.hpp"
#include "config.hpp"
#include "textselect.hpp"

// Keep track of a styled text segment
struct StyledTextSegment {
    std::string text;
    ImFont* font;
    bool isBold;
    float startX = 0.0f;
    float endX = 0.0f;
};

// Keep track of a line with its styled segments
struct StyledTextLine {
    std::vector<StyledTextSegment> segments;
    float totalWidth = 0.0f;
    float heightMultiplier = 1.0f;
};

class MarkdownRenderer : public imgui_md
{
public:
    MarkdownRenderer() = default;
    ~MarkdownRenderer() override = default;

    int chatCounter = 0;

    // For text selection
    std::vector<std::string> textLines;
    std::unique_ptr<TextSelect> textSelect;
    float verticalOffset = 20.0F;

    // Add storage for font information
    std::vector<StyledTextLine> styledLines;

    // Expose currentLine for final cleanup
    std::string currentLine;

    // Current line styled segments
    StyledTextLine currentStyledLine;
    StyledTextSegment currentSegment;

    // Tracking variables
    int linePartCount = 0;
    float lastCursorX = 0;
    float lastCursorY = 0;
    bool sameLineSequence = false;
    float currentLineWidth = 0;
    bool textWrapped = false;
    int listNestingLevel = 0;  // Track list nesting level for indentation
    bool inListItem = false;   // Track if we're inside a list item
    bool lastFontWasBold = false; // Track if the last used font was bold
    ImFont* currentFont = nullptr; // Track the current font

    // Initialize text selection
    void initTextSelect() {
        if (!textSelect) {
            // Create TextSelect with font information accessor
            textSelect = std::make_unique<TextSelect>(
                [this](std::size_t idx) -> std::string_view {
                    return idx < textLines.size() ? textLines[idx] : std::string_view();
                },
                [this]() -> std::size_t {
                    return textLines.size();
                },
                [this](std::size_t idx) -> TextLine {
                    if (idx >= styledLines.size()) {
                        return TextLine{}; // Return empty line if out of bounds
                    }

                    // Convert StyledTextLine to TextLine
                    TextLine line;
                    line.totalWidth = styledLines[idx].totalWidth;
                    line.heightMultiplier = styledLines[idx].heightMultiplier; // Copy the height multiplier

                    for (const auto& styledSegment : styledLines[idx].segments) {
                        TextSegment segment;
                        segment.text = styledSegment.text;
                        segment.font = styledSegment.font;
                        segment.isBold = styledSegment.isBold;
                        segment.startX = styledSegment.startX;
                        segment.endX = styledSegment.endX;
                        line.segments.push_back(segment);
                    }

                    return line;
                }
            );

            // Set the vertical offset
            textSelect->setVerticalOffset(verticalOffset);
        }
    }

    // Clear the text lines before rendering new content
    void clearTextLines() {
        textLines.clear();
        styledLines.clear();
        currentLine.clear();
        currentStyledLine = StyledTextLine{};
        currentSegment = StyledTextSegment{};
        linePartCount = 0;
        lastCursorX = 0;
        lastCursorY = 0;
        sameLineSequence = false;
        currentLineWidth = 0;
        textWrapped = false;
        listNestingLevel = 0;
        inListItem = false;
        lastFontWasBold = false;
        currentFont = nullptr;
    }

    // Set the vertical offset for text selection
    void setVerticalOffset(float offset) {
        verticalOffset = offset;
        if (textSelect) {
            textSelect->setVerticalOffset(offset);
        }
    }

    // Get the proper indentation string based on list nesting level
    std::string getListIndent() const {
        return std::string(listNestingLevel * 8, ' ');
    }

    // Apply list indentation to a line
    void applyListIndent(std::string& line) {
        if (listNestingLevel > 0 && !line.empty()) {
            line = getListIndent() + line;
        }
    }

    // Finish the current segment and add it to the current line
    void finishCurrentSegment() {
        if (!currentSegment.text.empty()) {
            // Calculate segment width using the actual font
            ImFont* oldFont = ImGui::GetFont();
            if (currentSegment.font) {
                ImGui::PushFont(currentSegment.font);
            }

            float segmentWidth = ImGui::CalcTextSize(currentSegment.text.c_str()).x;

            if (currentSegment.font) {
                ImGui::PopFont();
            }

            // Set the segment positions
            currentSegment.startX = currentStyledLine.totalWidth;
            currentSegment.endX = currentStyledLine.totalWidth + segmentWidth;

            // Update total line width
            currentStyledLine.totalWidth += segmentWidth;

            // Add segment to the current line
            currentStyledLine.segments.push_back(currentSegment);

            // Reset the segment
            currentSegment = StyledTextSegment{};
            currentSegment.font = currentFont;
            currentSegment.isBold = lastFontWasBold;
        }
    }

    // Finish the current line and add it to styled lines
    void finishCurrentLine() {
        if (!currentLine.empty()) {
            // Finish any pending segment
            finishCurrentSegment();

            // Add the line to text lines and styled lines
            if (inListItem) {
                applyListIndent(currentLine);

                // Also apply indentation to styled line
                float indentWidth = ImGui::CalcTextSize(getListIndent().c_str()).x;
                for (auto& segment : currentStyledLine.segments) {
                    segment.startX += indentWidth;
                    segment.endX += indentWidth;
                }
                currentStyledLine.totalWidth += indentWidth;
            }

            textLines.push_back(currentLine);
            styledLines.push_back(currentStyledLine);

            // Reset current line
            currentLine.clear();
            currentStyledLine = StyledTextLine{};
            currentSegment = StyledTextSegment{};
            currentSegment.font = currentFont;
            currentSegment.isBold = lastFontWasBold;

            linePartCount = 0;
            currentLineWidth = 0;
        }
    }

protected:

    // Override how fonts are selected
    void push_font() const override
    {
        // A reference to your FontsManager singleton
        auto& fm = FontsManager::GetInstance();

        // Update bold font tracking when font changes
        // Note: We need to cast away const here because this is a const method
        // but we need to update the tracking state
        MarkdownRenderer* self = const_cast<MarkdownRenderer*>(this);

        // Store the previous font
        ImFont* prevFont = ImGui::GetFont();

        // If we are rendering a table header, you might want it bold:
        if (m_is_table_header)
        {
            // Return BOLD in Medium size, for example
            fm.PushFont(FontsManager::BOLD, FontsManager::MD);
            self->lastFontWasBold = true;
            self->currentFont = ImGui::GetFont();

            // Finish the current segment before changing font
            self->finishCurrentSegment();
            return;
        }

        // If we are inside a code block or inline code:
        if (m_is_code)
        {
            // Return code font in Medium size
            fm.PushFont(FontsManager::CODE, FontsManager::MD);
            self->lastFontWasBold = false; // Code font is not bold
            self->currentFont = ImGui::GetFont();

            // Finish the current segment before changing font
            self->finishCurrentSegment();
            return;
        }

        if (m_hlevel >= 1 && m_hlevel <= 4)
        {
            switch (m_hlevel)
            {
            case 1:
                // e.g., BOLD in XL
                fm.PushFont(FontsManager::BOLD, FontsManager::LG);
                self->lastFontWasBold = true;
                break;
            case 2:
                // e.g., BOLD in LG
                fm.PushFont(FontsManager::BOLD, FontsManager::LG);
                self->lastFontWasBold = true;
                break;
            case 3:
                // e.g., BOLD in MD
                fm.PushFont(FontsManager::BOLD, FontsManager::MD);
                self->lastFontWasBold = true;
                break;
            case 4:
            default:
                // e.g., BOLD in SM
                fm.PushFont(FontsManager::BOLD, FontsManager::SM);
                self->lastFontWasBold = true;
                break;
            }

            self->currentFont = ImGui::GetFont();

            // Finish the current segment before changing font
            self->finishCurrentSegment();
            return;
        }

        if (m_is_strong && m_is_em)
        {
            fm.PushFont(FontsManager::BOLDITALIC, FontsManager::MD);
            self->lastFontWasBold = true;
            self->currentFont = ImGui::GetFont();

            // Finish the current segment before changing font
            self->finishCurrentSegment();
            return;
        }
        if (m_is_strong)
        {
            fm.PushFont(FontsManager::BOLD, FontsManager::MD);
            self->lastFontWasBold = true;
            self->currentFont = ImGui::GetFont();

            // Finish the current segment before changing font
            self->finishCurrentSegment();
            return;
        }
        if (m_is_em)
        {
            fm.PushFont(FontsManager::ITALIC, FontsManager::MD);
            self->lastFontWasBold = false; // Italic is not bold
            self->currentFont = ImGui::GetFont();

            // Finish the current segment before changing font
            self->finishCurrentSegment();
            return;
        }

        // Otherwise, just return regular MD font
        fm.PushFont(FontsManager::REGULAR, FontsManager::MD);
        self->lastFontWasBold = false;
        self->currentFont = ImGui::GetFont();

        // Finish the current segment before changing font
        self->finishCurrentSegment();
        return;
    }

    // Override set_font to handle font changes
    void set_font(bool e) override
    {
        if (e)
        {
            // Push the font based on the current state
            push_font();
        }
        else
        {
            // Finish the current segment before popping the font
            finishCurrentSegment();

            // Pop the font when leaving the scope
            FontsManager::GetInstance().PopFont();

            // Reset the bold flag when popping fonts
            // This isn't perfect since we don't know what the previous font was,
            // but for text selection purposes it's better than nothing
            lastFontWasBold = false;
            currentFont = ImGui::GetFont();
        }
    }

    // Override render_text to support text selection with proper font width handling
    void render_text(const char* str, const char* str_end) override
    {
        const float scale = ImGui::GetIO().FontGlobalScale;
        const ImGuiStyle& s = ImGui::GetStyle();
        bool is_lf = false;

        // Get the current cursor position
        float cursorX = ImGui::GetCursorPosX();
        float cursorY = ImGui::GetCursorPosY();

        // Available width for text wrapping
        float availWidth = ImGui::GetContentRegionAvail().x;

        // Check if this is a new line (Y has changed significantly)
        bool isNewLine = (cursorY > lastCursorY + 2.0f) && !sameLineSequence;

        // Check if we started a new line due to wrapping from the previous segment
        bool isWrappedNewLine = (cursorX < lastCursorX - 10.0f) && (cursorY > lastCursorY + 2.0f);

        // If this is a true new line (not just a formatting change), finish the previous line
        if ((isNewLine || isWrappedNewLine) && !currentLine.empty()) {
            finishCurrentLine();
        }

        // Get the exact text size before rendering to account for font differences
        ImVec2 fullTextSize = ImGui::CalcTextSize(str, str_end);
        bool fontIsBold = lastFontWasBold; // Capture current font state
        ImFont* fontBeforeRender = ImGui::GetFont(); // Capture current font

        // Update the current segment with the current font
        if (currentSegment.font == nullptr) {
            currentSegment.font = fontBeforeRender;
            currentSegment.isBold = fontIsBold;
        }

        while (!m_is_image && str < str_end) {
            const char* te = str_end;

            // Check for text wrapping
            bool willWrap = false;
            if (!m_is_table_header) {
                float wl = availWidth;

                if (m_is_table_body) {
                    wl = (m_table_next_column < m_table_col_pos.size() ?
                        m_table_col_pos[m_table_next_column] : m_table_last_pos.x);
                    wl -= ImGui::GetCursorPosX();
                }

                te = ImGui::GetFont()->CalcWordWrapPositionA(
                    scale, str, str_end, wl);

                if (te == str) ++te;

                // Check if this text segment will be wrapped
                willWrap = (te < str_end && *te != '\n');
            }

            // Measure text segment width - this accounts for the actual font being used
            ImVec2 textSize = ImGui::CalcTextSize(str, te);

            // Remember position before rendering
            float preRenderX = ImGui::GetCursorPosX();

            // Text rendering using TextUnformatted
            ImGui::TextUnformatted(str, te);

            // Get new cursor position after rendering text
            float newCursorX = ImGui::GetCursorPosX();
            float newCursorY = ImGui::GetCursorPosY();

            // Calculate the actual width used by this text segment
            float actualTextWidth = newCursorX - preRenderX;

            // Update line width tracking with accurate measure
            currentLineWidth += actualTextWidth;

            // Check if ImGui wrapped the text during rendering
            // This happens when Y changed after rendering, but we didn't explicitly create a new line
            bool wasWrapped = (newCursorY > cursorY + 2.0f) && (newCursorX < cursorX + textSize.x - 5.0f);

            // If text was wrapped during rendering, we need to finish the current line
            if (wasWrapped && !textWrapped) {
                textWrapped = true;

                // Add current line to text lines and start a new one
                finishCurrentLine();
            }

            // Append the rendered text to the current line and current segment
            std::string textSegment(str, te);
            currentLine.append(textSegment);
            currentSegment.text.append(textSegment);
            linePartCount++;

            // Check if we hit a newline or end of text
            if (te > str && *(te - 1) == '\n') {
                is_lf = true;
                finishCurrentLine();
                textWrapped = false;
            }

            // Handle styling
            if (!m_href.empty()) {
                ImVec4 c;
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", m_href.c_str());
                    c = s.Colors[ImGuiCol_ButtonHovered];
                    if (ImGui::IsMouseReleased(0)) {
                        open_url();
                    }
                }
                else {
                    c = s.Colors[ImGuiCol_Button];
                }
                line(c, true);
            }
            if (m_is_underline) {
                line(s.Colors[ImGuiCol_Text], true);
            }
            if (m_is_strikethrough) {
                line(s.Colors[ImGuiCol_Text], false);
            }

            // Check if wrapping will likely occur on the next segment
            // If currentLineWidth is approaching availWidth, prepare for wrapping
            if (willWrap || (currentLineWidth > availWidth * 0.95f)) {
                textWrapped = true;
                finishCurrentLine();
            }

            // Update cursor positions for next iteration
            lastCursorX = newCursorX;
            lastCursorY = newCursorY;
            cursorX = newCursorX;
            cursorY = newCursorY;

            str = te;
            while (str < str_end && *str == ' ') ++str;
        }

        // Set flag for continuing on the same line
        sameLineSequence = !is_lf;

        // Only perform SameLine if not at end of line
        if (!is_lf) ImGui::SameLine(0.0f, 0.0f);
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

    void open_url() const override
    {
        // Get the URL from the base class's m_href member
        const std::string url = m_href;

        if (url.empty()) {
            return; // No URL to open
        }

#ifdef _WIN32
        ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
#elif defined(__APPLE__)
        std::string cmd = "open \"" + url + "\"";
        system(cmd.c_str());
#else
        std::string cmd = "xdg-open \"" + url + "\"";
        system(cmd.c_str());
#endif
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

            FontsManager::GetInstance().PushFont(FontsManager::CODE, FontsManager::MD);
            lastFontWasBold = false; // Code font is not bold
            currentFont = ImGui::GetFont();

            m_is_code_block = true;

            // Finish the current line for text selection
            finishCurrentLine();
            sameLineSequence = false;
            textWrapped = false;
        }
        else {
            if (!m_code_stack.empty()) {
                CodeBlock& block = m_code_stack.back();

                // remove last newline
                if (!block.content.empty() && block.content.back() == '\n')
                    block.content.pop_back();

                // Calculate height
                const float line_height = ImGui::GetTextLineHeight();
                const int   line_count = std::count(block.content.begin(), block.content.end(), '\n') + 2;
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

                // Add code text to text selection - preserve lines
                std::istringstream codeStream(block.content);
                std::string codeLine;
                while (std::getline(codeStream, codeLine)) {
                    if (inListItem) {
                        applyListIndent(codeLine);
                    }

                    // Create a styled line for code text
                    StyledTextLine styledLine;
                    StyledTextSegment codeSegment;
                    codeSegment.text = codeLine;
                    codeSegment.font = currentFont;
                    codeSegment.isBold = false;
                    codeSegment.startX = 0;

                    // Calculate width
                    ImFont* oldFont = ImGui::GetFont();
                    if (currentFont) {
                        ImGui::PushFont(currentFont);
                    }
                    codeSegment.endX = ImGui::CalcTextSize(codeLine.c_str()).x;
                    if (currentFont) {
                        ImGui::PopFont();
                    }

                    styledLine.segments.push_back(codeSegment);
                    styledLine.totalWidth = codeSegment.endX;

                    // Add to lines
                    textLines.push_back(codeLine);
                    styledLines.push_back(styledLine);
                }

                // Add a blank line after code block
                textLines.push_back("");
                styledLines.push_back(StyledTextLine{});
                currentLine.clear();
                currentStyledLine = StyledTextLine{};
                currentSegment = StyledTextSegment{};
                currentSegment.font = currentFont;
                currentSegment.isBold = lastFontWasBold;
                linePartCount = 0;
                sameLineSequence = false;
                textWrapped = false;
                currentLineWidth = 0;
            }
            FontsManager::GetInstance().PopFont();
            lastFontWasBold = false; // Reset bold state after popping font
            currentFont = ImGui::GetFont();

            m_is_code_block = false;
        }
    }

    void SPAN_CODE(bool e) override
    {
        if (e) {
            finishCurrentSegment(); // Finish current segment before style change
            FontsManager::GetInstance().PushFont(FontsManager::CODE, FontsManager::MD);
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(180, 230, 180, 255)); // Greenish text
            lastFontWasBold = false; // Code font is not bold
            currentFont = ImGui::GetFont();

            // Initialize the new segment with the current font
            currentSegment.font = currentFont;
            currentSegment.isBold = lastFontWasBold;
        }
        else {
            finishCurrentSegment(); // Finish the code segment
            ImGui::PopStyleColor();
            FontsManager::GetInstance().PopFont();
            lastFontWasBold = false; // Reset after popping font
            currentFont = ImGui::GetFont();

            // Initialize the next segment with the restored font
            currentSegment.font = currentFont;
            currentSegment.isBold = lastFontWasBold;
        }
    }

    // SPAN_STRONG implementation - handle bold text spans
    void SPAN_STRONG(bool e) override
    {
        if (e) {
            finishCurrentSegment(); // Finish current segment before style change
            // Set bold state flag
            lastFontWasBold = true;
        }
        else {
            finishCurrentSegment(); // Finish the bold segment
            // Reset bold state flag 
            lastFontWasBold = false;
        }

        // Call base implementation
        imgui_md::SPAN_STRONG(e);

        // Update current font after style change
        currentFont = ImGui::GetFont();

        // Initialize the next segment with the updated font
        currentSegment.font = currentFont;
        currentSegment.isBold = lastFontWasBold;
    }

    // Handle explicit line breaks
    void soft_break() override
    {
        ImGui::NewLine();

        // Finish the current line
        finishCurrentLine();
        sameLineSequence = false;
        textWrapped = false;
    }

    void BLOCK_P(bool e) override
    {
        if (!m_list_stack.empty()) return;

        ImGui::NewLine();

        // Complete the current paragraph
        if (!e) {
            // Finish the current line
            finishCurrentLine();

            // Add an empty line for paragraph breaks
            textLines.push_back("");
            styledLines.push_back(StyledTextLine{}); // Add empty styled line
            sameLineSequence = false;
            textWrapped = false;
        }
    }

    // Ensure we properly finish the current line when blocks end
    void BLOCK_DOC(bool e) override
    {
        if (!e) {
            finishCurrentLine();
        }
        imgui_md::BLOCK_DOC(e);
    }

    // Override for unordered lists
    void BLOCK_UL(const MD_BLOCK_UL_DETAIL* d, bool e) override
    {
        if (e) {
            // Entering a new list - increase nesting level
            listNestingLevel++;

            // Add any pending line before starting the list
            finishCurrentLine();
        }
        else {
            // Exiting a list - decrease nesting level
            listNestingLevel--;

            // Add any pending line at the end of the list
            finishCurrentLine();

            // Add an empty line after the list ends, but only if we're 
            // completely outside all lists or this is a top-level list
            if (listNestingLevel == 0) {
                textLines.push_back("");
                styledLines.push_back(StyledTextLine{});
            }
        }

        imgui_md::BLOCK_UL(d, e);
    }

    // Override for ordered lists
    void BLOCK_OL(const MD_BLOCK_OL_DETAIL* d, bool e) override
    {
        if (e) {
            // Entering a new list - increase nesting level
            listNestingLevel++;

            // Add any pending line before starting the list
            finishCurrentLine();
        }
        else {
            // Exiting a list - decrease nesting level
            listNestingLevel--;

            // Add any pending line at the end of the list
            finishCurrentLine();

            // Add an empty line after the list ends, but only if we're 
            // completely outside all lists or this is a top-level list
            if (listNestingLevel == 0) {
                textLines.push_back("");
                styledLines.push_back(StyledTextLine{});
            }
        }

        imgui_md::BLOCK_OL(d, e);
    }

    // Override for list items
    void BLOCK_LI(const MD_BLOCK_LI_DETAIL* d, bool e) override
    {
        if (e) {
            // Entering a list item
            inListItem = true;

            // Add any pending line before starting the list item
            finishCurrentLine();
        }
        else {
            // Exiting a list item
            finishCurrentLine();
            inListItem = false;
        }

        imgui_md::BLOCK_LI(d, e);
    }

    // Add support for emphasis (italic) with font tracking
    void SPAN_EM(bool e) override
    {
        if (e) {
            finishCurrentSegment(); // Finish current segment before style change
        }
        else {
            finishCurrentSegment(); // Finish the italic segment
        }

        // Call base implementation
        imgui_md::SPAN_EM(e);

        // Update current font after style change
        currentFont = ImGui::GetFont();

        // Initialize the next segment with the updated font
        currentSegment.font = currentFont;
        currentSegment.isBold = lastFontWasBold;
    }

    void BLOCK_HR(bool e) override {
        if (e) {
            // Finish any pending line before the HR
            finishCurrentLine();

            // Add a line representing the horizontal rule to text selection
            std::string hrLine = "---";
            textLines.push_back(hrLine);

            // Create a styled line for the HR
            StyledTextLine hrStyledLine;
            StyledTextSegment hrSegment;
            hrSegment.text = hrLine;
            hrSegment.font = ImGui::GetFont(); // Use current font
            hrSegment.isBold = false;
            hrSegment.startX = 0;

            // Calculate width
            hrSegment.endX = ImGui::CalcTextSize(hrLine.c_str()).x;

            hrStyledLine.segments.push_back(hrSegment);
            hrStyledLine.totalWidth = hrSegment.endX;

            // Add to styled lines collection
            styledLines.push_back(hrStyledLine);

            // Reset current line state
            currentLine.clear();
            currentStyledLine = StyledTextLine{};
            currentSegment = StyledTextSegment{};
            currentSegment.font = currentFont;
            currentSegment.isBold = lastFontWasBold;
            linePartCount = 0;
            sameLineSequence = false;
            textWrapped = false;
            currentLineWidth = 0;
        }

        // Call the base implementation to render the actual HR
        imgui_md::BLOCK_HR(e);
    }

    void BLOCK_H(const MD_BLOCK_H_DETAIL* d, bool e) override {
        if (e) {
            // Set heading level when entering a heading
            m_hlevel = d->level;
            ImGui::NewLine();

            // Finish any current line
            finishCurrentLine();

            // Set height multiplier based on heading level
            // H1 and H2 are larger, so they need more space
            if (d->level == 1) {
                currentStyledLine.heightMultiplier = 1.4f;
            }
            else if (d->level == 2) {
                currentStyledLine.heightMultiplier = 1.4f;
            }
            else if (d->level == 3) {
                currentStyledLine.heightMultiplier = 1.4f;
            }
            else {
                currentStyledLine.heightMultiplier = 1.4f;
            }

            // Update font for the heading
            set_font(true);
        }
        else {
            if (d->level <= 2) {
                ImGui::NewLine();
            }

            // Finish the heading line before exiting
            finishCurrentLine();

            // Add a blank line after headings to ensure proper separation
            textLines.push_back("");
            StyledTextLine emptyLine;
            emptyLine.heightMultiplier = 1.0f; // Normal height for empty line
            styledLines.push_back(emptyLine);

            // Reset heading level
            m_hlevel = 0;

            // Restore font
            set_font(false);
        }
    }
};

// Global map of markdown renderers by ID
std::unordered_map<int, std::shared_ptr<MarkdownRenderer>> g_markdownRenderers;

inline void RenderMarkdown(const char* text, int id)
{
    if (!text || !*text)
        return;

    // Get or create a renderer for this ID
    auto& renderer = g_markdownRenderers[id];
    if (!renderer) {
        renderer = std::make_shared<MarkdownRenderer>();
        renderer->chatCounter = id * 100;
    }

    // Store the initial cursor position before rendering text
    ImVec2 initialCursorPos = ImGui::GetCursorScreenPos();

    // Clear previous text lines and prepare for rendering
    renderer->clearTextLines();

    // Render the markdown text
    renderer->print(text, text + std::strlen(text));

    // After rendering, ensure the final line is captured if not empty
    if (!renderer->currentLine.empty()) {
        // Finish the current line properly
        renderer->finishCurrentLine();
    }

    // Initialize text selection if needed
    renderer->initTextSelect();

    // Update text selection with the correct cursor position
    if (renderer->textSelect) {
        // Use the initial cursor position for text selection
        renderer->textSelect->update(initialCursorPos);

        // Add right-click context menu
        if (ImGui::BeginPopupContextWindow()) {
            ImGui::BeginDisabled(!renderer->textSelect->hasSelection());
            if (ImGui::MenuItem("Copy", "Ctrl+C")) {
                renderer->textSelect->copy();
            }
            ImGui::EndDisabled();

            if (ImGui::MenuItem("Select All", "Ctrl+A")) {
                renderer->textSelect->selectAll();
            }

            ImGui::EndPopup();
        }
    }
}

inline float ApproxMarkdownHeight(const char* text, float width)
{
    return MarkdownRenderer::ComputeMarkdownHeight(text, width);
}

#endif // MARKDOWN_SELECTABLE_HPP