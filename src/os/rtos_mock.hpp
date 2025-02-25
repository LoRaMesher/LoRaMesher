/**
 * @file os/rtos_mock.hpp
 * @brief Mock RTOS implementation for native platform
 */
#pragma once

#include "config/system_config.hpp"

#ifdef LORAMESHER_BUILD_NATIVE

#include <chrono>
#include <condition_variable>
#include <cstdlib>  // For std::rand()
#include <map>
#include <mutex>
#include <queue>
#include <thread>
#include "os/rtos.hpp"

namespace loramesher {
namespace os {

/**
 * @class RTOSMock
 * @brief Mock implementation of RTOS interface
 */
class RTOSMock : public RTOS {
   public:
    /**
     * @brief Creates a new task in the RTOS mock
     * @param taskFunction Function to be executed by the task
     * @param name Name of the task
     * @param stackSize Stack size for the task (unused in mock)
     * @param parameters Parameters to pass to the task function
     * @param priority Task priority (unused in mock)
     * @param taskHandle Pointer to store the task handle
     * @return true if task creation was successful
     */
    bool CreateTask(TaskFunction_t taskFunction, const char* name,
                    uint32_t stackSize, void* parameters, uint32_t priority,
                    TaskHandle_t* taskHandle) override {

        // Log the task creation with its parameters for debugging
        printf("MOCK: Creating task '%s' with stack size %u and priority %u\n",
               name, stackSize, priority);

        auto* t = new std::thread(
            [taskFunction, parameters]() { taskFunction(parameters); });

        // Create and store task configuration in our mock implementation
        {
            std::lock_guard<std::mutex> lock(tasksMutex_);

            // Create a new TaskInfo object - we need to use a temporary variable
            // because std::condition_variable doesn't support aggregate initialization
            auto& task_info = tasks_[t];
            task_info.name = name;
            task_info.stack_size = stackSize;
            task_info.priority = priority;
            task_info.suspended = false;
            task_info.stack_watermark = 0;
            task_info.notification_pending = false;
            // Note: cv and notify_cv are default-initialized
        }

        if (taskHandle) {
            *taskHandle = t;
        }

        t->detach();
        return true;
    }

    void DeleteTask(TaskHandle_t taskHandle) override {

        std::lock_guard<std::mutex> lock(tasksMutex_);
        auto* thread = static_cast<std::thread*>(taskHandle);
        tasks_.erase(thread);
        delete thread;
    }

    void SuspendTask(TaskHandle_t taskHandle) override {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        if (auto it = tasks_.find(static_cast<std::thread*>(taskHandle));
            it != tasks_.end()) {
            it->second.suspended = true;
        }
    }

    void ResumeTask(TaskHandle_t taskHandle) override {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        if (auto it = tasks_.find(static_cast<std::thread*>(taskHandle));
            it != tasks_.end()) {
            it->second.suspended = false;
            it->second.cv.notify_one();
        }
    }

    QueueHandle_t CreateQueue(uint32_t length, uint32_t itemSize) override {
        auto* q = new QueueData{
            .mutex = {},     // Default initialize mutex
            .notEmpty = {},  // Default initialize condition variable
            .notFull = {},   // Default initialize condition variable
            .data = {},      // Default initialize queue
            .maxSize = length,
            .itemSize = itemSize};
        return q;
    }

    void DeleteQueue(QueueHandle_t queue) override {
        auto* q = static_cast<QueueData*>(queue);
        delete q;
    }

    QueueResult SendToQueue(QueueHandle_t queue, const void* item,
                            uint32_t timeout) override {
        auto* q = static_cast<QueueData*>(queue);
        std::unique_lock<std::mutex> lock(q->mutex);

        if (q->data.size() >= q->maxSize) {
            if (timeout == 0) {
                return QueueResult::kFull;
            }

            auto status = q->notFull.wait_for(
                lock, std::chrono::milliseconds(timeout),
                [q]() { return q->data.size() < q->maxSize; });

            if (!status) {
                return QueueResult::kTimeout;
            }
        }

        auto* bytes = static_cast<const uint8_t*>(item);
        q->data.push(std::vector<uint8_t>(bytes, bytes + q->itemSize));
        q->notEmpty.notify_one();
        return QueueResult::kOk;
    }

    QueueResult SendToQueueISR(QueueHandle_t queue, const void* item) override {
        return SendToQueue(queue, item, 0);
    }

    QueueResult ReceiveFromQueue(QueueHandle_t queue, void* buffer,
                                 uint32_t timeout) override {
        auto* q = static_cast<QueueData*>(queue);
        std::unique_lock<std::mutex> lock(q->mutex);

        if (q->data.empty()) {
            if (timeout == 0) {
                return QueueResult::kEmpty;
            }

            auto status =
                q->notEmpty.wait_for(lock, std::chrono::milliseconds(timeout),
                                     [q]() { return !q->data.empty(); });

            if (!status) {
                return QueueResult::kTimeout;
            }
        }

        auto& item = q->data.front();
        memcpy(buffer, item.data(), q->itemSize);
        q->data.pop();
        q->notFull.notify_one();
        return QueueResult::kOk;
    }

    uint32_t getQueueMessagesWaiting(QueueHandle_t queue) override {
        auto* q = static_cast<QueueData*>(queue);
        std::lock_guard<std::mutex> lock(q->mutex);
        return static_cast<uint32_t>(q->data.size());
    }

    void delay(uint32_t ms) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }

    uint32_t getTickCount() override {
        auto now = std::chrono::steady_clock::now();
        auto duration = now.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::milliseconds>(duration)
            .count();
    }

    void StartScheduler() override {
        // Nothing to do in mock implementation
    }

    /**
     * @brief Gets the minimum amount of stack space that has remained for the task since it was created
     * 
     * In a real RTOS, this would return the actual minimum free stack space (watermark).
     * In this mock implementation, we track a simulated watermark level for each task.
     * 
     * @param taskHandle Handle to the task to be queried
     * @return Minimum free stack space in bytes, or default value if task not found
     */
    uint32_t getTaskStackWatermark(TaskHandle_t taskHandle) override {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        auto* thread = static_cast<std::thread*>(taskHandle);

        if (auto it = tasks_.find(thread); it != tasks_.end()) {
            // If we haven't set a specific watermark for this task, simulate one
            if (it->second.stack_watermark == 0) {
                // Simulate a watermark between 60-90% of total stack size
                uint32_t used_percentage =
                    10 + (std::rand() % 30);  // 10-40% used
                uint32_t free_percentage = 100 - used_percentage;
                it->second.stack_watermark =
                    (it->second.stack_size * free_percentage) / 100;
            }
            return it->second.stack_watermark;
        }

        // Task not found, return default value
        return 2048;
    }

    TaskState getTaskState(TaskHandle_t taskHandle) override {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        auto* thread = static_cast<std::thread*>(taskHandle);
        if (auto it = tasks_.find(thread); it != tasks_.end()) {
            return it->second.suspended ? TaskState::kSuspended
                                        : TaskState::kRunning;
        }
        return TaskState::kUnknown;
    }

    std::vector<TaskStats> getSystemTaskStats() override {
        std::vector<TaskStats> stats;
        std::lock_guard<std::mutex> lock(tasksMutex_);

        for (const auto& [thread, info] : tasks_) {
            // Get the stack watermark for this task (or calculate it if not set)
            uint32_t watermark = info.stack_watermark;
            if (watermark == 0) {
                // Simulate a watermark between 60-90% of total stack size
                uint32_t used_percentage =
                    10 + (std::rand() % 30);  // 10-40% used
                uint32_t free_percentage = 100 - used_percentage;
                watermark = (info.stack_size * free_percentage) / 100;
            }

            stats.push_back(TaskStats{.name = info.name,
                                      .state = info.suspended
                                                   ? TaskState::kSuspended
                                                   : TaskState::kRunning,
                                      .stackWatermark = watermark,
                                      .runtime = 0});
        }

        return stats;
    }

    void* RegisterISR(void (*callback)(), uint8_t pin = 0,
                      int mode = 0) override {

        // Prevent unused parameter warnings
        (void)pin;
        (void)mode;

        // Store the callback for testing purposes
        std::lock_guard<std::mutex> lock(isrMutex_);
        registeredISRs_.push_back(callback);
        return reinterpret_cast<void*>(callback);
    }

    // Helper method for testing
    void triggerISR(void* handle) {
        auto callback = reinterpret_cast<void (*)()>(handle);
        if (callback) {
            callback();
        }
    }

    /**
     * @brief Notifies a task from an ISR context
     * 
     * In embedded RTOS systems, this function would typically set a notification value
     * for the specified task and potentially wake it from a blocked state. In this mock
     * implementation, we track the notification in the task's state and wake any task
     * that might be waiting on notifications.
     * 
     * @param task_handle Handle to the task to be notified
     */
    void NotifyTaskFromISR(TaskHandle_t task_handle) override {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        auto* thread = static_cast<std::thread*>(task_handle);

        if (auto it = tasks_.find(thread); it != tasks_.end()) {
            // Set notification flag for this task
            it->second.notification_pending = true;

            // Wake up the task if it's waiting for a notification
            it->second.notify_cv.notify_one();
        }
    }

    /**
     * @brief Waits for a task notification with optional timeout
     * 
     * This function blocks the calling task until a notification is received or
     * the timeout expires. In a real RTOS, tasks can receive notifications directly
     * from ISRs or other tasks.
     * 
     * @param timeout Maximum time to wait for notification in milliseconds
     * @return QueueResult::kOk if notification was received, QueueResult::kTimeout if timeout occurred
     */
    QueueResult WaitForNotify(uint32_t timeout) override {
        // Since std::thread doesn't provide a way to get the current thread handle,
        // we need to use thread_local storage to track the current task
        thread_local TaskHandle_t this_task = nullptr;

        if (!this_task) {
            // First time call from this thread, find the task handle
            std::lock_guard<std::mutex> lock(tasksMutex_);

            // Try to find this thread in our task list by comparing thread IDs
            std::thread::id current_id = std::this_thread::get_id();
            for (const auto& [thread_ptr, info] : tasks_) {
                if (thread_ptr->get_id() == current_id) {
                    this_task = thread_ptr;
                    break;
                }
            }

            // If we couldn't find this thread, return error
            if (!this_task) {
                return QueueResult::kError;
            }
        }

        // Now use the cached task handle
        std::unique_lock<std::mutex> lock(tasksMutex_);
        auto it = tasks_.find(static_cast<std::thread*>(this_task));

        // Double-check that the task still exists
        if (it == tasks_.end()) {
            this_task = nullptr;  // Reset the cache
            return QueueResult::kError;
        }

        // If we already have a pending notification, consume it and return immediately
        if (it->second.notification_pending) {
            it->second.notification_pending = false;
            return QueueResult::kOk;
        }

        // Wait for notification with timeout
        if (timeout == 0) {
            // Special case: don't wait if timeout is 0
            return QueueResult::kTimeout;
        } else {
            // Wait with timeout
            auto status = it->second.notify_cv.wait_for(
                lock, std::chrono::milliseconds(timeout),
                [&it]() { return it->second.notification_pending; });

            if (status) {
                // Notification received, consume it
                it->second.notification_pending = false;
                return QueueResult::kOk;
            } else {
                // Timeout occurred
                return QueueResult::kTimeout;
            }
        }
    }

   private:
    struct TaskInfo {
        std::string name;
        bool suspended = false;
        uint32_t stack_size = 0;
        uint32_t priority = 0;
        uint32_t stack_watermark = 0;  // Tracks simulated stack watermark
        std::condition_variable cv;

        // For notification support
        bool notification_pending = false;
        std::condition_variable notify_cv;
    };

    struct QueueData {
        std::mutex mutex;
        std::condition_variable notEmpty;
        std::condition_variable notFull;
        std::queue<std::vector<uint8_t>> data;
        uint32_t maxSize;
        uint32_t itemSize;
    };

    std::map<std::thread*, TaskInfo> tasks_;
    std::mutex tasksMutex_;
    std::vector<void (*)()> registeredISRs_;
    std::mutex isrMutex_;
};

inline RTOS& RTOS::instance() {
    static RTOSMock instance;
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

#endif  // LORAMESHER_BUILD_NATIVE