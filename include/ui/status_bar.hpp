#pragma once

#include "../config.hpp"
#include "widgets.hpp"
#include "fonts.hpp"
#include <imgui.h>
#include <string>
#include <chrono>
#include <ctime>
#include <iomanip>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <lmcons.h>
#else
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>
#endif

class StatusBar {
public:
    StatusBar()
        : lastUpdateTime(std::chrono::steady_clock::now())
        , memoryUsageMB(0)
        , cpuUsage(0.0f)
        , updateInterval(1000)
    {
        // Get the username once during initialization
        getCurrentUsername();

        // Initialize CPU measurement data
#ifdef _WIN32
        ZeroMemory(&prevSysKernelTime, sizeof(FILETIME));
        ZeroMemory(&prevSysUserTime, sizeof(FILETIME));
        ZeroMemory(&prevProcKernelTime, sizeof(FILETIME));
        ZeroMemory(&prevProcUserTime, sizeof(FILETIME));

        // First call to CPU measurement (baseline)
        measureCpuUsage();
#endif

        updateMemoryUsage();
        updateCurrentTime();
    }

    void render() {
        ImGuiIO& io = ImGui::GetIO();

        // Only update metrics occasionally to reduce CPU impact
        auto currentTime = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastUpdateTime).count() > updateInterval) {
            measureCpuUsage();
            updateMemoryUsage();
            updateCurrentTime();
            lastUpdateTime = currentTime;
        }

        // Calculate status bar position and size
        ImVec2 windowPos(0, io.DisplaySize.y - Config::FOOTER_HEIGHT);
        ImVec2 windowSize(io.DisplaySize.x, Config::FOOTER_HEIGHT);

        // Begin a window with specific position and size
        ImGui::SetNextWindowPos(windowPos);
        ImGui::SetNextWindowSize(windowSize);

        // Status bar window flags
        ImGuiWindowFlags window_flags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus;

        // Minimal styling
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.1f, 0.4f));

        if (ImGui::Begin("##StatusBar", nullptr, window_flags)) {
            // Left side: Version and username
			LabelConfig versionLabel;
			versionLabel.id = "##versionLabel";
			versionLabel.label = "Version: " + std::string(APP_VERSION);
			versionLabel.size = ImVec2(200, 20);
			versionLabel.fontSize = FontsManager::SM;

            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 10);

			Label::render(versionLabel);

            ImGui::SameLine();

			std::stringstream ss;
            ss << std::fixed << std::setprecision(1) << cpuUsage;

			ButtonConfig cpuUsageLabel;
			cpuUsageLabel.id = "##cpuUsageLabel";
			cpuUsageLabel.label = "CPU: " + ss.str() + "%";
			cpuUsageLabel.size = ImVec2(100, 20);
            cpuUsageLabel.fontSize = FontsManager::SM;

			ButtonConfig memoryUsageLabel;
			memoryUsageLabel.id = "##memoryUsageLabel";
			memoryUsageLabel.label = "Memory: " + std::to_string(memoryUsageMB) + " MB";
			memoryUsageLabel.size = ImVec2(150, 20);
			memoryUsageLabel.fontSize = FontsManager::SM;

			Button::renderGroup({ cpuUsageLabel, memoryUsageLabel }, 
                ImGui::GetContentRegionAvail().x - 150, ImGui::GetCursorPosY() - 2, 0);
        }
        ImGui::End();

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }

private:
    std::chrono::steady_clock::time_point lastUpdateTime;
    size_t memoryUsageMB;
    float cpuUsage;
    int updateInterval;
    std::string username;
    char timeBuffer[64];

#ifdef _WIN32
    FILETIME prevSysKernelTime;
    FILETIME prevSysUserTime;
    FILETIME prevProcKernelTime;
    FILETIME prevProcUserTime;
#endif

    void getCurrentUsername() {
#ifdef _WIN32
        // Windows implementation
        char buffer[UNLEN + 1];
        DWORD size = UNLEN + 1;
        if (GetUserNameA(buffer, &size)) {
            username = buffer;
        }
        else {
            username = "unknown";
        }
#else
        // Unix/Linux/macOS implementation
        const char* user = getenv("USER");
        if (user != nullptr) {
            username = user;
        }
        else {
            // Fallback to getpwuid if USER environment variable is not available
            struct passwd* pwd = getpwuid(geteuid());
            if (pwd != nullptr) {
                username = pwd->pw_name;
            }
            else {
                username = "unknown";
            }
        }
#endif
    }

    void measureCpuUsage() {
#ifdef _WIN32
        FILETIME sysIdleTime, sysKernelTime, sysUserTime;
        FILETIME procCreationTime, procExitTime, procKernelTime, procUserTime;

        // Get system times
        if (!GetSystemTimes(&sysIdleTime, &sysKernelTime, &sysUserTime)) {
            return;
        }

        // Get process times
        HANDLE hProcess = GetCurrentProcess();
        if (!GetProcessTimes(hProcess, &procCreationTime, &procExitTime, &procKernelTime, &procUserTime)) {
            return;
        }

        // First call - just set the previous values and return
        if (prevSysKernelTime.dwLowDateTime == 0 && prevSysKernelTime.dwHighDateTime == 0) {
            prevSysKernelTime = sysKernelTime;
            prevSysUserTime = sysUserTime;
            prevProcKernelTime = procKernelTime;
            prevProcUserTime = procUserTime;
            return;
        }

        // Convert FILETIME to ULARGE_INTEGER for easier math
        ULARGE_INTEGER sysKernelTimeULI, sysUserTimeULI;
        ULARGE_INTEGER procKernelTimeULI, procUserTimeULI;
        ULARGE_INTEGER prevSysKernelTimeULI, prevSysUserTimeULI;
        ULARGE_INTEGER prevProcKernelTimeULI, prevProcUserTimeULI;

        sysKernelTimeULI.LowPart = sysKernelTime.dwLowDateTime;
        sysKernelTimeULI.HighPart = sysKernelTime.dwHighDateTime;
        sysUserTimeULI.LowPart = sysUserTime.dwLowDateTime;
        sysUserTimeULI.HighPart = sysUserTime.dwHighDateTime;

        procKernelTimeULI.LowPart = procKernelTime.dwLowDateTime;
        procKernelTimeULI.HighPart = procKernelTime.dwHighDateTime;
        procUserTimeULI.LowPart = procUserTime.dwLowDateTime;
        procUserTimeULI.HighPart = procUserTime.dwHighDateTime;

        prevSysKernelTimeULI.LowPart = prevSysKernelTime.dwLowDateTime;
        prevSysKernelTimeULI.HighPart = prevSysKernelTime.dwHighDateTime;
        prevSysUserTimeULI.LowPart = prevSysUserTime.dwLowDateTime;
        prevSysUserTimeULI.HighPart = prevSysUserTime.dwHighDateTime;

        prevProcKernelTimeULI.LowPart = prevProcKernelTime.dwLowDateTime;
        prevProcKernelTimeULI.HighPart = prevProcKernelTime.dwHighDateTime;
        prevProcUserTimeULI.LowPart = prevProcUserTime.dwLowDateTime;
        prevProcUserTimeULI.HighPart = prevProcUserTime.dwHighDateTime;

        // Calculate time differences
        ULONGLONG sysTimeChange = (sysKernelTimeULI.QuadPart - prevSysKernelTimeULI.QuadPart) +
            (sysUserTimeULI.QuadPart - prevSysUserTimeULI.QuadPart);

        ULONGLONG procTimeChange = (procKernelTimeULI.QuadPart - prevProcKernelTimeULI.QuadPart) +
            (procUserTimeULI.QuadPart - prevProcUserTimeULI.QuadPart);

        // Calculate CPU usage percentage
        if (sysTimeChange > 0) {
            // This gives us the percentage of system time that was spent in our process
            cpuUsage = (float)((100.0 * procTimeChange) / sysTimeChange);

            // Multiple core systems can exceed 100% - cap it
            if (cpuUsage > 100.0f) cpuUsage = 100.0f;
        }

        // Store current times for next calculation
        prevSysKernelTime = sysKernelTime;
        prevSysUserTime = sysUserTime;
        prevProcKernelTime = procKernelTime;
        prevProcUserTime = procUserTime;
#else
        // TODO: Implement for other OS
        cpuUsage = 0.0f;
#endif
    }

    void updateMemoryUsage() {
#ifdef _WIN32
        HANDLE hProcess = GetCurrentProcess();

        PROCESS_MEMORY_COUNTERS_EX pmc;
        if (GetProcessMemoryInfo(hProcess, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
            memoryUsageMB = pmc.PrivateUsage / (1024 * 1024);
        }
#else
		// TODO: Implement for other OS
        memoryUsageMB = 100; // Placeholder
#endif
    }

    void updateCurrentTime() {
        auto now = std::chrono::system_clock::now();
        auto now_time_t = std::chrono::system_clock::to_time_t(now);

        // Format time as UTC with the specified format
        std::tm tm_utc;
#ifdef _WIN32
        gmtime_s(&tm_utc, &now_time_t);
#else
        gmtime_r(&now_time_t, &tm_utc);
#endif

        std::stringstream ss;
        ss << std::put_time(&tm_utc, "%Y-%m-%d %H:%M:%S");
        std::string str = ss.str();

        // Copy to our buffer
        strncpy(timeBuffer, str.c_str(), sizeof(timeBuffer) - 1);
        timeBuffer[sizeof(timeBuffer) - 1] = '\0';
    }
};