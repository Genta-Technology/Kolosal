#pragma once

#include <cstddef>
#include <string>
#include <memory>
#include <optional>
#include <chrono>
#include <mutex>
#include <iostream>
#include <atomic>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <dxgi1_4.h>      // For IDXGIAdapter3 and QueryVideoMemoryInfo
#pragma comment(lib, "dxgi.lib")
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/sysinfo.h>
#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/vm_statistics.h>
#include <mach/mach_types.h>
#include <mach/mach_init.h>
#include <mach/mach_host.h>
#endif
#endif

constexpr size_t GB = 1024 * 1024 * 1024;

class SystemMonitor {
public:
    static SystemMonitor& getInstance() {
        static SystemMonitor instance;
        return instance;
    }

    // CPU/Memory statistics
    size_t getTotalSystemMemory() {
        return m_totalMemory;
    }
    size_t getAvailableSystemMemory() {
        return m_availableMemory;
    }
    size_t getUsedMemoryByProcess() {
        return m_usedMemory;
    }
    float getCpuUsagePercentage() {
        return m_cpuUsage;
    }

    // GPU Memory statistics using DirectX (global memory, not per process)
    bool hasGpuSupport() const { return m_gpuMonitoringSupported; }
    size_t getTotalGpuMemory() {
        if (!m_gpuMonitoringSupported) return 0;
        return m_totalGpuMemory;
    }
    size_t getAvailableGpuMemory() {
        if (!m_gpuMonitoringSupported) return 0;
        return m_availableGpuMemory;
    }
    size_t getUsedGpuMemoryByProcess() {
        if (!m_gpuMonitoringSupported) return 0;
        return m_usedGpuMemory;
    }

    // Initialize GPU monitoring with DirectX backend (Windows only)
    void initializeGpuMonitoring() {
#ifdef _WIN32
        std::lock_guard<std::mutex> lock(m_gpuMutex);
        initializeDirectX();
#else
        m_gpuMonitoringSupported = false;
#endif
    }

    // Calculate if there's enough memory to load a model
    bool hasEnoughMemoryForModel(size_t modelSizeBytes, size_t kvCacheSizeBytes) {
        // Update stats to get the latest values
        update();

        // Calculate total required memory
        size_t totalRequiredMemory = modelSizeBytes + kvCacheSizeBytes;

        // Add 20% overhead for safety margin
        totalRequiredMemory = static_cast<size_t>(totalRequiredMemory);

        if (m_gpuMonitoringSupported) {
            // Check if GPU has enough available memory
            if (m_availableGpuMemory + GB < totalRequiredMemory) {
                return false;
            }
            return true;
        }
        else {
            // Check if system RAM has enough memory (threshold of 2GB more)
            if (m_availableMemory + 2 * GB < totalRequiredMemory) {
                return false;
            }
            return true;
        }
    }

    // Update monitoring state - call periodically
    void update() {
        auto currentTime = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            currentTime - m_lastCpuMeasurement).count();

        // Only update every 1000ms to avoid excessive CPU usage
        if (elapsed >= 1000) {
            updateCpuUsage();
            updateMemoryStats();

            if (m_gpuMonitoringSupported) {
                updateGpuStats();
            }

            m_lastCpuMeasurement = currentTime;
        }
    }

    const std::string getGpuName() const {
        return m_gpuName;
    }

private:
    SystemMonitor() : m_lastCpuMeasurement(std::chrono::steady_clock::now())
    {
#ifdef _WIN32
        ZeroMemory(&m_prevSysKernelTime, sizeof(FILETIME));
        ZeroMemory(&m_prevSysUserTime, sizeof(FILETIME));
        ZeroMemory(&m_prevProcKernelTime, sizeof(FILETIME));
        ZeroMemory(&m_prevProcUserTime, sizeof(FILETIME));
#else
        m_prevTotalUser = 0;
        m_prevTotalUserLow = 0;
        m_prevTotalSys = 0;
        m_prevTotalIdle = 0;
        m_prevProcessTotalUser = 0;
        m_prevProcessTotalSys = 0;
#endif

        // Initialize memory stats and CPU usage tracking
        updateMemoryStats();
        updateCpuUsage();
    }
    ~SystemMonitor() {
#ifdef _WIN32
        if (m_dxgiAdapter) {
            m_dxgiAdapter->Release();
            m_dxgiAdapter = nullptr;
        }
        if (m_dxgiFactory) {
            m_dxgiFactory->Release();
            m_dxgiFactory = nullptr;
        }
#endif
    }

    // CPU monitoring members
    std::atomic<float> m_cpuUsage{ 0.0f };
    std::atomic<size_t> m_usedMemory{ 0 };
    std::atomic<size_t> m_availableMemory{ 0 };
    std::atomic<size_t> m_totalMemory{ 0 };
    std::chrono::steady_clock::time_point m_lastCpuMeasurement;
    std::mutex m_cpuMutex;

#ifdef _WIN32
    FILETIME m_prevSysKernelTime;
    FILETIME m_prevSysUserTime;
    FILETIME m_prevProcKernelTime;
    FILETIME m_prevProcUserTime;
#else
    unsigned long long m_prevTotalUser;
    unsigned long long m_prevTotalUserLow;
    unsigned long long m_prevTotalSys;
    unsigned long long m_prevTotalIdle;
    unsigned long long m_prevProcessTotalUser;
    unsigned long long m_prevProcessTotalSys;
#endif

    // GPU monitoring members
    bool m_gpuMonitoringSupported{ false };
    std::string         m_gpuName;
    std::atomic<size_t> m_totalGpuMemory{ 0 };
    std::atomic<size_t> m_availableGpuMemory{ 0 };
    std::atomic<size_t> m_usedGpuMemory{ 0 };
    std::mutex m_gpuMutex;

#ifdef _WIN32
    // DirectX-specific members
    IDXGIFactory1* m_dxgiFactory{ nullptr };
    IDXGIAdapter3* m_dxgiAdapter{ nullptr };
#endif

    // Private helper methods

    void updateCpuUsage() {
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

        // First call - just store previous times and return
        if (m_prevSysKernelTime.dwLowDateTime == 0 && m_prevSysKernelTime.dwHighDateTime == 0) {
            m_prevSysKernelTime = sysKernelTime;
            m_prevSysUserTime = sysUserTime;
            m_prevProcKernelTime = procKernelTime;
            m_prevProcUserTime = procUserTime;
            return;
        }

        // Convert FILETIME to ULARGE_INTEGER for arithmetic
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

        prevSysKernelTimeULI.LowPart = m_prevSysKernelTime.dwLowDateTime;
        prevSysKernelTimeULI.HighPart = m_prevSysKernelTime.dwHighDateTime;
        prevSysUserTimeULI.LowPart = m_prevSysUserTime.dwLowDateTime;
        prevSysUserTimeULI.HighPart = m_prevSysUserTime.dwHighDateTime;

        prevProcKernelTimeULI.LowPart = m_prevProcKernelTime.dwLowDateTime;
        prevProcKernelTimeULI.HighPart = m_prevProcKernelTime.dwHighDateTime;
        prevProcUserTimeULI.LowPart = m_prevProcUserTime.dwLowDateTime;
        prevProcUserTimeULI.HighPart = m_prevProcUserTime.dwHighDateTime;

        // Calculate time differences
        ULONGLONG sysTimeChange = (sysKernelTimeULI.QuadPart - prevSysKernelTimeULI.QuadPart) +
            (sysUserTimeULI.QuadPart - prevSysUserTimeULI.QuadPart);

        ULONGLONG procTimeChange = (procKernelTimeULI.QuadPart - prevProcKernelTimeULI.QuadPart) +
            (procUserTimeULI.QuadPart - prevProcUserTimeULI.QuadPart);

        // Calculate CPU usage percentage for the process
        if (sysTimeChange > 0) {
            m_cpuUsage = (float)((100.0 * procTimeChange) / sysTimeChange);
            if (m_cpuUsage > 100.0f) m_cpuUsage = 100.0f;
        }

        // Store current times for next measurement
        m_prevSysKernelTime = sysKernelTime;
        m_prevSysUserTime = sysUserTime;
        m_prevProcKernelTime = procKernelTime;
        m_prevProcUserTime = procUserTime;
#else
        m_cpuUsage = 0.0f;
#endif
    }

    void updateMemoryStats() {
#ifdef _WIN32
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        GlobalMemoryStatusEx(&memInfo);
        m_totalMemory = memInfo.ullTotalPhys;
        m_availableMemory = memInfo.ullAvailPhys;

        PROCESS_MEMORY_COUNTERS_EX pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
            m_usedMemory = pmc.PrivateUsage;
        }
#elif defined(__APPLE__)
        mach_port_t host_port = mach_host_self();
        vm_size_t page_size;
        host_page_size(host_port, &page_size);

        vm_statistics64_data_t vm_stats;
        mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
        if (host_statistics64(host_port, HOST_VM_INFO64, (host_info64_t)&vm_stats, &count) == KERN_SUCCESS) {
            m_availableMemory = (vm_stats.free_count + vm_stats.inactive_count) * page_size;
        }

        int mib[2] = { CTL_HW, HW_MEMSIZE };
        uint64_t total_memory = 0;
        size_t len = sizeof(total_memory);
        if (sysctl(mib, 2, &total_memory, &len, NULL, 0) == 0) {
            m_totalMemory = total_memory;
        }

        struct rusage usage;
        if (getrusage(RUSAGE_SELF, &usage) == 0) {
            m_usedMemory = usage.ru_maxrss * 1024;
        }
#else
        struct sysinfo memInfo;
        if (sysinfo(&memInfo) == 0) {
            m_totalMemory = memInfo.totalram * memInfo.mem_unit;
            m_availableMemory = memInfo.freeram * memInfo.mem_unit;
        }
        FILE* fp = fopen("/proc/self/statm", "r");
        if (fp) {
            unsigned long vm = 0, rss = 0;
            if (fscanf(fp, "%lu %lu", &vm, &rss) == 2) {
                m_usedMemory = rss * sysconf(_SC_PAGESIZE);
            }
            fclose(fp);
        }
#endif
    }

    void updateGpuStats() {
#ifdef _WIN32
        if (m_gpuMonitoringSupported) {
            updateDirectXGpuStats();
        }
#else
        m_totalGpuMemory = 0;
        m_availableGpuMemory = 0;
        m_usedGpuMemory = 0;
#endif
    }

#ifdef _WIN32
    // DirectX (DXGI) GPU monitoring methods

    void initializeDirectX() {
        HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&m_dxgiFactory));
        if (FAILED(hr)) {
            std::cerr << "[SystemMonitor] Failed to create DXGI Factory" << std::endl;
            m_gpuMonitoringSupported = false;
            return;
        }

        // Enumerate all adapters and find the one with the highest memory
        IDXGIAdapter* adapter = nullptr;
        IDXGIAdapter3* highestMemoryAdapter = nullptr;
        size_t highestMemory = 0;

        for (UINT i = 0; m_dxgiFactory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++) {
            IDXGIAdapter3* adapter3 = nullptr;
            hr = adapter->QueryInterface(__uuidof(IDXGIAdapter3), reinterpret_cast<void**>(&adapter3));
            if (SUCCEEDED(hr) && adapter3) {
                DXGI_ADAPTER_DESC desc;
                hr = adapter3->GetDesc(&desc);
                if (SUCCEEDED(hr)) {
                    // Check the dedicated video memory
                    size_t adapterMemory = desc.DedicatedVideoMemory;

                    // Only consider adapters with at least 1GB of memory
                    if (adapterMemory >= GB) {
                        if (adapterMemory > highestMemory) {
                            highestMemory = adapterMemory;
                            if (highestMemoryAdapter) {
                                highestMemoryAdapter->Release();
                            }
                            highestMemoryAdapter = adapter3;
                        }
                        else {
                            adapter3->Release();
                        }
                    }
                    else {
                        adapter3->Release();
                    }
                }
            }
            adapter->Release();
        }

        if (!highestMemoryAdapter) {
            std::cerr << "[SystemMonitor] No suitable GPU found with at least 1GB of memory." << std::endl;
            m_gpuMonitoringSupported = false;
            return;
        }

        m_dxgiAdapter = highestMemoryAdapter;
        m_gpuMonitoringSupported = true;
        updateDirectXGpuStats();

        // Print GPU name and memory details
        DXGI_ADAPTER_DESC adapterDesc;
        hr = m_dxgiAdapter->GetDesc(&adapterDesc);
        if (SUCCEEDED(hr)) {
            std::wstring gpuName(adapterDesc.Description);
            m_gpuName = std::string(gpuName.begin(), gpuName.end());

            std::wcout << L"[SystemMonitor] Selected GPU: " << adapterDesc.Description << std::endl;
            std::wcout << L"[SystemMonitor] Total GPU Memory: " << (adapterDesc.DedicatedVideoMemory / GB) << L" GB" << std::endl;
            std::wcout << L"[SystemMonitor] Available GPU Memory: " << (m_availableGpuMemory / GB) << L" GB" << std::endl;
        }
        else {
            std::cerr << "[SystemMonitor] Failed to get GPU description." << std::endl;
        }
    }

    void updateDirectXGpuStats() {
        if (!m_dxgiAdapter)
            return;

        DXGI_QUERY_VIDEO_MEMORY_INFO videoMemoryInfo = {};
        HRESULT hr = m_dxgiAdapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &videoMemoryInfo);
        if (SUCCEEDED(hr)) {
            m_usedGpuMemory = videoMemoryInfo.CurrentUsage;

            DXGI_ADAPTER_DESC adapterDesc = {};
            hr = m_dxgiAdapter->GetDesc(&adapterDesc);
            if (SUCCEEDED(hr)) {
                m_totalGpuMemory = static_cast<size_t>(adapterDesc.DedicatedVideoMemory);
            }
            else {
                m_totalGpuMemory = videoMemoryInfo.Budget;
            }

            m_availableGpuMemory = (m_totalGpuMemory > m_usedGpuMemory) ?
                m_totalGpuMemory - m_usedGpuMemory : 0;
        }
    }
#endif
};
