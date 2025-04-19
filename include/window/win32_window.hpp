#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <d3d10_1.h>
#include <d3d10.h>
#include <dxgi.h>
#include <string>
#include <system_error>
#include <dwmapi.h>
#include <stdexcept>
#include <memory>
#include <imgui_impl_win32.h>

#include "config.hpp"
#include "window.hpp"
#include "window_composition_attribute.hpp"
#include "dx10_context.hpp"
#include "ui/fonts.hpp"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

class Win32Window : public Window {
public:
    Win32Window(HINSTANCE hInstance)
        : hInstance(hInstance)
        , hwnd(nullptr)
        , is_window_active(false)
        , width(1280)
        , height(720)
        , should_close(false)
        , borderless(true)
        , borderless_shadow(false)
        , borderless_drag(false)
        , borderless_resize(true)
        , dxContext(nullptr) {
    }

    ~Win32Window()
    {
        if (hwnd) {
            DestroyWindow(hwnd);
            hwnd = nullptr;
        }
    }

    void createWindow(int width, int height, const std::string& title, const float tabButtonWidths) override
    {
        this->width = width;
        this->height = height;
        this->title = title;
        this->tabButtonWidths = tabButtonWidths;

        hwnd = create_window(&Win32Window::WndProc, hInstance, this);
        if (!hwnd) {
            throw std::runtime_error("Failed to create window");
        }

        set_borderless(borderless);

        // Apply visual effect (acrylic or fallback)
        applyVisualEffect();
    }

    // Set DirectX context for resize notifications
    void setDXContext(DX10Context* context) {
        dxContext = context;
    }

    // Handle window resize for DirectX
    void notifyResize(UINT width, UINT height) {
        // Store for internal tracking
        this->width = width;
        this->height = height;

        // The actual resize operation will be handled in the Application class
        pendingResize = true;
        pendingResizeWidth = width;
        pendingResizeHeight = height;
    }

    // Check if a resize is pending
    bool hasPendingResize() const {
        return pendingResize;
    }

    // Get pending resize dimensions
    void getPendingResizeSize(UINT& outWidth, UINT& outHeight) {
        outWidth = pendingResizeWidth;
        outHeight = pendingResizeHeight;
        pendingResize = false;  // Clear the flag after reading
    }

    void applyVisualEffect()
    {
        if (!hwnd) return;

        bool acrylicApplied = false;

        // Try to apply acrylic effect first
        HMODULE hUser = GetModuleHandle(TEXT("user32.dll"));
        if (hUser)
        {
            pfnSetWindowCompositionAttribute setWindowCompositionAttribute =
                (pfnSetWindowCompositionAttribute)GetProcAddress(hUser, "SetWindowCompositionAttribute");

            if (setWindowCompositionAttribute && isAcrylicSupported())
            {
                // Create accent policy for acrylic blur
                ACCENT_POLICY accent{ ACCENT_ENABLE_ACRYLICBLURBEHIND, 0, 0, 0 };

                // Set the gradient color ($AABBGGRR format)
                accent.GradientColor = 0xB3000000;  // Semi-transparent dark color

                // Apply the acrylic effect
                WINDOWCOMPOSITIONATTRIBDATA data;
                data.Attrib = WCA_ACCENT_POLICY;
                data.pvData = &accent;
                data.cbData = sizeof(accent);

                if (setWindowCompositionAttribute(hwnd, &data)) {
                    acrylicApplied = true;
                }
            }
        }

        // If acrylic effect failed or isn't supported, apply fallback with system accent color
        if (!acrylicApplied && hUser)
        {
            pfnSetWindowCompositionAttribute setWindowCompositionAttribute =
                (pfnSetWindowCompositionAttribute)GetProcAddress(hUser, "SetWindowCompositionAttribute");

            if (setWindowCompositionAttribute)
            {
                // Use system accent color as fallback
                DWORD systemAccentColor = getSystemAccentColor();

                ACCENT_POLICY accent{ ACCENT_ENABLE_GRADIENT, 0, systemAccentColor, 0 };

                WINDOWCOMPOSITIONATTRIBDATA data;
                data.Attrib = WCA_ACCENT_POLICY;
                data.pvData = &accent;
                data.cbData = sizeof(accent);

                setWindowCompositionAttribute(hwnd, &data);
            }
        }

        // Apply rounded corners
        applyRoundedCorners();
    }

    void applyRoundedCorners()
    {
        // Set rounded corners.
        // For Windows 11, try to use DWMWA_WINDOW_CORNER_PREFERENCE if available; otherwise, fallback to SetWindowRgn.
        typedef enum _DWM_WINDOW_CORNER_PREFERENCE {
            DWMWCP_DEFAULT = 0,
            DWMWCP_DONOTROUND = 1,
            DWMWCP_ROUND = 2,
            DWMWCP_ROUNDSMALL = 3
        } DWM_WINDOW_CORNER_PREFERENCE;

        // The DWMWA_WINDOW_CORNER_PREFERENCE attribute is 33.
        const DWORD DWMWA_WINDOW_CORNER_PREFERENCE = 33;
        DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_ROUND; // Or use DWMWCP_ROUNDSMALL per your taste

        HRESULT hr = DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
        if (SUCCEEDED(hr)) {
            // Successfully applied system-managed rounded corners on supported systems.
        }
        else {
            // Fallback: use SetWindowRgn to create rounded window corners.
            RECT rect;
            if (GetClientRect(hwnd, &rect)) {
                int width = rect.right - rect.left;
                int height = rect.bottom - rect.top;
                HRGN hRgn = CreateRoundRectRgn(0, 0, width, height, Config::WINDOW_CORNER_RADIUS, Config::WINDOW_CORNER_RADIUS);
                if (hRgn) {
                    SetWindowRgn(hwnd, hRgn, TRUE);
                }
            }
        }
    }

    bool isAcrylicSupported()
    {
        // Check if DWM composition is enabled (required for acrylic)
        BOOL compositionEnabled = FALSE;
        if (!SUCCEEDED(DwmIsCompositionEnabled(&compositionEnabled)) || !compositionEnabled) {
            return false;
        }

        // On versions before Windows 10 1803, acrylic is not available
        // Could use feature detection, but for simplicity, we'll just try to apply it
        // and let the fallback handle failure cases
        return true;
    }

    DWORD getSystemAccentColor()
    {
        // Default color (dark with some transparency) in case we can't get the system color
        DWORD defaultColor = 0xB3000000;

        // Try to get the Windows accent color
        DWORD colorizationColor = 0;
        BOOL opaqueBlend = FALSE;
        if (SUCCEEDED(DwmGetColorizationColor(&colorizationColor, &opaqueBlend)))
        {
            // Convert from ARGB to AABBGGRR format
            BYTE a = 0xB3; // Use a fixed alpha for semi-transparency
            BYTE r = (colorizationColor >> 16) & 0xFF;
            BYTE g = (colorizationColor >> 8) & 0xFF;
            BYTE b = colorizationColor & 0xFF;

            return (a << 24) | (b << 16) | (g << 8) | r;
        }

        return defaultColor;
    }

    void show() override
    {
        ::ShowWindow(hwnd, SW_SHOW);
    }

    void processEvents() override
    {
        MSG msg = { 0 };
        while (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) {
                should_close = true;
            }
        }
    }

    bool shouldClose() override
    {
        return should_close;
    }

    void* getNativeHandle() override
    {
        return static_cast<void*>(hwnd);
    }

    bool isActive() const override
    {
        return is_window_active;
    }

    int getWidth() const override
    {
        RECT rect;
        if (GetClientRect(hwnd, &rect)) {
            return rect.right - rect.left;
        }
        return width;
    }

    int getHeight() const override
    {
        RECT rect;
        if (GetClientRect(hwnd, &rect)) {
            return rect.bottom - rect.top;
        }
        return height;
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept
    {
        if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) {
            return true;
        }

        Win32Window* window = reinterpret_cast<Win32Window*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        if (msg == WM_NCCREATE) {
            auto userdata = reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams;
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(userdata));
            window = reinterpret_cast<Win32Window*>(userdata);
        }

        if (window) {
            switch (msg) {
            case WM_MOUSEWHEEL: {
                // Check if Ctrl key is pressed for font size adjustment
                bool ctrlPressed = (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) != 0;
                if (ctrlPressed) {
                    // Get scroll direction and convert to zoom delta
                    // A positive value means the wheel was rotated forward (away from the user)
                    // A negative value means the wheel was rotated backward (toward the user)
                    int wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
                    float zoomDelta = (wheelDelta > 0) ? 0.1f : -0.1f;

                    // Adjust font size using the FontManager
                    FontsManager::GetInstance().AdjustFontSize(zoomDelta);

                    // Force window redraw to reflect the size change
                    InvalidateRect(hwnd, NULL, FALSE);

                    // Prevent normal scrolling
                    return 0;
                }
                break;
            }
            case WM_ENTERSIZEMOVE: {
                // Window moving/resizing starts
                window->isMoving = true;
                if (window->dxContext) {
                    // Notify DirectX context to reduce rendering
                    static_cast<DX10Context*>(window->dxContext)->setWindowMoving(true);
                }
                break;
            }
            case WM_EXITSIZEMOVE: {
                // Window moving/resizing ends
                window->isMoving = false;
                if (window->dxContext) {
                    // Restore normal rendering
                    static_cast<DX10Context*>(window->dxContext)->setWindowMoving(false);

                    // Force a repaint to update the window contents immediately
                    InvalidateRect(hwnd, NULL, FALSE);
                    UpdateWindow(hwnd);
                }
                break;
            }
            case WM_NCCALCSIZE: {
                if (wParam == TRUE && window->borderless) {
                    auto& params = *reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
                    adjust_maximized_client_rect(hwnd, params.rgrc[0]);
                    return 0;
                }
                break;
            }
            case WM_NCHITTEST: {
                if (window->borderless) {
                    return window->hit_test(POINT{
                        GET_X_LPARAM(lParam),
                        GET_Y_LPARAM(lParam)
                        });
                }
                break;
            }
            case WM_NCACTIVATE: {
                window->is_window_active = (wParam != FALSE);
                break;
            }
            case WM_ACTIVATE: {
                window->is_window_active = (wParam != WA_INACTIVE);
                break;
            }
            case WM_SIZE: {
                // Handle DirectX-specific resize event
                if (wParam != SIZE_MINIMIZED) {
                    UINT width = LOWORD(lParam);
                    UINT height = HIWORD(lParam);
                    window->notifyResize(width, height);
                }

                // Reapply visual effect when the window is resized
                window->applyVisualEffect();
                break;
            }
            case WM_DPICHANGED:
            {
                // Update window position and size based on the suggested rect
                RECT* rect = (RECT*)lParam;
                SetWindowPos(hwnd,
                    NULL,
                    rect->left,
                    rect->top,
                    rect->right - rect->left,
                    rect->bottom - rect->top,
                    SWP_NOZORDER | SWP_NOACTIVATE);

                // Extract the new DPI scale
                // LOWORD(wParam) is the new DPI, typically HIWORD(wParam) is the same value
                // 96 is the default/reference DPI
                float newDpiScale = (float)LOWORD(wParam) / 96.0f;

                // Update fonts for the new DPI scale
                FontsManager::GetInstance().UpdateForDpiChange(newDpiScale);

                // Refresh visual effects since DPI changed
                window->applyVisualEffect();

                return 0;
            }
            case WM_DWMCOLORIZATIONCOLORCHANGED: {
                // System accent color changed, reapply visual effect
                window->applyVisualEffect();
                break;
            }
            case WM_CLOSE: {
                window->should_close = true;
                return 0;
            }
            case WM_DESTROY: {
                ::PostQuitMessage(0);
                return 0;
            }
            case WM_PAINT: {
                PAINTSTRUCT ps;
                BeginPaint(hwnd, &ps);
                EndPaint(hwnd, &ps);

                // If we're not actively moving the window, force a redraw
                if (!window->isMoving && window->dxContext) {
                    // This will cause the main render loop to do a full redraw
                    static_cast<DX10Context*>(window->dxContext)->setWindowMoving(false);
                }
                return 0;
            }
            default:
                break;
            }
        }

        return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    }

private:
    HWND hwnd;
    HINSTANCE hInstance;
    bool is_window_active;
    int width;
    int height;
    std::string title;
    bool should_close;
    bool isMoving = false;
    float tabButtonWidths;
    DX10Context* dxContext;
    bool pendingResize = false;
    UINT pendingResizeWidth = 0;
    UINT pendingResizeHeight = 0;

    // Borderless window specific
    bool borderless;
    bool borderless_shadow;
    bool borderless_drag;
    bool borderless_resize;

    // Additional methods and members
    enum class Style : DWORD
    {
        windowed         = WS_OVERLAPPEDWINDOW | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
        aero_borderless  = WS_POPUP | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX,
        basic_borderless = WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX
    };

    static bool maximized(HWND hwnd)
    {
        WINDOWPLACEMENT placement;
        if (!::GetWindowPlacement(hwnd, &placement)) {
            return false;
        }
        return placement.showCmd == SW_MAXIMIZE;
    }

    static void adjust_maximized_client_rect(HWND window, RECT& rect)
    {
        if (!maximized(window)) {
            return;
        }

        auto monitor = ::MonitorFromWindow(window, MONITOR_DEFAULTTONULL);
        if (!monitor) {
            return;
        }

        MONITORINFO monitor_info{};
        monitor_info.cbSize = sizeof(monitor_info);
        if (!::GetMonitorInfoW(monitor, &monitor_info)) {
            return;
        }

        rect = monitor_info.rcWork;
    }

    static std::system_error last_error(const std::string& message)
    {
        return std::system_error(
            std::error_code(::GetLastError(), std::system_category()),
            message
        );
    }

    static const wchar_t* window_class(WNDPROC wndproc, HINSTANCE hInstance)
    {
        static const wchar_t* window_class_name = [&] {
            WNDCLASSEXW wcx{};
            wcx.cbSize = sizeof(wcx);
            wcx.style = CS_HREDRAW | CS_VREDRAW;
            wcx.hInstance = hInstance;
            wcx.lpfnWndProc = wndproc;
            wcx.lpszClassName = L"BorderlessWindowClass";
            wcx.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            wcx.hCursor = ::LoadCursorW(hInstance, IDC_ARROW);
            const ATOM result = ::RegisterClassExW(&wcx);
            if (!result) {
                throw last_error("failed to register window class");
            }
            return wcx.lpszClassName;
            }();
        return window_class_name;
    }

    static bool composition_enabled()
    {
        BOOL composition_enabled = FALSE;
        bool success = ::DwmIsCompositionEnabled(&composition_enabled) == S_OK;
        return composition_enabled && success;
    }

    static Style select_borderless_style()
    {
        return composition_enabled() ? Style::aero_borderless : Style::basic_borderless;
    }

    static void set_shadow(HWND handle, bool enabled)
    {
        if (composition_enabled()) {
            static const MARGINS shadow_state[2]{ { 0,0,0,0 },{ 1,1,1,1 } };
            ::DwmExtendFrameIntoClientArea(handle, &shadow_state[enabled]);
        }
    }

    static HWND create_window(WNDPROC wndproc, HINSTANCE hInstance, void* userdata)
    {
        auto handle = CreateWindowExW(
            0, window_class(wndproc, hInstance), L"Kolosal AI",
            static_cast<DWORD>(Style::aero_borderless), CW_USEDEFAULT, CW_USEDEFAULT,
            1280, 720, nullptr, nullptr, hInstance, userdata
        );
        if (!handle) {
            throw last_error("failed to create window");
        }
        return handle;
    }

    void set_borderless(bool enabled)
    {
        Style new_style = (enabled) ? select_borderless_style() : Style::windowed;
        Style old_style = static_cast<Style>(::GetWindowLongPtrW(hwnd, GWL_STYLE));

        if (new_style != old_style) {
            borderless = enabled;

            ::SetWindowLongPtrW(hwnd, GWL_STYLE, static_cast<LONG>(new_style));

            set_shadow(hwnd, borderless_shadow && (new_style != Style::windowed));

            ::SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE);
            ::ShowWindow(hwnd, SW_SHOW);
        }
    }

    void set_borderless_shadow(bool enabled)
    {
        if (borderless) {
            borderless_shadow = enabled;
            set_shadow(hwnd, enabled);
        }
    }

    LRESULT hit_test(POINT cursor) const
    {
        const POINT border{
            ::GetSystemMetrics(SM_CXFRAME) + ::GetSystemMetrics(SM_CXPADDEDBORDER),
            ::GetSystemMetrics(SM_CYFRAME) + ::GetSystemMetrics(SM_CXPADDEDBORDER)
        };
        RECT window;
        if (!::GetWindowRect(hwnd, &window)) {
            return HTNOWHERE;
        }

        if ((cursor.y >= window.top && cursor.y < window.top + Config::TITLE_BAR_HEIGHT) &&
            ((cursor.x <= window.right - 45 * 3 && cursor.x >= window.left + /* logo width */ 40 + /* gap between logo and tab buttons */ 16 + this->tabButtonWidths) ||
                cursor.x <= window.left + /* logo width */ 40 + /* gap between logo and tab buttons */ 16)) {
            return HTCAPTION;
        }

        const auto drag = HTCLIENT;

        enum region_mask {
            client = 0b0000,
            left = 0b0001,
            right = 0b0010,
            top = 0b0100,
            bottom = 0b1000,
        };

        const auto result =
            left * (cursor.x < (window.left + border.x)) |
            right * (cursor.x >= (window.right - border.x)) |
            top * (cursor.y < (window.top + border.y)) |
            bottom * (cursor.y >= (window.bottom - border.y));

        switch (result) {
        case left: return borderless_resize ? HTLEFT : HTCLIENT;
        case right: return borderless_resize ? HTRIGHT : HTCLIENT;
        case top: return borderless_resize ? HTTOP : HTCLIENT;
        case bottom: return borderless_resize ? HTBOTTOM : HTCLIENT;
        case top | left: return borderless_resize ? HTTOPLEFT : HTCLIENT;
        case top | right: return borderless_resize ? HTTOPRIGHT : HTCLIENT;
        case bottom | left: return borderless_resize ? HTBOTTOMLEFT : HTCLIENT;
        case bottom | right: return borderless_resize ? HTBOTTOMRIGHT : HTCLIENT;
        case client: return HTCLIENT;
        default: return HTNOWHERE;
        }
    }
};