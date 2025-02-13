// src/utilities/task_monitor.hpp
#pragma once

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string>

#include "config/system_config.hpp"

namespace loramesher {
namespace utils {

/**
 * @brief Utility class for monitoring FreeRTOS tasks
 * 
 * Provides methods to monitor and log task statistics including stack usage,
 * runtime stats, and other metrics. Can be used across different tasks in the project.
 */
class TaskMonitor {
   public:
    /**
     * @brief Monitor a specific task's status
     * 
     * @param task_handle Handle to the task to monitor
     * @param task_name Name of the task for logging
     * @param min_stack_watermark Minimum acceptable stack watermark in bytes
     */
    static void MonitorTask(TaskHandle_t task_handle,
                            const std::string& task_name,
                            size_t min_stack_watermark) {
#ifdef DEBUG
        if (!task_handle) {
            ESP_LOGW("TaskMonitor", "Invalid task handle for %s",
                     task_name.c_str());
            return;
        }

        // Monitor stack usage
        UBaseType_t watermark = uxTaskGetStackHighWaterMark(task_handle);
        size_t watermark_bytes = watermark * 4;  // Convert from words to bytes

        ESP_LOGD("TaskMonitor", "[%s] Stack watermark: %u bytes",
                 task_name.c_str(), watermark_bytes);

        // Check for critical stack levels
        if (watermark_bytes < min_stack_watermark) {
            ESP_LOGW("TaskMonitor",
                     "[%s] Critical stack usage! Only %u bytes remaining",
                     task_name.c_str(), watermark_bytes);
        }

// Get task runtime stats if enabled
#if configGENERATE_RUN_TIME_STATS
        char stats_buffer[50];
        vTaskGetRunTimeStats(stats_buffer);
        ESP_LOGD("TaskMonitor", "[%s] Runtime stats:\n%s", task_name.c_str(),
                 stats_buffer);
#endif

        // Task state information
        eTaskState task_state = eTaskGetState(task_handle);
        const char* state_str = getTaskStateString(task_state);
        ESP_LOGD("TaskMonitor", "[%s] Current state: %s", task_name.c_str(),
                 state_str);
#endif
    }

    /**
     * @brief Get system-wide task list and statistics
     */
    static void MonitorSystemTasks() {
#ifdef DEBUG
        char* task_list_buffer =
            static_cast<char*>(pvPortMalloc(configMAX_TASK_NAME_LEN * 40));

        if (task_list_buffer) {
            vTaskList(task_list_buffer);
            ESP_LOGD("TaskMonitor", "Task List:\n%s", task_list_buffer);
            vPortFree(task_list_buffer);
        }
#endif
    }

   private:
    /**
     * @brief Convert task state to string representation
     * 
     * @param state Task state to convert
     * @return const char* String representation of the state
     */
    static const char* getTaskStateString(eTaskState state) {
        switch (state) {
            case eRunning:
                return "Running";
            case eReady:
                return "Ready";
            case eBlocked:
                return "Blocked";
            case eSuspended:
                return "Suspended";
            case eDeleted:
                return "Deleted";
            default:
                return "Unknown";
        }
    }
};

}  // namespace utils
}  // namespace loramesher