#pragma once

#include <cstddef>
#include <string>
#include <memory>
#include <optional>
#include <chrono>
#include <mutex>
#include <iostream>
#include <atomic>

// Platform-specific includes
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
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

#ifdef __has_include
#  if __has_include(<vulkan/vulkan.h>)
#    include <vulkan/vulkan.h>
#    define HAS_VULKAN 1
#  endif
#endif

constexpr size_t GB = 1024 * 1024 * 1024;

class SystemMonitor {
public:
    static SystemMonitor& getInstance() {
        static SystemMonitor instance;
        return instance;
    }

    // CPU Memory statistics
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

    // GPU Memory statistics - with support for different backends
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

    // Initialize GPU monitoring for specific backend
    void initializeGpuMonitoring(bool isVulkanBackend) {
        std::lock_guard<std::mutex> lock(m_gpuMutex);
        m_isVulkanBackend = isVulkanBackend;
        m_gpuMonitoringSupported = true;

        // Initialize GPU stats immediately
        updateGpuStats();
    }

    // Calculate if there's enough memory to load a model
    bool hasEnoughMemoryForModel(size_t modelSizeBytes, size_t kvCacheSizeBytes, bool useGpu) {
        // Update stats to get the latest values
        update();

        // Calculate total required memory
        size_t totalRequiredMemory = modelSizeBytes + kvCacheSizeBytes;

        // Add 20% overhead for safety margin
        totalRequiredMemory = static_cast<size_t>(totalRequiredMemory * 1.2);

        if (useGpu && m_gpuMonitoringSupported) {
            // Check if GPU has enough memory
            if (m_availableGpuMemory < totalRequiredMemory) {
                return false;
            }
            return true;
        }
        else {
            // Check if system RAM has enough memory (threshold 2GB more)
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

        // Initialize memory stats
        updateMemoryStats();

        // Initialize CPU usage tracking
        updateCpuUsage();
    }
    ~SystemMonitor() {
#ifdef HAS_VULKAN
        if (m_vulkanInitialized) {
            if (m_vkDevice != VK_NULL_HANDLE) {
                vkDestroyDevice(m_vkDevice, nullptr);
            }

            if (m_vkInstance != VK_NULL_HANDLE) {
                vkDestroyInstance(m_vkInstance, nullptr);
            }
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
    bool m_isVulkanBackend{ false };
    std::atomic<size_t> m_totalGpuMemory{ 0 };
    std::atomic<size_t> m_availableGpuMemory{ 0 };
    std::atomic<size_t> m_usedGpuMemory{ 0 };
    std::mutex m_gpuMutex;

	// Vulkan-specific members
#ifdef HAS_VULKAN
    VkInstance m_vkInstance{ VK_NULL_HANDLE };
    VkPhysicalDevice m_vkPhysicalDevice{ VK_NULL_HANDLE };
    VkDevice m_vkDevice{ VK_NULL_HANDLE };
    bool m_vulkanInitialized{ false };
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

        // First call - just set the previous values and return
        if (m_prevSysKernelTime.dwLowDateTime == 0 && m_prevSysKernelTime.dwHighDateTime == 0) {
            m_prevSysKernelTime = sysKernelTime;
            m_prevSysUserTime = sysUserTime;
            m_prevProcKernelTime = procKernelTime;
            m_prevProcUserTime = procUserTime;
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

        // Calculate CPU usage percentage
        if (sysTimeChange > 0) {
            // This gives us the percentage of system time that was spent in our process
            m_cpuUsage = (float)((100.0 * procTimeChange) / sysTimeChange);

            // Multiple core systems can exceed 100% - cap it
            if (m_cpuUsage > 100.0f) m_cpuUsage = 100.0f;
        }

        // Store current times for next calculation
        m_prevSysKernelTime = sysKernelTime;
        m_prevSysUserTime = sysUserTime;
        m_prevProcKernelTime = procKernelTime;
        m_prevProcUserTime = procUserTime;
#else
        // Linux/Mac implementation
        m_cpuUsage = 0.0f; // Placeholder
#endif
    }
    void updateMemoryStats() {
#ifdef _WIN32
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        GlobalMemoryStatusEx(&memInfo);
        m_totalMemory = memInfo.ullTotalPhys;
        m_availableMemory = memInfo.ullAvailPhys;

        // Get process memory usage
        PROCESS_MEMORY_COUNTERS_EX pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
            m_usedMemory = pmc.PrivateUsage;
        }
#elif defined(__APPLE__)
        // macOS implementation
        mach_port_t host_port = mach_host_self();
        vm_size_t page_size;
        host_page_size(host_port, &page_size);

        vm_statistics64_data_t vm_stats;
        mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
        if (host_statistics64(host_port, HOST_VM_INFO64, (host_info64_t)&vm_stats, &count) == KERN_SUCCESS) {
            m_availableMemory = (vm_stats.free_count + vm_stats.inactive_count) * page_size;
        }

        // Total physical memory
        int mib[2] = { CTL_HW, HW_MEMSIZE };
        uint64_t total_memory = 0;
        size_t len = sizeof(total_memory);
        if (sysctl(mib, 2, &total_memory, &len, NULL, 0) == 0) {
            m_totalMemory = total_memory;
        }

        // Process memory usage - approximate with resident memory
        struct rusage usage;
        if (getrusage(RUSAGE_SELF, &usage) == 0) {
            m_usedMemory = usage.ru_maxrss * 1024; // Convert to bytes
        }
#else
        // Linux implementation
        struct sysinfo memInfo;
        if (sysinfo(&memInfo) == 0) {
            m_totalMemory = memInfo.totalram * memInfo.mem_unit;
            m_availableMemory = memInfo.freeram * memInfo.mem_unit;
        }

        // Get process memory usage from /proc/self/statm
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

    void SystemMonitor::updateGpuStats() {
        if (!m_gpuMonitoringSupported) return;

        if (m_isVulkanBackend) {
            updateVulkanGpuStats();
        }
        else {
            // Default to zero values if no specific backend is configured
            m_totalGpuMemory = 0;
            m_availableGpuMemory = 0;
            m_usedGpuMemory = 0;
        }
    }

    // Vulkan-specific monitoring methods
    void updateVulkanGpuStats() {
#ifdef HAS_VULKAN
        if (!m_vulkanInitialized) {
            initializeVulkan();
        }

        if (!m_vulkanInitialized || m_vkPhysicalDevice == VK_NULL_HANDLE) {
            // Fallback to default values if Vulkan initialization failed
            m_totalGpuMemory = 8 * 1024 * 1024 * 1024ULL;     // 8 GB example
            m_availableGpuMemory = 6 * 1024 * 1024 * 1024ULL; // 6 GB example
            m_usedGpuMemory = 2 * 1024 * 1024 * 1024ULL;      // 2 GB example
            return;
        }

        // Query device memory properties
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(m_vkPhysicalDevice, &memProperties);

        // Calculate total and used memory across all DEVICE_LOCAL heaps
        size_t totalMemory = 0;
        size_t usedMemory = 0;

        for (uint32_t i = 0; i < memProperties.memoryHeapCount; i++) {
            if (memProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
                totalMemory += memProperties.memoryHeaps[i].size;

                // We can't directly query memory usage without extensions
                // For simplicity, estimate 25% usage for device-local memory
                usedMemory += memProperties.memoryHeaps[i].size / 4;
            }
        }

        // Update atomic variables
        m_totalGpuMemory = totalMemory;
        m_usedGpuMemory = usedMemory;
        m_availableGpuMemory = totalMemory > usedMemory ? totalMemory - usedMemory : 0;

        std::cout << "[SystemMonitor] Vulkan GPU stats updated - Total: "
            << (m_totalGpuMemory / (1024 * 1024)) << " MB, Used: "
            << (m_usedGpuMemory / (1024 * 1024)) << " MB, Available: "
            << (m_availableGpuMemory / (1024 * 1024)) << " MB" << std::endl;
#else
        // No Vulkan support, use default values
        m_totalGpuMemory = 8 * 1024 * 1024 * 1024ULL;     // 8 GB example
        m_availableGpuMemory = 6 * 1024 * 1024 * 1024ULL; // 6 GB example
        m_usedGpuMemory = 2 * 1024 * 1024 * 1024ULL;      // 2 GB example
#endif
    }

    void initializeVulkan() {
#ifdef HAS_VULKAN
        try {
            // Create Vulkan instance
            VkApplicationInfo appInfo = {};
            appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            appInfo.pApplicationName = "System Monitor";
            appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
            appInfo.pEngineName = "No Engine";
            appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
            appInfo.apiVersion = VK_API_VERSION_1_0;

            // Just use basic instance creation, no problematic extensions
            VkInstanceCreateInfo createInfo = {};
            createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
            createInfo.pApplicationInfo = &appInfo;

            // Create Vulkan instance
            VkResult result = vkCreateInstance(&createInfo, nullptr, &m_vkInstance);
            if (result != VK_SUCCESS) {
                std::cerr << "[SystemMonitor] Failed to create Vulkan instance: " << result << std::endl;
                return;
            }

            // Find physical devices
            uint32_t deviceCount = 0;
            vkEnumeratePhysicalDevices(m_vkInstance, &deviceCount, nullptr);

            if (deviceCount == 0) {
                std::cerr << "[SystemMonitor] No Vulkan physical devices found" << std::endl;
                vkDestroyInstance(m_vkInstance, nullptr);
                m_vkInstance = VK_NULL_HANDLE;
                return;
            }

            std::vector<VkPhysicalDevice> devices(deviceCount);
            vkEnumeratePhysicalDevices(m_vkInstance, &deviceCount, devices.data());

            // Just use the first device for simplicity
            m_vkPhysicalDevice = devices[0];

            // Query device properties
            VkPhysicalDeviceProperties deviceProps;
            vkGetPhysicalDeviceProperties(m_vkPhysicalDevice, &deviceProps);

            std::cout << "[SystemMonitor] Using GPU: " << deviceProps.deviceName << std::endl;

            m_vulkanInitialized = true;
        }
        catch (const std::exception& e) {
            std::cerr << "[SystemMonitor] Vulkan initialization failed: " << e.what() << std::endl;

            // Clean up any resources
            if (m_vkInstance != VK_NULL_HANDLE) {
                vkDestroyInstance(m_vkInstance, nullptr);
                m_vkInstance = VK_NULL_HANDLE;
            }

            m_vulkanInitialized = false;
        }
#endif
    }
};