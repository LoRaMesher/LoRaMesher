/**
 * @file os/rtos_mock.hpp
 * @brief Mock RTOS implementation for native platform
 */
#pragma once

#include "config/system_config.hpp"

#ifdef LORAMESHER_BUILD_NATIVE

#include <chrono>
#include <condition_variable>
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
        auto* t = new std::thread(
            [taskFunction, parameters]() { taskFunction(parameters); });

        std::lock_guard<std::mutex> lock(tasksMutex_);
        tasks_.try_emplace(t, name, false);

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
        auto* q = new QueueData{.maxSize = length, .itemSize = itemSize};
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

    uint32_t getTaskStackWatermark(TaskHandle_t taskHandle) override {
        // Mock implementation - return a reasonable value
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
            stats.push_back(TaskStats{.name = info.name,
                                      .state = info.suspended
                                                   ? TaskState::kSuspended
                                                   : TaskState::kRunning,
                                      .stackWatermark = 2048,
                                      .runtime = 0});
        }

        return stats;
    }

    void* RegisterISR(void (*callback)(), uint8_t pin = 0,
                      int mode = 0) override {
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

    void NotifyTaskFromISR(TaskHandle_t task_handle) override {
        // TODO: Implement
    }

    QueueResult WaitForNotify(uint32_t timeout) override {
        // TODO: Implement
        return QueueResult::kOk;
    }

   private:
    struct TaskInfo {
        std::string name;
        bool suspended;
        std::condition_variable cv;
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
