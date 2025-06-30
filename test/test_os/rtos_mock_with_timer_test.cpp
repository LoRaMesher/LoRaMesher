/**
 * @file rtos_mock_time_test.cpp
 * @brief Tests for virtual time functionality in RTOSMock
 */

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>
#include "os/rtos_mock.hpp"

#ifdef ARDUINO

TEST(RTOSMockTest, ImplementArduinoTests) {
    GTEST_SKIP();
}

#else

namespace loramesher {
namespace test {

/**
 * @class RTOSMockTimeTest
 * @brief Test fixture for testing the virtual time functionality in RTOSMock
 */
class RTOSMockTimeTest : public ::testing::Test {
   protected:
    void SetUp() override {
        // Get RTOS instance and cast to RTOSMock
        rtos_ = &GetRTOS();
        rtosMock_ = dynamic_cast<os::RTOSMock*>(rtos_);
        ASSERT_NE(rtosMock_, nullptr) << "RTOS is not an RTOSMock instance";

        // Set virtual time mode for testing
        rtosMock_->setTimeMode(os::RTOSMock::TimeMode::kVirtualTime);

        // Store initial time
        initialTime_ = rtos_->getTickCount();
    }

    void TearDown() override {
        // Clean up any remaining tasks
        for (auto& task : taskHandles_) {
            if (task) {
                rtos_->DeleteTask(task);
                task = nullptr;
            }
        }

        // Clean up any remaining queues
        for (auto& queue : queueHandles_) {
            if (queue) {
                rtos_->DeleteQueue(queue);
                queue = nullptr;
            }
        }

        // Clean up any remaining semaphores
        for (auto& sem : semaphoreHandles_) {
            if (sem) {
                rtos_->DeleteSemaphore(sem);
                sem = nullptr;
            }
        }

        // Return to real-time mode after tests
        rtosMock_->setTimeMode(os::RTOSMock::TimeMode::kRealTime);
    }

    /**
     * @brief Wait for tasks to execute
     * 
     * This helper function waits a short time to allow tasks to run and
     * process any events before continuing. It helps ensure proper test
     * sequencing, especially when virtual time is used.
     */
    void WaitForTasksToExecute() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    /**
     * @brief Helper method to safely create a task and track its handle
     */
    template <typename Func>
    os::TaskHandle_t CreateTrackedTask(Func taskFunc, const char* name,
                                       uint32_t stackSize, void* params,
                                       uint32_t priority) {
        os::TaskHandle_t handle = nullptr;
        EXPECT_TRUE(rtos_->CreateTask(taskFunc, name, stackSize, params,
                                      priority, &handle));
        if (handle) {
            taskHandles_.push_back(handle);
        }
        return handle;
    }

    /**
     * @brief Helper method to safely create a queue and track its handle
     */
    os::QueueHandle_t CreateTrackedQueue(uint32_t length, uint32_t itemSize) {
        os::QueueHandle_t handle = rtos_->CreateQueue(length, itemSize);
        if (handle) {
            queueHandles_.push_back(handle);
        }
        return handle;
    }

    /**
     * @brief Helper method to safely create a semaphore and track its handle
     */
    os::SemaphoreHandle_t CreateTrackedSemaphore() {
        os::SemaphoreHandle_t handle = rtos_->CreateBinarySemaphore();
        if (handle) {
            semaphoreHandles_.push_back(handle);
        }
        return handle;
    }

    // Test fixture members
    os::RTOS* rtos_ = nullptr;
    os::RTOSMock* rtosMock_ = nullptr;
    uint32_t initialTime_ = 0;

    // Vector to track task handles for cleanup
    std::vector<os::TaskHandle_t> taskHandles_;

    // Vector to track queue handles for cleanup
    std::vector<os::QueueHandle_t> queueHandles_;

    // Vector to track semaphore handles for cleanup
    std::vector<os::SemaphoreHandle_t> semaphoreHandles_;
};

/**
 * @brief Basic test for virtual time operation
 */
TEST_F(RTOSMockTimeTest, BasicVirtualTimeOperation) {
    // Verify initial mode
    ASSERT_EQ(rtosMock_->getTimeMode(), os::RTOSMock::TimeMode::kVirtualTime);

    // Get initial time
    uint32_t startTime = rtos_->getTickCount();

    // Advance time
    const uint32_t timeAdvance = 1000;
    rtosMock_->advanceTime(timeAdvance);

    // Verify time advanced
    uint32_t newTime = rtos_->getTickCount();
    EXPECT_GE(newTime, startTime + timeAdvance);
}

/**
 * @brief Test that delay responds to virtual time
 */
TEST_F(RTOSMockTimeTest, SimpleDelayWithVirtualTime) {
    // Create a shared state object with proper memory management
    struct SharedState {
        std::atomic<bool> taskStarted{false};
        std::atomic<bool> taskCompleted{false};
        std::atomic<uint32_t> startTime{0};
        std::atomic<uint32_t> endTime{0};
    };

    auto state = std::make_shared<SharedState>();

    // Create a task that uses delay
    os::TaskHandle_t task = CreateTrackedTask(
        [](void* param) {
            auto state = *static_cast<std::shared_ptr<SharedState>*>(param);

            // Record start time and set started flag
            state->startTime = GetRTOS().getTickCount();
            state->taskStarted = true;

            // Delay for 500ms
            GetRTOS().delay(500);

            // Record end time and set completed flag
            state->endTime = GetRTOS().getTickCount();
            state->taskCompleted = true;
        },
        "DelayTask", 2048, new std::shared_ptr<SharedState>(state), 1);

    ASSERT_NE(task, nullptr);

    // Wait for task to start
    uint32_t waitAttempts = 0;
    while (!state->taskStarted && waitAttempts < 50) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        waitAttempts++;
    }
    ASSERT_TRUE(state->taskStarted) << "Task did not start within timeout";

    // Task should not be completed yet
    EXPECT_FALSE(state->taskCompleted);

    // Advance time by 300ms (not enough to complete)
    rtosMock_->advanceTime(300);
    WaitForTasksToExecute();

    // Task should still not be completed
    EXPECT_FALSE(state->taskCompleted);

    // Advance time by 300ms more (enough to complete)
    rtosMock_->advanceTime(300);
    WaitForTasksToExecute();

    // Task should now be completed
    EXPECT_TRUE(state->taskCompleted);

    // Check timing
    EXPECT_GE(state->endTime, state->startTime + 500);
}

/**
 * @brief Test a simple queue operation with virtual time
 */
TEST_F(RTOSMockTimeTest, SimpleQueueOperation) {
    // Create a queue
    os::QueueHandle_t queue = CreateTrackedQueue(1, sizeof(int));
    ASSERT_NE(queue, nullptr);

    // Create shared state
    struct SharedState {
        std::atomic<bool> senderDone{false};
        std::atomic<bool> receiverDone{false};
        std::atomic<int> sentValue{0};
        std::atomic<int> receivedValue{0};
    };

    auto state = std::make_shared<SharedState>();

    // Value to send
    const int testValue = 42;

    // Create a sender task that delays then sends
    os::TaskHandle_t senderTask = CreateTrackedTask(
        [](void* param) {
            auto args = *static_cast<
                std::pair<os::QueueHandle_t, std::shared_ptr<SharedState>>*>(
                param);
            auto queue = args.first;
            auto state = args.second;

            // Wait 200ms
            GetRTOS().delay(200);

            // Send value
            int value = 42;
            state->sentValue = value;
            GetRTOS().SendToQueue(queue, &value, 0);
            state->senderDone = true;
        },
        "SenderTask", 2048,
        new std::pair<os::QueueHandle_t, std::shared_ptr<SharedState>>(queue,
                                                                       state),
        1);

    ASSERT_NE(senderTask, nullptr);

    // Create a receiver task
    os::TaskHandle_t receiverTask = CreateTrackedTask(
        [](void* param) {
            auto args = *static_cast<
                std::pair<os::QueueHandle_t, std::shared_ptr<SharedState>>*>(
                param);
            auto queue = args.first;
            auto state = args.second;

            // Wait for value with timeout
            int value = 0;
            auto result = GetRTOS().ReceiveFromQueue(queue, &value, 500);

            if (result == os::QueueResult::kOk) {
                state->receivedValue = value;
            }

            state->receiverDone = true;
        },
        "ReceiverTask", 2048,
        new std::pair<os::QueueHandle_t, std::shared_ptr<SharedState>>(queue,
                                                                       state),
        1);

    ASSERT_NE(receiverTask, nullptr);

    // Wait for tasks to start
    WaitForTasksToExecute();

    // Initially, neither task should be done
    EXPECT_FALSE(state->senderDone);
    EXPECT_FALSE(state->receiverDone);

    // Advance time to 250ms (sender should send)
    rtosMock_->advanceTime(250);
    WaitForTasksToExecute();

    // Sender should be done, receiver should have gotten the value
    EXPECT_TRUE(state->senderDone);
    EXPECT_TRUE(state->receiverDone);
    EXPECT_EQ(state->receivedValue, testValue);
}

}  // namespace test
}  // namespace loramesher

#endif  // ARDUINO