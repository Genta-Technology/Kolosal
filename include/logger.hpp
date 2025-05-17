#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <mutex>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <map>
#include <vector>
#include <memory>
#include <atomic>
#include <windows.h>
#include "wintoastlib.h" // Include WinToast library

namespace Logging {

    /**
     * @enum LogLevel
     * @brief Defines the severity levels for logging
     */
    enum class LogLevel {
        APP_TRACE,
        APP_DEBUG,
        APP_INFO,
        APP_WARNING,
        APP_ERROR,
        APP_FATAL,
        APP_OFF
    };

    /**
     * @brief Convert LogLevel to string representation
     */
    static std::string LogLevelToString(LogLevel level) {
        static const std::map<LogLevel, std::string> logLevelMap = {
            {LogLevel::APP_TRACE, "TRACE"},
            {LogLevel::APP_DEBUG, "DEBUG"},
            {LogLevel::APP_INFO, "INFO"},
            {LogLevel::APP_WARNING, "WARNING"},
            {LogLevel::APP_ERROR, "ERROR"},
            {LogLevel::APP_FATAL, "FATAL"},
            {LogLevel::APP_OFF, "OFF"}
        };

        auto it = logLevelMap.find(level);
        return it != logLevelMap.end() ? it->second : "UNKNOWN";
    }

    /**
     * @brief Convert string to LogLevel
     */
    static LogLevel StringToLogLevel(const std::string& levelStr) {
        static const std::map<std::string, LogLevel> stringLevelMap = {
            {"TRACE", LogLevel::APP_TRACE},
            {"DEBUG", LogLevel::APP_DEBUG},
            {"INFO", LogLevel::APP_INFO},
            {"WARNING", LogLevel::APP_WARNING},
            {"ERROR", LogLevel::APP_ERROR},
            {"FATAL", LogLevel::APP_FATAL},
            {"OFF", LogLevel::APP_OFF}
        };

        auto it = stringLevelMap.find(levelStr);
        return it != stringLevelMap.end() ? it->second : LogLevel::APP_INFO;
    }

    /**
     * @brief Convert std::wstring to std::string
     */
    static std::string WStringToString(const std::wstring& wstr) {
        if (wstr.empty()) return std::string();

        // Determine required size
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(),
            static_cast<int>(wstr.length()),
            nullptr, 0, nullptr, nullptr);

        // Allocate and convert
        std::string str(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.length()),
            &str[0], size_needed, nullptr, nullptr);

        return str;
    }

    /**
     * @brief Convert std::string to std::wstring
     */
    static std::wstring StringToWString(const std::string& str) {
        if (str.empty()) return std::wstring();

        // Determine required size
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(),
            static_cast<int>(str.length()),
            nullptr, 0);

        // Allocate and convert
        std::wstring wstr(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()),
            &wstr[0], size_needed);

        return wstr;
    }

    /**
     * @class LogSink
     * @brief Abstract base class for log destinations
     */
    class LogSink {
    public:
        virtual ~LogSink() = default;
        virtual void Write(LogLevel level, const std::string& message) = 0;
    };

    /**
     * @class ConsoleSink
     * @brief Log sink that writes to console/terminal
     */
    class ConsoleSink : public LogSink {
    public:
        ConsoleSink(bool colorOutput = true) : m_colorOutput(colorOutput) {}

        void Write(LogLevel level, const std::string& message) override {
            std::lock_guard<std::mutex> lock(m_mutex);

            if (m_colorOutput) {
                std::cout << GetColorCode(level) << message << "\033[0m" << std::endl;
            }
            else {
                std::cout << message << std::endl;
            }
        }

    private:
        std::mutex m_mutex;
        bool m_colorOutput;

        std::string GetColorCode(LogLevel level) {
            switch (level) {
            case LogLevel::APP_TRACE:   return "\033[37m"; // White
            case LogLevel::APP_DEBUG:   return "\033[36m"; // Cyan
            case LogLevel::APP_INFO:    return "\033[32m"; // Green
            case LogLevel::APP_WARNING: return "\033[33m"; // Yellow
            case LogLevel::APP_ERROR:   return "\033[31m"; // Red
            case LogLevel::APP_FATAL:   return "\033[35m"; // Magenta
            default:                    return "\033[0m";  // Reset
            }
        }
    };

    /**
     * @class FileSink
     * @brief Log sink that writes to a file
     */
    class FileSink : public LogSink {
    public:
        FileSink(const std::string& filename) {
            m_fileStream.open(filename, std::ios::out | std::ios::app);
            if (!m_fileStream.is_open()) {
                throw std::runtime_error("Failed to open log file: " + filename);
            }
        }

        ~FileSink() {
            if (m_fileStream.is_open()) {
                m_fileStream.close();
            }
        }

        void Write(LogLevel level, const std::string& message) override {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_fileStream.is_open()) {
                m_fileStream << message << std::endl;
                m_fileStream.flush();
            }
        }

    private:
        std::ofstream m_fileStream;
        std::mutex m_mutex;
    };

    /**
     * @class ToastNotificationHandler
     * @brief Handler for toast notifications
     * Note: WinToast takes ownership and will delete this object
     */
    class ToastNotificationHandler : public WinToastLib::IWinToastHandler {
    public:
        void toastActivated() const override {}
        void toastActivated(int actionIndex) const override {}
        void toastActivated(std::wstring response) const override {}
        void toastDismissed(WinToastDismissalReason state) const override {}
        void toastFailed() const override {}
    };

    /**
     * @class ToastSink
     * @brief Log sink that displays Windows toast notifications for info, warnings, errors and fatal logs
     */
    class ToastSink : public LogSink {
    public:
        ToastSink(const std::string& appName = "Logger", const std::string& companyName = "MyCompany")
            : m_appName(appName), m_companyName(companyName) {
            // Initialize WinToast
            WinToastLib::WinToast::instance()->setAppName(StringToWString(appName));
            WinToastLib::WinToast::instance()->setAppUserModelId(
                WinToastLib::WinToast::configureAUMI(
                    StringToWString(companyName),
                    StringToWString(appName)
                )
            );

            // Initialize WinToast
            WinToastLib::WinToast::WinToastError error;
            m_initialized = WinToastLib::WinToast::instance()->initialize(&error);

            if (!m_initialized) {
                std::string errorMsg = WStringToString(WinToastLib::WinToast::strerror(error));
                std::cerr << "Error initializing WinToast: " << errorMsg << std::endl;
            }
        }

        void Write(LogLevel level, const std::string& message) override {
            // Only show notifications for INFO, WARNING, ERROR, and FATAL
            if (!m_initialized || (level != LogLevel::APP_INFO &&
                level != LogLevel::APP_WARNING &&
                level != LogLevel::APP_ERROR &&
                level != LogLevel::APP_FATAL)) {
                return;
            }

            std::lock_guard<std::mutex> lock(m_mutex);

            // Create and configure the toast template
            WinToastLib::WinToastTemplate templ(WinToastLib::WinToastTemplate::Text02);

            // Extract the main message part without timestamp or log level
            std::string mainMessage = ExtractMainMessage(message);

            // Set the message directly without adding log level to title
            templ.setFirstLine(StringToWString(mainMessage));

            // Audio settings based on severity
            switch (level) {
            case LogLevel::APP_INFO:
                templ.setAudioOption(WinToastLib::WinToastTemplate::AudioOption::Silent);
                templ.setDuration(WinToastLib::WinToastTemplate::Duration::Short);
                break;
            case LogLevel::APP_WARNING:
                templ.setAudioPath(WinToastLib::WinToastTemplate::AudioSystemFile::Reminder);
                templ.setDuration(WinToastLib::WinToastTemplate::Duration::Short);
                break;
            case LogLevel::APP_ERROR:
                templ.setAudioPath(WinToastLib::WinToastTemplate::AudioSystemFile::SMS);
                templ.setDuration(WinToastLib::WinToastTemplate::Duration::Short);
                break;
            case LogLevel::APP_FATAL:
                templ.setAudioPath(WinToastLib::WinToastTemplate::AudioSystemFile::Alarm);
                templ.setDuration(WinToastLib::WinToastTemplate::Duration::Long);
                break;
            default:
                break;
            }

            // Create a new handler (WinToast takes ownership and will delete it)
            WinToastLib::WinToast::WinToastError error;
            WinToastLib::WinToast::instance()->showToast(templ, new ToastNotificationHandler(), &error);

            if (error != WinToastLib::WinToast::WinToastError::NoError) {
                std::string errorMsg = WStringToString(WinToastLib::WinToast::strerror(error));
                std::cerr << "Error showing toast notification: " << errorMsg << std::endl;
            }
        }

    private:
        std::mutex m_mutex;
        bool m_initialized = false;
        std::string m_appName;
        std::string m_companyName;

        // Extract the main message from a formatted log message
        std::string ExtractMainMessage(const std::string& formattedMessage) {
            // If the message format is "[%T] [%L] %M", we want just the %M part
            size_t lastBracketPos = formattedMessage.rfind("]");
            if (lastBracketPos != std::string::npos && lastBracketPos + 2 < formattedMessage.length()) {
                return formattedMessage.substr(lastBracketPos + 2);
            }
            return formattedMessage; // Return the whole message if we can't extract
        }
    };

    /**
     * @class Logger
     * @brief Main logger class
     */
    class Logger {
    public:
        /**
         * @brief Get the singleton instance of Logger
         */
        static Logger& Instance() {
            static Logger instance;
            return instance;
        }

        /**
         * @brief Set the global minimum log level
         */
        void SetLevel(LogLevel level) {
            m_logLevel.store(level);
        }

        /**
         * @brief Get the current log level
         */
        LogLevel GetLevel() const {
            return m_logLevel.load();
        }

        /**
         * @brief Add a sink to receive log messages
         */
        void AddSink(std::shared_ptr<LogSink> sink) {
            std::lock_guard<std::mutex> lock(m_sinkMutex);
            m_sinks.push_back(sink);
        }

        /**
         * @brief Clear all sinks
         */
        void ClearSinks() {
            std::lock_guard<std::mutex> lock(m_sinkMutex);
            m_sinks.clear();
        }

        /**
         * @brief Set the format for log messages
         *
         * Placeholders:
         * %L - Log level
         * %T - Timestamp
         * %F - Source file
         * %l - Line number
         * %M - Message
         */
        void SetFormat(const std::string& format) {
            std::lock_guard<std::mutex> lock(m_formatMutex);
            m_format = format;
        }

        /**
         * @brief Log a message with specified level, file, and line
         */
        void Log(LogLevel level, const std::string& msg,
            const std::string& file = "", int line = 0) {
            if (level < m_logLevel.load()) {
                return;
            }

            std::string formattedMessage = FormatMessage(level, msg, file, line);

            std::lock_guard<std::mutex> lock(m_sinkMutex);
            for (auto& sink : m_sinks) {
                sink->Write(level, formattedMessage);
            }
        }

        // Convenience methods for different log levels
        void Trace(const std::string& msg, const std::string& file = "", int line = 0) {
            Log(LogLevel::APP_TRACE, msg, file, line);
        }

        void Debug(const std::string& msg, const std::string& file = "", int line = 0) {
            Log(LogLevel::APP_DEBUG, msg, file, line);
        }

        void Info(const std::string& msg, const std::string& file = "", int line = 0) {
            Log(LogLevel::APP_INFO, msg, file, line);
        }

        void Warning(const std::string& msg, const std::string& file = "", int line = 0) {
            Log(LogLevel::APP_WARNING, msg, file, line);
        }

        void Error(const std::string& msg, const std::string& file = "", int line = 0) {
            Log(LogLevel::APP_ERROR, msg, file, line);
        }

        void Fatal(const std::string& msg, const std::string& file = "", int line = 0) {
            Log(LogLevel::APP_FATAL, msg, file, line);
        }

    private:
        // Private constructor for singleton
        Logger() : m_logLevel(LogLevel::APP_INFO) {
            m_format = "[%T] [%L] %M"; // Default format
        }

        // Non-copyable
        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;

        /**
         * @brief Format a log message according to the format string
         */
        std::string FormatMessage(LogLevel level, const std::string& msg,
            const std::string& file, int line) {
            std::lock_guard<std::mutex> lock(m_formatMutex);
            std::string result = m_format;

            // Replace placeholders
            size_t pos;

            // %L - Log level
            while ((pos = result.find("%L")) != std::string::npos) {
                result.replace(pos, 2, LogLevelToString(level));
            }

            // %T - Timestamp
            while ((pos = result.find("%T")) != std::string::npos) {
                result.replace(pos, 2, CurrentTimestamp());
            }

            // %F - Source file
            while ((pos = result.find("%F")) != std::string::npos) {
                result.replace(pos, 2, file);
            }

            // %l - Line number
            while ((pos = result.find("%l")) != std::string::npos) {
                result.replace(pos, 2, std::to_string(line));
            }

            // %M - Message
            while ((pos = result.find("%M")) != std::string::npos) {
                result.replace(pos, 2, msg);
            }

            return result;
        }

        /**
         * @brief Get current timestamp as string
         */
        std::string CurrentTimestamp() {
            auto now = std::chrono::system_clock::now();
            auto time_t_now = std::chrono::system_clock::to_time_t(now);
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) % 1000;

            std::stringstream ss;
            ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S");
            ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
            return ss.str();
        }

        std::atomic<LogLevel> m_logLevel;
        std::string m_format;
        std::vector<std::shared_ptr<LogSink>> m_sinks;
        std::mutex m_sinkMutex;
        std::mutex m_formatMutex;
    };

} // namespace Logging

// Fixed macros that properly handle expressions
#define LOG_TRACE(msg) Logging::Logger::Instance().Trace((msg), __FILE__, __LINE__)
#define LOG_DEBUG(msg) Logging::Logger::Instance().Debug((msg), __FILE__, __LINE__)
#define LOG_INFO(msg) Logging::Logger::Instance().Info((msg), __FILE__, __LINE__)
#define LOG_WARNING(msg) Logging::Logger::Instance().Warning((msg), __FILE__, __LINE__)
#define LOG_ERROR(msg) Logging::Logger::Instance().Error((msg), __FILE__, __LINE__)
#define LOG_FATAL(msg) Logging::Logger::Instance().Fatal((msg), __FILE__, __LINE__)

// Initialize logger with console output
#define INIT_LOGGER_CONSOLE() do { \
    auto console = std::make_shared<Logging::ConsoleSink>(); \
    Logging::Logger::Instance().AddSink(console); \
} while(0)

// Initialize logger with file output
#define INIT_LOGGER_FILE(filename) do { \
    try { \
        auto file = std::make_shared<Logging::FileSink>(filename); \
        Logging::Logger::Instance().AddSink(file); \
    } catch (const std::exception& e) { \
        std::cerr << "Failed to initialize logger file: " << e.what() << std::endl; \
    } \
} while(0)

// Initialize logger with toast notifications
#define INIT_LOGGER_TOAST(appName, companyName) do { \
    try { \
        auto toast = std::make_shared<Logging::ToastSink>(appName, companyName); \
        Logging::Logger::Instance().AddSink(toast); \
    } catch (const std::exception& e) { \
        std::cerr << "Failed to initialize toast notifications: " << e.what() << std::endl; \
    } \
} while(0)

// Initialize logger with toast notifications (with default app name)
#define INIT_LOGGER_TOAST_DEFAULT() do { \
    try { \
        auto toast = std::make_shared<Logging::ToastSink>(); \
        Logging::Logger::Instance().AddSink(toast); \
    } catch (const std::exception& e) { \
        std::cerr << "Failed to initialize toast notifications: " << e.what() << std::endl; \
    } \
} while(0)

#endif // LOGGER_HPP