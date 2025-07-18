#pragma once

#include <chrono>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>

#include "logger.hpp"
#include "os/os_port.hpp"

namespace loramesher {

/**
 * @brief File-based log handler implementation.
 * 
 * This handler writes log messages to a file with optional timestamps.
 * It supports both append and overwrite modes, and provides automatic
 * timestamp formatting for log visualization.
 */
class FileLogHandler : public LogHandler {
   public:
    /**
     * @brief Constructor that opens a file for logging
     * @param filename The path to the log file
     * @param append Whether to append to existing file (true) or overwrite (false)
     * @param add_timestamps Whether to add timestamps to each log entry
     */
    explicit FileLogHandler(const std::string& filename, bool append = true,
                            bool add_timestamps = true)
        : filename_(filename), add_timestamps_(add_timestamps) {

        auto mode = append ? (std::ios::out | std::ios::app) : std::ios::out;
        file_stream_.open(filename_, mode);

        if (!file_stream_.is_open()) {
            throw std::runtime_error("Failed to open log file: " + filename_);
        }

        // Write header if starting new file
        if (!append) {
            WriteHeader();
        }
    }

    /**
     * @brief Destructor that ensures file is properly closed
     */
    ~FileLogHandler() {
        if (file_stream_.is_open()) {
            Flush();
            file_stream_.close();
        }
    }

    /**
     * @brief Write a log message to the file
     * @param level The severity level of the message
     * @param message The message to be logged
     */
    void Write(LogLevel level, const std::string& message) override {
        std::lock_guard<std::mutex> lock(file_mutex_);

        if (!file_stream_.is_open()) {
            return;
        }

        // Build complete log line in buffer
        buffer_.clear();

        // Add timestamp if enabled
        if (add_timestamps_) {
            buffer_ += "[";
            buffer_ += GetTimestamp();
            buffer_ += "] ";
        }

        // Add log level
        buffer_ += "[";
        buffer_ += GetLevelString(level);
        buffer_ += "] ";

        // Add message
        buffer_ += message;
        buffer_ += '\n';

        // Write to file stream without immediate flush
        file_stream_.write(buffer_.c_str(), buffer_.length());

        // Only flush on error level or every N writes for performance
        if (level == LogLevel::kError ||
            (++write_count_ % flush_interval_ == 0)) {
            file_stream_.flush();
        }
    }

    /**
     * @brief Flushes buffered log messages to disk
     */
    void Flush() override {
        std::lock_guard<std::mutex> lock(file_mutex_);
        if (file_stream_.is_open()) {
            file_stream_.flush();
        }
    }

    /**
     * @brief Set the flush interval for performance tuning
     * @param interval Number of writes between forced flushes (default: 10)
     */
    void SetFlushInterval(size_t interval) {
        std::lock_guard<std::mutex> lock(file_mutex_);
        flush_interval_ = interval;
    }

    /**
     * @brief Get the filename being used for logging
     * @return The log file path
     */
    const std::string& GetFilename() const { return filename_; }

    /**
     * @brief Check if the file is open and ready for writing
     * @return True if file is open, false otherwise
     */
    bool IsOpen() const { return file_stream_.is_open(); }

   private:
    std::ofstream file_stream_;
    std::string filename_;
    bool add_timestamps_;
    std::chrono::steady_clock::time_point start_time_{
        std::chrono::steady_clock::now()};
    mutable std::mutex file_mutex_;  ///< Mutex to ensure atomic file writes
    std::string buffer_;         ///< Reusable buffer for log line construction
    size_t write_count_{0};      ///< Counter for writes since last flush
    size_t flush_interval_{10};  ///< Number of writes between forced flushes

    /**
     * @brief Write a header to the log file
     */
    void WriteHeader() {
        std::lock_guard<std::mutex> lock(file_mutex_);
        std::string header = "# LoRaMesh Test Log\n";
        header += "# Generated: " + GetCurrentTimeString() + "\n";
        header += "# Format: [timestamp] [level] message\n";
        header += "\n";

        file_stream_.write(header.c_str(), header.length());
        file_stream_.flush();  // Flush header immediately
    }

    /**
     * @brief Get current timestamp as milliseconds since start
     * @return Timestamp string in milliseconds
     */
    std::string GetTimestamp() const {
        return std::to_string(GetRTOS().getTickCount()) + " ms";
    }

    /**
     * @brief Get current wall clock time as string
     * @return Human-readable time string
     */
    std::string GetCurrentTimeString() const {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);

        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

    /**
     * @brief Convert log level to string
     * @param level The log level
     * @return String representation of the level
     */
    std::string GetLevelString(LogLevel level) const {
        switch (level) {
            case LogLevel::kDebug:
                return "DEBUG";
            case LogLevel::kInfo:
                return "INFO";
            case LogLevel::kWarning:
                return "WARNING";
            case LogLevel::kError:
                return "ERROR";
            default:
                return "UNKNOWN";
        }
    }
};

/**
 * @brief Utility function to create a file log handler with automatic filename
 * @param test_name The name of the test (used in filename)
 * @param directory The directory to save logs (default: current directory)
 * @return Unique pointer to the created file handler
 */
inline std::unique_ptr<FileLogHandler> CreateTestLogHandler(
    const std::string& test_name, const std::string& directory = ".") {

    // Create filename with timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);

    std::stringstream filename;
    filename << directory << "/";
    filename << test_name << "_";
    filename << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
    filename << ".log";

    return std::make_unique<FileLogHandler>(filename.str(), false, true);
}

}  // namespace loramesher