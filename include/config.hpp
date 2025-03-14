#pragma once

#include <imgui.h>


// TODO: Need to refactor this to use json file that is modifiable by the user in realtime
//       Set up a system to save and load the settings from a json file
//       The new json file should contain every detail of the settings of every widgets
namespace Config
{
    constexpr int WINDOW_WIDTH = 1280;
    constexpr int WINDOW_HEIGHT = 720;
    constexpr float WINDOW_CORNER_RADIUS = 8.0f;
    constexpr const char* WINDOW_TITLE = "Kolosal AI";
    constexpr const char* OPENGL_VERSION = "#version 330";
    constexpr float TRANSITION_DURATION = 0.3f; // Duration in seconds
    constexpr double TARGET_FRAME_TIME = 1.0 / 60.0;

    // Global constants for padding
    constexpr float FRAME_PADDING_X = 10.0F;
    constexpr float FRAME_PADDING_Y = 10.0F;

    // ModelManagerConstants to replace magic numbers
    namespace Font
    {
        constexpr float DEFAULT_FONT_SIZE = 18.0F;
    } // namespace Font

    namespace Icon
    {
        constexpr float DEFAULT_FONT_SIZE = 18.0F;
    } // namespace Icon

    namespace BackgroundColor
    {
        constexpr float R = 0.05F;
        constexpr float G = 0.07F;
        constexpr float B = 0.12F;
        constexpr float A = 1.00F;
    } // namespace BackgroundColor

    namespace UserColor
    {
        constexpr float COMPONENT = 47.0F / 255.0F;
    } // namespace UserColor

    namespace Bubble
    {
        constexpr float WIDTH_RATIO = 0.75F;
        constexpr float PADDING = 15.0F;
        constexpr float RIGHT_PADDING = 20.0F;
        constexpr float BOT_PADDING_X = 20.0F;
    } // namespace Bubble

    namespace Timing
    {
        constexpr float TIMESTAMP_OFFSET_Y = 5.0F;
    } // namespace Timing

    namespace Button
    {
        constexpr float WIDTH = 30.0F;
        constexpr float SPACING = 10.0F;
        constexpr float RADIUS = 5.0F;
    } // namespace Button

    namespace InputField
    {
		// max 64kb of text
        constexpr size_t TEXT_SIZE = 64 * 1024;

        constexpr float CHILD_ROUNDING = 10.0F;
        constexpr float FRAME_ROUNDING = 12.0F;

        constexpr ImVec4 INPUT_FIELD_BG_COLOR = ImVec4(0.15F, 0.15F, 0.15F, 1.0F);
    } // namespace InputField

    namespace ChatHistorySidebar
    {
        constexpr float SIDEBAR_WIDTH = 150.0F;
        constexpr float MIN_SIDEBAR_WIDTH = 150.0F;
        constexpr float MAX_SIDEBAR_WIDTH = 400.0F;
    } // namespace ChatHistorySidebar

    namespace ModelPresetSidebar
    {
        constexpr float SIDEBAR_WIDTH = 200.0F;
        constexpr float MIN_SIDEBAR_WIDTH = 200.0F;
        constexpr float MAX_SIDEBAR_WIDTH = 400.0F;
    } // namespace ModelSettings

	namespace DeploymentSettingsSidebar
	{
		constexpr float SIDEBAR_WIDTH = 200.0F;
		constexpr float MIN_SIDEBAR_WIDTH = 200.0F;
		constexpr float MAX_SIDEBAR_WIDTH = 400.0F;
	} // namespace DeploymentSettingsSidebar

    namespace Color
    {
        constexpr ImVec4 TRANSPARENT_COL = ImVec4(0.0F, 0.0F, 0.0F, 0.0F);
        constexpr ImVec4 PRIMARY = ImVec4(0.3F, 0.3F, 0.3F, 0.5F);
        constexpr ImVec4 SECONDARY = ImVec4(0.3F, 0.3F, 0.3F, 0.3F);
        constexpr ImVec4 DISABLED = ImVec4(0.3F, 0.3F, 0.3F, 0.1F);
    } // namespace Color

    namespace Slider
    {
        constexpr ImVec4 TRACK_COLOR = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
        constexpr ImVec4 GRAB_COLOR = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);

        constexpr float TRACK_THICKNESS = 0.2f;
        constexpr float GRAB_RADIUS = 100.0f;
        constexpr float GRAB_MIN_SIZE = 5.0f;
    } // namespace Slider

    namespace ComboBox
    {
        constexpr ImVec4 COMBO_BG_COLOR = ImVec4(0.15F, 0.15F, 0.15F, 1.0F);
        constexpr ImVec4 COMBO_BORDER_COLOR = ImVec4(0.0F, 0.0F, 0.0F, 0.0F);
        constexpr ImVec4 TEXT_COLOR = ImVec4(1.0F, 1.0F, 1.0F, 1.0F);
        constexpr ImVec4 BUTTON_HOVERED_COLOR = ImVec4(0.3F, 0.3F, 0.3F, 0.5F);
        constexpr ImVec4 BUTTON_ACTIVE_COLOR = ImVec4(0.3F, 0.3F, 0.3F, 0.5F);
        constexpr ImVec4 POPUP_BG_COLOR = ImVec4(0.12F, 0.12F, 0.12F, 1.0F);

        constexpr float FRAME_ROUNDING = 5.0F;
        constexpr float POPUP_ROUNDING = 2.0F;
    } // namespace ComboBox

    constexpr float HALF_DIVISOR = 2.0F;
    constexpr float BOTTOM_MARGIN = 10.0F;
    constexpr float INPUT_HEIGHT = 100.0F;
    constexpr float CHAT_WINDOW_CONTENT_WIDTH = 750.0F;
    constexpr float TITLE_BAR_HEIGHT = 50.0F;
} // namespace Config