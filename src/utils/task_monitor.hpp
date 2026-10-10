// src/utilities/task_monitor.hpp
#pragma once

#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

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

        LOG_INFO("TaskMonitor: Monitoring task %s", task_name);

        MonitorSystemTasks();

        auto& rtos = GetRTOS();
        uint32_t watermark = rtos.getTaskStackWatermark(task_handle);

        // Only log if watermark is below threshold
        if (watermark < min_stack_watermark) {
            // Use more stack-friendly logging
            log_stack_warning(task_name, watermark);
        }
#else
        // Prevent unused parameter warnings
        (void)task_handle;
        (void)task_name;
        (void)min_stack_watermark;
#endif
    }

    struct Registration {
        os::TaskHandle_t handle;
        const char* name;           ///< Assumed to point to a string literal.
        uint32_t configured_bytes;  ///< Total stack size in bytes.
    };

    /**
     * @brief Register the calling task in the global watch list.
     *
     * Called once at task entry. Subsequent PollAllAndWarn() calls iterate
     * this list to log each task's high-water-mark in bytes. An entry with
     * the same name is replaced, so a recreated task is listed once.
     */
    static void RegisterCurrentTask(const char* task_name,
                                    uint32_t configured_bytes) {
#ifdef LORAMESHER_BUILD_ARDUINO
        os::TaskHandle_t handle = xTaskGetCurrentTaskHandle();
        std::lock_guard<std::mutex> lock(GetRegistry().mutex);
        auto& entries = GetRegistry().entries;
        auto it = std::find_if(entries.begin(), entries.end(),
                               [task_name](const Registration& reg) {
                                   return std::strcmp(reg.name, task_name) == 0;
                               });
        if (it != entries.end()) {
            *it = {handle, task_name, configured_bytes};
        } else {
            entries.push_back({handle, task_name, configured_bytes});
        }
#else
        (void)task_name;
        (void)configured_bytes;
#endif
    }

    /**
     * @brief Remove the calling task from the global watch list.
     *
     * Called by a task before it exits, so PollAllAndWarn() never queries
     * the handle of a deleted task.
     */
    static void UnregisterCurrentTask() {
#ifdef LORAMESHER_BUILD_ARDUINO
        os::TaskHandle_t handle = xTaskGetCurrentTaskHandle();
        std::lock_guard<std::mutex> lock(GetRegistry().mutex);
        auto& entries = GetRegistry().entries;
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                                     [handle](const Registration& reg) {
                                         return reg.handle == handle;
                                     }),
                      entries.end());
#endif
    }

    /**
     * @brief Iterate every registered task, log its current high-water-mark
     * in bytes, and warn if the value drops below kStackWarnBytes.
     */
    static void PollAllAndWarn() {
#ifdef LORAMESHER_BUILD_ARDUINO
        std::lock_guard<std::mutex> lock(GetRegistry().mutex);
        for (const auto& reg : GetRegistry().entries) {
            uint32_t hwm_bytes = GetRTOS().getTaskStackWatermark(reg.handle);
            LOG_INFO("STACK[%s] total=%u free=%u", reg.name,
                     static_cast<unsigned>(reg.configured_bytes),
                     static_cast<unsigned>(hwm_bytes));
            if (hwm_bytes < config::TaskConfig::kStackWarnBytes) {
                LOG_WARNING("Task %s stack low: %u bytes free", reg.name,
                            static_cast<unsigned>(hwm_bytes));
            }
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

        LOG_DEBUG("TaskMonitor: System Task List:");
        for (const auto& stat : stats) {
            LOG_DEBUG("Task: %s", stat.name.c_str());
            LOG_DEBUG("  State: %s", os::RTOS::getTaskStateString(stat.state));
            LOG_DEBUG("  Stack Watermark: %u bytes", stat.stackWatermark);
            LOG_DEBUG("  Runtime: %u", stat.runtime);
        }
#endif
    }

   private:
    // Separate function to handle logging to avoid stack pressure in main function
    static void log_stack_warning(const char* task_name, uint32_t watermark) {
        static char buffer[50];  // Static buffer to avoid stack allocation
        snprintf(buffer, sizeof(buffer), "Stack warning %s: %u\n", task_name,
                 watermark);

        LOG_WARNING(buffer);
    }

    struct Registry {
        std::mutex mutex;
        std::vector<Registration> entries;
    };

    static Registry& GetRegistry() {
        static Registry r;
        return r;
    }
};

}  // namespace utils
}  // namespace loramesher