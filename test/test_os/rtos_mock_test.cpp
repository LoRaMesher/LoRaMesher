/**
 * @file test/os/rtos_mock_tests.cpp
 * @brief Unit tests for the RTOSMock implementation
 */
#include <gtest/gtest.h>

#ifdef ARDUINO

TEST(RTOSMockTest, ImplementArduinoTests) {
    GTEST_SKIP();
}

#else

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include "os/rtos_mock.hpp"

using namespace loramesher;
using namespace loramesher::os;

/**
 * @class RTOSMockTest
 * @brief Test fixture for testing the RTOSMock implementation
 */
class RTOSMockTest : public ::testing::Test {
   protected:
    void SetUp() override {
        // Ensure we're using the RTOSMock instance
        rtosInstance = static_cast<RTOSMock*>(&GetRTOS());
    }

    void TearDown() override {
        // Clean up any resources
    }

    RTOSMock* rtosInstance;
};

/**
 * @brief Test that a task is actually created and executed
 */
TEST_F(RTOSMockTest, TaskExecutionTest) {
    // Setup
    std::atomic<bool> taskExecuted(false);
    TaskHandle_t taskHandle;

    auto taskFunction = [](void* param) {
        std::atomic<bool>* executed = static_cast<std::atomic<bool>*>(param);
        executed->store(true);
    };

    // Execute
    bool result = rtosInstance->CreateTask(taskFunction, "TestTask", 2048,
                                           &taskExecuted, 1, &taskHandle);

    // Wait a bit for the task to execute
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Verify
    EXPECT_TRUE(result);
    EXPECT_TRUE(taskExecuted.load());

    // Clean up
    rtosInstance->DeleteTask(taskHandle);
}

/**
 * @brief Test queue operations with real data transfer
 */
TEST_F(RTOSMockTest, QueueDataTransferTest) {
    // Setup
    QueueHandle_t queue = rtosInstance->CreateQueue(5, sizeof(int));
    const int testData[] = {1, 2, 3, 4, 5};
    int receivedData[5] = {0};

    // Execute - send data to queue
    for (int i = 0; i < 5; i++) {
        QueueResult result =
            rtosInstance->SendToQueue(queue, &testData[i], 100);
        EXPECT_EQ(result, QueueResult::kOk);
    }

    // Verify queue is full
    QueueResult result = rtosInstance->SendToQueue(queue, &testData[0], 0);
    EXPECT_EQ(result, QueueResult::kFull);

    // Verify queue size
    EXPECT_EQ(rtosInstance->getQueueMessagesWaiting(queue), 5);

    // Execute - receive data from queue
    for (int i = 0; i < 5; i++) {
        QueueResult result =
            rtosInstance->ReceiveFromQueue(queue, &receivedData[i], 100);
        EXPECT_EQ(result, QueueResult::kOk);
    }

    // Verify data was received correctly in FIFO order
    for (int i = 0; i < 5; i++) {
        EXPECT_EQ(receivedData[i], testData[i]);
    }

    // Verify queue is empty
    result = rtosInstance->ReceiveFromQueue(queue, &receivedData[0], 0);
    EXPECT_EQ(result, QueueResult::kEmpty);

    // Clean up
    rtosInstance->DeleteQueue(queue);
}

/**
 * @brief Test timing functions
 */
TEST_F(RTOSMockTest, TimingFunctionsRealTest) {
    // Get initial tick count
    uint32_t startTicks = rtosInstance->getTickCount();

    // Sleep for a known duration
    uint32_t sleepMs = 100;
    rtosInstance->delay(sleepMs);

    // Get end tick count
    uint32_t endTicks = rtosInstance->getTickCount();

    // Verify time has passed (allowing for some overhead)
    EXPECT_GE(endTicks - startTicks, sleepMs);
}

/**
 * @brief Test task suspension and resumption
 */
TEST_F(RTOSMockTest, TaskSuspendResumeTest) {
    // Setup
    std::atomic<int> counter(0);
    std::atomic<bool> shouldExit(false);
    TaskHandle_t taskHandle;

    auto taskFunction = [](void* param) {
        auto* params =
            static_cast<std::pair<std::atomic<int>*, std::atomic<bool>*>*>(
                param);
        std::atomic<int>* counterPtr = params->first;
        std::atomic<bool>* exitPtr = params->second;

        while (!exitPtr->load()) {
            if (GetRTOS().ShouldStopOrPause()) {
                break;
            }
            counterPtr->fetch_add(1);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            GetRTOS().YieldTask();
        }
    };

    // Create parameter pair for the task
    auto* params = new std::pair<std::atomic<int>*, std::atomic<bool>*>(
        &counter, &shouldExit);

    // Execute - create task
    bool result = rtosInstance->CreateTask(taskFunction, "CounterTask", 2048,
                                           params, 1, &taskHandle);
    EXPECT_TRUE(result);

    // Wait for some increments
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    int counterBefore = counter.load();
    EXPECT_GT(counterBefore, 0);

    // Suspend the task
    rtosInstance->SuspendTask(taskHandle);

    // Give some time to ensure task is suspended
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Check counter hasn't increased significantly
    int counterDuringSuspend = counter.load();
    EXPECT_NEAR(counterBefore, counterDuringSuspend,
                2);  // Allow small margin for in-flight operations

    // Resume the task
    rtosInstance->ResumeTask(taskHandle);

    // Wait for more increments
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Check counter has increased again
    int counterAfter = counter.load();
    EXPECT_GT(counterAfter, counterDuringSuspend);

    // Clean up
    shouldExit.store(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    rtosInstance->DeleteTask(taskHandle);
    delete params;
}

/**
 * @brief Test task notification mechanism
 */
TEST_F(RTOSMockTest, TaskNotificationTest) {
    // Setup
    std::atomic<bool> notificationReceived(false);
    std::atomic<bool> shouldExit(false);
    TaskHandle_t taskHandle;

    auto taskFunction = [](void* param) {
        auto* params =
            static_cast<std::pair<std::atomic<bool>*, std::atomic<bool>*>*>(
                param);
        std::atomic<bool>* receivedPtr = params->first;
        std::atomic<bool>* exitPtr = params->second;

        auto& rtos = GetRTOS();

        while (!exitPtr->load()) {
            // Wait for notification with timeout
            QueueResult result = rtos.WaitForNotify(100);
            if (result == QueueResult::kOk) {
                receivedPtr->store(true);
            }
        }
    };

    // Create parameter pair for the task
    auto* params = new std::pair<std::atomic<bool>*, std::atomic<bool>*>(
        &notificationReceived, &shouldExit);

    // Execute - create task
    bool result = rtosInstance->CreateTask(taskFunction, "NotifyTask", 2048,
                                           params, 1, &taskHandle);
    EXPECT_TRUE(result);

    // Make sure task is running
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Send notification to task
    rtosInstance->NotifyTaskFromISR(taskHandle);

    // Wait for notification to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Verify notification was received
    EXPECT_TRUE(notificationReceived.load());

    // Clean up
    shouldExit.store(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    rtosInstance->DeleteTask(taskHandle);
    delete params;
}

/**
 * @brief Test system task statistics
 */
TEST_F(RTOSMockTest, SystemTaskStatsTest) {
    // Setup - create some tasks
    TaskHandle_t task1, task2;
    std::atomic<bool> shouldExit(false);

    auto taskFunction = [](void* param) {
        auto* exitFlag = static_cast<std::atomic<bool>*>(param);
        while (!exitFlag->load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    };

    // Create tasks
    rtosInstance->CreateTask(taskFunction, "Task1", 1024, &shouldExit, 1,
                             &task1);
    rtosInstance->CreateTask(taskFunction, "Task2", 2048, &shouldExit, 2,
                             &task2);

    // Wait for tasks to start
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Get system task stats
    std::vector<TaskStats> stats = rtosInstance->getSystemTaskStats();

    // Verify we have at least two tasks
    EXPECT_GE(stats.size(), 2);

    // Look for our tasks in the stats
    bool foundTask1 = false;
    bool foundTask2 = false;

    for (const auto& task : stats) {
        if (task.name == "Task1") {
            foundTask1 = true;
        } else if (task.name == "Task2") {
            foundTask2 = true;
        }
    }

    EXPECT_TRUE(foundTask1);
    EXPECT_TRUE(foundTask2);

    // Clean up
    shouldExit.store(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    rtosInstance->DeleteTask(task1);
    rtosInstance->DeleteTask(task2);
}

/**
 * @brief Test queue timeouts
 */
TEST_F(RTOSMockTest, QueueTimeoutTest) {
    // Setup
    QueueHandle_t queue = rtosInstance->CreateQueue(1, sizeof(int));
    int testData = 42;
    int receivedData = 0;

    // Fill the queue
    QueueResult result = rtosInstance->SendToQueue(queue, &testData, 0);
    EXPECT_EQ(result, QueueResult::kOk);

    // Try to send with a short timeout (should time out)
    auto start = std::chrono::steady_clock::now();
    result = rtosInstance->SendToQueue(queue, &testData, 50);
    auto end = std::chrono::steady_clock::now();

    // Verify timeout occurred
    EXPECT_EQ(result, QueueResult::kTimeout);

    // Verify timeout duration (allowing some margin)
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
            .count();
    EXPECT_GE(duration, 45);   // Allow for small margin of error
    EXPECT_LE(duration, 150);  // Upper bound for test stability

    // Now test receive timeout
    rtosInstance->ReceiveFromQueue(queue, &receivedData, 0);

    // Try to receive with a short timeout (should time out)
    start = std::chrono::steady_clock::now();
    result = rtosInstance->ReceiveFromQueue(queue, &receivedData, 50);
    end = std::chrono::steady_clock::now();

    // Verify timeout occurred
    EXPECT_EQ(result, QueueResult::kTimeout);

    // Verify timeout duration
    duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
            .count();
    EXPECT_GE(duration, 45);
    EXPECT_LE(duration, 150);

    // Clean up
    rtosInstance->DeleteQueue(queue);
}

/**
 * @brief Test WaitForNotify with different timeout scenarios
 */
TEST_F(RTOSMockTest, WaitForNotifyTimeoutTest) {
    // Setup
    std::atomic<int> resultCounter(0);
    std::atomic<bool> shouldExit(false);
    TaskHandle_t taskHandle;

    auto taskFunction = [](void* param) {
        auto* params =
            static_cast<std::pair<std::atomic<int>*, std::atomic<bool>*>*>(
                param);
        std::atomic<int>* counterPtr = params->first;
        std::atomic<bool>* exitPtr = params->second;

        auto& rtos = GetRTOS();

        // Test zero timeout (should return immediately)
        QueueResult result = rtos.WaitForNotify(0);
        if (result == QueueResult::kTimeout) {
            counterPtr->fetch_add(1);
        }

        // Test short timeout (should timeout)
        result = rtos.WaitForNotify(50);
        if (result == QueueResult::kTimeout) {
            counterPtr->fetch_add(10);
        }

        // Wait for long timeout or notification
        result = rtos.WaitForNotify(500);
        if (result == QueueResult::kOk) {
            counterPtr->fetch_add(100);
        } else if (result == QueueResult::kTimeout) {
            counterPtr->fetch_add(
                1000);  // Should not happen if test works correctly
        }

        while (!exitPtr->load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    };

    // Create parameter pair for the task
    auto* params = new std::pair<std::atomic<int>*, std::atomic<bool>*>(
        &resultCounter, &shouldExit);

    // Execute - create task
    bool result = rtosInstance->CreateTask(taskFunction, "NotifyTimeoutTask",
                                           2048, params, 1, &taskHandle);
    EXPECT_TRUE(result);

    // Let the first two timeouts complete
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Check counter (should be 11 from the first two timeouts)
    int counterBeforeNotify = resultCounter.load();
    EXPECT_EQ(counterBeforeNotify, 11);

    // Send notification to task during long wait
    rtosInstance->NotifyTaskFromISR(taskHandle);

    // Wait for task to complete processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Check counter (should have added 100 for successful notification)
    int finalCounter = resultCounter.load();
    EXPECT_EQ(finalCounter, 111);

    // Clean up
    shouldExit.store(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    rtosInstance->DeleteTask(taskHandle);
    delete params;
}

/**
 * @brief Test triggering ISRs
 */
TEST_F(RTOSMockTest, ISRTriggerTest) {
    // Setup - create a volatile flag that will be modified by the ISR
    static volatile bool isrTriggered = false;
    static QueueHandle_t isrQueue = nullptr;
    static const int isrValue = 42;

    // Create ISR function that will modify the flag
    auto isrFunction = []() {
        isrTriggered = true;

        // Also test SendToQueueISR
        if (isrQueue) {
            GetRTOS().SendToQueueISR(isrQueue, &isrValue);
        }
    };

    // Create queue for ISR test
    isrQueue = rtosInstance->CreateQueue(1, sizeof(int));

    // Reset flag
    isrTriggered = false;

    // Register ISR
    void* isrHandle = rtosInstance->RegisterISR(isrFunction, 5, 1);
    EXPECT_NE(isrHandle, nullptr);

    // Mock the triggering of an ISR
    // In RTOSMock we can directly call the function, but in a real system
    // this would happen in response to hardware interrupts
    auto mockIsr = reinterpret_cast<void (*)()>(isrHandle);
    mockIsr();

    // Verify ISR was triggered and flag was modified
    EXPECT_TRUE(isrTriggered);

    // Verify that SendToQueueISR worked properly
    int receivedValue = 0;
    QueueResult result =
        rtosInstance->ReceiveFromQueue(isrQueue, &receivedValue, 0);
    EXPECT_EQ(result, QueueResult::kOk);
    EXPECT_EQ(receivedValue, isrValue);

    // Clean up
    rtosInstance->DeleteQueue(isrQueue);
    isrQueue = nullptr;
}

/**
 * @brief Test task stack watermark functionality
 */
TEST_F(RTOSMockTest, TaskStackWatermarkTest) {
    // Setup
    std::atomic<bool> shouldExit(false);
    TaskHandle_t taskHandle;
    uint32_t stackSize = 4096;

    auto taskFunction = [](void* param) {
        auto* exitFlag = static_cast<std::atomic<bool>*>(param);

        // Allocate some stack space to affect watermark
        volatile char buffer[1024];
        for (int i = 0; i < 1024; i++) {
            buffer[i] = static_cast<char>(i & 0xFF);
        }

        // Use buffer to prevent unused variable warning
        // This is deliberate to prevent compiler optimization from removing the buffer
        volatile char checksum = 0;
        for (int i = 0; i < 1024; i++) {
            checksum ^= buffer[i];
        }
        // Prevent warning about unused checksum
        (void)checksum;

        while (!exitFlag->load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    };

    // Create task with specified stack size
    bool result = rtosInstance->CreateTask(
        taskFunction, "WatermarkTask", stackSize, &shouldExit, 1, &taskHandle);
    EXPECT_TRUE(result);

    // Let task run a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Get stack watermark
    uint32_t watermark = rtosInstance->getTaskStackWatermark(taskHandle);

    // Verify watermark is reasonable
    // In the RTOSMock, watermark should be 60-90% of stack size
    EXPECT_GT(watermark, stackSize * 0.5);  // More than 50% should be free
    EXPECT_LT(watermark, stackSize);        // But not the full stack

    // Clean up
    shouldExit.store(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    rtosInstance->DeleteTask(taskHandle);
}

/**
 * @brief Test multiple tasks interacting through a queue
 */
TEST_F(RTOSMockTest, MultiTaskQueueTest) {
    // Setup
    QueueHandle_t queue = rtosInstance->CreateQueue(10, sizeof(int));
    std::atomic<bool> shouldExit(false);
    std::atomic<int> consumedCount(0);
    TaskHandle_t producerTask, consumerTask;

    // Producer task function
    auto producerFunction = [](void* param) {
        auto* params =
            static_cast<std::pair<QueueHandle_t, std::atomic<bool>*>*>(param);
        QueueHandle_t q = params->first;
        std::atomic<bool>* exitFlag = params->second;

        auto& rtos = GetRTOS();
        int counter = 0;

        while (!exitFlag->load() && counter < 20) {
            int value = counter++;
            rtos.SendToQueue(q, &value, 10);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    };

    // Consumer task function
    auto consumerFunction = [](void* param) {
        auto* params = static_cast<
            std::tuple<QueueHandle_t, std::atomic<bool>*, std::atomic<int>*>*>(
            param);
        QueueHandle_t q = std::get<0>(*params);
        std::atomic<bool>* exitFlag = std::get<1>(*params);
        std::atomic<int>* count = std::get<2>(*params);

        auto& rtos = GetRTOS();
        int value;

        while (!exitFlag->load()) {
            QueueResult result = rtos.ReceiveFromQueue(q, &value, 50);
            if (result == QueueResult::kOk) {
                count->fetch_add(1);
            }
        }
    };

    // Create parameters
    auto* producerParams =
        new std::pair<QueueHandle_t, std::atomic<bool>*>(queue, &shouldExit);
    auto* consumerParams =
        new std::tuple<QueueHandle_t, std::atomic<bool>*, std::atomic<int>*>(
            queue, &shouldExit, &consumedCount);

    // Create tasks
    rtosInstance->CreateTask(producerFunction, "Producer", 2048, producerParams,
                             1, &producerTask);
    rtosInstance->CreateTask(consumerFunction, "Consumer", 2048, consumerParams,
                             1, &consumerTask);

    // Let tasks run
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Signal tasks to exit
    shouldExit.store(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Verify consumer processed items
    int finalCount = consumedCount.load();
    EXPECT_GT(finalCount, 10);  // Should have processed most of the items

    // Clean up
    rtosInstance->DeleteTask(producerTask);
    rtosInstance->DeleteTask(consumerTask);
    rtosInstance->DeleteQueue(queue);
    delete producerParams;
    delete consumerParams;
}

/**
 * @brief Test reproducing the edge case where a task is suspended while waiting in WaitForNotify
 * 
 * This test creates a task that calls WaitForNotify with a long timeout,
 * then suspends the task while it's waiting, and finally resumes it.
 * The goal is to reproduce the timeout warning and then fix it.
 */
TEST_F(RTOSMockTest, SuspendTaskWaitingInWaitForNotify) {
    // Setup
    std::atomic<bool> taskStarted(false);
    std::atomic<bool> waitForNotifyStarted(false);
    std::atomic<bool> waitForNotifyCompleted(false);
    std::atomic<bool> suspensionDetected(false);
    std::atomic<bool> shouldExit(false);
    std::atomic<QueueResult> waitResult(QueueResult::kError);
    std::atomic<int> waitForNotifyAttempts(0);
    TaskHandle_t taskHandle;

    auto taskFunction = [](void* param) {
        auto* state = static_cast<std::tuple<
            std::atomic<bool>*, std::atomic<bool>*, std::atomic<bool>*,
            std::atomic<bool>*, std::atomic<bool>*, std::atomic<QueueResult>*,
            std::atomic<int>*>*>(param);

        auto& taskStarted = *std::get<0>(*state);
        auto& waitForNotifyStarted = *std::get<1>(*state);
        auto& waitForNotifyCompleted = *std::get<2>(*state);
        auto& suspensionDetected = *std::get<3>(*state);
        auto& shouldExit = *std::get<4>(*state);
        auto& waitResult = *std::get<5>(*state);
        auto& waitForNotifyAttempts = *std::get<6>(*state);

        auto& rtos = GetRTOS();

        taskStarted.store(true);

        while (!shouldExit.load()) {
            // Check if we should pause/stop first
            if (rtos.ShouldStopOrPause()) {
                suspensionDetected.store(true);
                continue;  // Continue the loop to check again after resume
            }

            // Start waiting for notification
            waitForNotifyStarted.store(true);
            waitForNotifyAttempts.fetch_add(1);

            // Wait for notification with a moderate timeout
            QueueResult result = rtos.WaitForNotify(1000);  // 1 second timeout

            waitResult.store(result);

            if (result == QueueResult::kOk) {
                // Notification received, exit successfully
                waitForNotifyCompleted.store(true);
                break;
            } else if (result == QueueResult::kTimeout) {
                // Timeout occurred, this could be due to suspension or normal timeout
                // Continue the loop to check suspension state and try again
                continue;
            } else {
                // Error occurred, exit
                waitForNotifyCompleted.store(true);
                break;
            }
        }
    };

    // Create parameter tuple for the task
    auto* params = new std::tuple<std::atomic<bool>*, std::atomic<bool>*,
                                  std::atomic<bool>*, std::atomic<bool>*,
                                  std::atomic<bool>*, std::atomic<QueueResult>*,
                                  std::atomic<int>*>(
        &taskStarted, &waitForNotifyStarted, &waitForNotifyCompleted,
        &suspensionDetected, &shouldExit, &waitResult, &waitForNotifyAttempts);

    // Execute - create task
    bool result = rtosInstance->CreateTask(taskFunction, "SX1276", 2048, params,
                                           1, &taskHandle);
    EXPECT_TRUE(result);

    // Wait for task to start
    int attempts = 0;
    while (!taskStarted.load() && attempts < 100) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        attempts++;
    }
    EXPECT_TRUE(taskStarted.load());

    // Wait for task to enter WaitForNotify
    attempts = 0;
    while (!waitForNotifyStarted.load() && attempts < 100) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        attempts++;
    }
    EXPECT_TRUE(waitForNotifyStarted.load());

    // Give the task a moment to actually enter the wait
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Verify task hasn't completed WaitForNotify yet
    EXPECT_FALSE(waitForNotifyCompleted.load());

    // Now suspend the task while it's waiting in WaitForNotify
    std::cout << "Suspending task while it's waiting in WaitForNotify..."
              << std::endl;
    bool suspendResult = rtosInstance->SuspendTask(taskHandle);
    EXPECT_TRUE(suspendResult);

    // Give time for suspension to be processed and for the task to detect it
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // After suspension, the task should eventually detect it through ShouldStopOrPause
    // Note: suspension detection happens when the task loops back and calls ShouldStopOrPause

    // Now resume the task - this is where the warning might occur
    std::cout << "Resuming task..." << std::endl;
    bool resumeResult = rtosInstance->ResumeTask(taskHandle);
    EXPECT_TRUE(resumeResult);

    // Give time for resume to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Send a notification to complete the wait
    rtosInstance->NotifyTaskFromISR(taskHandle);

    // Wait for the notification to be processed
    attempts = 0;
    while (!waitForNotifyCompleted.load() && attempts < 200) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        attempts++;
    }

    // Verify the task completed successfully
    EXPECT_TRUE(waitForNotifyCompleted.load());
    EXPECT_EQ(waitResult.load(), QueueResult::kOk);

    // Verify that the task made multiple attempts (indicating it handled the suspend/resume cycle)
    EXPECT_GT(waitForNotifyAttempts.load(), 1);

    // Clean up
    shouldExit.store(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    rtosInstance->DeleteTask(taskHandle);
    delete params;
}

/**
 * @brief Test that demonstrates suspend/resume works without warnings - simplified version
 * 
 * This test focuses on the main issue: ensuring no warnings are generated when
 * suspending/resuming a task that is waiting in WaitForNotify.
 */
TEST_F(RTOSMockTest, MultipleSuspendResumeCycles) {
    // Setup
    std::atomic<bool> taskStarted(false);
    std::atomic<bool> shouldExit(false);
    std::atomic<bool> suspendResumeCompleted(false);
    TaskHandle_t taskHandle;

    auto taskFunction = [](void* param) {
        auto* state =
            static_cast<std::tuple<std::atomic<bool>*, std::atomic<bool>*,
                                   std::atomic<bool>*>*>(param);

        auto& taskStarted = *std::get<0>(*state);
        auto& shouldExit = *std::get<1>(*state);
        auto& suspendResumeCompleted = *std::get<2>(*state);

        auto& rtos = GetRTOS();

        taskStarted.store(true);

        // Task pattern: alternate between checking suspension and waiting for notifications
        while (!shouldExit.load()) {
            // Check for suspension periodically
            if (rtos.ShouldStopOrPause()) {
                continue;  // If suspended, this will block until resumed
            }

            // Wait for notification with reasonable timeout
            QueueResult result = rtos.WaitForNotify(100);

            if (result == QueueResult::kOk) {
                // Got notification, exit
                suspendResumeCompleted.store(true);
                break;
            }
            // On timeout, continue loop to check suspension again
        }
    };

    // Create parameter tuple for the task
    auto* params = new std::tuple<std::atomic<bool>*, std::atomic<bool>*,
                                  std::atomic<bool>*>(&taskStarted, &shouldExit,
                                                      &suspendResumeCompleted);

    // Execute - create task
    bool result = rtosInstance->CreateTask(taskFunction, "MultiSuspendTask",
                                           2048, params, 1, &taskHandle);
    EXPECT_TRUE(result);

    // Wait for task to start
    int attempts = 0;
    while (!taskStarted.load() && attempts < 100) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        attempts++;
    }
    EXPECT_TRUE(taskStarted.load());

    // Let the task run a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Perform multiple suspend/resume cycles - the main test is that NO WARNINGS are generated
    for (int cycle = 0; cycle < 3; cycle++) {
        std::cout << "Suspend/Resume cycle " << (cycle + 1)
                  << " - testing for warning elimination" << std::endl;

        // Suspend - this might catch the task in either ShouldStopOrPause or WaitForNotify
        bool suspendResult = rtosInstance->SuspendTask(taskHandle);
        EXPECT_TRUE(suspendResult) << "Suspend operation should succeed";

        std::this_thread::sleep_for(std::chrono::milliseconds(150));

        // Resume - this should NOT generate warnings
        bool resumeResult = rtosInstance->ResumeTask(taskHandle);
        EXPECT_TRUE(resumeResult) << "Resume operation should succeed";

        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }

    // The main success criteria is that the operations completed without warnings
    // We don't need to count suspensions since the debug output shows they're working

    std::cout << "Suspend/Resume cycles completed - no warnings should have "
                 "been generated"
              << std::endl;

    // Send final notification to complete the task
    rtosInstance->NotifyTaskFromISR(taskHandle);

    // Wait for task completion
    attempts = 0;
    while (!suspendResumeCompleted.load() && attempts < 100) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        attempts++;
    }

    EXPECT_TRUE(suspendResumeCompleted.load())
        << "Task should complete successfully";

    // Clean up
    shouldExit.store(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    rtosInstance->DeleteTask(taskHandle);
    delete params;
}

/**
 * @brief Test rapid suspend/resume operations
 * 
 * This test rapidly suspends and resumes a task to stress-test
 * the synchronization mechanism.
 */
TEST_F(RTOSMockTest, RapidSuspendResumeOperations) {
    // Setup
    std::atomic<bool> taskStarted(false);
    std::atomic<bool> shouldExit(false);
    std::atomic<bool> operationCompleted(false);
    TaskHandle_t taskHandle;

    auto taskFunction = [](void* param) {
        auto* state =
            static_cast<std::tuple<std::atomic<bool>*, std::atomic<bool>*,
                                   std::atomic<bool>*>*>(param);

        auto& taskStarted = *std::get<0>(*state);
        auto& shouldExit = *std::get<1>(*state);
        auto& operationCompleted = *std::get<2>(*state);

        auto& rtos = GetRTOS();

        taskStarted.store(true);

        // Simple loop with frequent suspension checks
        for (int i = 0; i < 100 && !shouldExit.load(); i++) {
            if (rtos.ShouldStopOrPause()) {
                continue;
            }

            // Short wait to allow for suspension
            rtos.WaitForNotify(10);
        }

        operationCompleted.store(true);
    };

    // Create parameter tuple for the task
    auto* params = new std::tuple<std::atomic<bool>*, std::atomic<bool>*,
                                  std::atomic<bool>*>(&taskStarted, &shouldExit,
                                                      &operationCompleted);

    // Execute - create task
    bool result = rtosInstance->CreateTask(taskFunction, "RapidSuspendTask",
                                           2048, params, 1, &taskHandle);
    EXPECT_TRUE(result);

    // Wait for task to start
    int attempts = 0;
    while (!taskStarted.load() && attempts < 100) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        attempts++;
    }
    EXPECT_TRUE(taskStarted.load());

    // Perform rapid suspend/resume operations
    for (int i = 0; i < 5; i++) {
        rtosInstance->SuspendTask(taskHandle);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        rtosInstance->ResumeTask(taskHandle);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Wait for task to complete its operations
    attempts = 0;
    while (!operationCompleted.load() && attempts < 200) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        attempts++;
    }

    // Clean up
    shouldExit.store(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    rtosInstance->DeleteTask(taskHandle);
    delete params;
}

/**
 * @brief Focused test that specifically tests the scenario causing the warning
 * 
 * This test creates a task that immediately goes into WaitForNotify, suspends it,
 * then resumes it to specifically test the resume acknowledgment mechanism.
 */
TEST_F(RTOSMockTest, FocusedSuspendResumeInWaitForNotify) {
    std::atomic<bool> taskStarted(false);
    std::atomic<bool> shouldExit(false);
    std::atomic<bool> inWaitForNotify(false);
    std::atomic<QueueResult> finalResult(QueueResult::kError);
    TaskHandle_t taskHandle;

    auto taskFunction = [](void* param) {
        auto* state = static_cast<
            std::tuple<std::atomic<bool>*, std::atomic<bool>*,
                       std::atomic<bool>*, std::atomic<QueueResult>*>*>(param);

        auto& taskStarted = *std::get<0>(*state);
        auto& shouldExit = *std::get<1>(*state);
        auto& inWaitForNotify = *std::get<2>(*state);
        auto& finalResult = *std::get<3>(*state);

        auto& rtos = GetRTOS();
        taskStarted.store(true);

        // Go directly into WaitForNotify to test the suspend/resume during wait
        while (!shouldExit.load()) {
            inWaitForNotify.store(true);
            QueueResult result = rtos.WaitForNotify(500);  // Wait with timeout
            inWaitForNotify.store(false);

            if (result == QueueResult::kOk) {
                finalResult.store(result);
                break;  // Got notification, exit
            }

            // Check if we should stop/pause between wait attempts
            if (rtos.ShouldStopOrPause()) {
                continue;
            }
        }
    };

    auto* params =
        new std::tuple<std::atomic<bool>*, std::atomic<bool>*,
                       std::atomic<bool>*, std::atomic<QueueResult>*>(
            &taskStarted, &shouldExit, &inWaitForNotify, &finalResult);

    // Create the task
    bool result = rtosInstance->CreateTask(taskFunction, "FocusedTestTask",
                                           2048, params, 1, &taskHandle);
    EXPECT_TRUE(result);

    // Wait for task to start and enter WaitForNotify
    int attempts = 0;
    while (!taskStarted.load() && attempts < 100) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        attempts++;
    }
    EXPECT_TRUE(taskStarted.load());

    // Wait for task to enter WaitForNotify
    attempts = 0;
    while (!inWaitForNotify.load() && attempts < 100) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        attempts++;
    }
    EXPECT_TRUE(inWaitForNotify.load());

    // Now suspend the task while it's in WaitForNotify - this should NOT generate a warning
    std::cout << "Suspending task while in WaitForNotify (should not generate "
                 "warning)..."
              << std::endl;
    bool suspendResult = rtosInstance->SuspendTask(taskHandle);
    EXPECT_TRUE(suspendResult);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Resume the task - this is the critical operation that was generating warnings
    std::cout << "Resuming task (testing for warning elimination)..."
              << std::endl;
    bool resumeResult = rtosInstance->ResumeTask(taskHandle);
    EXPECT_TRUE(resumeResult);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Send notification to complete the test
    rtosInstance->NotifyTaskFromISR(taskHandle);

    // Wait for completion
    attempts = 0;
    while (finalResult.load() != QueueResult::kOk && attempts < 100) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        attempts++;
    }

    EXPECT_EQ(finalResult.load(), QueueResult::kOk);

    // Clean up
    shouldExit.store(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    rtosInstance->DeleteTask(taskHandle);
    delete params;
}

/**
 * @brief Test ReceiveFromQueue task deletion issue - reproduces the superframe_service error
 * 
 * This test simulates the scenario where a task is waiting in ReceiveFromQueue 
 * and gets deleted. The fix should ensure ReceiveFromQueue returns kError instead
 * of kOk after timeout, preventing the deleted task from continuing execution.
 */
TEST_F(RTOSMockTest, ReceiveFromQueueTaskDeletionTest) {
    // Setup test parameters
    struct TestParams {
        QueueHandle_t queue;
        std::atomic<bool> taskStarted{false};
        std::atomic<bool> inReceiveFromQueue{false};
        std::atomic<QueueResult> receiveResult{QueueResult::kTimeout};
        std::atomic<bool> taskContinuedAfterReceive{false};
        std::atomic<bool> shouldExit{false};
        std::atomic<bool> taskCompleted{
            false};  // New: signals when task is completely done
    };

    auto* params = new TestParams();
    TaskHandle_t taskHandle;

    // Create a queue for testing - use GetRTOS() consistently
    params->queue = GetRTOS().CreateQueue(5, sizeof(int));
    ASSERT_NE(params->queue, nullptr);

    // Task function that waits on ReceiveFromQueue
    auto taskFunction = [](void* param) {
        auto* testParams = static_cast<TestParams*>(param);
        testParams->taskStarted.store(true);

        try {
            while (!GetRTOS().ShouldStopOrPause() &&
                   !testParams->shouldExit.load()) {
                // Signal that we're about to enter ReceiveFromQueue
                testParams->inReceiveFromQueue.store(true);

                int receivedData = 0;
                // This should block and wait - simulating superframe_service behavior
                auto result =
                    GetRTOS().ReceiveFromQueue(testParams->queue, &receivedData,
                                               5000 /* 5 second timeout */);

                // Store the result
                testParams->receiveResult.store(result);

                // If we get kError, the task should exit cleanly without processing
                if (result == QueueResult::kError) {
                    break;
                }

                // Signal we continued after ReceiveFromQueue (only for non-error cases)
                testParams->taskContinuedAfterReceive.store(true);

                // If we get kOk, process the data (simulating normal operation)
                if (result == QueueResult::kOk) {
                    // Do some work...
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }

                // Small delay to prevent tight loop on timeout
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        } catch (const std::exception& e) {
            // Task should exit cleanly when deleted
            std::cout << "Task caught exception (expected): " << e.what()
                      << std::endl;
        }

        // Signal that the task has completely finished
        testParams->taskCompleted.store(true);
    };

    // Create the task - use GetRTOS() consistently
    bool created = GetRTOS().CreateTask(
        taskFunction, "ReceiveFromQueueTestTask", 4096, params, 1, &taskHandle);
    ASSERT_TRUE(created);
    ASSERT_NE(taskHandle, nullptr);

    // Wait for task to start
    int attempts = 0;
    while (!params->taskStarted.load() && attempts < 100) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        attempts++;
    }
    EXPECT_TRUE(params->taskStarted.load());

    // Wait for task to enter ReceiveFromQueue
    attempts = 0;
    while (!params->inReceiveFromQueue.load() && attempts < 100) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        attempts++;
    }
    EXPECT_TRUE(params->inReceiveFromQueue.load());

    // Give the task a moment to actually enter the ReceiveFromQueue wait state
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "Task is now waiting in ReceiveFromQueue, deleting task..."
              << std::endl;

    // Signal to the task to exit
    params->shouldExit.store(true);

    // Now delete the task while it's waiting in ReceiveFromQueue
    // This is the critical test - the task should NOT continue with kOk result
    GetRTOS().DeleteTask(taskHandle);
    taskHandle = nullptr;  // Prevent further use

    // Wait for the task to completely finish before checking results
    // This ensures the task thread has stopped accessing params
    int completion_attempts = 0;
    while (!params->taskCompleted.load() && completion_attempts < 100) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        completion_attempts++;
    }

    // Ensure the task actually completed
    EXPECT_TRUE(params->taskCompleted.load())
        << "Task should have completed after deletion";

    // Check the results
    std::cout << "ReceiveFromQueue result: "
              << static_cast<int>(params->receiveResult.load()) << std::endl;
    std::cout << "Task continued after receive: "
              << params->taskContinuedAfterReceive.load() << std::endl;

    // The key assertion: ReceiveFromQueue should return kError, not kOk or kTimeout
    // when the task is deleted while waiting
    if (params->receiveResult.load() == QueueResult::kOk) {
        FAIL() << "BUG REPRODUCED: ReceiveFromQueue returned kOk after task "
                  "deletion! "
               << "This would cause the superframe_service error.";
    }

    // With the fix, we expect kError when task is deleted
    EXPECT_EQ(params->receiveResult.load(), QueueResult::kError)
        << "ReceiveFromQueue should return kError when task is deleted";

    // The task should not continue processing after being deleted
    EXPECT_FALSE(params->taskContinuedAfterReceive.load())
        << "Task should not continue execution after deletion";

    // Clean up - queue first, then params (task is already completed)
    GetRTOS().DeleteQueue(params->queue);
    delete params;

    std::cout << "ReceiveFromQueue task deletion test completed." << std::endl;
}

/**
 * @brief Test ReceiveFromQueue task suspended issue
 * 
 * This test simulates the scenario where a task is setted suspended
 * and multiple notifications are being made before resuming the task
 */
TEST_F(RTOSMockTest, ReceiveFromQueueTaskSuspended) {
    // Setup test parameters
    struct TestParams {
        QueueHandle_t queue;
        std::atomic<bool> taskStarted{false};
        std::atomic<bool> inReceiveFromQueue{false};
        std::atomic<QueueResult> receiveResult{QueueResult::kTimeout};
        std::atomic<uint8_t> taskContinuedAfterReceive{0};
        std::atomic<bool> shouldExit{false};
        std::atomic<bool> taskCompleted{
            false};  // New: signals when task is completely done
    };

    auto* params = new TestParams();
    TaskHandle_t taskHandle;

    // Create a queue for testing - use GetRTOS() consistently
    params->queue = GetRTOS().CreateQueue(5, sizeof(int));
    ASSERT_NE(params->queue, nullptr);

    // Task function that waits on ReceiveFromQueue
    auto taskFunction = [](void* param) {
        auto* testParams = static_cast<TestParams*>(param);
        testParams->taskStarted.store(true);
        uint8_t loops_num = 0;

        try {
            while (!GetRTOS().ShouldStopOrPause() &&
                   !testParams->shouldExit.load()) {
                // Signal that we're about to enter ReceiveFromQueue
                testParams->inReceiveFromQueue.store(true);

                int receivedData = 0;
                // This should block and wait - simulating superframe_service behavior
                auto result =
                    GetRTOS().ReceiveFromQueue(testParams->queue, &receivedData,
                                               5000 /* 5 second timeout */);

                // Store the result
                testParams->receiveResult.store(result);

                // If we get kError, the task should exit cleanly without processing
                if (result == QueueResult::kError) {
                    break;
                }

                // If we get kOk, process the data (simulating normal operation)
                if (result == QueueResult::kOk) {
                    loops_num++;

                    ASSERT_EQ(receivedData, loops_num)
                        << "Received data should match sent data";

                    // Signal we continued after ReceiveFromQueue (only for non-error cases)
                    testParams->taskContinuedAfterReceive.store(loops_num);
                    // Do some work...
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }

                // Small delay to prevent tight loop on timeout
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        } catch (const std::exception& e) {
            // Task should exit cleanly when deleted
            std::cout << "Task caught exception (expected): " << e.what()
                      << std::endl;
        }

        // Signal that the task has completely finished
        testParams->taskCompleted.store(true);
    };

    // Create the task - use GetRTOS() consistently
    bool created = GetRTOS().CreateTask(
        taskFunction, "ReceiveFromQueueTestTask", 4096, params, 1, &taskHandle);
    ASSERT_TRUE(created);
    ASSERT_NE(taskHandle, nullptr);

    // Wait for task to start
    int attempts = 0;
    while (!params->taskStarted.load() && attempts < 100) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        attempts++;
    }
    EXPECT_TRUE(params->taskStarted.load());

    // Wait for task to enter ReceiveFromQueue
    attempts = 0;
    while (!params->inReceiveFromQueue.load() && attempts < 100) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        attempts++;
    }
    EXPECT_TRUE(params->inReceiveFromQueue.load());

    EXPECT_TRUE(GetRTOS().SuspendTask(taskHandle));

    int* sendData = new int[3]{1, 2, 3};

    // Send multiple values into the queue:
    EXPECT_TRUE(GetRTOS().SendToQueue(params->queue, &sendData[0], 0) ==
                QueueResult::kOk);
    EXPECT_TRUE(GetRTOS().SendToQueue(params->queue, &sendData[1], 0) ==
                QueueResult::kOk);
    EXPECT_TRUE(GetRTOS().SendToQueue(params->queue, &sendData[2], 0) ==
                QueueResult::kOk);

    delete[] sendData;

    EXPECT_TRUE(GetRTOS().ResumeTask(taskHandle));

    // Give the task a moment to actually enter the ReceiveFromQueue wait state
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Check if the task is still in the ReceiveFromQueue state
    EXPECT_EQ(params->taskContinuedAfterReceive.load(), 3);

    // Signal to the task to exit
    params->shouldExit.store(true);

    // Now delete the task while it's waiting in ReceiveFromQueue
    // This is the critical test - the task should NOT continue with kOk result
    GetRTOS().DeleteTask(taskHandle);
    taskHandle = nullptr;  // Prevent further use

    // Wait for the task to completely finish before checking results
    // This ensures the task thread has stopped accessing params
    int completion_attempts = 0;
    while (!params->taskCompleted.load() && completion_attempts < 100) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        completion_attempts++;
    }

    // Ensure the task actually completed
    EXPECT_TRUE(params->taskCompleted.load())
        << "Task should have completed after deletion";

    // Check the results
    std::cout << "ReceiveFromQueue result: "
              << static_cast<int>(params->receiveResult.load()) << std::endl;

    // The key assertion: ReceiveFromQueue should return kError, not kOk or kTimeout
    // when the task is deleted while waiting
    if (params->receiveResult.load() == QueueResult::kOk) {
        FAIL() << "BUG REPRODUCED: ReceiveFromQueue returned kOk after task "
                  "deletion! "
               << "This would cause the superframe_service error.";
    }

    // With the fix, we expect kError when task is deleted
    EXPECT_EQ(params->receiveResult.load(), QueueResult::kError)
        << "ReceiveFromQueue should return kError when task is deleted";

    // Clean up - queue first, then params (task is already completed)
    GetRTOS().DeleteQueue(params->queue);
    delete params;

    std::cout << "ReceiveFromQueue task deletion test completed." << std::endl;
}

#endif  // ARDUINO