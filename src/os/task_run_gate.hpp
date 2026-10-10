/**
 * @file task_run_gate.hpp
 * @brief Cooperative run/park/exit handshake between a task and its owner
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <initializer_list>

#include "os/os_port.hpp"

namespace loramesher {
namespace os {

/**
 * @brief Lets an owner start, park and stop a task only at a point where the
 * task holds no locks
 *
 * The task checks the gate at the top of its loop: it leaves the loop when
 * ShouldExit() is true and calls Park() when no run is requested. The owner
 * never suspends or deletes a running task; it requests a park or an exit and
 * waits until the task reports it. The atomic flags carry the state and the
 * semaphores only wake the waiting side, so a stale token causes at most a
 * spurious wake-up.
 *
 * A task that exits calls SignalExited() and then WaitForDeletion(); its
 * owner deletes it once WaitExited() returns, so the handle stays valid until
 * the owner releases it. The owner must delete the task before destroying the
 * gate.
 *
 * Owner-side calls must be serialized by the owner.
 */
class TaskRunGate {
   public:
    TaskRunGate()
        : run_sem_(GetRTOS().CreateBinarySemaphore()),
          parked_sem_(GetRTOS().CreateBinarySemaphore()),
          exited_sem_(GetRTOS().CreateBinarySemaphore()) {}

    ~TaskRunGate() {
        for (SemaphoreHandle_t semaphore :
             {run_sem_, parked_sem_, exited_sem_}) {
            if (semaphore) {
                GetRTOS().DeleteSemaphore(semaphore);
            }
        }
    }

    TaskRunGate(const TaskRunGate&) = delete;
    TaskRunGate& operator=(const TaskRunGate&) = delete;

    /**
     * @brief Whether every semaphore was created
     */
    bool IsValid() const { return run_sem_ && parked_sem_ && exited_sem_; }

    // Owner side

    /**
     * @brief Let the task leave Park() and run its loop
     */
    void RequestRun() {
        run_requested_.store(true);
        GetRTOS().GiveSemaphore(run_sem_);
    }

    /**
     * @brief Ask the task to park at the top of its next loop iteration
     *
     * The caller wakes the task from whatever it blocks on, then waits with
     * WaitParked().
     */
    void RequestPark() { run_requested_.store(false); }

    /**
     * @brief Wait until the task is parked
     *
     * @param timeout_ms Maximum time to wait in milliseconds
     * @return true if the task is parked and stays parked until RequestRun()
     */
    bool WaitParked(uint32_t timeout_ms) {
        return WaitForFlag(parked_sem_, parked_, timeout_ms);
    }

    /**
     * @brief Whether a run is currently requested
     */
    bool IsRunRequested() const { return run_requested_.load(); }

    /**
     * @brief Ask the task to leave its loop
     */
    void RequestExit() {
        exit_requested_.store(true);
        GetRTOS().GiveSemaphore(run_sem_);
    }

    /**
     * @brief Wait until the task has left its loop
     *
     * @param timeout_ms Maximum time to wait in milliseconds
     * @return true if the task signalled its exit
     */
    bool WaitExited(uint32_t timeout_ms) {
        return WaitForFlag(exited_sem_, exited_, timeout_ms);
    }

    /**
     * @brief Whether the caller runs on the task bound to this gate
     */
    bool IsCurrentTask() const { return CurrentTaskGate() == this; }

    // Task side

    /**
     * @brief Bind the calling task to this gate; call once at task entry
     */
    void BindCurrentTask() { CurrentTaskGate() = this; }

    /**
     * @brief Whether the task must leave its loop
     */
    bool ShouldExit() const {
        return exit_requested_.load() || GetRTOS().ShouldStopOrPause();
    }

    /**
     * @brief Report the task parked and block until a run or an exit is
     * requested
     */
    void Park() {
        auto& rtos = GetRTOS();
        parked_.store(true);
        rtos.GiveSemaphore(parked_sem_);
        while (!ShouldExit()) {
            if (run_requested_.load()) {
                parked_.store(false);
                if (run_requested_.load()) {
                    return;
                }
                // A park request raced the release: stay parked
                parked_.store(true);
                rtos.GiveSemaphore(parked_sem_);
            }
            rtos.TakeSemaphore(run_sem_, kParkPollMs);
        }
    }

    /**
     * @brief Report that the task left its loop
     */
    void SignalExited() {
        exited_.store(true);
        GetRTOS().GiveSemaphore(exited_sem_);
    }

    /**
     * @brief Block until the owner deletes the task
     */
    void WaitForDeletion() {
        auto& rtos = GetRTOS();
        while (!rtos.ShouldStopOrPause()) {
            rtos.TakeSemaphore(run_sem_, MAX_DELAY);
        }
    }

   private:
    /// Upper bound of one blocking wait while parked
    static constexpr uint32_t kParkPollMs = 1000;

    bool WaitForFlag(SemaphoreHandle_t semaphore, const std::atomic<bool>& flag,
                     uint32_t timeout_ms) {
        auto& rtos = GetRTOS();
        const uint32_t start = rtos.getTickCount();
        while (!flag.load()) {
            uint32_t elapsed = rtos.getTickCount() - start;
            if (elapsed >= timeout_ms) {
                return flag.load();
            }
            rtos.TakeSemaphore(semaphore, timeout_ms - elapsed);
        }
        return true;
    }

    static const TaskRunGate*& CurrentTaskGate() {
        thread_local const TaskRunGate* gate = nullptr;
        return gate;
    }

    SemaphoreHandle_t run_sem_;
    SemaphoreHandle_t parked_sem_;
    SemaphoreHandle_t exited_sem_;
    std::atomic<bool> run_requested_{false};
    std::atomic<bool> exit_requested_{false};
    std::atomic<bool> parked_{false};
    std::atomic<bool> exited_{false};
};

}  // namespace os
}  // namespace loramesher
