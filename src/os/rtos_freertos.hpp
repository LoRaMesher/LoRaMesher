/**
 * @file rtos_freertos.hpp
 * @brief FreeRTOS implementation of RTOS interface
 */
#pragma once

#include "config/system_config.hpp"

#ifdef LORAMESHER_BUILD_ARDUINO

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "os/rtos.hpp"
#include "utils/logger.hpp"

namespace loramesher {
namespace os {

class RTOSFreeRTOS : public RTOS {
   private:
    struct TaskData {
        TaskFunction_t function;
        void* parameters;
    };

    /**
     * @brief Static wrapper function for FreeRTOS tasks
     * @param pvParameters Pointer to TaskParams structure
     */
    static void taskWrapper(void* pvParameters) {
        auto* taskData = static_cast<TaskData*>(pvParameters);
        if (taskData && taskData->function) {
            taskData->function(taskData->parameters);
        }
        // Clean up
        if (taskData) {
            delete taskData;
        }
        vTaskDelete(nullptr);
    }

   public:
    /**
     * @brief FreeRTOS implementation of task creation
     */
    bool CreateTask(TaskFunction_t taskFunction, const char* name,
                    uint32_t stackSize, void* parameters, uint32_t priority,
                    TaskHandle_t* taskHandle) override {
        if (!taskFunction || !name) {
            return false;
        }

        // Create parameter structure on heap
        auto* taskData =
            new TaskData{.function = taskFunction, .parameters = parameters};

        // Create the task
        BaseType_t result = xTaskCreate(taskWrapper, name, stackSize, taskData,
                                        priority, taskHandle);

        if (result != pdPASS) {
            delete taskData;
        }

        return (result == pdPASS);
    }

    void DeleteTask(TaskHandle_t taskHandle) override {
        vTaskDelete(taskHandle);
    }

    void SuspendTask(TaskHandle_t taskHandle) override {
        vTaskSuspend(taskHandle);
    }

    void ResumeTask(TaskHandle_t taskHandle) override {
        vTaskResume(taskHandle);
    }

    QueueHandle_t CreateQueue(uint32_t length, uint32_t itemSize) override {
        // Ensure minimum item size and alignment
        itemSize = ((itemSize + 3) & ~3);  // Align to 4 bytes
        return xQueueCreate(length, itemSize);
    }

    void DeleteQueue(QueueHandle_t queue) override { vQueueDelete(queue); }

    QueueResult SendToQueue(QueueHandle_t queue, const void* item,
                            uint32_t timeout) override {
        if (!queue || !item) {
            return QueueResult::kError;
        }

        BaseType_t result = xQueueSend(queue, item, pdMS_TO_TICKS(timeout));

        if (result == pdPASS) {
            return QueueResult::kOk;
        }
        return timeout > 0 ? QueueResult::kTimeout : QueueResult::kFull;
    }

    QueueResult SendToQueueISR(QueueHandle_t queue, const void* item) override {
        if (!queue || !item) {
            return QueueResult::kError;
        }

        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        BaseType_t result =
            xQueueSendFromISR(queue, item, &xHigherPriorityTaskWoken);

        if (xHigherPriorityTaskWoken == pdTRUE) {
            portYIELD_FROM_ISR();
        }

        return (result == pdPASS) ? QueueResult::kOk : QueueResult::kFull;
    }

    QueueResult ReceiveFromQueue(QueueHandle_t queue, void* buffer,
                                 uint32_t timeout) override {
        if (!queue || !buffer) {
            return QueueResult::kError;
        }

        TickType_t xTicksToWait;
        if (timeout == MAX_DELAY) {
            xTicksToWait = portMAX_DELAY;
        } else {
            xTicksToWait = pdMS_TO_TICKS(timeout);
        }

        BaseType_t result = xQueueReceive(queue, buffer, xTicksToWait);
        if (result == pdPASS) {
            return QueueResult::kOk;
        }
        return timeout > 0 ? QueueResult::kTimeout : QueueResult::kEmpty;
    }

    uint32_t getQueueMessagesWaiting(QueueHandle_t queue) override {
        return uxQueueMessagesWaiting(queue);
    }

    void delay(uint32_t ms) override { vTaskDelay(pdMS_TO_TICKS(ms)); }

    uint32_t getTickCount() override { return xTaskGetTickCount(); }

    void StartScheduler() override { vTaskStartScheduler(); }

    uint32_t getTaskStackWatermark(TaskHandle_t taskHandle) override {
        return uxTaskGetStackHighWaterMark(taskHandle) * 4;
    }

    TaskState getTaskState(TaskHandle_t taskHandle) override {
        eTaskState state = eTaskGetState(taskHandle);
        switch (state) {
            case eRunning:
                return TaskState::kRunning;
            case eReady:
                return TaskState::kReady;
            case eBlocked:
                return TaskState::kBlocked;
            case eSuspended:
                return TaskState::kSuspended;
            case eDeleted:
                return TaskState::kDeleted;
            default:
                return TaskState::kUnknown;
        }
    }

    std::vector<TaskStats> getSystemTaskStats() override {
        std::vector<TaskStats> stats;

        // Get current task info
        TaskHandle_t currentTask = xTaskGetCurrentTaskHandle();
        if (currentTask != nullptr) {
            TaskStats taskStats;
            taskStats.name = pcTaskGetName(currentTask);
            taskStats.state = TaskState::kRunning;
            taskStats.stackWatermark = getTaskStackWatermark(currentTask);
            taskStats.runtime = 0;
            stats.push_back(taskStats);
        }

        return stats;
    }

    void* RegisterISR(void (*callback)(), uint8_t pin, int mode) override {
        attachInterrupt(digitalPinToInterrupt(pin), callback, mode);
        return (void*)callback;
    }

    void NotifyTaskFromISR(TaskHandle_t task_handle) override {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;

        xHigherPriorityTaskWoken =
            xTaskNotifyFromISR(task_handle, 0, eSetValueWithoutOverwrite,
                               &xHigherPriorityTaskWoken);

        if (xHigherPriorityTaskWoken == pdTRUE)
            portYIELD_FROM_ISR();
    }

    QueueResult WaitForNotify(uint32_t timeout) override {
        if (timeout == MAX_DELAY) {
            timeout = portMAX_DELAY;
        }

        // Wait for notification from ISR
        BaseType_t response = xTaskNotifyWait(pdTRUE, pdFALSE, NULL, timeout);
        if (response == pdPASS) {
            return QueueResult::kOk;
        } else {
            return QueueResult::kTimeout;
        }
    }
};

RTOS& RTOS::instance() {
    static RTOSFreeRTOS instance;
    return instance;
}

}  // namespace os

/**  
 * @brief Provides access to the RTOS singleton instance
 * @return Reference to the RTOS singleton instance
*/
static os::RTOS& GetRTOS() {
    return os::RTOS::instance();
}

}  // namespace loramesher

#endif  // LORAMESHER_BUILD_ARDUINO