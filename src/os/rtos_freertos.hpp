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

    bool SuspendTask(TaskHandle_t taskHandle) override {
        vTaskSuspend(taskHandle);
        return true;
    }

    bool ResumeTask(TaskHandle_t taskHandle) override {
        vTaskResume(taskHandle);
        return true;
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
        // esp_get_free_heap_size
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

    QueueResult NotifyTask(TaskHandle_t task_handle, uint32_t value) override {
        BaseType_t response =
            xTaskNotify(task_handle, value, eSetValueWithOverwrite);

        if (response == pdPASS) {
            return QueueResult::kOk;
        } else {
            return QueueResult::kError;
        }
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

    /**
     * @brief Creates a binary semaphore using a queue
     * 
     * A binary semaphore is implemented as a queue of length 1, with item size 0.
     * When the queue is empty, the semaphore is unavailable.
     * When the queue has an item, the semaphore is available.
     * 
     * @return Handle to the created semaphore
     */
    SemaphoreHandle_t CreateBinarySemaphore() override {
        return xSemaphoreCreateBinary();
    }

    /**
     * @brief Creates a counting semaphore using a queue
     * 
     * A counting semaphore is implemented as a queue with length equal to maxCount.
     * The queue contains dummy items representing available tokens.
     * 
     * @param maxCount Maximum count value
     * @param initialCount Initial count value
     * @return Handle to the created semaphore
     */
    SemaphoreHandle_t CreateCountingSemaphore(uint32_t maxCount,
                                              uint32_t initialCount) override {
        if (initialCount > maxCount) {
            initialCount = maxCount;
        }

        // Create a queue with length maxCount and item size 1 (we just need a token)
        return xSemaphoreCreateCounting(maxCount, initialCount);
    }

    /**
     * @brief Deletes a semaphore
     * @param semaphore Handle to the semaphore to delete
     */
    void DeleteSemaphore(SemaphoreHandle_t semaphore) override {
        vSemaphoreDelete(semaphore);
    }

    /**
     * @brief Takes (acquires) a semaphore
     * 
     * Taking a semaphore is equivalent to receiving an item from the queue.
     * 
     * @param semaphore Handle to the semaphore
     * @param timeout Maximum time to wait in milliseconds
     * @return true if semaphore was acquired, false on timeout
     */
    bool TakeSemaphore(SemaphoreHandle_t semaphore, uint32_t timeout) override {
        TickType_t xTicksToWait;
        if (timeout == MAX_DELAY) {
            xTicksToWait = portMAX_DELAY;
        } else {
            xTicksToWait = pdMS_TO_TICKS(timeout);
        }

        BaseType_t result = xSemaphoreTake(semaphore, xTicksToWait);
        return result == pdPASS;
    }

    /**
     * @brief Gives (releases) a semaphore
     * 
     * Giving a semaphore is equivalent to sending an item to the queue.
     * 
     * @param semaphore Handle to the semaphore
     * @return true if successful, false otherwise
     */
    bool GiveSemaphore(SemaphoreHandle_t semaphore) override {
        return xSemaphoreGive(semaphore) == pdPASS;
    }

    /**
     * @brief Gives (releases) a semaphore from an ISR context
     * @param semaphore Handle to the semaphore
     * @return true if successful, false otherwise
     */
    bool GiveSemaphoreFromISR(SemaphoreHandle_t semaphore) override {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        BaseType_t result =
            xSemaphoreGiveFromISR(semaphore, &xHigherPriorityTaskWoken);

        if (xHigherPriorityTaskWoken == pdTRUE) {
            portYIELD_FROM_ISR();
        }

        return result == pdPASS;
    }

    /**
     * @brief Check if current task should pause or exit
     * 
     * In FreeRTOS, this only checks for task deletion requests.
     * In the native mock, this handles pause/resume functionality.
     * 
     * @return true if task should stop, false to continue
     */
    bool ShouldStopOrPause() override {
        // In FreeRTOS, we can't easily check for deletion requests
        // so we just return false to continue execution
        return false;
    }

    /**
     * @brief Cross-platform thread/task yield function
     * 
     * Allows a task to yield execution to other tasks of equal priority.
     */
    inline void YieldTask() override { taskYIELD(); }
};

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