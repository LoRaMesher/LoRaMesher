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

namespace loramesher {
namespace os {

class RTOSFreeRTOS : public RTOS {
   public:
    bool CreateTask(TaskFunction_t taskFunction, const char* name,
                    uint32_t stackSize, void* parameters, uint32_t priority,
                    TaskHandle_t* taskHandle) override {
        // Create a static function that will be passed to FreeRTOS
        static auto staticTaskFunction = [](void* params) {
            auto* taskData = static_cast<TaskData*>(params);
            taskData->function(taskData->parameters);
            delete taskData;
            vTaskDelete(nullptr);
        };

        auto* taskData =
            new TaskData{.function = taskFunction, .parameters = parameters};

        // Pass the static function to FreeRTOS
        BaseType_t result = xTaskCreate(
            staticTaskFunction,  // Now passing a regular function pointer
            name, stackSize, taskData, priority,
            reinterpret_cast<xTaskHandle*>(taskHandle));

        return (result == pdPASS);
    }

    void DeleteTask(TaskHandle_t taskHandle) override {
        vTaskDelete(static_cast<xTaskHandle>(taskHandle));
    }

    void SuspendTask(TaskHandle_t taskHandle) override {
        vTaskSuspend(static_cast<xTaskHandle>(taskHandle));
    }

    void ResumeTask(TaskHandle_t taskHandle) override {
        vTaskResume(static_cast<xTaskHandle>(taskHandle));
    }

    QueueHandle_t CreateQueue(uint32_t length, uint32_t itemSize) override {
        return xQueueCreate(length, itemSize);
    }

    void DeleteQueue(QueueHandle_t queue) override {
        vQueueDelete(static_cast<::QueueHandle_t>(queue));
    }

    QueueResult SendToQueue(QueueHandle_t queue, const void* item,
                            uint32_t timeout) override {
        BaseType_t result = xQueueSend(static_cast<::QueueHandle_t>(queue),
                                       item, pdMS_TO_TICKS(timeout));

        if (result == pdPASS) {
            return QueueResult::kOk;
        }
        return timeout > 0 ? QueueResult::kTimeout : QueueResult::kFull;
    }

    QueueResult SendToQueueISR(QueueHandle_t queue, const void* item) override {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;

        uint8_t event = 1;
        BaseType_t result =
            xQueueSendFromISR(static_cast<::QueueHandle_t>(queue), item,
                              &xHigherPriorityTaskWoken);

        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);

        if (result == pdPASS) {
            return QueueResult::kOk;
        }
        return QueueResult::kFull;
    }

    QueueResult ReceiveFromQueue(QueueHandle_t queue, void* buffer,
                                 uint32_t timeout) override {
        BaseType_t result =
            xQueueReceive(static_cast<::QueueHandle_t>(queue), buffer, timeout);

        if (result == pdPASS) {
            return QueueResult::kOk;
        }
        return timeout > 0 ? QueueResult::kTimeout : QueueResult::kEmpty;
    }

    uint32_t getQueueMessagesWaiting(QueueHandle_t queue) override {
        return uxQueueMessagesWaiting(static_cast<::QueueHandle_t>(queue));
    }

    void delay(uint32_t ms) override { vTaskDelay(pdMS_TO_TICKS(ms)); }

    uint32_t getTickCount() override { return xTaskGetTickCount(); }

    void StartScheduler() override { vTaskStartScheduler(); }

    uint32_t getTaskStackWatermark(TaskHandle_t taskHandle) override {
        return uxTaskGetStackHighWaterMark(
                   static_cast<xTaskHandle>(taskHandle)) *
               4;
    }

    TaskState getTaskState(TaskHandle_t taskHandle) override {
        eTaskState state = eTaskGetState(static_cast<xTaskHandle>(taskHandle));
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
        // Allocate buffer for task list
        char* taskListBuffer =
            static_cast<char*>(pvPortMalloc(configMAX_TASK_NAME_LEN * 40));

        if (taskListBuffer) {
            // Get task information as formatted string
            vTaskList(taskListBuffer);

            // Parse the taskListBuffer line by line
            char* line = strtok(taskListBuffer, "\n");
            // Skip header line
            line = strtok(NULL, "\n");

            while (line != NULL) {
                TaskStats taskStat;
                char name[configMAX_TASK_NAME_LEN];
                char state;
                UBaseType_t priority;
                UBaseType_t stackWatermark;
                UBaseType_t taskNumber;

                // Format is: <task_name> <state> <priority> <stack> <task_number>
                int result = sscanf(line, "%s %c %lu %lu %lu", name, &state,
                                    &priority, &stackWatermark, &taskNumber);

                if (result == 5) {
                    // Convert state character to TaskState enum
                    TaskState taskState;
                    switch (state) {
                        case 'X':
                            taskState = TaskState::kRunning;
                            break;
                        case 'B':
                            taskState = TaskState::kBlocked;
                            break;
                        case 'R':
                            taskState = TaskState::kReady;
                            break;
                        case 'S':
                            taskState = TaskState::kSuspended;
                            break;
                        case 'D':
                            taskState = TaskState::kDeleted;
                            break;
                        default:
                            taskState = TaskState::kUnknown;
                            break;
                    }

                    // Fill task statistics
                    taskStat.name = name;
                    taskStat.state = taskState;
                    taskStat.stackWatermark = stackWatermark;

// Calculate runtime from task runtime statistics if available
#if (configGENERATE_RUN_TIME_STATS == 1)
                    // We'll need to call a separate function for runtime stats
                    taskStat.runtime = getTaskRuntime(taskNumber);
#else
                    taskStat.runtime = 0;
#endif

                    stats.push_back(taskStat);
                }

                // Get next line
                line = strtok(NULL, "\n");
            }

            vPortFree(taskListBuffer);
        }

        return stats;
    }

    /**
     * @brief Helper function to get runtime for a specific task
     * @param taskNumber The task number to get runtime for
     * @return Runtime in milliseconds
     */
    uint32_t getTaskRuntime(UBaseType_t taskNumber) {
        uint32_t runtime = 0;

#if (configGENERATE_RUN_TIME_STATS == 1)
        char* runtimeBuffer =
            static_cast<char*>(pvPortMalloc(configMAX_TASK_NAME_LEN * 40));

        if (runtimeBuffer) {
            vTaskGetRunTimeStats(runtimeBuffer);

            // Parse the runtime buffer to find the specific task
            char* line = strtok(runtimeBuffer, "\n");
            // Skip header
            line = strtok(NULL, "\n");

            UBaseType_t currentTaskNumber = 0;
            while (line != NULL && currentTaskNumber <= taskNumber) {
                if (currentTaskNumber == taskNumber) {
                    char name[configMAX_TASK_NAME_LEN];
                    uint32_t absoluteRuntime;
                    uint32_t percentageRuntime;

                    // Format is: <task_name> <absolute_time> <percentage>
                    if (sscanf(line, "%s %lu %lu%%", name, &absoluteRuntime,
                               &percentageRuntime) >= 2) {
                        runtime = absoluteRuntime;
                    }
                    break;
                }

                currentTaskNumber++;
                line = strtok(NULL, "\n");
            }

            vPortFree(runtimeBuffer);
        }
#endif

        return runtime;
    }

    void* RegisterISR(void (*callback)(), uint8_t pin = 0,
                      int mode = 0) override {
        // On ESP32/Arduino, this would typically integrate with attachInterrupt
        // or other platform-specific interrupt registration
        attachInterrupt(digitalPinToInterrupt(pin), callback, RISING);

        // Return a pointer to the callback function as a handle
        // This allows the caller to identify this ISR later
        return (void*)callback;
    }

   private:
    struct TaskData {
        TaskFunction_t function;
        void* parameters;
    };
};

inline RTOS& RTOS::instance() {
    static RTOSFreeRTOS instance;
    return instance;
}

}  // namespace os

/**  
 * @brief Provides access to the RTOS singleton instance
 * @return Reference to the RTOS singleton instance
*/
static inline os::RTOS& GetRTOS() {
    return os::RTOS::instance();
}

}  // namespace loramesher

#endif  // LORAMESHER_BUILD_ARDUINO
