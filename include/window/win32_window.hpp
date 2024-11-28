﻿#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <glad/glad.h>
#include <string>
#include <system_error>
#include <dwmapi.h>
#include <stdexcept>
#include <memory>
#include <imgui_impl_win32.h>

#include "config.hpp"
#include "window.hpp"

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
        , borderless_shadow(true)
        , borderless_drag(false)
        , borderless_resize(true) {}

    ~Win32Window()
    {
        if (hwnd) {
            DestroyWindow(hwnd);
            hwnd = nullptr;
        }
    }

    void createWindow(int width, int height, const std::string& title) override
    {
        this->width = width;
        this->height = height;
        this->title = title;

        hwnd = create_window(&Win32Window::WndProc, hInstance, this);
        if (!hwnd) {
            throw std::runtime_error("Failed to create window");
        }

        set_borderless(borderless);
        set_borderless_shadow(borderless_shadow);
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
            case WM_CLOSE: {
                window->should_close = true;
                return 0;
            }
            case WM_DESTROY: {
                ::PostQuitMessage(0);
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
            (cursor.x <= window.right - 45 * 3)) {
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