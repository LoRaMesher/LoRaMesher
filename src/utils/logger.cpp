#include "logger.hpp"
#include <mutex>
#include "os/os_port.hpp"

namespace loramesher {

Logger::Logger() : logger_semaphore_(nullptr) {
#ifdef LORAMESHER_BUILD_ARDUINO
    handler_ = std::make_unique<SerialLogHandler>();
#else
    handler_ = std::make_unique<ConsoleLogHandler>();
#endif
}

void Logger::LogRaw(LogLevel level, const char* message) {
    EnsureSemaphoreInitialized();

    if (!logger_semaphore_ ||
        !GetRTOS().TakeSystemSemaphore(logger_semaphore_, 100)) {
        return;
    }

    try {
        LogMessage(level, message);
    } catch (...) {
        GetRTOS().GiveSystemSemaphore(logger_semaphore_);
        return;
    }

    GetRTOS().GiveSystemSemaphore(logger_semaphore_);
}

void Logger::Log(LogLevel level, const char* format, ...) {
    EnsureSemaphoreInitialized();

    char buffer[LOGGER_BUFFER_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (!logger_semaphore_ ||
        !GetRTOS().TakeSystemSemaphore(logger_semaphore_, 100)) {
        return;
    }

    try {
        LogMessage(level, buffer);
    } catch (...) {
        GetRTOS().GiveSystemSemaphore(logger_semaphore_);
        return;
    }

    GetRTOS().GiveSystemSemaphore(logger_semaphore_);
}

void Logger::EnsureSemaphoreInitialized() {
#ifdef _GLIBCXX_HAS_GTHREADS
    static std::once_flag flag;
    std::call_once(flag, [this] {
        if (!shutdown_requested_) {
            logger_semaphore_ = GetRTOS().CreateSystemSemaphore();
        }
    });
#else
    // Toolchains without gthreads (e.g. STM32) do not provide std::call_once.
    // The logger is initialized during startup before tasks run, so a plain
    // one-shot guard is sufficient here.
    static bool initialized = false;
    if (!initialized) {
        initialized = true;
        if (!shutdown_requested_) {
            logger_semaphore_ = GetRTOS().CreateSystemSemaphore();
        }
    }
#endif
}

void Logger::LogMessage(LogLevel level, const char* message) {
    if (shutdown_requested_) {
        return;
    }

    if (level >= min_log_level_ && handler_) {
        handler_->Write(level, GetRTOS().GetCurrentTaskNodeAddress(), message);
    }
}

void Logger::SetLogLevel(LogLevel level) {
    EnsureSemaphoreInitialized();

    if (logger_semaphore_ &&
        GetRTOS().TakeSystemSemaphore(logger_semaphore_, 100)) {
        min_log_level_ = level;
        GetRTOS().GiveSystemSemaphore(logger_semaphore_);
    }
}

void Logger::SetHandler(std::unique_ptr<LogHandler> handler) {
    EnsureSemaphoreInitialized();

    if (!logger_semaphore_ ||
        !GetRTOS().TakeSystemSemaphore(logger_semaphore_, 100)) {
        fprintf(stderr, "Logger::SetHandler: timeout acquiring semaphore\n");
        return;
    }
    handler_ = std::move(handler);
    GetRTOS().GiveSystemSemaphore(logger_semaphore_);
}

void Logger::Flush() {
    EnsureSemaphoreInitialized();

    if (logger_semaphore_ &&
        GetRTOS().TakeSystemSemaphore(logger_semaphore_, 100)) {
        if (handler_) {
            handler_->Flush();
        }
        GetRTOS().GiveSystemSemaphore(logger_semaphore_);
    }
}

Logger::~Logger() {
    shutdown_requested_ = true;

    // IMPORTANT: Do not delete the system semaphore during static destruction.
    // Deleting a potentially locked std::timed_mutex causes undefined behavior (SIGABRT).
    // This is intentional - the OS will reclaim this memory when the process exits.
    // This leak-by-design pattern is standard for global infrastructure objects.
    logger_semaphore_ = nullptr;
}

// Global logger instance
Logger LOG;

}  // namespace loramesher
