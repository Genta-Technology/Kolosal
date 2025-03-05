#pragma once

#include <string>

class Window {
public:
    virtual ~Window() = default;
    virtual void createWindow(int width, int height, const std::string& title, const float tabButtonWidths) = 0;
    virtual void show() = 0;
    virtual void processEvents() = 0;
    virtual bool shouldClose() = 0;
    virtual void* getNativeHandle() = 0;
    virtual bool isActive() const = 0;
    virtual int getWidth() const = 0;
    virtual int getHeight() const = 0;
};