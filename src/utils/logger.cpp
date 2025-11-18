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

void Logger::EnsureSemaphoreInitialized() {
    if (!logger_semaphore_) {
        logger_semaphore_ = GetRTOS().CreateBinarySemaphore();
        if (logger_semaphore_) {
            // Initialize semaphore as "given" so it can be taken for first use
            GetRTOS().GiveSemaphore(logger_semaphore_);
        }
    }
}

void Logger::LogMessage(LogLevel level, const std::string& message) {
    // Lazy initialization of semaphore to avoid static initialization order issues
    EnsureSemaphoreInitialized();

    // Use RTOS semaphore with timeout to prevent blocking
    if (!logger_semaphore_ ||
        !GetRTOS().TakeSemaphore(logger_semaphore_, 100)) {
        // If semaphore is unavailable or timeout, skip logging to prevent blocking
        return;
    }

    if (level >= min_log_level_ && handler_) {
        std::string formatted_message = FormatMessageWithAddress(message);
        handler_->Write(level, formatted_message);
    }

    GetRTOS().GiveSemaphore(logger_semaphore_);
}

void Logger::SetLogLevel(LogLevel level) {
    EnsureSemaphoreInitialized();

    if (logger_semaphore_ && GetRTOS().TakeSemaphore(logger_semaphore_, 10)) {
        min_log_level_ = level;
        GetRTOS().GiveSemaphore(logger_semaphore_);
    }
}

void Logger::SetHandler(std::unique_ptr<LogHandler> handler) {
    EnsureSemaphoreInitialized();

    if (logger_semaphore_ && GetRTOS().TakeSemaphore(logger_semaphore_, 10)) {
        handler_ = std::move(handler);
        GetRTOS().GiveSemaphore(logger_semaphore_);
    }
}

void Logger::Flush() {
    EnsureSemaphoreInitialized();

    if (logger_semaphore_ && GetRTOS().TakeSemaphore(logger_semaphore_, 10)) {
        if (handler_) {
            handler_->Flush();
        }
        GetRTOS().GiveSemaphore(logger_semaphore_);
    }
}

Logger::~Logger() {
    if (logger_semaphore_) {
        GetRTOS().DeleteSemaphore(logger_semaphore_);
        logger_semaphore_ = nullptr;
    }
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