// Copyright 2024-2025 Aidan Sun and the ImGuiTextSelect contributors
// SPDX-License-Identifier: MIT

#define IMGUI_DEFINE_MATH_OPERATORS

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>
#include <utf8.h>

#include "textselect.hpp"

// Calculates the midpoint between two numbers
template<typename T>
constexpr T midpoint(T a, T b) {
    return a + (b - a) / 2;
}

// Checks if a string view ends with the specified char suffix
bool endsWith(std::string_view str, char suffix) {
    return !str.empty() && str.back() == suffix;
}

// Simple word boundary detection, accounts for Latin Unicode blocks only.
static bool isBoundary(char32_t c) {
    using Range = std::array<char32_t, 2>;
    std::array ranges{
        Range{ 0x20, 0x2F },
        Range{ 0x3A, 0x40 },
        Range{ 0x5B, 0x60 },
        Range{ 0x7B, 0xBF }
    };

    return std::find_if(ranges.begin(), ranges.end(), [c](const Range& r) { return c >= r[0] && c <= r[1]; })
        != ranges.end();
}

// Gets the number of UTF-8 characters (not bytes) in a string.
static std::size_t utf8Length(std::string_view s) {
    return utf8::unchecked::distance(s.begin(), s.end());
}

// Gets the display width of a substring, using the current font.
static float substringSizeX(std::string_view s, std::size_t start, std::size_t length = std::string_view::npos) {
    // For an empty string, data() or begin() == end()
    if (s.empty()) {
        return 0;
    }

    // Convert char-based start and length into byte-based iterators
    auto stringStart = s.begin();
    utf8::unchecked::advance(stringStart, start);

    auto stringEnd = stringStart;
    if (length == std::string_view::npos) {
        stringEnd = s.end();
    }
    else {
        utf8::unchecked::advance(stringEnd, std::min(utf8Length(s), length));
    }

    // Dereferencing std::string_view::end() may be undefined behavior in some compilers,
    // because of that, we need to get the pointer value manually if stringEnd == s.end().
    const char* endPtr = stringEnd == s.end() ? s.data() + s.size() : &*stringEnd;

    // Calculate text size between start and end using the current font
    return ImGui::CalcTextSize(&*stringStart, endPtr).x;
}

// Character width cache to handle different font styles
struct CharWidthCache {
    // Cache character positions and widths
    std::vector<float> charPositions;
    bool initialized = false;

    // Build the character position cache for a line
    void build(std::string_view line) {
        if (line.empty()) {
            charPositions.clear();
            charPositions.push_back(0.0f);
            initialized = true;
            return;
        }

        // Calculate position for each character
        charPositions.clear();
        charPositions.reserve(utf8Length(line) + 1);
        charPositions.push_back(0.0f); // Initial position

        // We need to handle the string character by character to account for non-uniform widths
        auto it = line.begin();
        auto end = line.end();
        float currentPos = 0.0f;

        while (it != end) {
            // Get the next UTF-8 character
            char32_t codepoint;
            try {
                codepoint = utf8::unchecked::next(it);
            }
            catch (...) {
                break; // Break if we encounter invalid UTF-8
            }

            // Calculate the width of this single character
            // Convert the codepoint back to UTF-8 for width measurement
            char utf8Char[5] = { 0 }; // Space for up to 4 UTF-8 bytes plus null terminator
            char* p = utf8Char;
            utf8::unchecked::append(codepoint, p);

            // Calculate width of this character
            float charWidth = ImGui::CalcTextSize(utf8Char).x;
            currentPos += charWidth;

            // Store the position after this character
            charPositions.push_back(currentPos);
        }

        initialized = true;
    }

    // Build the character position cache for a line with font segments
    void buildWithFontInfo(const TextLine& line) {
        if (line.segments.empty()) {
            charPositions.clear();
            charPositions.push_back(0.0f);
            initialized = true;
            return;
        }

        // Calculate positions for each character accounting for font changes
        charPositions.clear();

        // Count total characters to reserve space
        size_t totalChars = 0;
        for (const auto& segment : line.segments) {
            totalChars += utf8Length(segment.text);
        }

        charPositions.reserve(totalChars + 1);
        charPositions.push_back(0.0f); // Initial position

        // Process each segment with its own font
        for (const auto& segment : line.segments) {
            ImFont* oldFont = ImGui::GetFont();

            // Use segment's font if available
            if (segment.font) {
                ImGui::PushFont(segment.font);
            }

            std::string_view text = segment.text;
            auto it = text.begin();
            auto end = text.end();

            while (it != end) {
                // Get the next UTF-8 character
                char32_t codepoint;
                try {
                    codepoint = utf8::unchecked::next(it);
                }
                catch (...) {
                    break;
                }

                // Convert codepoint back to UTF-8 for width measurement
                char utf8Char[5] = { 0 };
                char* p = utf8Char;
                utf8::unchecked::append(codepoint, p);

                // Calculate width using current font
                float charWidth = ImGui::CalcTextSize(utf8Char).x;
                float currentPos = charPositions.back() + charWidth;

                // Store the position after this character
                charPositions.push_back(currentPos);
            }

            // Restore previous font if we pushed one
            if (segment.font) {
                ImGui::PopFont();
            }
        }

        initialized = true;
    }

    // Get the character index at a given X position
    std::size_t getCharIndexAtPos(float xPos) {
        if (!initialized || charPositions.empty()) {
            return 0;
        }

        // Binary search to find the closest character position
        auto it = std::lower_bound(charPositions.begin(), charPositions.end(), xPos);

        if (it == charPositions.begin()) {
            return 0;
        }

        if (it == charPositions.end()) {
            return charPositions.size() - 1;
        }

        // Check which character boundary we're closer to
        std::size_t idx = std::distance(charPositions.begin(), it);
        float prevPos = *(it - 1);
        float currPos = *it;

        // If we're closer to the previous character, use that index
        if (xPos - prevPos < currPos - xPos) {
            return idx - 1;
        }

        return idx;
    }
};

// Modified getCharIndex using the width cache
static std::size_t getCharIndex(std::string_view s, float cursorPosX) {
    // Ignore cursor position when it is invalid
    if (cursorPosX < 0) {
        return 0;
    }

    // Check for empty strings
    if (s.empty()) {
        return 0;
    }

    // Build the character position cache
    static CharWidthCache cache;
    cache.build(s);

    // Use the cache to find the character index
    return cache.getCharIndexAtPos(cursorPosX);
}

// Gets character index using font information
std::size_t TextSelect::getCharIndexWithFontInfo(const TextLine& line, float cursorPosX) const {
    // Ignore cursor position when it is invalid
    if (cursorPosX < 0) {
        return 0;
    }

    // Handle empty lines
    if (line.segments.empty()) {
        return 0;
    }

    // Build the character position cache with font info
    static CharWidthCache cache;
    cache.buildWithFontInfo(line);

    // Use the cache to find the character index
    return cache.getCharIndexAtPos(cursorPosX);
}

// Gets the scroll delta for the given cursor position and window bounds.
static float getScrollDelta(float v, float min, float max) {
    const float deltaScale = 10.0f * ImGui::GetIO().DeltaTime;
    const float maxDelta = 100.0f;

    if (v < min) {
        return std::max(-(min - v), -maxDelta) * deltaScale;
    }
    else if (v > max) {
        return std::min(v - max, maxDelta) * deltaScale;
    }

    return 0.0f;
}

TextSelect::Selection TextSelect::getSelection() const {
    // Start and end may be out of order (ordering is based on Y position)
    bool startBeforeEnd = selectStart.y < selectEnd.y || (selectStart.y == selectEnd.y && selectStart.x < selectEnd.x);

    // Reorder X points if necessary
    std::size_t startX = startBeforeEnd ? selectStart.x : selectEnd.x;
    std::size_t endX = startBeforeEnd ? selectEnd.x : selectStart.x;

    // Get min and max Y positions for start and end
    std::size_t startY = std::min(selectStart.y, selectEnd.y);
    std::size_t endY = std::max(selectStart.y, selectEnd.y);

    return { startX, startY, endX, endY };
}

void TextSelect::handleMouseDown(const ImVec2& cursorPosStart) {
    std::size_t numLines = getNumLines();

    if (numLines == 0) {
        return;
    }

    const float textHeight = ImGui::GetTextLineHeightWithSpacing();

    // Get mouse position in window coordinates, then adjust by cursor position
    // This ensures the position is relative to the text's starting position
    ImVec2 mousePos = ImGui::GetMousePos();
    mousePos.x -= cursorPosStart.x;
    mousePos.y -= cursorPosStart.y;

    // Apply vertical offset
    mousePos.y -= verticalOffset;

    // Get Y position of mouse cursor, in terms of line number (clamped to the valid range)
    std::size_t y = static_cast<std::size_t>(std::min(std::max(std::floor(mousePos.y / textHeight), 0.0f), static_cast<float>(numLines - 1)));

    // Calculate the X character position using font information if available
    std::size_t x;
    if (hasFontInfo && getLineWithFontInfo) {
        TextLine line = getLineWithFontInfo(y);
        x = getCharIndexWithFontInfo(line, mousePos.x);
    }
    else {
        std::string_view currentLine = getLineAtIdx(y);
        x = getCharIndex(currentLine, mousePos.x);
    }

    // Get mouse click count and determine action
    if (int mouseClicks = ImGui::GetMouseClickedCount(ImGuiMouseButton_Left); mouseClicks > 0) {
        std::string_view currentLine = getLineAtIdx(y);

        if (mouseClicks % 3 == 0) {
            // Triple click - select line
            bool atLastLine = y == (numLines - 1);
            selectStart = { 0, y };
            selectEnd = { atLastLine ? utf8Length(currentLine) : 0, atLastLine ? y : y + 1 };
        }
        else if (mouseClicks % 2 == 0) {
            // Double click - select word
            // Initialize start and end iterators to current cursor position
            utf8::unchecked::iterator startIt{ currentLine.data() };
            utf8::unchecked::iterator endIt{ currentLine.data() };
            for (std::size_t i = 0; i < x && i < utf8Length(currentLine); i++) {
                startIt++;
                endIt++;
            }

            // Handle edge cases for double-click at end of line
            char32_t currentChar = 0;
            if (x < utf8Length(currentLine)) {
                currentChar = *startIt;
            }
            else if (!currentLine.empty()) {
                // If at end of line, use last character
                utf8::unchecked::iterator lastChar{ currentLine.data() };
                utf8::unchecked::advance(lastChar, utf8Length(currentLine) - 1);
                currentChar = *lastChar;
            }

            bool isCurrentBoundary = isBoundary(currentChar);

            // Scan to left until a word boundary is reached
            for (std::size_t startInv = 0; startInv <= x && startIt.base() > currentLine.data(); startInv++) {
                if (isBoundary(*startIt) != isCurrentBoundary) {
                    break;
                }
                selectStart = { x - startInv, y };
                startIt--;
            }

            // Scan to right until a word boundary is reached
            for (std::size_t end = x; end <= utf8Length(currentLine); end++) {
                selectEnd = { end, y };
                if (end == utf8Length(currentLine) || isBoundary(*endIt) != isCurrentBoundary) {
                    break;
                }
                endIt++;
            }
        }
        else if (ImGui::IsKeyDown(ImGuiMod_Shift)) {
            // Single click with shift - select text from start to click
            // The selection starts from the beginning if no start position exists
            if (selectStart.isInvalid()) {
                selectStart = { 0, 0 };
            }

            selectEnd = { x, y };
        }
        else {
            // Single click - set start position, invalidate end position
            selectStart = { x, y };
            selectEnd = { std::string_view::npos, std::string_view::npos };
        }
    }
    else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        // Mouse dragging - set end position
        selectEnd = { x, y };
    }
}

void TextSelect::handleScrolling() const {
    // Window boundaries
    ImVec2 windowMin = ImGui::GetWindowPos();
    ImVec2 windowMax = windowMin + ImGui::GetWindowSize();

    // Get current and active window information from Dear ImGui state
    ImGuiWindow* currentWindow = ImGui::GetCurrentWindow();
    const ImGuiWindow* activeWindow = GImGui->ActiveIdWindow;

    ImGuiID scrollXID = ImGui::GetWindowScrollbarID(currentWindow, ImGuiAxis_X);
    ImGuiID scrollYID = ImGui::GetWindowScrollbarID(currentWindow, ImGuiAxis_Y);
    ImGuiID activeID = ImGui::GetActiveID();
    bool scrollbarsActive = activeID == scrollXID || activeID == scrollYID;

    // Do not handle scrolling if:
    // - There is no active window
    // - The current window is not active
    // - The user is scrolling via the scrollbars
    if (activeWindow == nullptr || activeWindow->ID != currentWindow->ID || scrollbarsActive) {
        return;
    }

    // Get scroll deltas from mouse position
    ImVec2 mousePos = ImGui::GetMousePos();
    float scrollXDelta = getScrollDelta(mousePos.x, windowMin.x, windowMax.x);
    float scrollYDelta = getScrollDelta(mousePos.y, windowMin.y, windowMax.y);

    // If there is a nonzero delta, scroll in that direction
    if (std::abs(scrollXDelta) > 0.0f) {
        ImGui::SetScrollX(ImGui::GetScrollX() + scrollXDelta);
    }
    if (std::abs(scrollYDelta) > 0.0f) {
        ImGui::SetScrollY(ImGui::GetScrollY() + scrollYDelta);
    }
}

void TextSelect::drawSelection(const ImVec2& cursorPosStart) const {
    if (!hasSelection()) {
        return;
    }

    // Start and end positions
    auto [startX, startY, endX, endY] = getSelection();

    std::size_t numLines = getNumLines();
    if (startY >= numLines || endY >= numLines) {
        return;
    }

    // Track cumulative height for proper line positioning
    float cumulativeHeight = 0.0f;
    const float baseTextHeight = ImGui::GetTextLineHeightWithSpacing();

    // Add a rectangle to the draw list for each line contained in the selection
    for (std::size_t i = 0; i <= endY; i++) {
        // Calculate height multiplier for this line
        float heightMultiplier = 1.0f; // Default multiplier

        if (hasFontInfo && getLineWithFontInfo) {
            TextLine line = getLineWithFontInfo(i);
            heightMultiplier = line.heightMultiplier;
        }

        // Skip lines before selection starts
        if (i < startY) {
            // Still accumulate height for positioning
            cumulativeHeight += baseTextHeight * heightMultiplier;
            continue;
        }

        if (hasFontInfo && getLineWithFontInfo) {
            // Use font information to draw more accurate selections
            TextLine line = getLineWithFontInfo(i);

            // Get the height multiplier for this line (for headers)
            heightMultiplier = line.heightMultiplier;

            if (line.segments.empty()) {
                // For empty lines, draw a small rectangle
                float minY = cumulativeHeight + verticalOffset;
                float maxY = minY + (baseTextHeight * heightMultiplier);

                ImVec2 rectMin = cursorPosStart + ImVec2{ 0.0f, minY };
                ImVec2 rectMax = cursorPosStart + ImVec2{ ImGui::CalcTextSize(" ").x * 2, maxY };

                ImU32 color = ImGui::GetColorU32(ImGuiCol_TextSelectedBg);
                ImGui::GetWindowDrawList()->AddRectFilled(rectMin, rectMax, color);

                // Add this line's height to cumulative height
                cumulativeHeight += baseTextHeight * heightMultiplier;
                continue;
            }

            // Build position cache for accurate character indices
            static CharWidthCache cache;
            cache.buildWithFontInfo(line);

            // Get precise start and end positions from the cache
            float selStartX = 0.0f;
            float selEndX = line.totalWidth;

            // First line starts at selection start
            if (i == startY) {
                selStartX = startX < cache.charPositions.size() ?
                    cache.charPositions[startX] : 0.0f;
            }

            // Last line ends at selection end
            if (i == endY) {
                selEndX = endX < cache.charPositions.size() ?
                    cache.charPositions[endX] : cache.charPositions.back();
            }

            // Draw the selection rectangle with adjusted height and position
            float minY = cumulativeHeight + verticalOffset;
            float maxY = minY + (baseTextHeight * heightMultiplier);

            ImVec2 rectMin = cursorPosStart + ImVec2{ selStartX, minY };
            ImVec2 rectMax = cursorPosStart + ImVec2{ selEndX, maxY };

            ImU32 color = ImGui::GetColorU32(ImGuiCol_TextSelectedBg);
            ImGui::GetWindowDrawList()->AddRectFilled(rectMin, rectMax, color);
        }
        else {
            // Fallback to the original implementation if font info is not available
            std::string_view line = getLineAtIdx(i);

            // Build character position cache for this line
            static CharWidthCache cache;
            cache.build(line);

            // Display sizes
            const float newlineWidth = ImGui::CalcTextSize(" ").x;

            // Get precise X positions from the cache
            float minX = 0.0f;
            float maxX = 0.0f;

            if (!line.empty() && cache.initialized) {
                // For first line, start at the selection start position
                if (i == startY) {
                    minX = startX < cache.charPositions.size() ? cache.charPositions[startX] : 0.0f;
                }
                else {
                    minX = 0.0f;
                }

                // For last line, end at the selection end position
                if (i == endY) {
                    maxX = endX < cache.charPositions.size() ? cache.charPositions[endX] : cache.charPositions.back();
                }
                else {
                    maxX = cache.charPositions.back() + newlineWidth;
                }
            }
            else {
                // For empty lines, use a small but visible width
                maxX = newlineWidth * 2;
            }

            // Rectangle position based on cumulative height
            float minY = cumulativeHeight + verticalOffset;
            float maxY = minY + baseTextHeight;

            // Get rectangle corner points offset from the cursor's start position in the window
            ImVec2 rectMin = cursorPosStart + ImVec2{ minX, minY };
            ImVec2 rectMax = cursorPosStart + ImVec2{ maxX, maxY };

            // Draw the rectangle
            ImU32 color = ImGui::GetColorU32(ImGuiCol_TextSelectedBg);
            ImGui::GetWindowDrawList()->AddRectFilled(rectMin, rectMax, color);
        }

        // Accumulate height for proper positioning of next line
        cumulativeHeight += baseTextHeight * heightMultiplier;
    }
}

void TextSelect::copy() const {
    if (!hasSelection()) {
        return;
    }

    auto [startX, startY, endX, endY] = getSelection();

    // Collect selected text in a single string
    std::string selectedText;

    for (std::size_t i = startY; i <= endY; i++) {
        // Similar logic to drawing selections
        std::size_t subStart = i == startY ? startX : 0;
        std::string_view line = getLineAtIdx(i);

        // Handle empty lines properly
        if (line.empty()) {
            // If this is not the last line of the selection, add a newline
            if (i < endY) {
                selectedText += '\n';
            }
            continue;
        }

        auto stringStart = line.begin();
        utf8::unchecked::advance(stringStart, std::min(subStart, utf8Length(line)));

        auto stringEnd = stringStart;
        if (i == endY) {
            // Make sure we don't go past the end of the string
            std::size_t charsToAdvance = std::min(endX, utf8Length(line)) - std::min(subStart, utf8Length(line));
            utf8::unchecked::advance(stringEnd, charsToAdvance);
        }
        else {
            stringEnd = line.end();
        }

        std::string_view lineToAdd = line.substr(stringStart - line.begin(), stringEnd - stringStart);
        selectedText += lineToAdd;

        // If lines before the last line don't already end with newlines, add them in
        if (!endsWith(lineToAdd, '\n') && i < endY) {
            selectedText += '\n';
        }
    }

    ImGui::SetClipboardText(selectedText.c_str());
}

void TextSelect::selectAll() {
    std::size_t lastLineIdx = getNumLines() - 1;
    std::string_view lastLine = getLineAtIdx(lastLineIdx);

    // Set the selection range from the beginning to the end of the last line
    selectStart = { 0, 0 };
    selectEnd = { utf8Length(lastLine), lastLineIdx };
}

void TextSelect::update(const ImVec2& cursorPosStart) {
    // Switch cursors if the window is hovered
    bool hovered = ImGui::IsWindowHovered();
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
    }

    // Handle mouse events
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (hovered) {
            shouldHandleMouseDown = true;
        }
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        shouldHandleMouseDown = false;
    }

    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (shouldHandleMouseDown) {
            handleMouseDown(cursorPosStart);
        }
        if (!hovered) {
            handleScrolling();
        }
    }

    drawSelection(cursorPosStart);

    // Keyboard shortcuts
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_A)) {
        selectAll();
    }
    else if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C)) {
        copy();
    }
}

void TextSelect::update() {
    // Use window position plus cursor start position
    ImVec2 cursorPosStart = ImGui::GetWindowPos() + ImGui::GetCursorStartPos();
    update(cursorPosStart);
}
