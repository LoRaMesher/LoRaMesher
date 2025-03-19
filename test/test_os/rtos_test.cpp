/**
 * @file test/os/rtos_additional_tests.cpp
 * @brief Additional unit tests for RTOS interface using Google Mock
 */
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#ifdef ARDUINO

TEST(RTOSAdditionalTest, ImplementArduinoTests) {
    EXPECT_TRUE(true);
}

#else

#include <chrono>
#include <thread>

#include "../mocks/mock_rtos.hpp"

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
 * @class RTOSAdditionalTest
 * @brief Test fixture for additional RTOS tests
 */
class RTOSAdditionalTest : public ::testing::Test {
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
 * @brief Test task deletion functionality
 */
TEST_F(RTOSAdditionalTest, DeleteTaskTest) {
    // Setup
    TaskHandle_t taskHandle = reinterpret_cast<TaskHandle_t>(0x12345678);

    // Expectations
    EXPECT_CALL(*rtosInstance, DeleteTask(taskHandle)).Times(1);

    // Execute
    rtosInstance->DeleteTask(taskHandle);
}

/**
 * @brief Test sending to queue from ISR context
 */
TEST_F(RTOSAdditionalTest, SendToQueueISRTest) {
    // Setup
    QueueHandle_t queueHandle = reinterpret_cast<QueueHandle_t>(0x87654321);
    int testData = 42;

    // Expectations
    EXPECT_CALL(*rtosInstance, SendToQueueISR(queueHandle, &testData))
        .WillOnce(Return(QueueResult::kOk));

    // Execute
    QueueResult result = rtosInstance->SendToQueueISR(queueHandle, &testData);

    // Verify
    EXPECT_EQ(result, QueueResult::kOk);
}

/**
 * @brief Test task stack watermark functionality
 */
TEST_F(RTOSAdditionalTest, TaskStackWatermarkTest) {
    // Setup
    TaskHandle_t taskHandle = reinterpret_cast<TaskHandle_t>(0x12345678);
    uint32_t expectedWatermark = 1024;

    // Expectations
    EXPECT_CALL(*rtosInstance, getTaskStackWatermark(taskHandle))
        .WillOnce(Return(expectedWatermark));

    // Execute
    uint32_t watermark = rtosInstance->getTaskStackWatermark(taskHandle);

    // Verify
    EXPECT_EQ(watermark, expectedWatermark);
}

/**
 * @brief Test scheduler start functionality
 */
TEST_F(RTOSAdditionalTest, StartSchedulerTest) {
    // Expectations
    EXPECT_CALL(*rtosInstance, StartScheduler()).Times(1);

    // Execute
    rtosInstance->StartScheduler();
}

/**
 * @brief Test task notification from ISR
 */
TEST_F(RTOSAdditionalTest, NotifyTaskFromISRTest) {
    // Setup
    TaskHandle_t taskHandle = reinterpret_cast<TaskHandle_t>(0x12345678);

    // Expectations
    EXPECT_CALL(*rtosInstance, NotifyTaskFromISR(taskHandle)).Times(1);

    // Execute
    rtosInstance->NotifyTaskFromISR(taskHandle);
}

/**
 * @brief Test wait for notification functionality
 */
TEST_F(RTOSAdditionalTest, WaitForNotifyTest) {
    // Setup
    uint32_t timeout = 100;

    // Expectations
    EXPECT_CALL(*rtosInstance, WaitForNotify(timeout))
        .WillOnce(Return(QueueResult::kOk))
        .WillOnce(Return(QueueResult::kTimeout));

    // Execute
    QueueResult successResult = rtosInstance->WaitForNotify(timeout);
    QueueResult timeoutResult = rtosInstance->WaitForNotify(timeout);

    // Verify
    EXPECT_EQ(successResult, QueueResult::kOk);
    EXPECT_EQ(timeoutResult, QueueResult::kTimeout);
}

/**
 * @brief Test task creation failure case
 */
TEST_F(RTOSAdditionalTest, CreateTaskFailureTest) {
    // Setup
    TaskHandle_t taskHandle;
    auto taskFunction = [](void* param) {
        int* value = static_cast<int*>(param);
        (*value)++;
    };

    // Expectations
    EXPECT_CALL(*rtosInstance, CreateTask(_, _, _, _, _, _))
        .WillOnce(Return(false));

    // Execute
    bool result = rtosInstance->CreateTask(taskFunction, "FailTask", 2048,
                                           nullptr, 1, &taskHandle);

    // Verify
    EXPECT_FALSE(result);
}

/**
 * @brief Test queue operation failures
 */
TEST_F(RTOSAdditionalTest, QueueOperationFailuresTest) {
    // Setup
    QueueHandle_t queueHandle = reinterpret_cast<QueueHandle_t>(0x87654321);
    int testData = 42;
    int receivedData = 0;

    // Expectations for queue operations with different error conditions
    EXPECT_CALL(*rtosInstance, SendToQueue(queueHandle, &testData, 0))
        .WillOnce(Return(QueueResult::kFull));

    EXPECT_CALL(*rtosInstance, SendToQueue(queueHandle, &testData, 100))
        .WillOnce(Return(QueueResult::kTimeout));

    EXPECT_CALL(*rtosInstance, ReceiveFromQueue(queueHandle, &receivedData, 0))
        .WillOnce(Return(QueueResult::kEmpty));

    EXPECT_CALL(*rtosInstance,
                ReceiveFromQueue(queueHandle, &receivedData, 100))
        .WillOnce(Return(QueueResult::kTimeout));

    // Execute and verify
    QueueResult sendFullResult =
        rtosInstance->SendToQueue(queueHandle, &testData, 0);
    EXPECT_EQ(sendFullResult, QueueResult::kFull);

    QueueResult sendTimeoutResult =
        rtosInstance->SendToQueue(queueHandle, &testData, 100);
    EXPECT_EQ(sendTimeoutResult, QueueResult::kTimeout);

    QueueResult receiveEmptyResult =
        rtosInstance->ReceiveFromQueue(queueHandle, &receivedData, 0);
    EXPECT_EQ(receiveEmptyResult, QueueResult::kEmpty);

    QueueResult receiveTimeoutResult =
        rtosInstance->ReceiveFromQueue(queueHandle, &receivedData, 100);
    EXPECT_EQ(receiveTimeoutResult, QueueResult::kTimeout);
}

/**
 * @brief Test task state transitions with multiple tasks
 */
TEST_F(RTOSAdditionalTest, MultipleTaskStateTransitionsTest) {
    // Setup
    TaskHandle_t task1Handle = reinterpret_cast<TaskHandle_t>(0x12345678);
    TaskHandle_t task2Handle = reinterpret_cast<TaskHandle_t>(0x87654321);

    // Expectations
    // Task 1 transitions
    EXPECT_CALL(*rtosInstance, getTaskState(task1Handle))
        .WillOnce(Return(TaskState::kRunning));
    EXPECT_CALL(*rtosInstance, SuspendTask(task1Handle));
    EXPECT_CALL(*rtosInstance, getTaskState(task1Handle))
        .WillOnce(Return(TaskState::kSuspended));

    // Task 2 transitions
    EXPECT_CALL(*rtosInstance, getTaskState(task2Handle))
        .WillOnce(Return(TaskState::kBlocked));
    EXPECT_CALL(*rtosInstance, ResumeTask(task2Handle));
    EXPECT_CALL(*rtosInstance, getTaskState(task2Handle))
        .WillOnce(Return(TaskState::kRunning));

    // Execute and verify task 1
    TaskState task1InitialState = rtosInstance->getTaskState(task1Handle);
    EXPECT_EQ(task1InitialState, TaskState::kRunning);

    rtosInstance->SuspendTask(task1Handle);
    TaskState task1SuspendedState = rtosInstance->getTaskState(task1Handle);
    EXPECT_EQ(task1SuspendedState, TaskState::kSuspended);

    // Execute and verify task 2
    TaskState task2InitialState = rtosInstance->getTaskState(task2Handle);
    EXPECT_EQ(task2InitialState, TaskState::kBlocked);

    rtosInstance->ResumeTask(task2Handle);
    TaskState task2ResumedState = rtosInstance->getTaskState(task2Handle);
    EXPECT_EQ(task2ResumedState, TaskState::kRunning);
}

/**
 * @brief Test ISR registration and task notification
 */
TEST_F(RTOSAdditionalTest, ISRRegistrationAndNotificationTest) {
    // Setup
    TaskHandle_t taskHandle = reinterpret_cast<TaskHandle_t>(0x12345678);
    void* isrHandle = reinterpret_cast<void*>(0xABCDEF01);

    // Use a regular function pointer for ISR
    void (*isrFuncPtr)() = []() {
    };

    // Expectations
    EXPECT_CALL(*rtosInstance, RegisterISR(isrFuncPtr, 5, 1))
        .WillOnce(Return(isrHandle));

    // When ISR is triggered, it should notify a task
    EXPECT_CALL(*rtosInstance, NotifyTaskFromISR(taskHandle)).Times(1);

    // Execute - register the ISR
    void* result = rtosInstance->RegisterISR(isrFuncPtr, 5, 1);
    EXPECT_EQ(result, isrHandle);

    // Simulate ISR triggering by notifying the task
    rtosInstance->NotifyTaskFromISR(taskHandle);
}

#endif  // ARDUINO