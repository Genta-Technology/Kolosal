#pragma once

#include "graphics_context.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d10_1.h>
#include <d3d10.h>
#include <dxgi.h>
#include <stdexcept>

class DX10Context : public GraphicsContext {
public:
    DX10Context() :
        device(nullptr),
        swapChain(nullptr),
        mainRenderTargetView(nullptr),
        swapChainOccluded(false),
        isWindowMoving(false),
        lastRenderTime(0) {
    }

    ~DX10Context() {
        cleanup();
    }

    void initialize(void* nativeWindowHandle) override {
        HWND hwnd = static_cast<HWND>(nativeWindowHandle);

        // Setup swap chain
        DXGI_SWAP_CHAIN_DESC sd;
        ZeroMemory(&sd, sizeof(sd));
        sd.BufferCount = 2;
        sd.BufferDesc.Width = 0;
        sd.BufferDesc.Height = 0;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferDesc.RefreshRate.Numerator = 60;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        UINT createDeviceFlags = 0;
        //createDeviceFlags |= D3D10_CREATE_DEVICE_DEBUG;
        HRESULT res = D3D10CreateDeviceAndSwapChain(nullptr, D3D10_DRIVER_TYPE_HARDWARE, nullptr,
            createDeviceFlags, D3D10_SDK_VERSION, &sd, &swapChain, &device);

        if (res == DXGI_ERROR_UNSUPPORTED) {
            // Try high-performance WARP software driver if hardware is not available
            res = D3D10CreateDeviceAndSwapChain(nullptr, D3D10_DRIVER_TYPE_WARP, nullptr,
                createDeviceFlags, D3D10_SDK_VERSION, &sd, &swapChain, &device);
        }

        if (res != S_OK) {
            throw std::runtime_error("Failed to create DirectX 10 device and swap chain");
        }

        // Disable Alt+Enter fullscreen toggle - can cause issues with custom window
        IDXGIFactory* factory = nullptr;
        if (SUCCEEDED(swapChain->GetParent(IID_PPV_ARGS(&factory)))) {
            factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
            factory->Release();
        }

        createRenderTarget();
    }

    void swapBuffers() override {
        // During window movement, we'll still render but at a reduced rate
        // This prevents freezing while still updating the window contents
        DWORD currentTime = GetTickCount();

        if (isWindowMoving) {
            // Only render every 33ms (approx. 30fps) during window movement
            if (currentTime - lastRenderTime < 33) {
                return;
            }
        }

        lastRenderTime = currentTime;

        // Use DXGI_PRESENT_DO_NOT_WAIT to prevent blocking
        UINT presentFlags = isWindowMoving ? DXGI_PRESENT_DO_NOT_WAIT : 0;

        // Present with vsync (1) and handle window occlusion
        HRESULT hr = swapChain->Present(isWindowMoving ? 0 : 1, presentFlags);

        // If the present operation was dropped due to being non-blocking,
        // that's acceptable during window movement
        if (hr == DXGI_ERROR_WAS_STILL_DRAWING && isWindowMoving) {
            hr = S_OK;
        }

        // Check if the window is occluded (minimized, etc)
        swapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);

        // If occluded, we can sleep a bit to reduce CPU usage
        if (swapChainOccluded) {
            Sleep(10);
        }
    }

    // Set window movement state to reduce rendering during window movement
    void setWindowMoving(bool moving) {
        isWindowMoving = moving;

        // Force an immediate render when movement stops to ensure window is fully redrawn
        if (!moving) {
            lastRenderTime = 0;
        }
    }

    // DirectX-specific methods
    ID3D10Device* getDevice() { return device; }
    ID3D10RenderTargetView* getRenderTargetView() { return mainRenderTargetView; }

    void resizeBuffers(UINT width, UINT height) {
        if (width == 0 || height == 0 || !device || !swapChain)
            return;

        cleanupRenderTarget();

        HRESULT hr = swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
        if (SUCCEEDED(hr)) {
            createRenderTarget();
        }
    }

private:
    ID3D10Device* device;
    IDXGISwapChain* swapChain;
    ID3D10RenderTargetView* mainRenderTargetView;
    DWORD lastRenderTime;
    bool swapChainOccluded;
    bool isWindowMoving;

    void createRenderTarget() {
        ID3D10Texture2D* pBackBuffer = nullptr;
        if (SUCCEEDED(swapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer)))) {
            device->CreateRenderTargetView(pBackBuffer, nullptr, &mainRenderTargetView);
            pBackBuffer->Release();
        }
    }

    void cleanupRenderTarget() {
        if (mainRenderTargetView) {
            mainRenderTargetView->Release();
            mainRenderTargetView = nullptr;
        }
    }

    void cleanup() {
        cleanupRenderTarget();
        if (swapChain) {
            swapChain->Release();
            swapChain = nullptr;
        }
        if (device) {
            device->Release();
            device = nullptr;
        }
    }
};