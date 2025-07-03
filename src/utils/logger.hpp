// utils/logger.hpp
#pragma once

#include <cstdarg>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>

#include "config/system_config.hpp"

#ifndef LORAMESHER_BUILD_ARDUINO
#include <ctime>
#include <iostream>
#endif

namespace loramesher {

#ifndef LOGGER_DISABLE_COLORS

/**
 * @brief ANSI color codes for terminal output
 */
struct Colors {
    static constexpr const char* kReset = "\033[0m\r";
    static constexpr const char* kRed = "\033[31m";
    static constexpr const char* kGreen = "\033[32m";
    static constexpr const char* kYellow = "\033[33m";
    static constexpr const char* kBlue = "\033[34m";
    static constexpr const char* kMagenta = "\033[35m";
    static constexpr const char* kCyan = "\033[36m";
    static constexpr const char* kWhite = "\033[37m";
};

#endif  // LOGGER_DISABLE_COLORS

/**
 * @brief Enumeration for different logging levels.
 */
enum class LogLevel { kDebug, kInfo, kWarning, kError };

/**
 * @brief Abstract interface for log output handlers.
 */
class LogHandler {
   public:
    virtual ~LogHandler() = default;

    /**
     * @brief Write a log message.
     * @param level The severity level of the message.
     * @param message The message to be logged.
     */
    virtual void Write(LogLevel level, const std::string& message) = 0;

    /**
     * @brief Flushes any buffered log messages.
     * 
     * This ensures all pending log messages are written to the output device.
     */
    virtual void Flush() = 0;

   protected:
#ifndef LOGGER_DISABLE_COLORS
    /**
     * @brief Get color code for log level
     * @param level Log level
     * @return Color code string
     */
    const char* GetColorForLevel(LogLevel level) const {
        switch (level) {
            case LogLevel::kDebug:
                return Colors::kCyan;
            case LogLevel::kInfo:
                return Colors::kGreen;
            case LogLevel::kWarning:
                return Colors::kYellow;
            case LogLevel::kError:
                return Colors::kRed;
            default:
                return Colors::kWhite;
        }
    }
#endif  // LOGGER_DISABLE_COLORS
};

#ifdef LORAMESHER_BUILD_ARDUINO
/**
 * @brief Arduino-specific Serial output handler implementation.
 */
class SerialLogHandler : public LogHandler {
   public:
    /**
     * @brief Constructor that initializes Serial if not already done
     * @param baud_rate The baud rate for Serial communication
     */
    explicit SerialLogHandler(unsigned long baud_rate = 115200) {
        if (!Serial) {
            Serial.begin(baud_rate);
        }
    }

    void Write(LogLevel level, const std::string& message) override {
        const char* level_str;
        switch (level) {
            case LogLevel::kDebug:
                level_str = "DEBUG";
                break;
            case LogLevel::kInfo:
                level_str = "INFO";
                break;
            case LogLevel::kWarning:
                level_str = "WARNING";
                break;
            case LogLevel::kError:
                level_str = "ERROR";
                break;
        }

#ifndef LOGGER_DISABLE_COLORS
        const char* color = GetColorForLevel(level);
        Serial.print(color);
#endif
        Serial.print("[");
        Serial.print(level_str);
        Serial.print("] ");
        Serial.print(message.c_str());
#ifndef LOGGER_DISABLE_COLORS
        Serial.println(Colors::kReset);
#endif
    }

    void Flush() override {
        Serial.flush();  // This waits for TX buffer to be empty
    }
};

#else
/**
 * @brief Native console output handler implementation.
 */
class ConsoleLogHandler : public LogHandler {
   public:
    void Write(LogLevel level, const std::string& message) override {
        const char* level_str;
        switch (level) {
            case LogLevel::kDebug:
                level_str = "DEBUG";
                break;
            case LogLevel::kInfo:
                level_str = "INFO";
                break;
            case LogLevel::kWarning:
                level_str = "WARNING";
                break;
            case LogLevel::kError:
                level_str = "ERROR";
                break;
        }

#ifndef LOGGER_DISABLE_COLORS
        const char* color = GetColorForLevel(level);
        std::cout << color << " [" << level_str << "] " << message
                  << Colors::kReset << std::endl;
#else
        std::cout << " [" << level_str << "] " << message << std::endl;
#endif
    }

    void Flush() override { std::cout.flush(); }
};
#endif

/**
 * @brief Main logger class providing logging functionality.
 * 
 * This class implements a configurable logging system that can be used
 * across the library. It supports different log levels and custom handlers.
 */
class Logger {
   public:
    Logger();
    ~Logger() = default;

    /**
     * @brief Set the minimum log level to be processed.
     * @param level The minimum log level.
     */
    void SetLogLevel(LogLevel level) {
        std::lock_guard<std::mutex> lock(logger_mutex_);
        min_log_level_ = level;
    }

    /**
     * @brief Set a custom log handler.
     * @param handler Unique pointer to the log handler implementation.
     */
    void SetHandler(std::unique_ptr<LogHandler> handler) {
        std::lock_guard<std::mutex> lock(logger_mutex_);
        handler_ = std::move(handler);
    }

    /**
     * @brief Log a message with the specified level.
     * @param level The severity level of the message.
     * @param message The message to log.
     * @thread_safety Thread-safe
     */
    void Log(LogLevel level, const std::string& message) {
        LogMessage(level, message);
    }

    /**
     * @brief Log a formatted message with the specified level.
     * @param level The severity level of the message.
     * @param format The format string.
     * @param ... Variable arguments for formatting.
     * @thread_safety Thread-safe
     */
    void Log(LogLevel level, const char* format, ...) {
        static char buffer
            [LOGGER_BUFFER_SIZE];  // Static buffer to avoid stack allocations

        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        LogMessage(level, buffer);
    }

    /**
     * @brief Reset the logger state.
     * 
     * This method is used to reset the logger state, including the log level
     * and any other internal state. It is useful for testing and cleanup.
     */
    void Reset();

    /**
     * @brief Flushes all pending log messages.
     * 
     * This method ensures that all buffered log messages are written to 
     * the output device before returning. It's particularly useful before 
     * system resets or critical operations.
     * 
     * @thread_safety Thread-safe
     */
    void Flush() {
        std::lock_guard<std::mutex> lock(logger_mutex_);
        if (handler_) {
            handler_->Flush();
        }
    }

    // Convenience methods for different log levels with formatting
    template <typename... Args>
    void Debug(const char* format, Args... args) {
        Log(LogLevel::kDebug, format, args...);
    }

    template <typename... Args>
    void Info(const char* format, Args... args) {
        Log(LogLevel::kInfo, format, args...);
    }

    template <typename... Args>
    void Warning(const char* format, Args... args) {
        Log(LogLevel::kWarning, format, args...);
    }

    template <typename... Args>
    void Error(const char* format, Args... args) {
        Log(LogLevel::kError, format, args...);
    }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

   private:
    LogLevel min_log_level_{LogLevel::kDebug};
    std::unique_ptr<LogHandler> handler_;
    // Mutex for thread safety
    std::mutex logger_mutex_;

    void LogMessage(LogLevel level, const std::string& message);
    std::string FormatMessageWithAddress(const std::string& message) const;
};

/**
 * @brief Global logger instance
 */
extern Logger LOG;

// Convenience macros for logging with format strings
// Then modify your logging macros
#if LORAMESHER_LOG_LEVEL <= 0
#define LOG_DEBUG(fmt, ...) loramesher::LOG.Debug(fmt, ##__VA_ARGS__)
#else
#define LOG_DEBUG(fmt, ...) \
    do {                    \
    } while (0)  // Compiles to nothing
#endif

#if LORAMESHER_LOG_LEVEL <= 1
#define LOG_INFO(fmt, ...) loramesher::LOG.Info(fmt, ##__VA_ARGS__)
#else
#define LOG_INFO(fmt, ...) \
    do {                   \
    } while (0)
#endif

#if LORAMESHER_LOG_LEVEL <= 2
#define LOG_WARNING(fmt, ...) loramesher::LOG.Warning(fmt, ##__VA_ARGS__)
#else
#define LOG_WARNING(fmt, ...) \
    do {                      \
    } while (0)
#endif

#if LORAMESHER_LOG_LEVEL <= 3
#define LOG_ERROR(fmt, ...) loramesher::LOG.Error(fmt, ##__VA_ARGS__)
#else
#define LOG_ERROR(fmt, ...) \
    do {                    \
    } while (0)
#endif

#define LOG_FLUSH() loramesher::LOG.Flush()

}  // namespace loramesher