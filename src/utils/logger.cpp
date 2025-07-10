#include "logger.hpp"
#include "os/os_port.hpp"

namespace loramesher {

Logger::Logger() : logger_mutex_() {
#ifdef LORAMESHER_BUILD_ARDUINO
    handler_ = std::make_unique<SerialLogHandler>();
#else
    handler_ = std::make_unique<ConsoleLogHandler>();
#endif
}

void Logger::LogMessage(LogLevel level, const std::string& message) {
    // Try to acquire the mutex with a timeout to prevent deadlock
    std::unique_lock<std::timed_mutex> lock(logger_mutex_, std::try_to_lock);

    if (!lock.owns_lock()) {
        // If we can't get the lock immediately, try with a short timeout
        if (!lock.try_lock_for(std::chrono::milliseconds(10))) {
            // If still can't get the lock, skip this log to prevent blocking
            return;
        }
    }

    if (level >= min_log_level_ && handler_) {
        std::string formatted_message = FormatMessageWithAddress(message);
        handler_->Write(level, formatted_message);
    }
}

void Logger::Reset() {
    std::unique_lock<std::timed_mutex> lock(logger_mutex_, std::try_to_lock);
    if (!lock.owns_lock()) {
        // The mutex is already locked, which is problematic
        // Create a new mutex to replace the locked one
        logger_mutex_.~timed_mutex();
        new (&logger_mutex_) std::timed_mutex();
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