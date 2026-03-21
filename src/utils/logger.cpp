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

void Logger::Log(LogLevel level, const char* format, ...) {
    // Lazy initialization of semaphore to avoid static initialization order issues
    EnsureSemaphoreInitialized();

    // Use RTOS system semaphore with timeout to prevent blocking
    if (!logger_semaphore_ ||
        !GetRTOS().TakeSystemSemaphore(logger_semaphore_, 100)) {
        // If semaphore is unavailable or timeout, skip logging to prevent blocking
        return;
    }

    // Format the message using variable arguments
    char buffer[LOGGER_BUFFER_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    // Log the formatted message; release semaphore even if an exception is thrown
    try {
        LogMessage(level, std::string(buffer));
    } catch (...) {
        GetRTOS().GiveSystemSemaphore(logger_semaphore_);
        return;
    }

    GetRTOS().GiveSystemSemaphore(logger_semaphore_);
}

void Logger::EnsureSemaphoreInitialized() {
    if (!shutdown_requested_ && !logger_semaphore_) {
        // Use SYSTEM semaphore - logging is infrastructure, not part of simulation
        // System semaphores always use real-time and bypass virtual time mode
        logger_semaphore_ = GetRTOS().CreateSystemSemaphore();
    }
}

void Logger::LogMessage(LogLevel level, const std::string& message) {
    // Skip logging if shutdown has been requested (destructor running)
    if (shutdown_requested_) {
        return;
    }

    if (level >= min_log_level_ && handler_) {
        std::string formatted_message = FormatMessageWithAddress(message);
        handler_->Write(level, formatted_message);
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
    // Set shutdown flag to prevent new logging attempts
    shutdown_requested_ = true;

    // IMPORTANT: Do not delete the system semaphore during static destruction.
    // Deleting a potentially locked std::timed_mutex causes undefined behavior (SIGABRT).
    // This is intentional - the OS will reclaim this memory when the process exits.
    // This leak-by-design pattern is standard for global infrastructure objects.
    logger_semaphore_ = nullptr;
}

std::string Logger::FormatMessageWithAddress(const std::string& message) const {
    // Get the node address from the current task
    std::string node_address = GetRTOS().GetCurrentTaskNodeAddress();
    if (!node_address.empty()) {
        // Create a formatted string with node address prefix
        return "[" + node_address + "] " + message;
    }
    return message;
}

// Global logger instance
Logger LOG;

}  // namespace loramesher