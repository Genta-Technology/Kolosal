// Copyright 2024-2025 Aidan Sun and the ImGuiTextSelect contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <functional>
#include <string_view>
#include <vector>

#include <imgui.h>

// Represents a segment of text with specific font information
struct TextSegment {
    std::string_view text;  // The text content
    ImFont* font;           // Font used for this segment
    bool isBold;            // Whether this is a bold font variant
    float startX;           // Starting X position
    float endX;             // Ending X position
};

// Represents a line of text with its segments
struct TextLine {
    std::vector<TextSegment> segments;
    float totalWidth;
    float heightMultiplier = 1.0f;
};

// Manages text selection in a GUI window.
// This class only works if the window only has text, and line wrapping is not supported.
// The window should also have the "NoMove" flag set so mouse drags can be used to select text.
class TextSelect {
    // Cursor position in the window.
    struct CursorPos {
        std::size_t x = std::string_view::npos; // X index of character
        std::size_t y = std::string_view::npos; // Y index of character

        // Checks if this position is invalid.
        bool isInvalid() const {
            // Invalid cursor positions are indicated by std::string::npos
            return x == std::string_view::npos || y == std::string_view::npos;
        }
    };

    // Text selection in the window.
    struct Selection {
        std::size_t startX;
        std::size_t startY;
        std::size_t endX;
        std::size_t endY;
    };

    // Selection bounds
    // In a selection, the start and end positions may not be in order (the user can click and drag left/up which
    // reverses start and end).
    CursorPos selectStart;
    CursorPos selectEnd;

    // Accessor functions to get line information
    // This class only knows about line numbers so it must be provided with functions that give it text data.
    std::function<std::string_view(std::size_t)> getLineAtIdx; // Gets the string given a line number
    std::function<std::size_t()> getNumLines;                 // Gets the total number of lines

    // Optional accessor for font information
    std::function<TextLine(std::size_t)> getLineWithFontInfo;

    // Vertical offset for text selection
    float verticalOffset = 0.0f;

    // Indicates whether selection should be updated. This is needed for distinguishing mouse drags that are
    // initiated by clicking the text, or different element.
    bool shouldHandleMouseDown = false;

    // Flag to indicate if we have font information available
    bool hasFontInfo = false;

    // Gets the user selection. Start and end are guaranteed to be in order.
    Selection getSelection() const;

    // Processes mouse down (click/drag) events.
    void handleMouseDown(const ImVec2& cursorPosStart);

    // Processes scrolling events.
    void handleScrolling() const;

    // Draws the text selection rectangle in the window.
    void drawSelection(const ImVec2& cursorPosStart) const;

    // Gets the character index at a given X position, accounting for font differences
    std::size_t getCharIndexWithFontInfo(const TextLine& line, float cursorPosX) const;

public:
    // Sets the text accessor functions.
    // getLineAtIdx: Function taking a std::size_t (line number) and returning the string in that line
    // getNumLines: Function returning a std::size_t (total number of lines of text)
    template <class T, class U>
    TextSelect(const T& getLineAtIdx, const U& getNumLines)
        : getLineAtIdx(getLineAtIdx), getNumLines(getNumLines), hasFontInfo(false) {
    }

    // Constructor that also takes font information accessor
    template <class T, class U, class V>
    TextSelect(const T& getLineAtIdx, const U& getNumLines, const V& getLineWithFontInfo)
        : getLineAtIdx(getLineAtIdx), getNumLines(getNumLines),
        getLineWithFontInfo(getLineWithFontInfo), hasFontInfo(true) {
    }

    // Set font information accessor after construction
    template <class V>
    void setFontInfoAccessor(const V& fontInfoAccessor) {
        getLineWithFontInfo = fontInfoAccessor;
        hasFontInfo = true;
    }

    // Checks if there is an active selection in the text.
    bool hasSelection() const {
        return !selectStart.isInvalid() && !selectEnd.isInvalid();
    }

    // Copies the selected text to the clipboard.
    void copy() const;

    // Selects all text in the window.
    void selectAll();

    // Set vertical offset for text selection
    void setVerticalOffset(float offset) {
        verticalOffset = offset;
    }

    // Get current vertical offset
    float getVerticalOffset() const {
        return verticalOffset;
    }

    // Draws the text selection rectangle and handles user input.
    void update();

    // Update with explicit cursor start position
    void update(const ImVec2& cursorPosStart);
};
