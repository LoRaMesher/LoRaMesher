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
    // Fast path: if already initialized, return immediately
    if (semaphore_initialized_.load(std::memory_order_acquire)) {
        return;
    }

    // Detect re-entrant calls during initialization
    // This can happen if RTOS operations trigger logging
    if (initialization_in_progress_.load(std::memory_order_acquire)) {
        // We're being called recursively during initialization - skip to avoid deadlock
        return;
    }

    // Use compare-exchange to ensure only one thread initializes
    bool expected = false;
    if (initialization_in_progress_.compare_exchange_strong(expected, true,
                                                             std::memory_order_acq_rel)) {
        // We won the race - initialize the semaphore
        if (!semaphore_initialized_.load(std::memory_order_acquire)) {
            logger_semaphore_ = GetRTOS().CreateBinarySemaphore();
            if (logger_semaphore_) {
                // Initialize semaphore as "given" so it can be taken for first use
                GetRTOS().GiveSemaphore(logger_semaphore_);

                // Mark as initialized
                semaphore_initialized_.store(true, std::memory_order_release);
            }
        }

        // Clear the in-progress flag
        initialization_in_progress_.store(false, std::memory_order_release);
    } else {
        // Another thread is initializing - spin-wait until done
        while (!semaphore_initialized_.load(std::memory_order_acquire)) {
            // Yield to avoid busy-waiting
            GetRTOS().YieldTask();
        }
    }
}

void Logger::LogMessage(LogLevel level, const std::string& message) {
    // Lazy initialization of semaphore to avoid static initialization order issues
    EnsureSemaphoreInitialized();

    // If semaphore failed to initialize or we're in re-entrant init, skip logging
    if (!semaphore_initialized_.load(std::memory_order_acquire) || !logger_semaphore_) {
        // Semaphore not initialized yet or initialization failed
        // Skip logging to prevent deadlock
        return;
    }

    // Use RTOS semaphore with timeout to prevent blocking
    if (!GetRTOS().TakeSemaphore(logger_semaphore_, 100)) {
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

    // If not initialized, set without synchronization (best effort)
    if (!semaphore_initialized_.load(std::memory_order_acquire) || !logger_semaphore_) {
        min_log_level_ = level;
        return;
    }

    if (GetRTOS().TakeSemaphore(logger_semaphore_, 10)) {
        min_log_level_ = level;
        GetRTOS().GiveSemaphore(logger_semaphore_);
    }
}

void Logger::SetHandler(std::unique_ptr<LogHandler> handler) {
    EnsureSemaphoreInitialized();

    // If not initialized, set without synchronization (best effort)
    if (!semaphore_initialized_.load(std::memory_order_acquire) || !logger_semaphore_) {
        handler_ = std::move(handler);
        return;
    }

    if (GetRTOS().TakeSemaphore(logger_semaphore_, 10)) {
        handler_ = std::move(handler);
        GetRTOS().GiveSemaphore(logger_semaphore_);
    }
}

void Logger::Flush() {
    EnsureSemaphoreInitialized();

    // If not initialized, flush without synchronization (best effort)
    if (!semaphore_initialized_.load(std::memory_order_acquire) || !logger_semaphore_) {
        if (handler_) {
            handler_->Flush();
        }
        return;
    }

    if (GetRTOS().TakeSemaphore(logger_semaphore_, 10)) {
        if (handler_) {
            handler_->Flush();
        }
        GetRTOS().GiveSemaphore(logger_semaphore_);
    }
}

Logger::~Logger() {
    if (semaphore_initialized_.load(std::memory_order_acquire) && logger_semaphore_) {
        GetRTOS().DeleteSemaphore(logger_semaphore_);
        logger_semaphore_ = nullptr;
        semaphore_initialized_.store(false, std::memory_order_release);
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