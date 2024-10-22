#ifndef KOLASAL_H
#define KOLASAL_H

#include <string>
#include <vector>
#include <chrono>
#include <tuple>
#include <array>
#include "imgui.h"

//-----------------------------------------------------------------------------
// [SECTION] Constants and Configurations
//-----------------------------------------------------------------------------

namespace Config {
    // Global constants for padding
    constexpr float FRAME_PADDING_X = 10.0F;
    constexpr float FRAME_PADDING_Y = 10.0F;

    // Constants to replace magic numbers
    namespace Font {
        constexpr float DEFAULT_FONT_SIZE = 16.0F;
    }

    namespace BackgroundColor {
        constexpr float R = 0.1F;
        constexpr float G = 0.1F;
        constexpr float B = 0.1F;
        constexpr float A = 1.0F;
    }

    namespace UserColor {
        constexpr float COMPONENT = 47.0F / 255.0F;
    }

    namespace Bubble {
        constexpr float WIDTH_RATIO = 0.75F;
        constexpr float PADDING = 15.0F;
        constexpr float RIGHT_PADDING = 20.0F;
        constexpr float BOT_PADDING_X = 20.0F;
    }

    namespace Timing {
        constexpr float TIMESTAMP_OFFSET_Y = 5.0F;
    }

    namespace Button {
        constexpr float WIDTH = 60.0F;
        constexpr float SPACING = 10.0F;
    }

    namespace Style {
        constexpr float CHILD_ROUNDING = 10.0F;
        constexpr float FRAME_ROUNDING = 12.0F;
        constexpr float INPUT_FIELD_BG_COLOR = 0.15F;
    }

    namespace InputField {
        constexpr size_t TEXT_SIZE = 1024;
    }

    constexpr float HALF_DIVISOR = 2.0F;
    constexpr float BOTTOM_MARGIN = 10.0F;
    constexpr float INPUT_HEIGHT = 100.0F;
}

//-----------------------------------------------------------------------------
// [SECTION] Forward Declarations and Global Variables
//-----------------------------------------------------------------------------

// Forward declaration of GLFWwindow to avoid including GLFW/glfw3.h
struct GLFWwindow;

// Global chat bot instance
extern class ChatBot chatBot;

//-----------------------------------------------------------------------------
// [SECTION] Classes
//-----------------------------------------------------------------------------

// Message class with timestamp
class Message {
public:
    // Constructors
    Message(std::string content, bool isUser);

    // Accessors
    auto getContent() const -> std::string;
    auto isUserMessage() const -> bool;
    auto getTimestamp() const -> std::string;

private:
    std::string content;
    bool isUser;
    std::chrono::system_clock::time_point timestamp;
};

// Manages the history of chat messages
class ChatHistory {
public:
    void addMessage(const Message& message);
    auto getMessages() const -> const std::vector<Message>&;

private:
    std::vector<Message> messages;
};

// Handles simple bot responses
class ChatBot {
public:
    ChatBot() = default; // Use default constructor
    void processUserInput(const std::string& input);
    auto getChatHistory() const -> const ChatHistory&;

private:
    ChatHistory chatHistory;
};

//-----------------------------------------------------------------------------
// [SECTION] Function Prototypes
//-----------------------------------------------------------------------------

// Initialization and Cleanup Functions
auto initializeGLFW() -> bool;
auto createWindow() -> GLFWwindow*;
auto initializeGLAD() -> bool;
void setupImGui(GLFWwindow* window);
void mainLoop(GLFWwindow* window);
void cleanup(GLFWwindow* window);

// Rendering Functions
void renderChatWindow(bool& focusInputField, float inputHeight);
void renderChatHistory(const ChatHistory& chatHistory);
void renderMessage(const Message& msg, int index);
void pushIDAndColors(const Message& msg, int index);
auto calculateDimensions(const Message& msg) -> std::tuple<float, float, float, float>;
void renderMessageContent(const Message& msg, float bubbleWidth, float bubblePadding);
void renderTimestamp(const Message& msg, float bubblePadding);
void renderButtons(const Message& msg, int index, float bubbleWidth, float bubblePadding);
void renderInputField(bool& focusInputField, float inputHeight);
void setInputFieldStyle();
void restoreInputFieldStyle();
void handleInputSubmission(char* inputText, bool& focusInputField);

#endif // KOLASAL_H
