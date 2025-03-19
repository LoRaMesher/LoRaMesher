/**
 * @file test/os/rtos_mock.hpp
 * @brief Google Mock implementation for RTOS interface
 */
#pragma once

#include <gmock/gmock.h>
#include "os/rtos.hpp"

namespace loramesher {
namespace test {

/**
 * @class MockRTOS
 * @brief Google Mock implementation of the RTOS interface
 */
class MockRTOS : public os::RTOS {
   public:
    /**
     * @brief Get singleton instance of MockRTOS
     * @return Reference to MockRTOS instance
     */
    static MockRTOS& instance() {
        static MockRTOS instance;
        return instance;
    }

    MOCK_METHOD(bool, CreateTask,
                (os::TaskFunction_t taskFunction, const char* name,
                 uint32_t stackSize, void* parameters, uint32_t priority,
                 os::TaskHandle_t* taskHandle),
                (override));

    MOCK_METHOD(void, DeleteTask, (os::TaskHandle_t taskHandle), (override));

    MOCK_METHOD(void, SuspendTask, (os::TaskHandle_t taskHandle), (override));

    MOCK_METHOD(void, ResumeTask, (os::TaskHandle_t taskHandle), (override));

    MOCK_METHOD(os::QueueHandle_t, CreateQueue,
                (uint32_t length, uint32_t itemSize), (override));

    MOCK_METHOD(void, DeleteQueue, (os::QueueHandle_t queue), (override));

    MOCK_METHOD(os::QueueResult, SendToQueue,
                (os::QueueHandle_t queue, const void* item, uint32_t timeout),
                (override));

    MOCK_METHOD(os::QueueResult, SendToQueueISR,
                (os::QueueHandle_t queue, const void* item), (override));

    MOCK_METHOD(os::QueueResult, ReceiveFromQueue,
                (os::QueueHandle_t queue, void* buffer, uint32_t timeout),
                (override));

    MOCK_METHOD(uint32_t, getQueueMessagesWaiting, (os::QueueHandle_t queue),
                (override));

    MOCK_METHOD(void, delay, (uint32_t ms), (override));

    MOCK_METHOD(uint32_t, getTickCount, (), (override));

    MOCK_METHOD(void, StartScheduler, (), (override));

    MOCK_METHOD(uint32_t, getTaskStackWatermark, (os::TaskHandle_t taskHandle),
                (override));

    MOCK_METHOD(os::TaskState, getTaskState, (os::TaskHandle_t taskHandle),
                (override));

    MOCK_METHOD(std::vector<os::TaskStats>, getSystemTaskStats, (), (override));

    MOCK_METHOD(void*, RegisterISR, (void (*callback)(), uint8_t pin, int mode),
                (override));

    MOCK_METHOD(void, NotifyTaskFromISR, (os::TaskHandle_t taskHandle),
                (override));

    MOCK_METHOD(os::QueueResult, WaitForNotify, (uint32_t timeout), (override));
};

}  // namespace test
}  // namespace loramesher