#pragma once

#include <cstddef>
#include <string>
#include <memory>
#include <optional>
#include <chrono>
#include <mutex>
#include <iostream>
#include <atomic>
#include <vector>

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

// OpenGL includes
#include <glad/glad.h>

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

    // GPU Memory statistics
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

    // Initialize GPU monitoring using the application's existing OpenGL context
    void initializeOpenGL() {
        std::lock_guard<std::mutex> lock(m_gpuMutex);

        // Check if GLAD is initialized (OpenGL is working)
        if (!gladLoadGL()) {
            std::cerr << "[SystemMonitor] GPU monitoring failed: GLAD not loaded" << std::endl;
            return;
        }

        // Check for OpenGL version
        GLint majorVersion = 0, minorVersion = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &majorVersion);
        glGetIntegerv(GL_MINOR_VERSION, &minorVersion);

        if (glGetError() != GL_NO_ERROR || majorVersion < 3) {
            std::cerr << "[SystemMonitor] GPU monitoring requires OpenGL 3.0+ (detected "
                << majorVersion << "." << minorVersion << ")" << std::endl;
            return;
        }

        // Check for memory info extensions
        checkGLExtensions();

        if (!m_hasNvidiaExtension && !m_hasAmdExtension) {
            std::cout << "[SystemMonitor] No GPU memory extensions found, using estimated values" << std::endl;
        }

        m_openGLInitialized = true;

        // Get initial GPU memory values
        updateGpuStats();

        std::cout << "[SystemMonitor] GPU monitoring initialized successfully" << std::endl;
        std::cout << "[SystemMonitor] Total GPU Memory: " << (m_totalGpuMemory / (1024 * 1024))
            << " MB, Available: " << (m_availableGpuMemory / (1024 * 1024)) << " MB" << std::endl;
    }

    void initializeGpuMonitoring(const bool useGpu) {
        std::lock_guard<std::mutex> lock(m_gpuMutex);

        if (!m_openGLInitialized) {
            std::cerr << "[SystemMonitor] OpenGL context not initialized. Cannot monitor GPU." << std::endl;
            return;
        }

        m_gpuMonitoringSupported = true;
        // Initial query of GPU stats
        updateGpuStats();
    }

    // Calculate if there's enough memory to load a model
    bool hasEnoughMemoryForModel(size_t modelSizeBytes, size_t kvCacheSizeBytes) {
        // Update stats to get the latest values
        update();

        // Calculate total required memory
        size_t totalRequiredMemory = modelSizeBytes + kvCacheSizeBytes;

        // Add 20% overhead for safety margin
        totalRequiredMemory = static_cast<size_t>(totalRequiredMemory * 1.2);

        if (m_gpuMonitoringSupported) {
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

        // Only update every 500ms to avoid excessive CPU usage
        if (elapsed >= 500) {
            updateCpuUsage();
            updateMemoryStats();

            if (m_gpuMonitoringSupported && m_openGLInitialized) {
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

        // Initialize GPU monitoring flags
        m_hasNvidiaExtension = false;
        m_hasAmdExtension = false;
    }

    ~SystemMonitor() {
        // No cleanup needed as we don't own the OpenGL context
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
    bool m_openGLInitialized{ false };
    bool m_hasNvidiaExtension{ false };
    bool m_hasAmdExtension{ false };
    std::atomic<size_t> m_totalGpuMemory{ 0 };
    std::atomic<size_t> m_availableGpuMemory{ 0 };
    std::atomic<size_t> m_usedGpuMemory{ 0 };
    std::mutex m_gpuMutex;

    // Memory usage estimation for app
    std::atomic<size_t> m_lastKnownAppGpuUsage{ 0 };
    std::chrono::steady_clock::time_point m_lastUsageIncrease;

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

    // Check for available OpenGL extensions
    void checkGLExtensions() {
        // Reset extension flags
        m_hasNvidiaExtension = false;
        m_hasAmdExtension = false;

        // Check for supported extensions
        GLint numExtensions = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &numExtensions);

        if (glGetError() != GL_NO_ERROR || numExtensions <= 0) {
            return;
        }

        for (GLint i = 0; i < numExtensions; i++) {
            const GLubyte* extension = glGetStringi(GL_EXTENSIONS, i);
            if (!extension) continue;

            const char* extStr = reinterpret_cast<const char*>(extension);
            if (strcmp(extStr, "GL_NVX_gpu_memory_info") == 0) {
                m_hasNvidiaExtension = true;
            }
            else if (strcmp(extStr, "GL_ATI_meminfo") == 0) {
                m_hasAmdExtension = true;
            }
        }
    }

    void updateGpuStats() {
        if (!m_gpuMonitoringSupported || !m_openGLInitialized) return;

        // Define NVIDIA and AMD extension constants
        constexpr GLenum GL_GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX = 0x9047;
        constexpr GLenum GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX = 0x9049;
        constexpr GLenum GL_TEXTURE_FREE_MEMORY_ATI = 0x87FC;

        // Try NVIDIA extension first
        if (m_hasNvidiaExtension) {
            GLint totalMemKb = 0;
            glGetIntegerv(GL_GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX, &totalMemKb);

            if (glGetError() == GL_NO_ERROR && totalMemKb > 0) {
                m_totalGpuMemory = static_cast<size_t>(totalMemKb) * 1024; // Convert KB to bytes

                GLint availableMemKb = 0;
                glGetIntegerv(GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX, &availableMemKb);

                if (glGetError() == GL_NO_ERROR && availableMemKb > 0) {
                    m_availableGpuMemory = static_cast<size_t>(availableMemKb) * 1024; // Convert KB to bytes

                    // Calculate total used memory (by all applications)
                    size_t totalUsedMemory = m_totalGpuMemory > m_availableGpuMemory ?
                        m_totalGpuMemory - m_availableGpuMemory : 0;

                    // Update application GPU usage estimation based on system memory usage
                    estimateAppGpuUsage(totalUsedMemory);
                }
            }
        }
        // Try AMD extension if NVIDIA is not available
        else if (m_hasAmdExtension) {
            GLint info[4];
            glGetIntegerv(GL_TEXTURE_FREE_MEMORY_ATI, info);

            if (glGetError() == GL_NO_ERROR && info[0] > 0) {
                // info[0] is the available memory in KB
                m_availableGpuMemory = static_cast<size_t>(info[0]) * 1024; // Convert KB to bytes

                // AMD doesn't provide total memory directly, use estimation from GPU info
                if (m_totalGpuMemory == 0) {
                    // Try to detect the GPU memory size
                    const GLubyte* renderer = glGetString(GL_RENDERER);
                    if (renderer) {
                        std::string rendererStr(reinterpret_cast<const char*>(renderer));

                        // Very simple heuristic - can be expanded for better accuracy
                        if (rendererStr.find("Radeon") != std::string::npos) {
                            // Look for memory hints in the renderer string
                            if (rendererStr.find("8GB") != std::string::npos) {
                                m_totalGpuMemory = 8 * GB;
                            }
                            else if (rendererStr.find("6GB") != std::string::npos) {
                                m_totalGpuMemory = 6 * GB;
                            }
                            else if (rendererStr.find("4GB") != std::string::npos) {
                                m_totalGpuMemory = 4 * GB;
                            }
                            else {
                                // Default for modern AMD cards
                                m_totalGpuMemory = 8 * GB;
                            }
                        }
                        else {
                            // Generic default
                            m_totalGpuMemory = 4 * GB;
                        }
                    }
                    else {
                        // Fallback
                        m_totalGpuMemory = 4 * GB;
                    }
                }

                // Calculate total used memory
                size_t totalUsedMemory = m_totalGpuMemory > m_availableGpuMemory ?
                    m_totalGpuMemory - m_availableGpuMemory : 0;

                // Update application GPU usage estimation
                estimateAppGpuUsage(totalUsedMemory);
            }
        }
        else {
            // No GPU memory extension available, use hardware detection or fallbacks

            // Try to get GPU info from renderer string if we haven't set total memory yet
            if (m_totalGpuMemory == 0) {
                const GLubyte* renderer = glGetString(GL_RENDERER);
                if (renderer) {
                    std::string rendererStr(reinterpret_cast<const char*>(renderer));

                    // Simple heuristic based on GPU model name
                    if (rendererStr.find("RTX") != std::string::npos ||
                        rendererStr.find("Quadro") != std::string::npos) {
                        // Higher-end NVIDIA GPUs
                        m_totalGpuMemory = 8 * GB;
                    }
                    else if (rendererStr.find("GTX") != std::string::npos) {
                        // Mid-range NVIDIA GPUs
                        m_totalGpuMemory = 6 * GB;
                    }
                    else if (rendererStr.find("Radeon") != std::string::npos) {
                        // AMD GPUs
                        m_totalGpuMemory = 8 * GB;
                    }
                    else {
                        // Generic fallback
                        m_totalGpuMemory = 4 * GB;
                    }
                }
                else {
                    // Fallback if we can't detect
                    m_totalGpuMemory = 4 * GB;
                }
            }

            // Estimate available memory based on user memory usage
            float memoryUsagePercent = static_cast<float>(m_usedMemory) / static_cast<float>(m_totalMemory);
            m_availableGpuMemory = static_cast<size_t>(m_totalGpuMemory * (1.0f - (memoryUsagePercent * 0.5f)));

            // Estimate app GPU usage based on system memory usage
            m_usedGpuMemory = static_cast<size_t>(m_usedMemory * 0.7f); // Rough estimate

            // Make sure available + used memory is consistent
            if (m_usedGpuMemory > m_totalGpuMemory * 0.8f) {
                m_usedGpuMemory = static_cast<size_t>(m_totalGpuMemory * 0.8f);
            }

            if (m_usedGpuMemory + m_availableGpuMemory > m_totalGpuMemory) {
                m_availableGpuMemory = m_totalGpuMemory > m_usedGpuMemory ?
                    m_totalGpuMemory - m_usedGpuMemory : 0;
            }
        }
    }

    // Estimate the application's GPU memory usage based on system memory patterns
    void estimateAppGpuUsage(size_t totalUsedGpuMemory) {
        // Get current time
        auto currentTime = std::chrono::steady_clock::now();

        // Use system memory usage as a proxy for GPU usage trend
        static size_t lastSystemMemoryUsage = m_usedMemory;

        // Calculate memory usage delta
        bool memoryIncreased = (m_usedMemory > lastSystemMemoryUsage);
        size_t memoryDelta = memoryIncreased ?
            m_usedMemory - lastSystemMemoryUsage :
            lastSystemMemoryUsage - m_usedMemory;

        // If system memory increased significantly, assume GPU memory might have increased too
        if (memoryIncreased && memoryDelta > 10 * 1024 * 1024) { // 10MB threshold
            // Estimate that 70% of system memory increase affects GPU
            size_t estimatedGpuIncrease = static_cast<size_t>(memoryDelta * 0.7f);

            // Add to our tracked app usage, but cap at 80% of total GPU usage
            size_t maxAppUsage = static_cast<size_t>(totalUsedGpuMemory * 0.8f);
            m_usedGpuMemory += estimatedGpuIncrease;

            if (m_usedGpuMemory > maxAppUsage) {
                m_usedGpuMemory = maxAppUsage;
            }

            m_lastUsageIncrease = currentTime;
        }
        // If memory decreased significantly, reduce estimated GPU usage
        else if (!memoryIncreased && memoryDelta > 20 * 1024 * 1024) { // 20MB threshold
            // Reduce GPU usage estimate at a slower rate (50% of system memory decrease)
            size_t estimatedGpuDecrease = static_cast<size_t>(memoryDelta * 0.5f);

            // Ensure we don't go below a minimum level
            if (m_usedGpuMemory > estimatedGpuDecrease) {
                m_usedGpuMemory -= estimatedGpuDecrease;
            }
            else {
                // Minimum baseline app GPU usage (adjust based on your application)
                m_usedGpuMemory = std::min<size_t>(50 * 1024 * 1024, totalUsedGpuMemory * 0.1f);
            }
        }

        // If no significant activity for a long time, gradually reduce our estimation
        auto timeSinceLastIncrease = std::chrono::duration_cast<std::chrono::seconds>(
            currentTime - m_lastUsageIncrease).count();

        if (timeSinceLastIncrease > 30) { // 30 seconds of no increases
            // Slowly decay the estimated usage (5% every update)
            m_usedGpuMemory = static_cast<size_t>(m_usedGpuMemory * 0.95f);

            // Ensure we keep a minimum baseline
            size_t minBaseline = std::min<size_t>(50 * 1024 * 1024, totalUsedGpuMemory * 0.1f);
            if (m_usedGpuMemory < minBaseline) {
                m_usedGpuMemory = minBaseline;
            }
        }

        // Update for next comparison
        lastSystemMemoryUsage = m_usedMemory;
    }
};