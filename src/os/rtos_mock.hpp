/**
 * @file os/rtos_mock.hpp
 * @brief Mock RTOS implementation compatible with FreeRTOS task style
 */
#pragma once

#include "config/system_config.hpp"

#ifdef LORAMESHER_BUILD_NATIVE

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "os/rtos.hpp"
#include "utils/logger.hpp"

namespace loramesher {
namespace os {

/**
 * @class RTOSMock
 * @brief Mock implementation of RTOS interface compatible with FreeRTOS style tasks
 */
class RTOSMock : public RTOS {
   public:
    /**
     * @brief Struct representing a timer callback registration
     */
    struct TimerCallback {
        std::function<void()>
            callback;         ///< Function to call when timer expires
        uint64_t expiryTime;  ///< Virtual time when the timer expires
        uint32_t period;      ///< Period for repeating timers (0 for one-shot)
        bool active;          ///< Whether the timer is active
    };

    /**
     * @brief Enum defining time modes available in the RTOS mock
     */
    enum class TimeMode {
        kRealTime,    ///< Use system real-time clock
        kVirtualTime  ///< Use virtual time that can be manually advanced
    };

    /**
     * @brief Constructor initializes with real-time mode by default
     */
    RTOSMock()
        : timeMode_(TimeMode::kRealTime),
          virtualTimeMs_(0),
          timeMutex_(),
          waitingTasks_(),
          timerCallbacks_() {}

    /**
     * @brief Sets the time mode for the RTOS mock
     * 
     * @param mode The time mode to use (real or virtual)
     */
    void setTimeMode(TimeMode mode) {
        std::lock_guard<std::mutex> lock(timeMutex_);

        // If switching to real time, sync the virtual time with real time
        if (mode == TimeMode::kRealTime &&
            timeMode_ == TimeMode::kVirtualTime) {
            auto now = std::chrono::steady_clock::now();
            auto duration = now.time_since_epoch();
            virtualTimeMs_ =
                std::chrono::duration_cast<std::chrono::milliseconds>(duration)
                    .count();
            LOG_DEBUG(
                "MOCK: Switching to real time mode, synced virtual time to "
                "%llu ms",
                (unsigned long long)virtualTimeMs_);
        }
        // If switching to virtual time, initialize with current real time
        else if (mode == TimeMode::kVirtualTime &&
                 timeMode_ == TimeMode::kRealTime) {
            auto now = std::chrono::steady_clock::now();
            auto duration = now.time_since_epoch();
            virtualTimeMs_ =
                std::chrono::duration_cast<std::chrono::milliseconds>(duration)
                    .count();
            LOG_DEBUG(
                "MOCK: Switching to virtual time mode, initialized to %llu ms",
                (unsigned long long)virtualTimeMs_);
        }

        timeMode_ = mode;
    }

    /**
     * @brief Gets the current time mode
     * 
     * @return Current time mode (real or virtual)
     */
    TimeMode getTimeMode() const { return timeMode_; }

    /**
     * @brief Advances the virtual time by the specified number of milliseconds
     * 
     * This method only has an effect in virtual time mode. It advances the
     * virtual time counter and wakes up any tasks or timers that should
     * be triggered by this time advancement.
     * 
     * @param ms Number of milliseconds to advance
     * @return The new virtual time value in milliseconds
     */
    uint64_t advanceTime(uint32_t ms) {
        if (timeMode_ != TimeMode::kVirtualTime) {
            LOG_WARNING("MOCK: Cannot advance time in real-time mode");
            return getTickCount();
        }

        std::vector<std::pair<std::condition_variable*, uint64_t>> tasksToWake;
        std::vector<TimerCallback*> timersToTrigger;

        // First, advance the time and identify tasks and timers to wake up
        {
            std::lock_guard<std::mutex> lock(timeMutex_);

            virtualTimeMs_ += ms;

            // Find tasks that should wake up within the new time window
            auto it = waitingTasks_.begin();
            while (it != waitingTasks_.end()) {
                if (it->second <= virtualTimeMs_) {
                    tasksToWake.push_back({it->first, it->second});
                    it = waitingTasks_.erase(it);
                } else {
                    ++it;
                }
            }

            // Find timers that should trigger
            for (auto& timer : timerCallbacks_) {
                if (timer.active && timer.expiryTime <= virtualTimeMs_) {
                    timersToTrigger.push_back(&timer);

                    // If it's a periodic timer, schedule the next trigger
                    if (timer.period > 0) {
                        // Calculate how many periods have passed and set next expiry time
                        uint64_t periods =
                            (virtualTimeMs_ - timer.expiryTime) / timer.period +
                            1;
                        timer.expiryTime += periods * timer.period;
                    } else {
                        // One-shot timer - disable it
                        timer.active = false;
                    }
                }
            }
        }

        // Now wake up tasks (outside the lock to prevent deadlocks)
        for (auto& task : tasksToWake) {
            // LOG_DEBUG("MOCK: Waking up task waiting until %llu ms",
            //           (unsigned long long)task.second);
            task.first->notify_all();
        }

        // Trigger timers (also outside the lock)
        for (auto timer : timersToTrigger) {
            LOG_DEBUG("MOCK: Triggering timer callback at %llu ms",
                      (unsigned long long)timer->expiryTime);
            if (timer->callback) {
                timer->callback();
            }
        }

        return virtualTimeMs_;
    }

    /**
     * @brief Gets the current virtual time value
     * 
     * @return Current virtual time in milliseconds
     */
    uint64_t getVirtualTime() const {
        std::lock_guard<std::mutex> lock(timeMutex_);
        return virtualTimeMs_;
    }

    /**
     * @brief Creates a new task in the RTOS mock
     * 
     * This implementation is compatible with both FreeRTOS-style tasks that contain
     * their own infinite loops and simpler functions that perform a single operation.
     * 
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

        LOG_DEBUG("MOCK: Creating task '%s' with stack size %u and priority %u",
                  name, stackSize, priority);

        // Store parameters in a shared_ptr to ensure it stays alive for the lambda
        std::shared_ptr<void*> shared_params =
            std::make_shared<void*>(parameters);
        std::string task_name = name ? name : "unnamed";

        auto* thread = new std::thread([this, taskFunction, shared_params,
                                        task_name]() {
            try {
                // Call the user's task function with the captured parameters
                taskFunction(*shared_params);
            } catch (const std::exception& e) {
                LOG_ERROR("Exception in task '%s': %s", task_name.c_str(),
                          e.what());
            } catch (...) {
                LOG_ERROR("Unknown exception in task '%s'", task_name.c_str());
            }

            // LOG_DEBUG("MOCK: Task '%s' exiting", task_name.c_str());
        });

        // Store task information
        {
            std::lock_guard<std::timed_mutex> lock(tasksMutex_);

            // Initialize the TaskInfo directly in the map to avoid copy assignment
            auto& task_info = tasks_[thread];
            task_info.name = task_name;
            task_info.stack_size = stackSize;
            task_info.priority = priority;
            task_info.thread_id = thread->get_id();
            task_info.stack_watermark = 0;
            task_info.notification_pending = false;
            task_info.suspended = false;
            task_info.stop_requested = false;

            // LOG_DEBUG("MOCK: Task '%s' registered with thread ID %p",
            //   task_name.c_str(), task_info.thread_id);
        }

        if (taskHandle) {
            *taskHandle = thread;
        }

        return true;
    }

    /**
     * @brief Deletes a task and cleans up resources
     * 
     * This implementation properly handles deleting suspended tasks by resuming them
     * and requesting termination through the TaskControl mechanism.
     * 
     * @param taskHandle Handle to the task to be deleted
     */
    void DeleteTask(TaskHandle_t taskHandle) override {
        if (!taskHandle)
            return;

        auto* thread = static_cast<std::thread*>(taskHandle);
        std::thread::id thread_id;
        std::string task_name = "unknown";
        bool was_suspended = false;
        TaskInfo* task_info = nullptr;

        // Get thread information
        {
            std::lock_guard<std::timed_mutex> lock(tasksMutex_);
            auto it = tasks_.find(thread);
            if (it != tasks_.end()) {
                thread_id = it->second.thread_id;
                task_name = it->second.name;
                was_suspended = it->second.suspended;
                task_info = &(it->second);

                // LOG_DEBUG(
                //     "MOCK: Deleting task '%s' (thread ID: %p, suspended: %d)",
                //     task_name.c_str(), thread_id, was_suspended);

                // Set the stop flag first, before changing suspended state
                it->second.stop_requested = true;

                // If task is suspended, resume it so it can process the stop request
                if (was_suspended) {
                    // LOG_DEBUG(
                    //     "MOCK: Resuming suspended task '%s' before deletion",
                    //     task_name.c_str());
                    it->second.suspended = false;
                }
            } else {
                LOG_WARNING("MOCK: Task handle %p not found in tasks map",
                            taskHandle);
                return;
            }
        }

        // Notify the task to wake up - do this outside the tasksMutex_ lock to prevent deadlock
        if (task_info) {
            // CRITICAL: Notify ALL condition variables the task might be waiting on
            task_info->cv.notify_all();         // For ShouldStopOrPause
            task_info->notify_cv.notify_all();  // For WaitForNotify
            task_info->suspend_ack_cv
                .notify_all();  // For suspend acknowledgment
            task_info->resume_ack_cv.notify_all();  // For resume acknowledgment

            // LOG_DEBUG(
            //     "MOCK: Notified all condition variables for task '%s' deletion",
            //     task_name.c_str());
        }

        if (thread_id != std::thread::id()) {
            // Give the task a moment to exit gracefully
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            // Wait for thread to finish with a reasonable timeout
            if (thread->joinable()) {
                // LOG_DEBUG("MOCK: Waiting for task '%s' to finish",
                //           task_name.c_str());

                // Use a timeout mechanism to avoid waiting indefinitely
                std::atomic<bool> joined{false};
                std::thread watchdog([&]() {
                    if (thread->joinable()) {
                        thread->join();
                        joined = true;
                    }
                });
                watchdog.detach();

                // Wait with timeout (500ms should be plenty for a simple task)
                const int kMaxWaitMs = 500;
                const int kCheckIntervalMs = 10;
                for (int i = 0; i < kMaxWaitMs / kCheckIntervalMs && !joined;
                     i++) {
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(kCheckIntervalMs));
                }

                if (!joined) {
                    LOG_WARNING(
                        "MOCK: Task '%s' did not exit cleanly within timeout",
                        task_name.c_str());
                    // We can't forcibly terminate std::thread, so we have to leak it
                } else {
                    // LOG_DEBUG("MOCK: Task '%s' exited cleanly",
                    //   task_name.c_str());
                }
            }
        }

        // Clean up resources
        {
            std::lock_guard<std::timed_mutex> lock(tasksMutex_);
            tasks_.erase(thread);
        }

        delete thread;
        // LOG_DEBUG("MOCK: Task '%s' deleted", task_name.c_str());
    }

    /**
     * @brief Suspends a task's execution with confirmation
     * @param taskHandle Handle to the task to be suspended
     * @return true if task was successfully suspended, false otherwise
     */
    bool SuspendTask(TaskHandle_t taskHandle) override {
        std::thread::id thread_id;
        std::string task_name = "current";
        TaskInfo* task_info = nullptr;

        if (taskHandle) {
            auto* thread = static_cast<std::thread*>(taskHandle);

            {
                std::lock_guard<std::timed_mutex> lock(tasksMutex_);
                auto it = tasks_.find(thread);
                if (it != tasks_.end()) {
                    thread_id = it->second.thread_id;
                    task_name = it->second.name;
                    task_info = &(it->second);
                } else {
                    LOG_WARNING("MOCK: Task handle %p not found for suspension",
                                taskHandle);
                    return false;
                }
            }
        } else {
            thread_id = std::this_thread::get_id();

            // Find task info for current thread
            std::lock_guard<std::timed_mutex> lock(tasksMutex_);
            for (auto& pair : tasks_) {
                if (pair.second.thread_id == thread_id) {
                    task_name = pair.second.name;
                    task_info = &(pair.second);
                    break;
                }
            }
        }

        if (!task_info) {
            LOG_WARNING(
                "MOCK: Failed to find task with thread ID %p for suspension",
                thread_id);
            return false;
        }

        // LOG_DEBUG("MOCK: Suspending task '%s' (thread ID: %p)",
        //           task_name.c_str(), thread_id);

        // Set up confirmation mechanism
        {
            std::unique_lock<std::mutex> lock(task_info->mutex);

            // If already suspended, nothing to do
            if (task_info->suspended) {
                // LOG_DEBUG("MOCK: Task '%s' is already suspended",
                //   task_name.c_str());
                return true;
            }

            // Set suspended state and prepare for acknowledgment
            task_info->suspended = true;
            task_info->suspension_acknowledged = false;

            // LOG_DEBUG("MOCK: Set suspended flag for task '%s'",
            //           task_name.c_str());
        }

        // CRITICAL FIX: Notify ALL condition variables the task might be waiting on
        task_info->cv.notify_all();  // For ShouldStopOrPause
        task_info->notify_cv
            .notify_all();  // For WaitForNotify - THIS WAS MISSING!
        task_info->suspend_ack_cv.notify_all();  // For suspend acknowledgment
        task_info->resume_ack_cv.notify_all();   // For resume acknowledgment

        // LOG_DEBUG(
        //     "MOCK: Notified all condition variables for task '%s' suspension",
        //     task_name.c_str());

        // For self-suspension (current task), we don't need to wait for acknowledgment
        if (!taskHandle || static_cast<std::thread*>(taskHandle)->get_id() ==
                               std::this_thread::get_id()) {
            LOG_DEBUG("MOCK: Self-suspension of task '%s'", task_name.c_str());
            return true;
        }

        // Wait for the task to acknowledge it's suspended
        // This happens when the task calls ShouldStopOrPause()
        {
            std::unique_lock<std::mutex> lock(task_info->mutex);

            // Wait with a reasonable timeout (500ms)
            // Use our waitFor helper that respects virtual time
            bool acknowledged =
                waitFor(task_info->suspend_ack_cv, lock, 500, [task_info]() {
                    return task_info->suspension_acknowledged ||
                           task_info->stop_requested;
                });

            if (!acknowledged) {
                LOG_WARNING(
                    "MOCK: Timeout waiting for task '%s' to acknowledge "
                    "suspension",
                    task_name.c_str());
                // Return true anyway, as the suspension flag is set
                return true;
            }

            // LOG_DEBUG("MOCK: Task '%s' acknowledged suspension",
            //   task_name.c_str());
        }

        return true;
    }

    /**
     * @brief Resumes a suspended task with confirmation
     * @param taskHandle Handle to the task to be resumed
     * @return true if task was successfully resumed, false otherwise
     */
    bool ResumeTask(TaskHandle_t taskHandle) override {
        std::thread::id thread_id;
        std::string task_name = "current";
        TaskInfo* task_info = nullptr;

        if (taskHandle) {
            auto* thread = static_cast<std::thread*>(taskHandle);

            std::lock_guard<std::timed_mutex> lock(tasksMutex_);
            auto it = tasks_.find(thread);
            if (it != tasks_.end()) {
                thread_id = it->second.thread_id;
                task_name = it->second.name;
                task_info = &(it->second);
            } else {
                LOG_WARNING("MOCK: Task handle %p not found for resume",
                            taskHandle);
                return false;
            }
        } else {
            thread_id = std::this_thread::get_id();

            // Find task info for current thread
            std::lock_guard<std::timed_mutex> lock(tasksMutex_);
            for (auto& pair : tasks_) {
                if (pair.second.thread_id == thread_id) {
                    task_name = pair.second.name;
                    task_info = &(pair.second);
                    break;
                }
            }
        }

        if (!task_info) {
            LOG_WARNING(
                "MOCK: Failed to find task with thread ID %p for resume",
                thread_id);
            return false;
        }

        // LOG_DEBUG("MOCK: Resuming task '%s' (thread ID: %p)", task_name.c_str(),
        //           thread_id);

        {
            std::unique_lock<std::mutex> lock(task_info->mutex);

            // If not suspended, nothing to do
            if (!task_info->suspended) {
                // LOG_DEBUG("MOCK: Task '%s' is already running",
                //   task_name.c_str());
                return true;
            }

            // Set the resumed state and reset acknowledgment flag
            task_info->suspended = false;
            task_info->resume_acknowledged = false;
        }

        // Notify the task to wake up - notify ALL condition variables
        task_info->cv.notify_all();         // For ShouldStopOrPause
        task_info->notify_cv.notify_all();  // For WaitForNotify - CRITICAL!
        // LOG_DEBUG(
        //     "MOCK: Task '%s' resume signal sent to all condition variables",
        //     task_name.c_str());

        // Wait for task to acknowledge the resume
        // We only wait if this isn't a self-resume
        if (taskHandle && static_cast<std::thread*>(taskHandle)->get_id() !=
                              std::this_thread::get_id()) {

            std::unique_lock<std::mutex> lock(task_info->mutex);

            // Use our waitFor helper that respects virtual time
            // IMPORTANT: Increased timeout and better predicate
            bool acknowledged =
                waitFor(task_info->resume_ack_cv, lock,
                        1000 /* 1 second timeout */, [task_info]() {
                            return task_info->resume_acknowledged ||
                                   task_info->stop_requested;
                        });

            if (!acknowledged) {
                // LOG_DEBUG(
                //     "MOCK: Task '%s' did not acknowledge resume within "
                //     "timeout, "
                //     "but resume signal was sent successfully",
                //     task_name.c_str());
                // Changed from WARNING to DEBUG and improved message
                // Return true anyway as we've sent the resume signal
                return true;
            }

            // LOG_DEBUG("MOCK: Task '%s' acknowledged resume", task_name.c_str());
        }

        return true;
    }

    /**
     * @brief Checks if the current task should pause or stop
     * 
     * This function should be called periodically from task functions that
     * have their own infinite loops. It will block if the task is suspended.
     * 
     * @param taskHandle Handle to the task to check, or nullptr for the current task
     * @return true if the task should stop, false to continue
     */
    bool ShouldStopOrPause() override {
        std::thread::id thread_id = std::this_thread::get_id();
        std::string task_name = "current";
        TaskInfo* task_info = nullptr;
        bool should_stop = false;
        bool is_suspended = false;

        // Find the task info with proper locking
        {
            std::lock_guard<std::timed_mutex> lock(tasksMutex_);

            // Look for the task with this thread ID
            for (auto& pair : tasks_) {
                if (pair.second.thread_id == thread_id) {
                    task_info = &(pair.second);
                    task_name = pair.second.name;

                    // Quick check for stop request without additional locking
                    should_stop = task_info->stop_requested;
                    is_suspended = task_info->suspended;
                    break;
                }
            }
        }

        if (!task_info) {
            LOG_WARNING(
                "MOCK: Failed to find task with thread ID %p for "
                "ShouldStopOrPause",
                thread_id);
            return false;
        }

        // If stop requested, return immediately
        if (should_stop) {
            // LOG_DEBUG("MOCK: Task '%s' should stop (quick check)",
            //   task_name.c_str());
            return true;
        }

        // If task is suspended, wait until resumed or stop requested
        if (is_suspended) {
            // LOG_DEBUG(
            //     "MOCK: Task '%s' is suspended, waiting for resume or stop",
            //     task_name.c_str());

            std::unique_lock<std::mutex> lock(task_info->mutex);

            // Before waiting, acknowledge that we're now suspended
            task_info->suspension_acknowledged = true;
            task_info->suspend_ack_cv.notify_all();

            // Wait with timeout to prevent indefinite blocking
            // Use our waitFor helper that respects virtual time
            bool status = waitFor(
                task_info->cv, lock, 500 /* 500ms timeout */, [task_info]() {
                    return !task_info->suspended || task_info->stop_requested;
                });

            if (!status) {
                // LOG_DEBUG("MOCK: Task '%s' wait timeout, rechecking condition",
                //   task_name.c_str());
            } else {
                // LOG_DEBUG("MOCK: Task '%s' condition changed",
                //   task_name.c_str());
            }

            // After wait, recheck stop flag
            if (task_info->stop_requested) {
                // LOG_DEBUG("MOCK: Task '%s' should stop after wait",
                //   task_name.c_str());
                return true;
            }

            // If we're resumed, acknowledge it
            if (!task_info->suspended) {
                task_info->resume_acknowledged = true;
                task_info->resume_ack_cv.notify_all();
                // LOG_DEBUG("MOCK: Task '%s' acknowledged resume",
                //   task_name.c_str());
            }

            // LOG_DEBUG("MOCK: Task '%s' resumed", task_name.c_str());
        }

        return false;
    }

    struct QueueData {
        std::mutex mutex;
        std::condition_variable notEmpty;
        std::condition_variable notFull;
        std::queue<std::vector<uint8_t>> data;
        uint32_t maxSize;
        uint32_t itemSize;
    };

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
        LOG_DEBUG("MOCK: Deleting queue");
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

            // Use our waitFor helper that respects virtual time
            bool success = waitFor(q->notFull, lock, timeout, [q]() {
                return q->data.size() < q->maxSize;
            });

            if (!success) {
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

            // Use our waitFor helper that respects virtual time
            bool success = waitFor(q->notEmpty, lock, timeout,
                                   [q]() { return !q->data.empty(); });

            if (!success) {
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

    /**
     * @brief Delays the current task for the specified time
     * 
     * In real-time mode, this uses actual thread sleep
     * In virtual time mode, this registers a wait that can be unblocked by advanceTime
     * 
     * @param ms Number of milliseconds to delay
     */
    void delay(uint32_t ms) override {
        if (timeMode_ == TimeMode::kRealTime) {
            // In real-time mode, use actual sleep
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            return;
        }

        // In virtual time mode, calculate wake time and wait conditionally
        std::condition_variable cv;
        std::mutex m;
        std::unique_lock<std::mutex> lock(m);

        uint64_t wakeTimeMs;

        // Register this task as waiting until the wake time
        {
            std::lock_guard<std::mutex> timeLock(timeMutex_);
            wakeTimeMs = virtualTimeMs_ + ms;
            waitingTasks_[&cv] = wakeTimeMs;
            // // LOG_DEBUG("MOCK: Task will wait until virtual time %llu ms",
            //           (unsigned long long)wakeTimeMs);
        }

        // Wait until either the virtual time advances beyond our wake time,
        // or we are explicitly woken up by advanceTime
        cv.wait(lock, [this, wakeTimeMs]() {
            std::lock_guard<std::mutex> timeLock(timeMutex_);
            return virtualTimeMs_ >= wakeTimeMs;
        });

        // // LOG_DEBUG("MOCK: Task woke up after waiting until %llu ms",
        //           (unsigned long long)wakeTimeMs);

        // Clean up (in case we were woken by something other than advanceTime)
        {
            std::lock_guard<std::mutex> timeLock(timeMutex_);
            waitingTasks_.erase(&cv);
        }
    }

    /**
     * @brief Gets the current tick count (time)
     * 
     * In real-time mode, returns actual system time
     * In virtual time mode, returns the virtual time counter
     * 
     * @return Current time in milliseconds
     */
    uint32_t getTickCount() override {
        if (timeMode_ == TimeMode::kRealTime) {
            // In real-time mode, use actual system time
            auto now = std::chrono::steady_clock::now();
            auto duration = now.time_since_epoch();
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                       duration)
                .count();
        } else {
            // In virtual time mode, return the virtual time
            std::lock_guard<std::mutex> lock(timeMutex_);
            return static_cast<uint32_t>(virtualTimeMs_);
        }
    }

    void StartScheduler() override {
        // Nothing to do in mock implementation
    }

    /**
     * @brief Gets the stack watermark for a task
     * @param taskHandle Handle to the task, or nullptr for the current task
     * @return Current stack watermark value in bytes
     */
    uint32_t getTaskStackWatermark(TaskHandle_t taskHandle) override {
        std::lock_guard<std::timed_mutex> lock(tasksMutex_);
        std::thread::id current_id;

        if (taskHandle == nullptr) {
            // Get the stack watermark for the current task
            current_id = std::this_thread::get_id();
        } else {
            auto* thread = static_cast<std::thread*>(taskHandle);
            current_id = thread->get_id();
        }

        for (const auto& [thread, info] : tasks_) {
            if (thread->get_id() == current_id) {
                // If we haven't set a specific watermark for this task, simulate one
                if (info.stack_watermark == 0) {
                    // Simulate a watermark between 60-90% of total stack size
                    uint32_t used_percentage =
                        10 + (std::rand() % 30);  // 10-40% used
                    uint32_t free_percentage = 100 - used_percentage;
                    return (info.stack_size * free_percentage) / 100;
                }
                return info.stack_watermark;
            }
        }

        // Task not found, return default value
        return 2048;
    }

    /**
     * @brief Gets the current state of a task
     * @param taskHandle Handle to the task, or nullptr for the current task
     * @return Task state
     */
    TaskState getTaskState(TaskHandle_t taskHandle) override {
        std::lock_guard<std::timed_mutex> lock(tasksMutex_);
        std::thread::id current_id;

        if (taskHandle == nullptr) {
            current_id = std::this_thread::get_id();

            // Find the task entry for the current thread
            for (const auto& [thread, info] : tasks_) {
                if (thread->get_id() == current_id) {
                    return info.suspended ? TaskState::kSuspended
                                          : TaskState::kRunning;
                }
            }
        } else {
            auto* thread = static_cast<std::thread*>(taskHandle);
            if (auto it = tasks_.find(thread); it != tasks_.end()) {
                return it->second.suspended ? TaskState::kSuspended
                                            : TaskState::kRunning;
            }
        }

        return TaskState::kUnknown;
    }

    std::vector<TaskStats> getSystemTaskStats() override {
        std::vector<TaskStats> stats;
        std::lock_guard<std::timed_mutex> lock(tasksMutex_);

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

    // The rest of the methods remain the same...
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
     * @brief Notifies a task, waking it if it was waiting for notification
     * @param task_handle Handle to the task to notify, or nullptr for the current task
     */
    void NotifyTaskFromISR(TaskHandle_t task_handle) override {
        std::lock_guard<std::timed_mutex> lock(tasksMutex_);
        std::thread::id current_id;

        if (task_handle == nullptr) {
            current_id = std::this_thread::get_id();

            // Find the task entry for the current thread
            for (const auto& [thread, info] : tasks_) {
                if (thread->get_id() == current_id) {
                    // Set notification flag for this task
                    tasks_[thread].notification_pending = true;

                    // Wake up the task if it's waiting for a notification
                    tasks_[thread].notify_cv.notify_one();
                    return;
                }
            }
        } else {
            auto* thread = static_cast<std::thread*>(task_handle);

            if (auto it = tasks_.find(thread); it != tasks_.end()) {
                // Set notification flag for this task
                it->second.notification_pending = true;

                // Wake up the task if it's waiting for a notification
                it->second.notify_cv.notify_one();
            }
        }
    }

    QueueResult NotifyTask(TaskHandle_t task_handle, uint32_t value) override {
        // Prevent unused parameter warnings
        (void)value;

        std::lock_guard<std::timed_mutex> lock(tasksMutex_);
        std::thread::id current_id;

        if (task_handle == nullptr) {
            current_id = std::this_thread::get_id();

            // Find the task entry for the current thread
            for (const auto& [thread, info] : tasks_) {
                if (thread->get_id() == current_id) {
                    // Set notification flag for this task
                    tasks_[thread].notification_pending = true;

                    // Wake up the task if it's waiting for a notification
                    tasks_[thread].notify_cv.notify_one();
                    return QueueResult::kOk;
                }
            }
        } else {
            auto* thread = static_cast<std::thread*>(task_handle);

            if (auto it = tasks_.find(thread); it != tasks_.end()) {
                // Set notification flag for this task
                it->second.notification_pending = true;

                // Wake up the task if it's waiting for a notification
                it->second.notify_cv.notify_one();
                return QueueResult::kOk;
            }
        }

        return QueueResult::kError;
    }

    QueueResult WaitForNotify(uint32_t timeout) override {
        // LOG_DEBUG("MOCK: Waiting for notification with timeout %u ms", timeout);

        // Find the current task's handle
        thread_local TaskHandle_t this_task = nullptr;
        TaskInfo* task_info = nullptr;

        // First time call from this thread, find the task handle
        {
            std::lock_guard<std::timed_mutex> lock(tasksMutex_);

            if (!this_task) {
                // Try to find this thread in our task list by comparing thread IDs
                std::thread::id current_id = std::this_thread::get_id();
                for (const auto& [thread_ptr, info] : tasks_) {
                    if (thread_ptr->get_id() == current_id) {
                        this_task = thread_ptr;
                        task_info = &(tasks_[thread_ptr]);
                        break;
                    }
                }

                // If we couldn't find this thread, return error
                if (!this_task) {
                    LOG_WARNING(
                        "MOCK: Could not find task handle for current thread "
                        "in WaitForNotify");
                    return QueueResult::kError;
                }
            } else {
                // We have a cached task handle, verify it still exists
                auto it = tasks_.find(static_cast<std::thread*>(this_task));
                if (it == tasks_.end()) {
                    this_task = nullptr;  // Reset the cache
                    LOG_WARNING(
                        "MOCK: Cached task handle invalid in WaitForNotify");
                    return QueueResult::kError;
                }
                task_info = &(it->second);
            }

            // Quick check for stop request or pending notification without waiting
            if (task_info->stop_requested) {
                LOG_DEBUG(
                    "MOCK: Task received stop request during WaitForNotify");
                return QueueResult::kError;
            }

            // IMPORTANT: Check for suspension before checking notification
            if (task_info->suspended) {
                LOG_DEBUG(
                    "MOCK: Task is suspended during WaitForNotify, will wait "
                    "for resume");
                // Don't return error here - we should wait for resume or stop
            }

            // If we already have a pending notification and not suspended, consume it
            if (task_info->notification_pending && !task_info->suspended) {
                task_info->notification_pending = false;
                LOG_DEBUG("MOCK: Consumed pending notification immediately");
                return QueueResult::kOk;
            }
        }

        // If timeout is 0, don't wait
        if (timeout == 0) {
            return QueueResult::kTimeout;
        }

        // Wait for notification, resume, or stop request
        {
            std::unique_lock<std::mutex> lock(task_info->mutex);

            // Remember the initial suspension state to detect changes
            bool initial_suspended_state = task_info->suspended;

            // CRITICAL FIX: Set up resume acknowledgment BEFORE waiting
            // This ensures that if we get suspended and then resumed while waiting,
            // the ResumeTask call won't timeout waiting for acknowledgment
            auto wait_predicate = [this, task_info, initial_suspended_state]() {
                // Check conditions under the proper lock
                std::lock_guard<std::timed_mutex> tasks_lock(tasksMutex_);
                auto it = tasks_.find(static_cast<std::thread*>(this_task));
                if (it == tasks_.end()) {
                    return true;  // Task deleted, exit wait
                }

                // If we were just resumed, acknowledge it immediately
                if (initial_suspended_state && !it->second.suspended &&
                    !it->second.resume_acknowledged) {
                    it->second.resume_acknowledged = true;
                    it->second.resume_ack_cv.notify_all();
                    LOG_DEBUG(
                        "MOCK: WaitForNotify acknowledged resume operation");
                }

                // Return true if:
                // 1. Stop requested (should exit)
                // 2. Not suspended AND have pending notification (normal notification)
                // 3. Suspension state changed (either got suspended or resumed)
                return it->second.stop_requested ||
                       (!it->second.suspended &&
                        it->second.notification_pending) ||
                       (it->second.suspended != initial_suspended_state);
            };

            if (timeout == MAX_DELAY) {
                // Use our waitFor helper with a very long timeout
                waitFor(task_info->notify_cv, lock,
                        3600 * 1000,  // 1 hour timeout as MAX_DELAY equivalent
                        wait_predicate);
            } else {
                // Regular timeout case
                waitFor(task_info->notify_cv, lock, timeout, wait_predicate);
            }
        }

        // After waiting, check what happened
        {
            std::lock_guard<std::timed_mutex> lock(tasksMutex_);

            // Double-check that the task still exists
            auto it = tasks_.find(static_cast<std::thread*>(this_task));
            if (it == tasks_.end()) {
                this_task = nullptr;  // Reset the cache
                // LOG_DEBUG("MOCK: Task deleted during WaitForNotify");
                return QueueResult::kError;
            }

            if (it->second.stop_requested) {
                LOG_DEBUG(
                    "MOCK: Task received stop request after wait in "
                    "WaitForNotify");
                return QueueResult::kError;
            }

            // Check what caused us to wake up
            if (it->second.suspended) {
                LOG_DEBUG("MOCK: WaitForNotify woke up due to suspension");

                // IMPORTANT: Acknowledge the suspension here since we detected it
                // This prevents SuspendTask from waiting indefinitely
                if (!it->second.suspension_acknowledged) {
                    it->second.suspension_acknowledged = true;
                    it->second.suspend_ack_cv.notify_all();
                    LOG_DEBUG("MOCK: WaitForNotify acknowledged suspension");
                }

                // Don't consume any pending notification while suspended
                return QueueResult::
                    kTimeout;  // Return timeout to indicate we should check suspension state
            }

            if (it->second.notification_pending) {
                // Notification received and we're not suspended, consume it
                it->second.notification_pending = false;
                LOG_DEBUG("MOCK: Notification received after wait");
                return QueueResult::kOk;
            } else {
                // Timeout occurred
                LOG_DEBUG("MOCK: Notification wait timeout");
                return QueueResult::kTimeout;
            }
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

        // Create a queue with length 1 and item size 0 (we don't need to store data)
        QueueHandle_t queue = CreateQueue(1, sizeof(uint8_t));

        // Binary semaphores in FreeRTOS start in the "unavailable" state
        // No need to add any data to the queue

        return queue;
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
        // Validate parameters
        if (initialCount > maxCount) {
            initialCount = maxCount;
        }

        // Create a queue with length maxCount and item size 1 (we just need a token)
        QueueHandle_t queue = CreateQueue(maxCount, sizeof(uint8_t));

        // Add initialCount items to the queue
        uint8_t dummy =
            1;  // Dummy data, we only care about the presence of an item
        for (uint32_t i = 0; i < initialCount; i++) {
            SendToQueue(queue, &dummy, 0);
        }

        return queue;
    }

    /**
     * @brief Deletes a semaphore
     * @param semaphore Handle to the semaphore to delete
     */
    void DeleteSemaphore(SemaphoreHandle_t semaphore) override {
        DeleteQueue(semaphore);
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
        uint8_t dummy;
        QueueResult result = ReceiveFromQueue(semaphore, &dummy, timeout);
        return result == QueueResult::kOk;
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
        uint8_t dummy = 1;  // Dummy data
        QueueResult result = SendToQueue(semaphore, &dummy, 0);
        return result == QueueResult::kOk;
    }

    /**
     * @brief Gives (releases) a semaphore from an ISR context
     * @param semaphore Handle to the semaphore
     * @return true if successful, false otherwise
     */
    bool GiveSemaphoreFromISR(SemaphoreHandle_t semaphore) override {
        uint8_t dummy = 1;  // Dummy data
        QueueResult result = SendToQueueISR(semaphore, &dummy);
        return result == QueueResult::kOk;
    }

    /**
     * @brief Cross-platform thread/task yield function
     * 
     * Allows a task to yield execution to other tasks of equal priority.
     */
    inline void YieldTask() override { std::this_thread::yield(); }

    /**
     * @brief Registers a timer callback that will be called when time advances
     * 
     * This method is only relevant in virtual time mode.
     * 
     * @param callback Function to call when timer expires
     * @param delayMs Delay before first trigger in milliseconds
     * @param periodMs Repeat period (0 for one-shot timer)
     * @return Timer ID for later management
     */
    uint32_t createTimer(std::function<void()> callback, uint32_t delayMs,
                         uint32_t periodMs = 0) {
        std::lock_guard<std::mutex> lock(timeMutex_);

        TimerCallback timer;
        timer.callback = callback;
        timer.expiryTime = virtualTimeMs_ + delayMs;
        timer.period = periodMs;
        timer.active = true;

        uint32_t timerId = static_cast<uint32_t>(timerCallbacks_.size());
        timerCallbacks_.push_back(timer);

        LOG_DEBUG("MOCK: Created timer %u, expires at %llu ms, period %u ms",
                  timerId, (unsigned long long)timer.expiryTime, periodMs);

        return timerId;
    }

    /**
     * @brief Stops a previously created timer
     * 
     * @param timerId ID of the timer to stop
     * @return true if timer was found and stopped, false otherwise
     */
    bool stopTimer(uint32_t timerId) {
        std::lock_guard<std::mutex> lock(timeMutex_);

        if (timerId >= timerCallbacks_.size()) {
            LOG_WARNING("MOCK: Invalid timer ID %u", timerId);
            return false;
        }

        timerCallbacks_[timerId].active = false;
        LOG_DEBUG("MOCK: Stopped timer %u", timerId);
        return true;
    }

    /**
     * @brief Updates or replaces all condition variable wait_for operations
     * to be compatible with virtual time
     * 
     * @param cv The condition variable to wait on
     * @param lock The unique lock protecting the condition variable
     * @param relTimeMs The relative time to wait in milliseconds
     * @param pred The predicate function that determines when to stop waiting
     * @return true if predicate became true, false if timeout occurred
     */
    template <typename Predicate>
    bool waitFor(std::condition_variable& cv,
                 std::unique_lock<std::mutex>& lock, uint32_t relTimeMs,
                 Predicate pred) {
        if (timeMode_ == TimeMode::kRealTime) {
            // In real-time mode, use standard wait_for
            return cv.wait_for(lock, std::chrono::milliseconds(relTimeMs),
                               pred);
        }

        // In virtual time mode, we need to register this wait
        uint64_t wakeTimeMs;

        // Fast path: check predicate before waiting
        if (pred()) {
            return true;
        }

        // Register this task as waiting until the wake time
        {
            std::lock_guard<std::mutex> timeLock(timeMutex_);
            wakeTimeMs = virtualTimeMs_ + relTimeMs;
            waitingTasks_[&cv] = wakeTimeMs;
        }

        // Wait until either:
        // 1. The predicate becomes true
        // 2. The virtual time advances beyond our wake time
        cv.wait_until(lock,
                      std::chrono::steady_clock::now() + std::chrono::hours(1),
                      [this, wakeTimeMs, &pred]() {
                          // Check if the predicate is true (normal wake up)
                          if (pred()) {
                              return true;
                          }

                          // Check if we've reached the virtual timeout
                          std::lock_guard<std::mutex> timeLock(timeMutex_);
                          return virtualTimeMs_ >= wakeTimeMs;
                      });

        // Clean up our wait registration
        {
            std::lock_guard<std::mutex> timeLock(timeMutex_);
            waitingTasks_.erase(&cv);
        }

        // Return true only if the predicate is satisfied
        return pred();
    }

    /**
     * @brief Set the node address for the current task
     * @param address The node address as a string (e.g., "0x1001")
     */
    void SetCurrentTaskNodeAddress(const std::string& address) override {
        // Update thread-local cache first for immediate availability
        setThreadLocalNodeAddress(address);

        std::lock_guard<std::timed_mutex> lock(tasksMutex_);

        // Find the current task and update TaskInfo
        std::thread::id current_id = std::this_thread::get_id();
        for (auto& [thread_ptr, task_info] : tasks_) {
            if (task_info.thread_id == current_id) {
                task_info.node_address = address;
                break;
            }
        }
    }

    /**
     * @brief Get the node address for the current task
     * @return The node address as a string, or empty string if not set
     */
    std::string GetCurrentTaskNodeAddress() const override {
        // First try thread-local cache for best performance
        std::string cached_address = getThreadLocalNodeAddress();
        if (!cached_address.empty()) {
            return cached_address;
        }

        // Fallback to mutex-protected lookup with a short timeout
        std::unique_lock<std::timed_mutex> lock(tasksMutex_, std::try_to_lock);

        // If we can't acquire the lock immediately, try with a short timeout
        if (!lock.owns_lock()) {
            if (!lock.try_lock_for(std::chrono::milliseconds(5))) {
                // If still can't get the lock after timeout, return empty to prevent deadlock
                // This should be rare since most operations are fast
                return "";
            }
        }

        // Find the current task and update thread-local cache
        std::thread::id current_id = std::this_thread::get_id();
        for (const auto& [thread_ptr, task_info] : tasks_) {
            if (task_info.thread_id == current_id) {
                // Update thread-local cache for future calls
                if (!task_info.node_address.empty()) {
                    setThreadLocalNodeAddress(task_info.node_address);
                }
                return task_info.node_address;
            }
        }
        return "";
    }

   private:
    /**
     * @brief Thread-local storage for node address cache
     * This avoids mutex contention in GetCurrentTaskNodeAddress()
     */
    static thread_local std::string thread_local_node_address_;

    /**
     * @brief Set the thread-local node address cache
     * @param address The node address to cache
     */
    static void setThreadLocalNodeAddress(const std::string& address) {
        thread_local_node_address_ = address;
    }

    /**
     * @brief Get the thread-local node address cache
     * @return The cached node address, or empty string if not cached
     */
    static std::string getThreadLocalNodeAddress() {
        return thread_local_node_address_;
    }

    struct TaskInfo {
        std::string name;
        uint32_t stack_size = 0;
        uint32_t priority = 0;
        uint32_t stack_watermark = 0;
        std::thread::id thread_id;

        // For notification support
        bool notification_pending = false;
        std::condition_variable notify_cv;

        std::mutex mutex;
        std::condition_variable cv;
        bool suspended = false;
        bool stop_requested = false;

        // For suspend/resume acknowledgment
        bool suspension_acknowledged = false;
        bool resume_acknowledged = false;
        std::condition_variable suspend_ack_cv;
        std::condition_variable resume_ack_cv;

        // For logging context
        std::string node_address;
    };

    TimeMode timeMode_;       ///< Current time mode (real or virtual)
    uint64_t virtualTimeMs_;  ///< Virtual time counter in milliseconds
    mutable std::mutex
        timeMutex_;  ///< Mutex protecting time-related operations

    std::map<std::condition_variable*, uint64_t>
        waitingTasks_;  ///< Tasks waiting for time to advance
    std::vector<TimerCallback> timerCallbacks_;  ///< Timer callbacks

    std::map<std::thread*, TaskInfo> tasks_;
    mutable std::timed_mutex tasksMutex_;
    std::vector<void (*)()> registeredISRs_;
    std::mutex isrMutex_;
};

}  // namespace os

/**  
 * @brief Provides access to the RTOS singleton instance
 * @return Reference to the RTOS singleton instance
*/
[[maybe_unused]] static os::RTOS& GetRTOS() {
    return os::RTOS::instance();
}

}  // namespace loramesher

#endif  // LORAMESHER_BUILD_NATIVE