#include "logger.hpp"

namespace loramesher {

Logger::Logger() : logger_mutex_() {
#ifdef LORAMESHER_BUILD_ARDUINO
    handler_ = std::make_unique<SerialLogHandler>();
#else
    handler_ = std::make_unique<ConsoleLogHandler>();
#endif
}

void Logger::LogMessage(LogLevel level, const std::string& message) {
    std::unique_lock<std::mutex> lock(logger_mutex_, std::try_to_lock);
    if (lock.owns_lock()) {
        if (level >= min_log_level_ && handler_) {
            handler_->Write(level, message);
        }
    }
}

void Logger::Reset() {
    std::unique_lock<std::mutex> lock(logger_mutex_, std::try_to_lock);
    if (!lock.owns_lock()) {
        // The mutex is already locked, which is problematic
        // Create a new mutex to replace the locked one
        logger_mutex_.~mutex();
        new (&logger_mutex_) std::mutex();
    }
}

// Global logger instance
Logger LOG;

}  // namespace loramesher