/**
 * @file os/rtos_mock.hpp
 * @brief Mock RTOS implementation compatible with FreeRTOS task style
 */
#pragma once

#include "config/system_config.hpp"

#ifdef LORAMESHER_BUILD_NATIVE

// Provide __lsan_ignore_object only when AddressSanitizer (which includes LSan)
// is active. Without -fsanitize=address the sanitizer runtime is not linked, so
// the function would be an undefined reference. Fall back to a no-op otherwise.
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#include <sanitizer/lsan_interface.h>
#else
#define __lsan_ignore_object(p) ((void)(p))
#endif
#elif defined(__SANITIZE_ADDRESS__)
#include <sanitizer/lsan_interface.h>
#else
#define __lsan_ignore_object(p) ((void)(p))
#endif
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <future>
#include <map>
#include <mutex>
#include <optional>
#include <queue>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "os/rtos.hpp"
#include "utils/logger.hpp"

namespace loramesher {
namespace os {

/**
 * @class TaskTerminationException
 * @brief Exception thrown when a task needs to be terminated during RTOS operations
 */
class TaskTerminationException : public std::exception {
   public:
    const char* what() const noexcept override {
        return "Task terminated by RTOS";
    }
};

/**
 * @class RTOSMock
 * @brief Mock implementation of RTOS interface compatible with FreeRTOS style tasks
 */
class RTOSMock : public RTOS {
   private:
    struct TaskInfo;  // Forward declaration

   public:
    /// Virtual time value (ms) set whenever the mock switches to virtual time
    static constexpr uint64_t kVirtualEpochMs = 1'000'000;

    /// Seed of the GetRandom() generators until SeedRandom() is called
    static constexpr uint32_t kDefaultRandomSeed = 42;

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
     * @brief Source of virtual-time events outside the RTOS (e.g. a simulated
     * radio network) that advanceTime() interleaves with task wake-ups
     */
    class VirtualEventSource {
       public:
        virtual ~VirtualEventSource() = default;

        /**
         * @brief Virtual time of the earliest pending event, if any
         */
        virtual std::optional<uint64_t> NextEventTime() = 0;

        /**
         * @brief Process the earliest pending event; called with the virtual
         * clock set to that event's time
         *
         * @param now Current virtual time in milliseconds
         */
        virtual void ProcessNextEvent(uint64_t now) = 0;
    };

    /// Real-time budget for woken tasks to block again before a step is
    /// reported as a reblock timeout
    static constexpr uint32_t kReblockTimeoutMs = 1000;

    /// Upper bound of events processed at one virtual instant; exceeding it
    /// means some task re-arms a zero-length wait forever
    static constexpr uint32_t kMaxEventsPerInstant = 100000;

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
          waiters_(),
          timerCallbacks_() {}

    /**
     * @brief Sets the time mode for the RTOS mock
     * 
     * @param mode The time mode to use (real or virtual)
     */
    void setTimeMode(TimeMode mode) {
        uint64_t debug_time = 0;
        int debug_case = 0;  // 1 = to-real, 2 = to-virtual

        {
            std::lock_guard<std::mutex> lock(timeMutex_);

            // If switching to real time, sync the virtual time with real time
            if (mode == TimeMode::kRealTime &&
                timeMode_ == TimeMode::kVirtualTime) {
                auto now = std::chrono::steady_clock::now();
                virtualTimeMs_ =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch())
                        .count();
                virtualTimeMsAtomic_.store(virtualTimeMs_,
                                           std::memory_order_release);
                debug_time = virtualTimeMs_;
                debug_case = 1;
            }
            // Virtual time always starts at the same fixed epoch
            else if (mode == TimeMode::kVirtualTime &&
                     timeMode_ == TimeMode::kRealTime) {
                virtualTimeMs_ = kVirtualEpochMs;
                virtualTimeMsAtomic_.store(virtualTimeMs_,
                                           std::memory_order_release);
                debug_time = virtualTimeMs_;
                debug_case = 2;
            }

            timeMode_ = mode;
        }  // timeMutex_ released here

        if (debug_case == 2) {
            std::lock_guard<std::timed_mutex> lock(tasksMutex_);
            task_name_counts_.clear();
        }

        // Log AFTER releasing timeMutex_ to avoid M1→M2 lock-order inversion.
        if (debug_case == 1) {
            LOG_DEBUG(
                "MOCK: Switching to real time mode, synced virtual time to "
                "%llu ms",
                (unsigned long long)debug_time);
        } else if (debug_case == 2) {
            LOG_DEBUG(
                "MOCK: Switching to virtual time mode, initialized to %llu ms",
                (unsigned long long)debug_time);
        }
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
     * Only has an effect in virtual time mode. Time advances event by event:
     * the clock jumps to the earliest pending deadline (task wait, timer, or
     * @p source event), that single event is processed, and the call waits for
     * every task to block again before looking for the next event. Events at
     * the same instant are processed task wake-ups first (ordered by task key),
     * then timers, then @p source events. Tasks therefore observe their exact
     * deadlines regardless of @p ms, and tasks due at the same instant never
     * run concurrently.
     *
     * @param ms Number of milliseconds to advance
     * @param source Optional external event source interleaved with task
     *        wake-ups
     * @return The new virtual time value in milliseconds
     */
    uint64_t advanceTime(uint32_t ms, VirtualEventSource* source = nullptr) {
        if (timeMode_ != TimeMode::kVirtualTime) {
            LOG_WARNING("MOCK: Cannot advance time in real-time mode");
            return getTickCount();
        }

        // Work triggered from outside advanceTime (e.g. the test thread sending
        // to a queue or starting a task) must settle before the next event.
        waitForTasksToReblock(kReblockTimeoutMs);

        const uint64_t target =
            virtualTimeMsAtomic_.load(std::memory_order_acquire) + ms;
        uint64_t instant = 0;
        uint32_t events_at_instant = 0;

        while (true) {
            std::optional<uint64_t> external_time =
                source ? source->NextEventTime() : std::nullopt;

            enum class EventKind { kNone, kWaiter, kTimer, kExternal };
            EventKind kind = EventKind::kNone;
            uint64_t event_time = 0;
            std::function<void()> timer_callback;
            {
                std::lock_guard<std::mutex> lock(timeMutex_);
                auto waiter_it = FindEarliestWaiterLocked();
                auto timer_it = FindEarliestTimerLocked();

                if (waiter_it != waiters_.end()) {
                    kind = EventKind::kWaiter;
                    event_time = waiter_it->second.deadline;
                }
                if (timer_it != timerCallbacks_.end() &&
                    (kind == EventKind::kNone ||
                     timer_it->expiryTime < event_time)) {
                    kind = EventKind::kTimer;
                    event_time = timer_it->expiryTime;
                }
                if (external_time &&
                    (kind == EventKind::kNone || *external_time < event_time)) {
                    kind = EventKind::kExternal;
                    event_time = *external_time;
                }
                if (kind == EventKind::kNone || event_time > target) {
                    break;
                }

                SetVirtualTimeLocked(std::max(event_time, virtualTimeMs_));

                if (kind == EventKind::kWaiter) {
                    WakeWaiterLocked(waiter_it);
                } else if (kind == EventKind::kTimer) {
                    timer_callback = timer_it->callback;
                    if (timer_it->period > 0) {
                        timer_it->expiryTime += timer_it->period;
                    } else {
                        timer_it->active = false;
                    }
                }
            }

            if (kind == EventKind::kTimer && timer_callback) {
                timer_callback();
            } else if (kind == EventKind::kExternal) {
                source->ProcessNextEvent(event_time);
            }

            waitForTasksToReblock(kReblockTimeoutMs);

            if (event_time != instant) {
                instant = event_time;
                events_at_instant = 0;
            }
            if (++events_at_instant > kMaxEventsPerInstant) {
                LOG_ERROR(
                    "MOCK: more than %u events at virtual time %llu ms; "
                    "skipping to %llu ms",
                    kMaxEventsPerInstant, (unsigned long long)instant,
                    (unsigned long long)target);
                break;
            }
        }

        {
            std::lock_guard<std::mutex> lock(timeMutex_);
            if (target > virtualTimeMs_) {
                SetVirtualTimeLocked(target);
            }
        }
        return target;
    }

    /**
     * @brief Gets the current virtual time value
     * 
     * @return Current virtual time in milliseconds
     */
    uint64_t getVirtualTime() const {
        return virtualTimeMsAtomic_.load(std::memory_order_acquire);
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

        // Create exit signal BEFORE thread starts so the lambda can capture it
        auto exit_signal = std::make_shared<std::promise<void>>();
        auto exit_future = exit_signal->get_future().share();

        // Start-gate: block the task thread until it is registered in tasks_
        auto ready_signal = std::make_shared<std::promise<void>>();
        auto ready_future = ready_signal->get_future().share();

        auto* thread = new std::thread([taskFunction, shared_params, task_name,
                                        exit_signal = std::move(exit_signal),
                                        ready_future, this]() mutable {
            ready_future
                .wait();  // Wait until this task is registered in tasks_

            // Cache task_info in thread-local storage for zero-cost lookup in
            // ReceiveFromQueue — one M0 acquisition per task lifetime.
            {
                std::thread::id my_id = std::this_thread::get_id();
                std::lock_guard<std::timed_mutex> lock(tasksMutex_);
                GetThreadLocalTaskInfo() = findCurrentTaskInfoUnsafe(my_id);
            }

            // Capture this task's stack base address now (just before the
            // user's task function runs). __builtin_frame_address(0) returns
            // the current function's frame pointer; on downward-growing stacks
            // (x86_64, ARM, AArch64) every nested frame has a smaller address.
            if (TaskInfo* my_info = GetThreadLocalTaskInfo()) {
                my_info->stack_base_addr.store(
                    reinterpret_cast<uintptr_t>(__builtin_frame_address(0)),
                    std::memory_order_relaxed);
            }

            try {
                // Call the user's task function with the captured parameters
                taskFunction(*shared_params);
            } catch (const std::exception& e) {
                LOG_ERROR("Exception in task '%s': %s", task_name.c_str(),
                          e.what());
            } catch (...) {
                LOG_ERROR("Unknown exception in task '%s'", task_name.c_str());
            }
            if (TaskInfo* my_info = GetThreadLocalTaskInfo()) {
                my_info->exited.store(true, std::memory_order_release);
            }
            NotifyReblockWaiter();
            GetThreadLocalTaskInfo() = nullptr;  // clear before signaling exit
            exit_signal->set_value();  // signal DeleteTask() we're done
        });

        // Tasks are ordered by the node address of the creating thread, the
        // task name and a per-(address, name) counter, all independent of
        // thread scheduling.
        std::string order_base =
            std::string(getThreadLocalNodeAddress()) + "/" + task_name;

        // Store task information
        {
            std::lock_guard<std::timed_mutex> lock(tasksMutex_);

            // Initialize the TaskInfo directly in the map to avoid copy assignment
            auto& task_info = tasks_[thread];
            task_info.name = task_name;
            uint32_t index = task_name_counts_[order_base]++;
            char suffix[16];
            snprintf(suffix, sizeof(suffix), "#%06u", index);
            task_info.order_key = order_base + suffix;
            task_info.stack_size = stackSize;
            task_info.priority = priority;
            task_info.thread_id = thread->get_id();
            task_info.stack_watermark = 0;
            task_info.notification_pending.store(false,
                                                 std::memory_order_relaxed);
            task_info.suspended = false;
            task_info.stop_requested.store(false, std::memory_order_relaxed);
            task_info.exit_future = exit_future;

            // LOG_DEBUG("MOCK: Task '%s' registered with thread ID %p",
            //   task_name.c_str(), task_info.thread_id);
        }

        if (taskHandle) {
            *taskHandle = thread;
        }

        // Unblock the task thread now that registration is complete
        ready_signal->set_value();

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
        bool task_not_found = false;
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
                // Use release ordering to ensure all previous writes are visible
                it->second.stop_requested.store(true,
                                                std::memory_order_release);

                // Set delay interruption flag to wake up any delay() calls
                it->second.delay_interrupted.store(true,
                                                   std::memory_order_release);

                // If task is suspended, resume it so it can process the stop request
                if (was_suspended) {
                    // LOG_DEBUG(
                    //     "MOCK: Resuming suspended task '%s' before deletion",
                    //     task_name.c_str());
                    it->second.suspended = false;
                }
            } else {
                task_not_found = true;
            }
        }
        if (task_not_found) {
            LOG_WARNING("MOCK: Task handle %p not found in tasks map",
                        taskHandle);
            return;
        }

        // Notify the task to wake up - do this outside the tasksMutex_ lock to prevent deadlock
        if (task_info) {
            // Update stop_requested and suspended under task_info->mutex so that
            // ShouldStopOrPause's re-check (also under task_info->mutex) sees
            // the updated state before we call notify_all().  This closes the
            // missed-notification race where notify fires before the waiter
            // enters cv.wait_for.
            {
                std::lock_guard<std::mutex> lock(task_info->mutex);
                task_info->stop_requested.store(true,
                                                std::memory_order_release);
                task_info->suspended.store(false, std::memory_order_release);

                // CRITICAL: Notify all queue condition variables the task is waiting on
                for (auto* queue_cv : task_info->waiting_on_queue_cvs) {
                    if (queue_cv) {
                        queue_cv->notify_all();  // For ReceiveFromQueue calls
                    }
                }
            }

            // CRITICAL: Notify ALL condition variables the task might be waiting on
            task_info->cv.notify_all();         // For ShouldStopOrPause
            task_info->notify_cv.notify_all();  // For WaitForNotify
            task_info->suspend_ack_cv
                .notify_all();  // For suspend acknowledgment
            task_info->resume_ack_cv.notify_all();  // For resume acknowledgment
            task_info->delay_cv.notify_all();       // For delay() calls
            tasks_blocked_cv_.notify_all();

            // LOG_DEBUG(
            //     "MOCK: Notified all condition variables for task '%s' deletion",
            //     task_name.c_str());
        }

        if (thread_id != std::thread::id()) {
            // Give the task a moment to exit gracefully
            std::this_thread::yield();

            // Wait for thread to finish with a reasonable timeout.
            // Must be > waitFor's 1000ms wall-clock wait_until to avoid
            // detaching a thread that is still blocked inside ReceiveFromQueue
            // (missed-notification race) and then freeing the queue under it.
            if (thread->joinable()) {
                auto status = task_info->exit_future.wait_for(
                    std::chrono::milliseconds(2000));
                if (status == std::future_status::ready) {
                    thread->join();
                } else {
                    LOG_WARNING(
                        "MOCK: Task '%s' did not exit cleanly within timeout",
                        task_name.c_str());
                    if (thread->joinable()) {
                        thread->detach();
                    }
                }
            }
        }

        // Clean up resources
        {
            std::lock_guard<std::timed_mutex> lock(tasksMutex_);
            tasks_.erase(thread);
        }

        delete thread;
        LOG_DEBUG("MOCK: Task '%s' deleted", task_name.c_str());
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

            bool task_not_found = false;
            {
                std::lock_guard<std::timed_mutex> lock(tasksMutex_);
                auto it = tasks_.find(thread);
                if (it != tasks_.end()) {
                    thread_id = it->second.thread_id;
                    task_name = it->second.name;
                    task_info = &(it->second);
                } else {
                    task_not_found = true;
                }
            }
            if (task_not_found) {
                LOG_WARNING("MOCK: Task handle %p not found for suspension",
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
                "MOCK: Failed to find task with thread ID %p for suspension",
                thread_id);
            return false;
        }

        LOG_DEBUG("MOCK: Suspending task '%s' (thread ID: %p)",
                  task_name.c_str(), thread_id);

        // Set up confirmation mechanism
        bool already_suspended = false;
        {
            std::unique_lock<std::mutex> lock(task_info->mutex);

            // If already suspended, nothing to do
            if (task_info->suspended) {
                already_suspended = true;
            } else {
                // Set suspended state and prepare for acknowledgment
                task_info->suspended = true;
                task_info->suspension_acknowledged = false;
                task_info->cv.notify_all();
                task_info->notify_cv.notify_all();
            }
        }  // task_info->mutex released before logging
        if (!already_suspended) {
            tasks_blocked_cv_.notify_all();
        }
        if (already_suspended) {
            LOG_DEBUG("MOCK: Task '%s' is already suspended",
                      task_name.c_str());
            return true;
        }
        LOG_DEBUG("MOCK: Set suspended flag for task '%s'", task_name.c_str());

        // CRITICAL FIX: Notify ALL condition variables the task might be waiting on
        task_info->cv.notify_all();  // For ShouldStopOrPause
        task_info->notify_cv
            .notify_all();  // For WaitForNotify - THIS WAS MISSING!
        task_info->suspend_ack_cv.notify_all();  // For suspend acknowledgment
        task_info->resume_ack_cv.notify_all();   // For resume acknowledgment

        // CRITICAL: Notify all queue condition variables the task is waiting on
        {
            std::lock_guard<std::mutex> lock(task_info->mutex);
            for (auto* queue_cv : task_info->waiting_on_queue_cvs) {
                if (queue_cv) {
                    queue_cv->notify_all();  // For ReceiveFromQueue calls
                }
            }
        }

        LOG_DEBUG(
            "MOCK: Notified all condition variables for task '%s' suspension",
            task_name.c_str());

        // For self-suspension (current task), we don't need to wait for acknowledgment
        if (!taskHandle || static_cast<std::thread*>(taskHandle)->get_id() ==
                               std::this_thread::get_id()) {
            LOG_DEBUG("MOCK: Self-suspension of task '%s'", task_name.c_str());
            return true;
        }

        // Wait for the task to acknowledge it's suspended
        // This happens when the task calls ShouldStopOrPause()
        bool acknowledged;
        {
            std::unique_lock<std::mutex> lock(task_info->mutex);

            // Wait with a reasonable timeout (500ms)
            // Use our waitFor helper that respects virtual time
            acknowledged =
                waitFor(task_info->suspend_ack_cv, lock, 10, [task_info]() {
                    return task_info->suspension_acknowledged ||
                           task_info->stop_requested.load(
                               std::memory_order_relaxed);
                });
        }  // task_info->mutex released before logging
        if (!acknowledged) {
            LOG_WARNING(
                "MOCK: Timeout waiting for task '%s' to acknowledge suspension",
                task_name.c_str());
            // Return true anyway, as the suspension flag is set
            return true;
        }
        LOG_DEBUG("MOCK: Task '%s' acknowledged suspension", task_name.c_str());

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

            bool task_not_found = false;
            {
                std::lock_guard<std::timed_mutex> lock(tasksMutex_);
                auto it = tasks_.find(thread);
                if (it != tasks_.end()) {
                    thread_id = it->second.thread_id;
                    task_name = it->second.name;
                    task_info = &(it->second);
                } else {
                    task_not_found = true;
                }
            }
            if (task_not_found) {
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
            task_info->cv.notify_all();
            task_info->notify_cv.notify_all();
        }

        // Notify the task to wake up - notify ALL condition variables
        task_info->cv.notify_all();         // For ShouldStopOrPause
        task_info->notify_cv.notify_all();  // For WaitForNotify - CRITICAL!

        // CRITICAL: Notify all queue condition variables the task is waiting on
        {
            std::lock_guard<std::mutex> lock(task_info->mutex);
            for (auto* queue_cv : task_info->waiting_on_queue_cvs) {
                if (queue_cv) {
                    queue_cv->notify_all();  // For ReceiveFromQueue calls
                }
            }
        }

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
                                   task_info->stop_requested.load(
                                       std::memory_order_relaxed);
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
        // Sample stack pointer for the calling task before any other work.
        // Frame address taken in this function — close enough to the user
        // task's deepest frame (RTOS call only adds a few bytes).
        SampleStackUsage(
            GetThreadLocalTaskInfo(),
            reinterpret_cast<uintptr_t>(__builtin_frame_address(0)));

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
                    should_stop = task_info->stop_requested.load(
                        std::memory_order_relaxed);
                    is_suspended = task_info->suspended;
                    break;
                }
            }
        }

        if (!task_info) {
            // Task not in map: erased by DeleteTask (detach path). Stop the task.
            return true;
        }

        // If stop requested, return immediately
        if (should_stop) {
            // LOG_DEBUG("MOCK: Task '%s' should stop (quick check)",
            //   task_name.c_str());
            return true;
        }

        if (!is_suspended) {
            AcknowledgeResume(*task_info);
        }

        // If task is suspended, wait until resumed or stop requested
        if (is_suspended) {
            // LOG_DEBUG(
            //     "MOCK: Task '%s' is suspended, waiting for resume or stop",
            //     task_name.c_str());

            std::unique_lock<std::mutex> lock(task_info->mutex);

            // Re-check under task_info->mutex to close the missed-notification
            // window: DeleteTask may have set stop_requested=true after we
            // released tasksMutex_ and before we acquired task_info->mutex.
            if (task_info->stop_requested.load(std::memory_order_acquire)) {
                return true;
            }
            if (!task_info->suspended) {
                // Already resumed between the two locks.
                task_info->resume_acknowledged = true;
                task_info->resume_ack_cv.notify_all();
                return false;
            }

            // Before waiting, acknowledge that we're now suspended
            task_info->suspension_acknowledged = true;
            task_info->suspend_ack_cv.notify_all();

            // Wait with timeout to prevent indefinite blocking
            // Use our waitFor helper that respects virtual time
            bool status = waitFor(
                task_info->cv, lock, 500000 /* 500s timeout */,
                [task_info]() {
                    return !task_info->suspended ||
                           task_info->stop_requested.load(
                               std::memory_order_relaxed);
                },
                WaitKind::kPark);

            if (!status) {
                // LOG_DEBUG("MOCK: Task '%s' wait timeout, rechecking condition",
                //   task_name.c_str());
            } else {
                // LOG_DEBUG("MOCK: Task '%s' condition changed",
                //   task_name.c_str());
            }

            // After wait, recheck stop flag
            if (task_info->stop_requested.load(std::memory_order_relaxed)) {
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
        bool
            isSystemQueue;  // True for system semaphores that bypass virtual time
        std::atomic<uint32_t> size{
            0};  // mirrors data.size(); readable without the queue mutex
    };

    QueueHandle_t CreateQueue(uint32_t length, uint32_t itemSize) override {
        auto* q = new QueueData{
            .mutex = {},     // Default initialize mutex
            .notEmpty = {},  // Default initialize condition variable
            .notFull = {},   // Default initialize condition variable
            .data = {},      // Default initialize queue
            .maxSize = length,
            .itemSize = itemSize,
            .isSystemQueue = false};  // Regular queues respect virtual time
        return q;
    }

    void DeleteQueue(QueueHandle_t queue) override {
        LOG_DEBUG("MOCK: Deleting queue");
        auto* q = static_cast<QueueData*>(queue);
        {
            std::lock_guard<std::mutex> lock(q->mutex);
            int leftover = static_cast<int>(q->data.size());
            if (leftover > 0)
                pending_queue_items_.fetch_sub(leftover,
                                               std::memory_order_relaxed);
        }
        delete q;
    }

    QueueResult SendToQueue(QueueHandle_t queue, const void* item,
                            uint32_t timeout) override {
        auto* q = static_cast<QueueData*>(queue);
        std::unique_lock<std::mutex> lock(q->mutex);

        if (q->data.size() >= q->maxSize) {
            if (timeout == 0) {
                std::cout << "MOCK: Queue is full, cannot send item"
                          << std::endl;

                return QueueResult::kFull;
            }

            // Use our waitFor helper that respects virtual time
            bool success = waitFor(
                q->notFull, lock, timeout,
                [q]() { return q->data.size() < q->maxSize; },
                WaitKind::kQueue);

            if (!success) {
                std::cout << "MOCK: Queue send timeout" << std::endl;
                return QueueResult::kTimeout;
            }
        }

        auto* bytes = static_cast<const uint8_t*>(item);
        q->data.push(std::vector<uint8_t>(bytes, bytes + q->itemSize));
        q->size.fetch_add(1, std::memory_order_release);
        pending_queue_items_.fetch_add(1, std::memory_order_relaxed);
        q->notEmpty.notify_one();
        return QueueResult::kOk;
    }

    QueueResult SendToQueueISR(QueueHandle_t queue, const void* item) override {
        return SendToQueue(queue, item, 0);
    }

    QueueResult ReceiveFromQueue(QueueHandle_t queue, void* buffer,
                                 uint32_t timeout) override {
        if (!queue || !buffer) {
            return QueueResult::kError;
        }

        // Use thread-local cache set in CreateTask — no mutex needed.
        // nullptr for non-task threads (e.g., the test thread).
        TaskInfo* task_info = GetThreadLocalTaskInfo();
        bool is_registered_task = (task_info != nullptr);

        auto* q = static_cast<QueueData*>(queue);
        std::unique_lock<std::mutex> lock(q->mutex);  // M6

        if (q->data.empty()) {
            if (timeout == 0) {
                return QueueResult::kEmpty;
            }

            if (is_registered_task) {
                if (task_info->stop_requested.load(std::memory_order_acquire)) {
                    return QueueResult::kError;
                }

                // Capture suspension state before registering the queue CV.
                // If already suspended, return immediately so the task loop calls
                // ShouldStopOrPause() to acknowledge — avoids virtual-time deadlock.
                bool initial_suspended_state =
                    task_info->suspended.load(std::memory_order_acquire);
                if (initial_suspended_state) {
                    return QueueResult::kTimeout;
                }

                // Register that this task is waiting on this queue's condition variable.
                // Uses task_info overload to avoid acquiring M0 while M6 is held.
                registerQueueCV(&q->notEmpty, task_info);
                task_info->waiting_queue_.store(q, std::memory_order_release);

                bool success = waitFor(
                    q->notEmpty, lock, timeout,
                    [q, task_info, initial_suspended_state]() {
                        if (!q->data.empty()) {
                            return true;
                        }
                        return task_info->stop_requested.load(
                                   std::memory_order_acquire) ||
                               (task_info->suspended.load(
                                    std::memory_order_acquire) !=
                                initial_suspended_state);
                    },
                    WaitKind::kQueue);

                task_info->waiting_queue_.store(nullptr,
                                                std::memory_order_release);
                unregisterQueueCV(&q->notEmpty, task_info);

                if (task_info->stop_requested.load(std::memory_order_acquire)) {
                    return QueueResult::kError;
                }

                if (!success) {
                    return QueueResult::kTimeout;
                }
            } else {
                // Non-task thread (e.g., test thread) - use simple timeout wait
                bool success = waitFor(
                    q->notEmpty, lock, timeout,
                    [q]() { return !q->data.empty(); }, WaitKind::kQueue);

                if (!success) {
                    return QueueResult::kTimeout;
                }
            }
        }

        if (is_registered_task &&
            task_info->stop_requested.load(std::memory_order_acquire)) {
            return QueueResult::kError;
        }

        if (q->data.empty()) {
            return QueueResult::kEmpty;
        }

        auto& item = q->data.front();
        memcpy(buffer, item.data(), q->itemSize);
        q->data.pop();
        q->size.fetch_sub(1, std::memory_order_release);
        pending_queue_items_.fetch_sub(1, std::memory_order_relaxed);
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
        // Find the current task's TaskInfo (needed for both real-time and virtual time modes)
        std::thread::id current_id = std::this_thread::get_id();
        TaskInfo* task_info = nullptr;

        {
            std::lock_guard<std::timed_mutex> lock(tasksMutex_);
            for (auto& [thread_ptr, info] : tasks_) {
                if (info.thread_id == current_id) {
                    task_info = &info;
                    break;
                }
            }
        }

        if (!task_info) {
            LOG_WARNING(
                "MOCK: Could not find TaskInfo for current thread in delay()");
            // Fallback to non-interruptible sleep
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            return;
        }

        // Check if stop or delay interruption was already requested
        {
            std::lock_guard<std::mutex> lock(task_info->mutex);
            if (task_info->stop_requested.load(std::memory_order_relaxed) ||
                task_info->delay_interrupted.load(std::memory_order_acquire)) {
                throw TaskTerminationException();  // Terminate task execution
            }
        }

        if (timeMode_ == TimeMode::kRealTime) {
            // Real-time mode: Use interruptible wait with condition variable
            std::unique_lock<std::mutex> lock(task_info->mutex);
            task_info->delay_cv.wait_for(
                lock, std::chrono::milliseconds(ms), [task_info]() {
                    return task_info->stop_requested.load(
                               std::memory_order_relaxed) ||
                           task_info->delay_interrupted.load(
                               std::memory_order_acquire);
                });

            // After wait returns, check if we were interrupted
            if (task_info->stop_requested.load(std::memory_order_relaxed) ||
                task_info->delay_interrupted.load(std::memory_order_acquire)) {
                throw TaskTerminationException();
            }
            return;
        }

        // Virtual time mode continues below
        uint64_t wakeTimeMs;

        // Register this task as waiting until the wake time
        wakeTimeMs = virtualTimeMsAtomic_.load(std::memory_order_acquire) + ms;
        uint64_t wait_id =
            registerWait(&task_info->delay_cv, &task_info->mutex, wakeTimeMs,
                         WaitKind::kDelay, task_info);

        // Wait until either:
        // 1. The virtual time advances beyond our wake time
        // 2. We are explicitly woken up by advanceTime
        // 3. Stop or delay interruption is requested
        {
            std::unique_lock<std::mutex> lock(task_info->mutex);
            task_info->delay_cv.wait(lock, [this, wakeTimeMs, task_info]() {
                // Check stop/interruption flags first
                if (task_info->stop_requested.load(std::memory_order_relaxed) ||
                    task_info->delay_interrupted.load(
                        std::memory_order_acquire)) {
                    return true;
                }

                // Check if virtual time has advanced enough using atomic mirror
                return virtualTimeMsAtomic_.load(std::memory_order_acquire) >=
                       wakeTimeMs;
            });

            // After wait returns, check if we were woken due to termination request
            if (task_info->stop_requested.load(std::memory_order_relaxed) ||
                task_info->delay_interrupted.load(std::memory_order_acquire)) {
                // Release M1 before acquiring M2 (timeMutex_) to maintain lock order.
                lock.unlock();
                unregisterWait(wait_id);
                throw TaskTerminationException();  // Terminate task execution
            }
        }

        // Clean up (in case we were woken by something other than advanceTime)
        unregisterWait(wait_id);
    }

    void LightSleep(uint32_t ms) override { delay(ms); }

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
            return static_cast<uint32_t>(
                virtualTimeMsAtomic_.load(std::memory_order_acquire));
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
            current_id = std::this_thread::get_id();
        } else {
            auto* thread = static_cast<std::thread*>(taskHandle);
            current_id = thread->get_id();
        }

        for (const auto& [thread, info] : tasks_) {
            if (thread->get_id() == current_id) {
                return ComputeFreeStackBytes(info);
            }
        }

        return 0;
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
            stats.push_back(
                TaskStats{.name = info.name,
                          .state = info.suspended ? TaskState::kSuspended
                                                  : TaskState::kRunning,
                          .stackWatermark = ComputeFreeStackBytes(info),
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
                    SetNotificationPending(tasks_[thread]);
                    return;
                }
            }
        } else {
            auto* thread = static_cast<std::thread*>(task_handle);

            if (auto it = tasks_.find(thread); it != tasks_.end()) {
                SetNotificationPending(it->second);
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
                    SetNotificationPending(tasks_[thread]);
                    return QueueResult::kOk;
                }
            }
        } else {
            auto* thread = static_cast<std::thread*>(task_handle);

            if (auto it = tasks_.find(thread); it != tasks_.end()) {
                SetNotificationPending(it->second);
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
                    // LOG_WARNING(
                    //     "MOCK: Could not find task handle for current thread "
                    //     "in WaitForNotify");
                    return QueueResult::kError;
                }
            } else {
                // We have a cached task handle, verify it still exists
                auto it = tasks_.find(static_cast<std::thread*>(this_task));
                if (it == tasks_.end()) {
                    this_task = nullptr;  // Reset the cache
                    // LOG_WARNING(
                    //     "MOCK: Cached task handle invalid in WaitForNotify");
                    return QueueResult::kError;
                }
                task_info = &(it->second);
            }

            // Quick check for stop request or pending notification without waiting
            if (task_info->stop_requested.load(std::memory_order_relaxed)) {
                // std::cout << "MOCK: Task received stop request during WaitForNotify" << std::endl;
                return QueueResult::kError;
            }

            // IMPORTANT: Check for suspension before checking notification
            if (task_info->suspended) {
                // std::cout << "MOCK: Task is suspended during WaitForNotify, will wait for resume" << std::endl;
                // Don't return error here - we should wait for resume or stop
            }

            if (!task_info->suspended.load(std::memory_order_acquire)) {
                AcknowledgeResume(*task_info);
            }

            // If we already have a pending notification and not suspended, consume it
            if (task_info->notification_pending.load(
                    std::memory_order_acquire) &&
                !task_info->suspended.load(std::memory_order_acquire)) {
                task_info->notification_pending.store(
                    false, std::memory_order_relaxed);
                // std::cout << "MOCK: Consumed pending notification immediately" << std::endl;
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
            bool initial_suspended_state =
                task_info->suspended.load(std::memory_order_acquire);

            // If already suspended on entry, acknowledge now. SuspendTask may have
            // fired notify_cv before cv.wait_until is registered, so the notification
            // is lost; without this, SuspendTask waits indefinitely for
            // suspension_acknowledged that never gets set.
            if (initial_suspended_state) {
                task_info->suspension_acknowledged.store(
                    true, std::memory_order_release);
                task_info->suspend_ack_cv.notify_all();
            }

            // Capture task_info pointer to avoid acquiring tasksMutex_ (M0) inside
            // the predicate while task_info->mutex (M1) is held by waitFor's
            // cv.wait_until, which would create a M1 → M0 lock-order inversion.
            // task_info is stable: this thread cannot be deleted while executing here.
            auto wait_predicate = [task_info, initial_suspended_state]() {
                if (task_info->stop_requested.load(std::memory_order_acquire))
                    return true;
                bool currently_suspended =
                    task_info->suspended.load(std::memory_order_acquire);
                // If we were just resumed, acknowledge it using CAS to avoid races.
                if (initial_suspended_state && !currently_suspended) {
                    bool expected = false;
                    if (task_info->resume_acknowledged.compare_exchange_strong(
                            expected, true, std::memory_order_acq_rel)) {
                        task_info->resume_ack_cv.notify_all();
                    }
                }
                // Return true if:
                // 1. Not suspended AND have pending notification (normal notification)
                // 2. Suspension state changed (got suspended or resumed)
                return (!currently_suspended &&
                        task_info->notification_pending.load(
                            std::memory_order_acquire)) ||
                       (currently_suspended != initial_suspended_state);
            };

            if (timeout == MAX_DELAY) {
                // Use our waitFor helper with a very long timeout
                waitFor(task_info->notify_cv, lock,
                        3600 * 1000,  // 1 hour timeout as MAX_DELAY equivalent
                        wait_predicate, WaitKind::kNotify);
            } else {
                // Regular timeout case
                waitFor(task_info->notify_cv, lock, timeout, wait_predicate,
                        WaitKind::kNotify);
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

            if (it->second.stop_requested.load(std::memory_order_relaxed)) {
                // std::cout << "MOCK: Task received stop request after wait in WaitForNotify" << std::endl;
                return QueueResult::kError;
            }

            // Check what caused us to wake up
            if (it->second.suspended) {
                // std::cout << "MOCK: WaitForNotify woke up due to suspension" << std::endl;

                // IMPORTANT: Acknowledge the suspension here since we detected it
                // This prevents SuspendTask from waiting indefinitely
                if (!it->second.suspension_acknowledged) {
                    it->second.suspension_acknowledged = true;
                    it->second.suspend_ack_cv.notify_all();
                    // std::cout << "MOCK: WaitForNotify acknowledged suspension" << std::endl;
                }

                // Don't consume any pending notification while suspended
                return QueueResult::
                    kTimeout;  // Return timeout to indicate we should check suspension state
            }

            if (it->second.notification_pending.load(
                    std::memory_order_acquire)) {
                // Notification received and we're not suspended, consume it
                it->second.notification_pending.store(
                    false, std::memory_order_relaxed);
                // std::cout << "MOCK: Notification received after wait" << std::endl;
                return QueueResult::kOk;
            } else {
                // Timeout occurred
                // std::cout << "MOCK: Notification wait timeout" << std::endl;
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

        std::cout << "MOCK: Creating binary semaphore" << std::endl;

        // Create a queue with length 1 and item size 0 (we don't need to store data)
        QueueHandle_t queue = CreateQueue(1, sizeof(uint8_t));

        // Binary semaphores in FreeRTOS start in the "unavailable" (taken) state
        // The queue starts empty, which correctly represents this state
        // No initialization needed - matches xSemaphoreCreateBinary() behavior

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
     * If the queue is empty (semaphore unavailable), this will block until
     * an item becomes available or the timeout expires.
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
     * This makes the semaphore available for another thread to take.
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
        QueueResult result = SendToQueue(semaphore, &dummy, 0);
        return result == QueueResult::kOk;
    }

    /**
     * @brief Creates a system-level binary semaphore using wall-clock time.
     *
     * System semaphores explicitly bypass virtual-time mode and always operate
     * on real (wall-clock) time. This is required for infrastructure components
     * such as logging and diagnostics, which must remain functional regardless
     * of test-time control or time virtualization.
     *
     * The returned semaphore has process lifetime and is intentionally never
     * destroyed. Destroying a std::timed_mutex during static shutdown or while
     * it may still be locked results in undefined behavior. The operating system
     * will reclaim the resources when the process exits.
     *
     * LeakSanitizer is explicitly instructed to ignore this allocation to avoid
     * reporting a false-positive leak for this intentional process-lifetime object.
     *
     * @return Handle to the created system semaphore
     */
    SemaphoreHandle_t CreateSystemSemaphore() override {
        std::timed_mutex* system_mutex = new std::timed_mutex();
        __lsan_ignore_object(system_mutex);
        return (SemaphoreHandle_t)system_mutex;
    }

    /**
     * @brief Takes a system semaphore (always uses real-time)
     *
     * This method bypasses virtual time mode and always uses wall-clock timing.
     * It's specifically for system semaphores created with CreateSystemSemaphore().
     *
     * @param semaphore Handle to the system semaphore
     * @param timeout Maximum time to wait in milliseconds
     * @return true if semaphore was acquired, false on timeout
     */
    bool TakeSystemSemaphore(SemaphoreHandle_t semaphore,
                             uint32_t timeout) override {
        if (!semaphore) {
            return false;
        }

        auto* mutex = static_cast<std::timed_mutex*>(semaphore);

        if (timeout == 0) {
            // No wait - try immediate lock
            return mutex->try_lock();
        } else if (timeout == MAX_DELAY) {
            // Wait forever
            mutex->lock();
            return true;
        } else {
            // Timed wait
            return mutex->try_lock_for(std::chrono::milliseconds(timeout));
        }
    }

    /**
     * @brief Gives a system semaphore
     *
     * @param semaphore Handle to the system semaphore
     * @return true if successful, false otherwise
     */
    bool GiveSystemSemaphore(SemaphoreHandle_t semaphore) override {
        if (!semaphore) {
            return false;
        }

        auto* mutex = static_cast<std::timed_mutex*>(semaphore);
        mutex->unlock();
        return true;
    }

    /**
     * @brief Deletes a system semaphore
     * @param semaphore Handle to the system semaphore to delete
     */
    void DeleteSystemSemaphore(SemaphoreHandle_t semaphore) override {
        if (!semaphore) {
            return;
        }

        auto* mutex = static_cast<std::timed_mutex*>(semaphore);

        // Safety check: Only delete if mutex is unlocked
        // This prevents SIGABRT from deleting a locked mutex
        if (mutex->try_lock()) {
            // Mutex was unlocked, safe to delete
            mutex->unlock();
            delete mutex;
        } else {
            // Mutex is locked - skip deletion to avoid crash
            // This is acceptable during program shutdown
            std::cout << "MOCK: System semaphore is locked, skipping deletion "
                         "to prevent SIGABRT"
                      << std::endl;
        }
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
        uint32_t timerId;
        uint64_t expiryTime;
        {
            std::lock_guard<std::mutex> lock(timeMutex_);

            TimerCallback timer;
            timer.callback = callback;
            timer.expiryTime = virtualTimeMs_ + delayMs;
            timer.period = periodMs;
            timer.active = true;

            timerId = static_cast<uint32_t>(timerCallbacks_.size());
            expiryTime = timer.expiryTime;
            timerCallbacks_.push_back(timer);
        }  // timeMutex_ released

        LOG_DEBUG("MOCK: Created timer %u, expires at %llu ms, period %u ms",
                  timerId, (unsigned long long)expiryTime, periodMs);

        return timerId;
    }

    /**
     * @brief Stops a previously created timer
     * 
     * @param timerId ID of the timer to stop
     * @return true if timer was found and stopped, false otherwise
     */
    bool stopTimer(uint32_t timerId) {
        bool invalid = false;
        {
            std::lock_guard<std::mutex> lock(timeMutex_);

            if (timerId >= timerCallbacks_.size()) {
                invalid = true;
            } else {
                timerCallbacks_[timerId].active = false;
            }
        }  // timeMutex_ released

        if (invalid) {
            LOG_WARNING("MOCK: Invalid timer ID %u", timerId);
            return false;
        }
        LOG_DEBUG("MOCK: Stopped timer %u", timerId);
        return true;
    }

    /**
     * @brief Kind of a registered virtual-time wait
     *
     * Delay, notification and queue waits mean the task is idle until an
     * event arrives. A park wait is idle only while the task is suspended.
     * Other waits (acknowledgement handshakes) are transient: the task is
     * still considered running.
     */
    enum class WaitKind : uint8_t { kDelay, kNotify, kQueue, kPark, kOther };

    /**
     * @brief Wait on a condition variable, honouring virtual time
     *
     * @param cv The condition variable to wait on
     * @param lock The unique lock protecting the condition variable
     * @param relTimeMs The relative time to wait in milliseconds
     * @param pred The predicate function that determines when to stop waiting
     * @param kind Kind of wait, used to decide whether the task is idle
     * @return true if predicate became true, false if timeout occurred
     */
    template <typename Predicate>
    bool waitFor(std::condition_variable& cv,
                 std::unique_lock<std::mutex>& lock, uint32_t relTimeMs,
                 Predicate pred, WaitKind kind = WaitKind::kOther) {
        if (timeMode_ == TimeMode::kRealTime) {
            // In real-time mode, use standard wait_for
            return cv.wait_for(lock, std::chrono::milliseconds(relTimeMs),
                               pred);
        }

        // Fast path: check predicate before waiting
        if (pred()) {
            return true;
        }

        // Register the wait with its virtual deadline. The virtual clock does
        // not move while this thread runs, so reading the atomic mirror here
        // yields the current instant.
        const uint64_t wakeTimeMs =
            virtualTimeMsAtomic_.load(std::memory_order_acquire) + relTimeMs;
        lock.unlock();
        uint64_t wait_id = registerWait(&cv, lock.mutex(), wakeTimeMs, kind,
                                        GetThreadLocalTaskInfo());
        lock.lock();

        // advanceTime() notifies under the caller's mutex when the deadline
        // is reached, so no wake-up is lost. The short real-time timeout only
        // bounds the latency of notifications sent without that mutex.
        auto combined_pred = [this, wakeTimeMs, &pred]() {
            if (pred())
                return true;
            return virtualTimeMsAtomic_.load(std::memory_order_acquire) >=
                   wakeTimeMs;
        };
        while (!cv.wait_for(lock, std::chrono::milliseconds(kWaitPollMs),
                            combined_pred)) {
            if (timeMode_ != TimeMode::kVirtualTime) {
                break;
            }
        }

        // Unlock the caller's lock before acquiring timeMutex_ to keep the
        // timeMutex_ -> caller-mutex lock order.
        lock.unlock();
        unregisterWait(wait_id);
        lock.lock();

        // Return true only if the predicate is satisfied
        return pred();
    }

    /**
     * @brief Draw a random value from the caller's random stream
     *
     * Every node owns an independent stream, selected by the node address of
     * the calling task and seeded from the SeedRandom() seed and that address.
     * A node's sequence therefore does not depend on how its draws interleave
     * with other nodes' draws. Non-task threads (e.g. the test thread) share
     * one separate stream; a task without a node address uses a stream keyed
     * by its task name.
     *
     * @return Uniformly distributed 32-bit value
     */
    uint32_t GetRandom() override {
        std::string key = RandomStreamKey();
        std::lock_guard<std::mutex> lock(prng_mutex_);
        auto it = prng_streams_.find(key);
        if (it == prng_streams_.end()) {
            std::seed_seq seq{prng_seed_, HashStreamKey(key)};
            it = prng_streams_.emplace(key, std::mt19937(seq)).first;
        }
        return prng_distribution_(it->second);
    }

    /**
     * @brief Seed every random stream for deterministic sequences in tests
     *
     * Restarts all streams: the next draw of each node starts its sequence
     * for @p seed.
     *
     * @param seed The seed value
     */
    void SeedRandom(uint32_t seed) {
        std::lock_guard<std::mutex> lock(prng_mutex_);
        prng_seed_ = seed;
        prng_streams_.clear();
    }

    /**
     * @brief Set the node address for the current task
     * @param address The node address (e.g., "0x1001"), max 7 chars
     */
    void SetCurrentTaskNodeAddress(const char* address) override {
        // Update thread-local cache first for immediate availability
        setThreadLocalNodeAddress(address);

        std::lock_guard<std::timed_mutex> lock(tasksMutex_);

        // Find the current task and update TaskInfo
        std::thread::id current_id = std::this_thread::get_id();
        for (auto& [thread_ptr, task_info] : tasks_) {
            if (task_info.thread_id == current_id) {
                snprintf(task_info.node_address, sizeof(task_info.node_address),
                         "%s", address);
                break;
            }
        }
    }

    /**
     * @brief Get the node address for the current task
     * @return The node address, or "" if not set
     */
    const char* GetCurrentTaskNodeAddress() const override {
        // First try thread-local cache for best performance
        const char* cached = getThreadLocalNodeAddress();
        if (cached[0] != '\0') {
            return cached;
        }

        // Fallback to mutex-protected lookup with a short timeout
        std::unique_lock<std::timed_mutex> lock(tasksMutex_, std::try_to_lock);

        if (!lock.owns_lock()) {
            if (!lock.try_lock_for(std::chrono::milliseconds(5))) {
                return "";
            }
        }

        // Find the current task and update thread-local cache
        std::thread::id current_id = std::this_thread::get_id();
        for (const auto& [thread_ptr, task_info] : tasks_) {
            if (task_info.thread_id == current_id) {
                if (task_info.node_address[0] != '\0') {
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
    static thread_local char thread_local_node_address_[8];

    /**
     * @brief Sample the current stack pointer against the calling task's
     * recorded base, update peak_stack_used, and abort if the peak has
     * reached or exceeded the configured stack size.
     *
     * Called from hot RTOS paths (ShouldStopOrPause, etc.). Cheap when no
     * task is registered or budget hasn't been exceeded; aborts the test
     * loudly when the caller's stack usage crosses the configured budget,
     * surfacing on native what would be a hard FreeRTOS overflow on ESP32.
     */
    /**
     * @brief Compute free stack bytes from peak observed usage. Returns 0 when
     * no sample has been taken yet (task hasn't called into the RTOS) or when
     * usage has met/exceeded the configured budget.
     */
    static inline uint32_t ComputeFreeStackBytes(const TaskInfo& info) {
        if (info.stack_size == 0) {
            return 0;
        }
        uint32_t used = info.peak_stack_used.load(std::memory_order_relaxed);
        if (used == 0) {
            return info.stack_size;
        }
        return used >= info.stack_size ? 0u : info.stack_size - used;
    }

    static inline void SampleStackUsage(TaskInfo* info,
                                        uintptr_t current_frame) {
        if (!info) {
            return;
        }
        uintptr_t base = info->stack_base_addr.load(std::memory_order_relaxed);
        if (base == 0 || current_frame >= base) {
            return;
        }
        uint32_t used = static_cast<uint32_t>(base - current_frame);
        uint32_t prev = info->peak_stack_used.load(std::memory_order_relaxed);
        while (used > prev && !info->peak_stack_used.compare_exchange_weak(
                                  prev, used, std::memory_order_relaxed)) {}
        if (info->stack_size > 0 && used >= info->stack_size) {
            std::fprintf(stderr,
                         "\n*** MOCK STACK OVERFLOW: task '%s' used %u "
                         "bytes, budget=%u ***\n",
                         info->name.c_str(), used, info->stack_size);
            std::abort();
        }
    }

    /**
     * @brief Key of the random stream used by the calling thread
     */
    static std::string RandomStreamKey() {
        const TaskInfo* info = GetThreadLocalTaskInfo();
        if (info == nullptr) {
            return "<host>";
        }
        const char* address = getThreadLocalNodeAddress();
        if (address[0] != '\0') {
            return std::string("node:") + address;
        }
        return "task:" + info->name;
    }

    /**
     * @brief FNV-1a hash of a random stream key
     */
    static uint32_t HashStreamKey(const std::string& key) {
        uint32_t hash = 2166136261u;
        for (char c : key) {
            hash ^= static_cast<uint8_t>(c);
            hash *= 16777619u;
        }
        return hash;
    }

    /**
     * @brief Returns a reference to this thread's cached TaskInfo pointer.
     * Set once in CreateTask before the task function runs, cleared on exit.
     * Nullptr for non-task threads (e.g., test thread).
     * Eliminates per-call M0 (tasksMutex_) acquisition in ReceiveFromQueue.
     */
    static TaskInfo*& GetThreadLocalTaskInfo() {
        thread_local TaskInfo* info = nullptr;
        return info;
    }

    /**
     * @brief Set the thread-local node address cache
     * @param address The node address to cache
     */
    static void setThreadLocalNodeAddress(const char* address) {
        snprintf(thread_local_node_address_, sizeof(thread_local_node_address_),
                 "%s", address);
    }

    /**
     * @brief Get the thread-local node address cache
     * @return The cached node address, or "" if not cached
     */
    static const char* getThreadLocalNodeAddress() {
        return thread_local_node_address_;
    }

    /**
     * @brief Find the current task's TaskInfo by thread ID (lock-free version)
     * @param current_id The thread ID to search for
     * @param tasks_lock_held Whether tasksMutex_ is already held by caller
     * @return Pointer to TaskInfo or nullptr if not found
     */
    TaskInfo* findCurrentTaskInfoUnsafe(std::thread::id current_id) {
        for (auto& [thread_ptr, task_info] : tasks_) {
            if (task_info.thread_id == current_id) {
                return &task_info;
            }
        }
        return nullptr;
    }

    /**
     * @brief Register a queue condition variable that the current task is waiting on
     * @param cv Pointer to the condition variable
     * @return true if successfully registered, false otherwise
     */
    bool registerQueueCV(std::condition_variable* cv) {
        if (!cv)
            return false;

        std::thread::id current_id = std::this_thread::get_id();

        TaskInfo* task_info = nullptr;
        {
            // Acquire tasksMutex_ only to find the task_info pointer
            std::lock_guard<std::timed_mutex> lock(tasksMutex_);
            task_info = findCurrentTaskInfoUnsafe(current_id);
            if (!task_info)
                return false;
        }  // tasksMutex_ released before acquiring task_info->mutex (breaks lock-order cycle)

        {
            std::lock_guard<std::mutex> task_lock(task_info->mutex);
            task_info->waiting_on_queue_cvs.push_back(cv);
            task_info->queue_cv_count_.fetch_add(1, std::memory_order_relaxed);
        }  // task_info->mutex released

        tasks_blocked_cv_.notify_all();
        return true;
    }

    /**
     * @brief Unregister a queue condition variable that the current task was waiting on
     * @param cv Pointer to the condition variable
     * @return true if successfully unregistered, false otherwise
     */
    bool unregisterQueueCV(std::condition_variable* cv) {
        if (!cv)
            return false;

        std::thread::id current_id = std::this_thread::get_id();

        TaskInfo* task_info = nullptr;
        {
            // Acquire tasksMutex_ only to find the task_info pointer
            std::lock_guard<std::timed_mutex> lock(tasksMutex_);
            task_info = findCurrentTaskInfoUnsafe(current_id);
            if (!task_info)
                return false;
        }  // tasksMutex_ released before acquiring task_info->mutex (breaks lock-order cycle)

        bool found = false;
        {
            std::lock_guard<std::mutex> task_lock(task_info->mutex);
            auto& cvs = task_info->waiting_on_queue_cvs;
            auto it = std::find(cvs.begin(), cvs.end(), cv);
            if (it != cvs.end()) {
                cvs.erase(it);
                task_info->queue_cv_count_.fetch_sub(1,
                                                     std::memory_order_relaxed);
                found = true;
            }
        }
        if (found)
            tasks_blocked_cv_.notify_all();
        return found;
    }

    /**
     * @brief Register a queue CV with a pre-fetched task_info — avoids M0 acquisition.
     * Call this overload when task_info is already known and q->mutex (M6) is held,
     * to prevent a M6→M0 lock-order inversion.
     */
    bool registerQueueCV(std::condition_variable* cv, TaskInfo* task_info) {
        if (!cv || !task_info)
            return false;
        {
            std::lock_guard<std::mutex> task_lock(
                task_info->mutex);  // M1 only, no M0
            task_info->waiting_on_queue_cvs.push_back(cv);
            task_info->queue_cv_count_.fetch_add(1, std::memory_order_relaxed);
        }
        tasks_blocked_cv_.notify_all();
        return true;
    }

    /**
     * @brief Unregister a queue CV with a pre-fetched task_info — avoids M0 acquisition.
     */
    bool unregisterQueueCV(std::condition_variable* cv, TaskInfo* task_info) {
        if (!cv || !task_info)
            return false;
        bool found = false;
        {
            std::lock_guard<std::mutex> task_lock(
                task_info->mutex);  // M1 only, no M0
            auto& cvs = task_info->waiting_on_queue_cvs;
            auto it = std::find(cvs.begin(), cvs.end(), cv);
            if (it != cvs.end()) {
                cvs.erase(it);
                task_info->queue_cv_count_.fetch_sub(1,
                                                     std::memory_order_relaxed);
                found = true;
            }
        }
        if (found)
            tasks_blocked_cv_.notify_all();
        return found;
    }

    /**
     * @brief Check if the current task has been marked for deletion
     * @return true if task should stop, false otherwise
     */
    bool isCurrentTaskDeleted() {
        std::thread::id current_id = std::this_thread::get_id();

        // Acquire lock before accessing tasks_ map to prevent use-after-free
        std::lock_guard<std::timed_mutex> lock(tasksMutex_);

        TaskInfo* task_info = findCurrentTaskInfoUnsafe(current_id);
        if (!task_info) {
            // Task not in map: it was already erased by DeleteTask (detach path).
            // Treat as deleted so the calling task self-terminates.
            return true;
        }

        // Use atomic load with acquire ordering to ensure we see the latest value
        return task_info->stop_requested.load(std::memory_order_acquire);
    }

    static uint32_t WaitingQueueSize(const TaskInfo& task_info) {
        const QueueData* q =
            task_info.waiting_queue_.load(std::memory_order_acquire);
        return q != nullptr ? q->size.load(std::memory_order_acquire) : 0;
    }

    /**
     * @brief A registered virtual-time wait
     */
    struct Waiter {
        std::condition_variable* cv;
        std::mutex*
            mutex;  ///< Mutex the waiter holds while checking its predicate
        uint64_t deadline;  ///< Virtual wake time in milliseconds
        WaitKind kind;
        TaskInfo* owner;  ///< nullptr for non-task threads
    };

    /**
     * @brief Register a virtual-time wait and return its id
     */
    uint64_t registerWait(std::condition_variable* cv, std::mutex* mutex,
                          uint64_t deadline, WaitKind kind, TaskInfo* owner) {
        uint64_t id;
        {
            std::lock_guard<std::mutex> lock(timeMutex_);
            id = ++next_wait_id_;
            waiters_.emplace(id, Waiter{cv, mutex, deadline, kind, owner});
        }
        NotifyReblockWaiter();
        return id;
    }

    /**
     * @brief Remove a registered wait (no-op if advanceTime already woke it)
     */
    void unregisterWait(uint64_t id) {
        std::lock_guard<std::mutex> lock(timeMutex_);
        waiters_.erase(id);
    }

    /**
     * @brief Wake whoever waits in waitForTasksToReblock() to re-evaluate
     */
    void NotifyReblockWaiter() {
        { std::lock_guard<std::mutex> lock(tasks_blocked_mutex_); }
        tasks_blocked_cv_.notify_all();
    }

    /**
     * @brief Set a task's notification flag and wake it
     *
     * The flag is set under the task mutex that WaitForNotify() holds while
     * checking it, so the wake-up cannot be lost.
     */
    static void SetNotificationPending(TaskInfo& info) {
        std::lock_guard<std::mutex> lock(info.mutex);
        info.notification_pending.store(true, std::memory_order_release);
        info.notify_cv.notify_all();
    }

    /**
     * @brief Acknowledge a pending ResumeTask() once the task observes that it
     * is no longer suspended, wherever it observes it
     */
    static void AcknowledgeResume(TaskInfo& info) {
        if (info.resume_acknowledged.load(std::memory_order_acquire)) {
            return;
        }
        std::lock_guard<std::mutex> lock(info.mutex);
        if (!info.suspended.load(std::memory_order_acquire)) {
            info.resume_acknowledged.store(true, std::memory_order_release);
            info.resume_ack_cv.notify_all();
        }
    }

    /**
     * @brief Set the virtual clock; caller holds timeMutex_
     */
    void SetVirtualTimeLocked(uint64_t time_ms) {
        virtualTimeMs_ = time_ms;
        virtualTimeMsAtomic_.store(time_ms, std::memory_order_release);
    }

    /**
     * @brief Earliest registered wait, ordered by (deadline, task key, id);
     * caller holds timeMutex_
     */
    std::map<uint64_t, Waiter>::iterator FindEarliestWaiterLocked() {
        auto best = waiters_.end();
        for (auto it = waiters_.begin(); it != waiters_.end(); ++it) {
            if (best == waiters_.end() || WaiterPrecedes(*it, *best)) {
                best = it;
            }
        }
        return best;
    }

    static bool WaiterPrecedes(const std::pair<const uint64_t, Waiter>& a,
                               const std::pair<const uint64_t, Waiter>& b) {
        if (a.second.deadline != b.second.deadline) {
            return a.second.deadline < b.second.deadline;
        }
        // Task waits precede non-task waits; tasks order by their stable key
        if ((a.second.owner == nullptr) != (b.second.owner == nullptr)) {
            return a.second.owner != nullptr;
        }
        if (a.second.owner && b.second.owner &&
            a.second.owner->order_key != b.second.owner->order_key) {
            return a.second.owner->order_key < b.second.owner->order_key;
        }
        return a.first < b.first;
    }

    /**
     * @brief Earliest active timer; caller holds timeMutex_
     */
    std::vector<TimerCallback>::iterator FindEarliestTimerLocked() {
        auto best = timerCallbacks_.end();
        for (auto it = timerCallbacks_.begin(); it != timerCallbacks_.end();
             ++it) {
            if (it->active && (best == timerCallbacks_.end() ||
                               it->expiryTime < best->expiryTime)) {
                best = it;
            }
        }
        return best;
    }

    /**
     * @brief Remove a wait and wake its thread; caller holds timeMutex_
     *
     * Notifies under the waiter's own mutex so the wake-up cannot fall
     * between the waiter's predicate check and its wait.
     */
    void WakeWaiterLocked(std::map<uint64_t, Waiter>::iterator it) {
        Waiter waiter = it->second;
        waiters_.erase(it);
        std::lock_guard<std::mutex> lock(*waiter.mutex);
        waiter.cv->notify_all();
    }

    /**
     * @brief Whether a task is idle: blocked until an external event and with
     * no event already pending for it. Caller holds timeMutex_ and tasksMutex_.
     *
     * @param info The task
     * @param idle_wait Whether the task has a registered delay, notification
     *        or queue wait
     * @param park_wait Whether the task has a registered park wait
     */
    static bool IsTaskIdle(const TaskInfo& info, bool idle_wait,
                           bool park_wait) {
        if (info.stop_requested.load(std::memory_order_acquire) ||
            info.exited.load(std::memory_order_acquire)) {
            return true;
        }
        bool suspended = info.suspended.load(std::memory_order_acquire);
        if (!suspended &&
            info.notification_pending.load(std::memory_order_acquire)) {
            return false;
        }
        if (WaitingQueueSize(info) > 0) {
            return false;
        }
        if (suspended) {
            return park_wait || idle_wait ||
                   info.suspension_acknowledged.load(std::memory_order_acquire);
        }
        return idle_wait;
    }

    /**
     * @brief Name of the first task that is not idle, or empty if every task
     * is idle. Caller holds timeMutex_ and tasksMutex_.
     */
    std::string FindBusyTaskLocked() const {
        std::map<const TaskInfo*, std::pair<bool, bool>> waits;
        for (const auto& [id, waiter] : waiters_) {
            if (waiter.owner == nullptr) {
                continue;
            }
            auto& flags = waits[waiter.owner];
            if (waiter.kind == WaitKind::kPark) {
                flags.second = true;
            } else if (waiter.kind != WaitKind::kOther) {
                flags.first = true;
            }
        }
        for (const auto& [thread_ptr, task_info] : tasks_) {
            auto it = waits.find(&task_info);
            bool idle_wait = it != waits.end() && it->second.first;
            bool park_wait = it != waits.end() && it->second.second;
            if (!IsTaskIdle(task_info, idle_wait, park_wait)) {
                return task_info.name.empty() ? "unnamed" : task_info.name;
            }
        }
        return {};
    }

    void LogReblockTimeout(const std::string& busy_task) {
        LOG_ERROR(
            "MOCK: reblock timeout at virtual time %llu ms: task '%s' did "
            "not block again within %u ms",
            (unsigned long long)virtualTimeMsAtomic_.load(
                std::memory_order_acquire),
            busy_task.c_str(), kReblockTimeoutMs);
    }

   public:
    /**
     * @brief Wait for tasks that were just woken to re-block
     *
     * This ensures deterministic virtual time behavior by waiting for tasks
     * to process their work and re-enter a wait state before allowing
     * further time advancement.
     *
     * @param timeout_ms Maximum time to wait in real milliseconds (default 10ms)
     */
    void waitForTasksToReblock(uint32_t timeout_ms = 100) {
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
        std::string busy_task;
        auto all_idle = [this, &busy_task]() {
            std::lock_guard<std::mutex> time_lock(timeMutex_);
            std::lock_guard<std::timed_mutex> tasks_lock(tasksMutex_);
            busy_task = FindBusyTaskLocked();
            return busy_task.empty();
        };
        std::unique_lock<std::mutex> lock(tasks_blocked_mutex_);
        while (std::chrono::steady_clock::now() < deadline) {
            if (tasks_blocked_cv_.wait_for(lock, std::chrono::milliseconds(1),
                                           all_idle)) {
                return;
            }
        }
        lock.unlock();
        LogReblockTimeout(busy_task);
    }

   private:
    struct TaskInfo {
        std::string name;
        uint32_t stack_size = 0;
        uint32_t priority = 0;
        uint32_t stack_watermark = 0;
        std::thread::id thread_id;

        // For notification support
        std::atomic<bool> notification_pending{false};
        std::condition_variable notify_cv;

        std::mutex mutex;
        std::condition_variable cv;
        std::atomic<bool> suspended{false};
        std::atomic<bool> stop_requested{false};

        // For suspend/resume acknowledgment
        std::atomic<bool> suspension_acknowledged{false};
        std::atomic<bool> resume_acknowledged{false};
        std::condition_variable suspend_ack_cv;
        std::condition_variable resume_ack_cv;

        // For delay interruption support
        std::atomic<bool> delay_interrupted{false};
        std::condition_variable delay_cv;

        // For logging context
        char node_address[8] = {};

        // For tracking queue condition variables the task is waiting on
        std::vector<std::condition_variable*> waiting_on_queue_cvs;
        std::atomic<int> queue_cv_count_{
            0};  // mirrors waiting_on_queue_cvs.size()
        std::atomic<QueueData*> waiting_queue_{
            nullptr};  // queue this task is blocked on in ReceiveFromQueue

        // Stable ordering key for tasks woken at the same virtual instant
        std::string order_key;

        // Set once the task function has returned
        std::atomic<bool> exited{false};

        // For timed-join in DeleteTask()
        std::shared_ptr<std::promise<void>> exit_signal;
        std::shared_future<void> exit_future;

        // Real stack-usage tracking: captured at task entry via
        // __builtin_frame_address(0). Sampling happens inside hot RTOS paths
        // (e.g. ShouldStopOrPause) and updates peak_stack_used. If
        // peak_stack_used >= stack_size, the test aborts with a clear message.
        std::atomic<uintptr_t> stack_base_addr{0};
        std::atomic<uint32_t> peak_stack_used{0};
    };

    TimeMode timeMode_;  ///< Current time mode (real or virtual)
    uint64_t
        virtualTimeMs_;  ///< Virtual time counter in milliseconds (written under timeMutex_)
    std::atomic<uint64_t> virtualTimeMsAtomic_{
        0};  ///< Atomic mirror of virtualTimeMs_ for lock-free reads
    mutable std::mutex
        timeMutex_;  ///< Mutex protecting time-related operations

    std::map<uint64_t, Waiter> waiters_;  ///< Virtual-time waits by id
    uint64_t next_wait_id_ = 0;
    std::vector<TimerCallback> timerCallbacks_;  ///< Timer callbacks

    std::map<std::thread*, TaskInfo> tasks_;
    mutable std::timed_mutex tasksMutex_;
    /// Tasks created per (creator node address, task name) since the last
    /// switch to virtual time; guarded by tasksMutex_
    std::map<std::string, uint32_t> task_name_counts_;
    std::vector<void (*)()> registeredISRs_;
    std::mutex isrMutex_;

    // PRNG for GetRandom()
    uint32_t prng_seed_ = kDefaultRandomSeed;
    std::map<std::string, std::mt19937> prng_streams_;  ///< Streams per key
    std::uniform_int_distribution<uint32_t> prng_distribution_;
    std::mutex prng_mutex_;

    std::atomic<int> pending_queue_items_{0};  ///< items sitting in all queues
    std::condition_variable tasks_blocked_cv_;
    std::mutex tasks_blocked_mutex_;

    /// Real-time poll interval of virtual-time waits
    static constexpr uint32_t kWaitPollMs = 20;
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