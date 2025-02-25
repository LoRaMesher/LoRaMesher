/**
 * @file test/os/rtos_test.cpp
 * @brief Unit tests for RTOS interface using Google Mock
 */
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#ifdef ARDUINO

TEST(RTOSTest, ImplementArduinoTests) {
    EXPECT_TRUE(true);
}

#else

#include <chrono>
#include <thread>

#include "mock_rtos.hpp"

using namespace loramesher;
using namespace loramesher::os;
using namespace loramesher::test;
using ::testing::_;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;

/**
 * @class RTOSTest
 * @brief Test fixture for RTOS tests
 */
class RTOSTest : public ::testing::Test {
   protected:
    void SetUp() override {
        // Replace the global RTOS instance with our mock
        rtosInstance = &MockRTOS::instance();
    }

    void TearDown() override {
        // Clean up any resources
    }

    MockRTOS* rtosInstance;
};

/**
 * @brief Test task creation functionality
 */
TEST_F(RTOSTest, CreateTaskTest) {
    // Setup
    TaskHandle_t taskHandle;
    auto taskFunction = [](void* param) {
        int* value = static_cast<int*>(param);
        (*value)++;
    };

    // Expectations
    EXPECT_CALL(*rtosInstance, CreateTask(_, _, _, _, _, _))
        .WillOnce(
            DoAll(SetArgPointee<5>(reinterpret_cast<TaskHandle_t>(0x12345678)),
                  Return(true)));

    // Execute
    bool result = rtosInstance->CreateTask(taskFunction, "TestTask", 2048,
                                           nullptr, 1, &taskHandle);

    // Verify
    EXPECT_TRUE(result);
    EXPECT_EQ(taskHandle, reinterpret_cast<TaskHandle_t>(0x12345678));
}

/**
 * @brief Test queue operations
 */
TEST_F(RTOSTest, QueueOperationsTest) {
    // Setup
    QueueHandle_t queueHandle = reinterpret_cast<QueueHandle_t>(0x87654321);
    int testData = 42;
    int receivedData = 0;

    // Expectations
    EXPECT_CALL(*rtosInstance, CreateQueue(10, sizeof(int)))
        .WillOnce(Return(queueHandle));

    EXPECT_CALL(*rtosInstance, SendToQueue(queueHandle, &testData, 100))
        .WillOnce(Return(QueueResult::kOk));

    EXPECT_CALL(*rtosInstance,
                ReceiveFromQueue(queueHandle, &receivedData, 100))
        .WillOnce(DoAll(Invoke([&receivedData, testData](QueueHandle_t queueArg,
                                                         void* buffer,
                                                         uint32_t timeoutArg) {
                            // Use parameters to prevent unused variable warnings
                            (void)queueArg;    // Explicitly mark as used
                            (void)timeoutArg;  // Explicitly mark as used

                            *static_cast<int*>(buffer) = testData;
                            return QueueResult::kOk;
                        }),
                        Return(QueueResult::kOk)));

    EXPECT_CALL(*rtosInstance, getQueueMessagesWaiting(queueHandle))
        .WillOnce(Return(1));

    EXPECT_CALL(*rtosInstance, DeleteQueue(queueHandle));

    // Execute and verify
    QueueHandle_t createdQueue = rtosInstance->CreateQueue(10, sizeof(int));
    EXPECT_EQ(createdQueue, queueHandle);

    QueueResult sendResult =
        rtosInstance->SendToQueue(queueHandle, &testData, 100);
    EXPECT_EQ(sendResult, QueueResult::kOk);

    uint32_t waitingMessages =
        rtosInstance->getQueueMessagesWaiting(queueHandle);
    EXPECT_EQ(waitingMessages, 1);

    QueueResult receiveResult =
        rtosInstance->ReceiveFromQueue(queueHandle, &receivedData, 100);
    EXPECT_EQ(receiveResult, QueueResult::kOk);
    EXPECT_EQ(receivedData, testData);

    rtosInstance->DeleteQueue(queueHandle);
}

/**
 * @brief Test timing functions
 */
TEST_F(RTOSTest, TimingFunctionsTest) {
    // Setup
    uint32_t currentTicks = 12345;

    // Expectations
    EXPECT_CALL(*rtosInstance, getTickCount())
        .WillOnce(Return(currentTicks))
        .WillOnce(Return(currentTicks + 100));

    EXPECT_CALL(*rtosInstance, delay(100)).Times(1);

    // Execute and verify
    uint32_t beforeTicks = rtosInstance->getTickCount();
    EXPECT_EQ(beforeTicks, currentTicks);

    rtosInstance->delay(100);

    uint32_t afterTicks = rtosInstance->getTickCount();
    EXPECT_EQ(afterTicks, currentTicks + 100);
}

/**
 * @brief Test task state management
 */
TEST_F(RTOSTest, TaskStateManagementTest) {
    // Setup
    TaskHandle_t taskHandle = reinterpret_cast<TaskHandle_t>(0x12345678);

    // Expectations
    EXPECT_CALL(*rtosInstance, getTaskState(taskHandle))
        .WillOnce(Return(TaskState::kRunning))
        .WillOnce(Return(TaskState::kSuspended))
        .WillOnce(Return(TaskState::kReady));

    EXPECT_CALL(*rtosInstance, SuspendTask(taskHandle));
    EXPECT_CALL(*rtosInstance, ResumeTask(taskHandle));

    // Execute and verify
    TaskState initialState = rtosInstance->getTaskState(taskHandle);
    EXPECT_EQ(initialState, TaskState::kRunning);
    EXPECT_STREQ(RTOS::getTaskStateString(initialState), "Running");

    rtosInstance->SuspendTask(taskHandle);
    TaskState suspendedState = rtosInstance->getTaskState(taskHandle);
    EXPECT_EQ(suspendedState, TaskState::kSuspended);
    EXPECT_STREQ(RTOS::getTaskStateString(suspendedState), "Suspended");

    rtosInstance->ResumeTask(taskHandle);
    TaskState resumedState = rtosInstance->getTaskState(taskHandle);
    EXPECT_EQ(resumedState, TaskState::kReady);
    EXPECT_STREQ(RTOS::getTaskStateString(resumedState), "Ready");
}

/**
 * @brief Test system statistics collection
 */
TEST_F(RTOSTest, SystemStatisticsTest) {
    // Setup
    std::vector<TaskStats> mockStats = {
        {"Task1", TaskState::kRunning, 1024, 5000},
        {"Task2", TaskState::kBlocked, 2048, 3000},
        {"Task3", TaskState::kReady, 4096, 1000}};

    // Expectations
    EXPECT_CALL(*rtosInstance, getSystemTaskStats())
        .WillOnce(Return(mockStats));

    // Execute
    std::vector<TaskStats> stats = rtosInstance->getSystemTaskStats();

    // Verify
    ASSERT_EQ(stats.size(), 3);
    EXPECT_EQ(stats[0].name, "Task1");
    EXPECT_EQ(stats[0].state, TaskState::kRunning);
    EXPECT_EQ(stats[0].stackWatermark, 1024);
    EXPECT_EQ(stats[0].runtime, 5000);

    EXPECT_EQ(stats[1].name, "Task2");
    EXPECT_EQ(stats[1].state, TaskState::kBlocked);

    EXPECT_EQ(stats[2].name, "Task3");
    EXPECT_EQ(stats[2].state, TaskState::kReady);
}

/**
 * @brief Test ISR registration
 */
TEST_F(RTOSTest, ISRRegistrationTest) {
    // Setup
    void* isrHandle = reinterpret_cast<void*>(0xABCDEF01);
    void (*testCallback)() = []() {
    };

    // Expectations
    EXPECT_CALL(*rtosInstance, RegisterISR(testCallback, 5, 1))
        .WillOnce(Return(isrHandle));

    // Execute
    void* result = rtosInstance->RegisterISR(testCallback, 5, 1);

    // Verify
    EXPECT_EQ(result, isrHandle);
}

#endif  // ARDUINO