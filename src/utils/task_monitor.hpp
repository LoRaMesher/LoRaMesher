// src/utilities/task_monitor.hpp
#pragma once

#include <string>

#include "config/system_config.hpp"
#include "config/task_config.hpp"
#include "os/os_port.hpp"

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
    static void MonitorTask(os::TaskHandle_t task_handle, const char* task_name,
                            size_t min_stack_watermark) {
#ifdef DEBUG
        if (!task_handle) {
            return;
        }

        auto& rtos = GetRTOS();
        uint32_t watermark = rtos.getTaskStackWatermark(task_handle);

        // Only log if watermark is below threshold
        if (watermark < min_stack_watermark) {
            // Use more stack-friendly logging
            log_stack_warning(task_name, watermark);
        }
#endif
    }

    /**
     * @brief Monitor all system tasks
     */
    static void MonitorSystemTasks() {
#ifdef DEBUG
        auto& rtos = GetRTOS();
        auto stats = rtos.getSystemTaskStats();

        printf("TaskMonitor: System Task List:\n");
        for (const auto& stat : stats) {
            printf("Task: %s\n", stat.name.c_str());
            printf("  State: %s\n", os::RTOS::getTaskStateString(stat.state));
            printf("  Stack Watermark: %u bytes\n", stat.stackWatermark);
            printf("  Runtime: %u\n", stat.runtime);
        }
#endif
    }

   private:
    // Separate function to handle logging to avoid stack pressure in main function
    static void log_stack_warning(const char* task_name, uint32_t watermark) {
        static char buffer[50];  // Static buffer to avoid stack allocation
        snprintf(buffer, sizeof(buffer), "Stack warning %s: %u\n", task_name,
                 watermark);
        // Use a stack-friendly print method
        // Serial.print(buffer);  // Or your preferred logging method
    }
};

}  // namespace utils
}  // namespace loramesher