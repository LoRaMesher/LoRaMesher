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
    static void MonitorTask(os::TaskHandle_t task_handle,
                            const std::string& task_name,
                            size_t min_stack_watermark) {
#ifdef DEBUG
        if (!task_handle) {
            printf("TaskMonitor: Invalid task handle for %s\n",
                   task_name.c_str());
            return;
        }

        auto& rtos = GetRTOS();

        // Monitor stack usage
        uint32_t watermark = rtos.getTaskStackWatermark(task_handle);
        printf("TaskMonitor: [%s] Stack watermark: %u bytes\n",
               task_name.c_str(), watermark);

        // Check for critical stack levels
        if (watermark < min_stack_watermark) {
            printf(
                "TaskMonitor: [%s] Critical stack usage! Only %u bytes "
                "remaining\n",
                task_name.c_str(), watermark);
        }

        // Task state information
        os::TaskState state = rtos.getTaskState(task_handle);
        printf("TaskMonitor: [%s] Current state: %s\n", task_name.c_str(),
               os::RTOS::getTaskStateString(state));
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
};

}  // namespace utils
}  // namespace loramesher