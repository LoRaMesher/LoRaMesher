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
    std::lock_guard<std::mutex> lock(logger_mutex_);
    if (level >= min_log_level_ && handler_) {
        handler_->Write(level, message);
    }
}

// Global logger instance
Logger LOG;

}  // namespace loramesher