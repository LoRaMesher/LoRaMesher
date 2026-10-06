/**
 * @file test/test_utils/logger_test.cpp
 * @brief Unit tests for the Logger utility
 */
#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "utils/logger.hpp"

using namespace loramesher;

/**
 * @class LoggerTest
 * @brief Test fixture for testing the Logger utility
 */
class LoggerTest : public ::testing::Test {
   protected:
    void SetUp() override {
        // Setup code for Logger tests
    }

    void TearDown() override {
        // Clean up any resources
    }
};

/**
 * @brief Test logging at different levels
 */
TEST_F(LoggerTest, LogLevelTest) {
    // Set log level to Info
    LOG.SetLogLevel(LogLevel::kInfo);

    // This Debug message should not appear
    LOG.Debug("This is a debug message: %d", 1);

    // This Info message should appear
    LOG.Info("This is an info message: %s", "info");

    // This Warning message should appear
    LOG.Warning("This is a warning message");

    // This Error message should appear
    LOG.Error("This is an error message: %.2f", 3.14);

    // Change log level to Debug
    LOG.SetLogLevel(LogLevel::kDebug);

    // Now Debug messages should appear
    LOG.Debug("This is another debug message: %d", 2);
}

namespace {

class CountingLogHandler : public LogHandler {
   public:
    int writes_ = 0;
    int flushes_ = 0;
    LogLevel last_level_ = LogLevel::kInfo;
    std::string last_message_;

    void Write(LogLevel level, const char* /*node_addr*/,
               const char* message) override {
        writes_++;
        last_level_ = level;
        last_message_ = message ? message : "";
    }

    void Flush() override { flushes_++; }
};

}  // namespace

TEST_F(LoggerTest, LogRawWritesPreformattedMessage) {
    auto handler = std::make_unique<CountingLogHandler>();
    auto* raw = handler.get();
    LOG.SetHandler(std::move(handler));
    LOG.SetLogLevel(LogLevel::kDebug);

    LOG.LogRaw(LogLevel::kInfo, "raw-message");

    EXPECT_GE(raw->writes_, 1);
    EXPECT_EQ(raw->last_message_, "raw-message");
    EXPECT_EQ(raw->last_level_, LogLevel::kInfo);
}

TEST_F(LoggerTest, FlushDispatchesToHandler) {
    auto handler = std::make_unique<CountingLogHandler>();
    auto* raw = handler.get();
    LOG.SetHandler(std::move(handler));

    int before = raw->flushes_;
    LOG.Flush();
    EXPECT_GT(raw->flushes_, before);
}

TEST_F(LoggerTest, SetHandlerReturnsTheReplacedHandler) {
    auto first = std::make_unique<CountingLogHandler>();
    auto* first_raw = first.get();
    LOG.SetHandler(std::move(first));

    std::unique_ptr<LogHandler> previous =
        LOG.SetHandler(std::make_unique<CountingLogHandler>());

    EXPECT_EQ(previous.get(), first_raw);
}

TEST_F(LoggerTest, SetHandlerToNullDoesNotCrashOnSubsequentLog) {
    LOG.SetHandler(nullptr);
    EXPECT_NO_THROW(LOG.Info("after-null-handler"));
}