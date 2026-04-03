#include "logger.hpp"
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

    if (!logger_semaphore_ ||
        !GetRTOS().TakeSystemSemaphore(logger_semaphore_, 100)) {
        return;
    }

    char buffer[LOGGER_BUFFER_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    try {
        LogMessage(level, buffer);
    } catch (...) {
        GetRTOS().GiveSystemSemaphore(logger_semaphore_);
        return;
    }

    GetRTOS().GiveSystemSemaphore(logger_semaphore_);
}

void Logger::EnsureSemaphoreInitialized() {
    if (!shutdown_requested_ && !logger_semaphore_) {
        logger_semaphore_ = GetRTOS().CreateSystemSemaphore();
    }
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
