/**
 * @file rtos_coverage_test.cpp
 * @brief Coverage tests for RTOSMock inline functions.
 *
 * This file lives in the utils/test_utils suite, which produces the FINAL
 * binary used by llvm-cov for coverage reporting.  RTOSMock functions that
 * are only exercised in the test_os binary (an earlier binary) show 0% in the
 * final report.  These tests re-exercise those functions so coverage is counted
 * in the FINAL binary.
 */

#include <gtest/gtest.h>

#ifdef ARDUINO

TEST(RTOSCoverageTest, SkipOnArduino) {
    GTEST_SKIP();
}

#else

#include <atomic>
#include <chrono>
#include <thread>

#include "os/rtos_mock.hpp"

using namespace loramesher;
using namespace loramesher::os;

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------

static RTOSMock& GetMock() {
    auto* mock = dynamic_cast<RTOSMock*>(&GetRTOS());
    if (!mock) {
        // Provide a sensible assertion failure message
        EXPECT_NE(mock, nullptr) << "GetRTOS() did not return an RTOSMock";
    }
    return *mock;
}

// ---------------------------------------------------------------------------
// advanceTime() in real-time mode
// ---------------------------------------------------------------------------

/**
 * @brief advanceTime() called in real-time mode returns the current tick count
 * and emits a warning (covers lines 166-168 in rtos_mock.hpp).
 */
TEST(RTOSCoverageTest, AdvanceTimeInRealMode) {
    RTOSMock& rtos = GetMock();

    // Default mode is real time; make sure we did not accidentally leave
    // virtual time mode from a previous test
    rtos.setTimeMode(RTOSMock::TimeMode::kRealTime);

    uint32_t tick_before = rtos.getTickCount();
    uint64_t result = rtos.advanceTime(100);

    // In real-time mode the function returns getTickCount() and logs a warning.
    // We only verify that the return value is at least tick_before (no crash).
    EXPECT_GE(result, static_cast<uint64_t>(tick_before));
}

// ---------------------------------------------------------------------------
// advanceTime() wakes a sleeping task in virtual-time mode
// ---------------------------------------------------------------------------

/**
 * @brief advanceTime() advances virtual time and wakes a task sleeping via
 * delay() (covers lines 171-235 in rtos_mock.hpp).
 */
TEST(RTOSCoverageTest, AdvanceTimeWakesTaskInVirtualMode) {
    RTOSMock& rtos = GetMock();
    rtos.setTimeMode(RTOSMock::TimeMode::kVirtualTime);

    std::atomic<bool> task_ran{false};
    std::atomic<bool> should_exit{false};

    auto task_fn = [](void* param) {
        auto* p =
            static_cast<std::pair<std::atomic<bool>*, std::atomic<bool>*>*>(
                param);
        auto* ran = p->first;
        auto* exit_flag = p->second;

        // Sleep for 500 ms in virtual time
        GetRTOS().delay(500);
        ran->store(true);

        // Spin-wait until test signals done
        while (!exit_flag->load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    };

    auto params = std::make_pair(&task_ran, &should_exit);
    TaskHandle_t handle;
    ASSERT_TRUE(
        rtos.CreateTask(task_fn, "VtWakeTask", 2048, &params, 1, &handle));

    // Give the task time to enter delay() and register its virtual-time wait
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(task_ran.load()) << "task should still be sleeping";

    // Advance virtual time by 1000 ms – wakes the 500 ms delay
    rtos.advanceTime(1000);

    // advanceTime() calls waitForTasksToReblock(), so the task has processed
    // its wake-up by the time we return here.
    EXPECT_TRUE(task_ran.load()) << "task should have woken after advanceTime";

    should_exit.store(true);
    rtos.DeleteTask(handle);

    // Restore real-time mode so subsequent tests are unaffected
    rtos.setTimeMode(RTOSMock::TimeMode::kRealTime);
}

// ---------------------------------------------------------------------------
// advanceTime() fires one-shot and periodic timer callbacks
// ---------------------------------------------------------------------------

/**
 * @brief advanceTime() fires a one-shot timer and then a periodic timer
 * (covers lines 192-206 in rtos_mock.hpp).
 */
TEST(RTOSCoverageTest, AdvanceTimeFiresTimer) {
    RTOSMock& rtos = GetMock();
    rtos.setTimeMode(RTOSMock::TimeMode::kVirtualTime);

    std::atomic<int> one_shot_calls{0};
    std::atomic<int> periodic_calls{0};

    // One-shot timer: period = 0, fires after 100 ms
    rtos.createTimer([&one_shot_calls]() { one_shot_calls.fetch_add(1); },
                     100 /*delayMs*/, 0 /*one-shot*/);

    // Periodic timer: period = 150 ms, first fire after 150 ms
    uint32_t periodic_timer_id =
        rtos.createTimer([&periodic_calls]() { periodic_calls.fetch_add(1); },
                         150 /*delayMs*/, 150 /*periodMs*/);

    // --- First advance: 200 ms ---
    // One-shot (100 ms) should fire once; periodic (150 ms) should fire once.
    rtos.advanceTime(200);
    EXPECT_EQ(one_shot_calls.load(), 1)
        << "one-shot timer should have fired once";
    EXPECT_EQ(periodic_calls.load(), 1)
        << "periodic timer should have fired once at 150 ms";

    // --- Second advance: 200 ms more (total 400 ms) ---
    // One-shot should NOT fire again (already deactivated at line 204-205).
    // Periodic fires at 300 ms and again at 450 ms — only 300 ms falls within
    // this window, so +1 more fire.
    rtos.advanceTime(200);
    EXPECT_EQ(one_shot_calls.load(), 1) << "one-shot timer must not fire again";
    EXPECT_GE(periodic_calls.load(), 2)
        << "periodic timer should have fired at least twice total";

    rtos.stopTimer(periodic_timer_id);
    rtos.setTimeMode(RTOSMock::TimeMode::kRealTime);
}

// ---------------------------------------------------------------------------
// getTaskState() — uncovered paths
// ---------------------------------------------------------------------------

/**
 * @brief getTaskState(nullptr) from the main test thread returns kUnknown
 * because the main thread is not a registered RTOS task (covers lines
 * 1136-1145 in rtos_mock.hpp).
 */
TEST(RTOSCoverageTest, GetTaskStateNullHandle) {
    RTOSMock& rtos = GetMock();
    TaskState state = rtos.getTaskState(nullptr);
    EXPECT_EQ(state, TaskState::kUnknown);
}

/**
 * @brief getTaskState(nullptr) called from WITHIN a registered task returns
 * kRunning (covers lines 1140-1143 in rtos_mock.hpp).
 */
TEST(RTOSCoverageTest, GetTaskStateOfOwnThread) {
    RTOSMock& rtos = GetMock();

    std::atomic<TaskState> observed_state{TaskState::kUnknown};
    std::atomic<bool> done{false};

    auto task_fn = [](void* param) {
        auto* p = static_cast<
            std::pair<std::atomic<TaskState>*, std::atomic<bool>*>*>(param);
        // nullptr = "current task"
        p->first->store(GetRTOS().getTaskState(nullptr));
        p->second->store(true);
    };

    auto params = std::make_pair(&observed_state, &done);
    TaskHandle_t handle;
    ASSERT_TRUE(
        rtos.CreateTask(task_fn, "TaskStateTask", 2048, &params, 1, &handle));

    // Wait for the task to store its result
    for (int i = 0; i < 100 && !done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(done.load()) << "task should have completed";
    // Running (not suspended) → kRunning
    EXPECT_EQ(observed_state.load(), TaskState::kRunning);

    rtos.DeleteTask(handle);
}

// ---------------------------------------------------------------------------
// StartScheduler()
// ---------------------------------------------------------------------------

/**
 * @brief StartScheduler() is a no-op on the mock; just verify no crash
 * (covers lines 1088-1090 in rtos_mock.hpp).
 */
TEST(RTOSCoverageTest, StartScheduler) {
    EXPECT_NO_THROW(GetRTOS().StartScheduler());
}

// ---------------------------------------------------------------------------
// DeleteSystemSemaphore() paths
// ---------------------------------------------------------------------------

/**
 * @brief DeleteSystemSemaphore(nullptr) is a no-op (covers line 1617-1618).
 */
TEST(RTOSCoverageTest, DeleteSystemSemaphoreNullHandle) {
    EXPECT_NO_THROW(GetRTOS().DeleteSystemSemaphore(nullptr));
}

/**
 * @brief DeleteSystemSemaphore() on an unlocked semaphore deletes it cleanly
 * (covers lines 1625-1628 in rtos_mock.hpp).
 */
TEST(RTOSCoverageTest, DeleteSystemSemaphoreWhenUnlocked) {
    SemaphoreHandle_t sem = GetRTOS().CreateSystemSemaphore();
    ASSERT_NE(sem, nullptr);
    // The semaphore is unlocked (timed_mutex starts unlocked).
    // DeleteSystemSemaphore should lock, unlock, then delete without crash.
    EXPECT_NO_THROW(GetRTOS().DeleteSystemSemaphore(sem));
}

/**
 * @brief DeleteSystemSemaphore() on a locked semaphore skips deletion
 * (covers lines 1629-1635 in rtos_mock.hpp).
 */
TEST(RTOSCoverageTest, DeleteSystemSemaphoreWhenLocked) {
    SemaphoreHandle_t sem = GetRTOS().CreateSystemSemaphore();
    ASSERT_NE(sem, nullptr);

    // Lock the semaphore so DeleteSystemSemaphore finds it locked
    bool taken = GetRTOS().TakeSystemSemaphore(sem, MAX_DELAY);
    ASSERT_TRUE(taken);

    // Should print the "locked, skipping deletion" message and NOT delete
    EXPECT_NO_THROW(GetRTOS().DeleteSystemSemaphore(sem));

    // Release the semaphore manually (the mutex was not deleted above)
    GetRTOS().GiveSystemSemaphore(sem);

    // Now we can properly delete it
    EXPECT_NO_THROW(GetRTOS().DeleteSystemSemaphore(sem));
}

// ---------------------------------------------------------------------------
// TakeSystemSemaphore() uncovered paths
// ---------------------------------------------------------------------------

/**
 * @brief TakeSystemSemaphore(nullptr, ...) returns false immediately
 * (covers lines 1577-1578 in rtos_mock.hpp).
 */
TEST(RTOSCoverageTest, TakeSystemSemaphoreNullHandle) {
    bool result = GetRTOS().TakeSystemSemaphore(nullptr, 100);
    EXPECT_FALSE(result);
}

/**
 * @brief TakeSystemSemaphore with timeout=0 uses try_lock; returns false when
 * the semaphore is already held (covers lines 1583-1585 in rtos_mock.hpp).
 */
TEST(RTOSCoverageTest, TakeSystemSemaphoreTimeout0) {
    SemaphoreHandle_t sem = GetRTOS().CreateSystemSemaphore();
    ASSERT_NE(sem, nullptr);

    // Lock the semaphore first so try_lock() will fail
    ASSERT_TRUE(GetRTOS().TakeSystemSemaphore(sem, MAX_DELAY));

    // Timeout=0 should attempt try_lock and fail because we already hold it
    bool result = GetRTOS().TakeSystemSemaphore(sem, 0);
    EXPECT_FALSE(result);

    // Unlock
    GetRTOS().GiveSystemSemaphore(sem);
    // Clean up
    GetRTOS().DeleteSystemSemaphore(sem);
}

/**
 * @brief TakeSystemSemaphore with MAX_DELAY uses mutex::lock()
 * (covers lines 1586-1589 in rtos_mock.hpp).
 */
TEST(RTOSCoverageTest, TakeSystemSemaphoreMaxDelay) {
    SemaphoreHandle_t sem = GetRTOS().CreateSystemSemaphore();
    ASSERT_NE(sem, nullptr);

    // Semaphore is initially unlocked; MAX_DELAY path should acquire it
    bool result = GetRTOS().TakeSystemSemaphore(sem, MAX_DELAY);
    EXPECT_TRUE(result);

    // Release
    GetRTOS().GiveSystemSemaphore(sem);
    GetRTOS().DeleteSystemSemaphore(sem);
}

// ---------------------------------------------------------------------------
// GiveSemaphoreFromISR()
// ---------------------------------------------------------------------------

/**
 * @brief GiveSemaphoreFromISR() releases a token on a counting semaphore
 * (covers GiveSemaphoreFromISR lines in rtos_mock.hpp).
 */
TEST(RTOSCoverageTest, GiveSemaphoreFromISR) {
    // Counting semaphore starting empty
    SemaphoreHandle_t sem = GetRTOS().CreateCountingSemaphore(2, 0);
    ASSERT_NE(sem, nullptr);

    // Nothing available yet
    EXPECT_FALSE(GetRTOS().TakeSemaphore(sem, 0));

    // Give from ISR
    EXPECT_TRUE(GetRTOS().GiveSemaphoreFromISR(sem));

    // Now one token available
    EXPECT_TRUE(GetRTOS().TakeSemaphore(sem, 0));
    EXPECT_FALSE(GetRTOS().TakeSemaphore(sem, 0));

    GetRTOS().DeleteSemaphore(sem);
}

// ---------------------------------------------------------------------------
// ResumeTask() with non-existent / unknown handle
// ---------------------------------------------------------------------------

/**
 * @brief ResumeTask() with a handle that is not in the tasks_ map hits the
 * "task not found" warning path and returns false (covers lines 577-580 in
 * rtos_mock.hpp).
 */
TEST(RTOSCoverageTest, ResumeNonexistentTask) {
    RTOSMock& rtos = GetMock();
    // Use a non-null sentinel pointer that was never registered as a task
    TaskHandle_t fake_handle =
        reinterpret_cast<TaskHandle_t>(static_cast<uintptr_t>(0xDEAD0001));
    bool result = rtos.ResumeTask(fake_handle);
    EXPECT_FALSE(result);
}

// ---------------------------------------------------------------------------
// getTaskStackWatermark() — known and unknown task handle
// ---------------------------------------------------------------------------

/**
 * @brief getTaskStackWatermark(nullptr) returns a simulated watermark for
 * the current thread if it happens to be a registered RTOS task; for the
 * main test thread (not registered) it returns the default 2048
 * (covers lines 1097-1124 in rtos_mock.hpp).
 */
TEST(RTOSCoverageTest, GetTaskStackWatermarkCurrentThread) {
    RTOSMock& rtos = GetMock();

    // From the main test thread (not a registered task) → returns default 2048
    uint32_t watermark = rtos.getTaskStackWatermark(nullptr);
    EXPECT_EQ(watermark, 2048u);
}

/**
 * @brief getTaskStackWatermark() for a real RTOS task returns a sensible value.
 */
TEST(RTOSCoverageTest, GetTaskStackWatermarkRealTask) {
    RTOSMock& rtos = GetMock();

    std::atomic<uint32_t> measured_watermark{0};
    std::atomic<bool> done{false};

    auto task_fn = [](void* param) {
        auto* p =
            static_cast<std::pair<std::atomic<uint32_t>*, std::atomic<bool>*>*>(
                param);
        // Ask for own watermark (nullptr = current task)
        uint32_t wm = GetRTOS().getTaskStackWatermark(nullptr);
        p->first->store(wm);
        p->second->store(true);
    };

    auto params = std::make_pair(&measured_watermark, &done);
    TaskHandle_t handle;
    ASSERT_TRUE(
        rtos.CreateTask(task_fn, "WatermarkTask", 4096, &params, 1, &handle));

    for (int i = 0; i < 100 && !done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(done.load());
    // The simulated watermark should be >0 and <= stack_size (4096)
    EXPECT_GT(measured_watermark.load(), 0u);

    rtos.DeleteTask(handle);
}

// ---------------------------------------------------------------------------
// getSystemTaskStats()
// ---------------------------------------------------------------------------

/**
 * @brief getSystemTaskStats() returns one entry per running task.
 * This exercises lines 1157-1181 in rtos_mock.hpp.
 */
TEST(RTOSCoverageTest, GetSystemTaskStats) {
    RTOSMock& rtos = GetMock();

    std::atomic<bool> should_exit{false};
    auto task_fn = [](void* param) {
        auto* exit_flag = static_cast<std::atomic<bool>*>(param);
        while (!exit_flag->load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    };

    TaskHandle_t h1, h2;
    ASSERT_TRUE(
        rtos.CreateTask(task_fn, "StatsTask1", 2048, &should_exit, 1, &h1));
    ASSERT_TRUE(
        rtos.CreateTask(task_fn, "StatsTask2", 2048, &should_exit, 1, &h2));

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    auto stats = rtos.getSystemTaskStats();
    // At least the 2 tasks we just created should be in the list
    EXPECT_GE(stats.size(), 2u);

    // Check fields are populated
    for (const auto& s : stats) {
        EXPECT_FALSE(s.name.empty());
    }

    should_exit.store(true);
    rtos.DeleteTask(h1);
    rtos.DeleteTask(h2);
}

// ---------------------------------------------------------------------------
// stopTimer() — valid and invalid timer ID
// ---------------------------------------------------------------------------

/**
 * @brief stopTimer() with an invalid ID returns false (line 1697-1699).
 */
TEST(RTOSCoverageTest, StopTimerInvalidId) {
    RTOSMock& rtos = GetMock();
    rtos.setTimeMode(RTOSMock::TimeMode::kVirtualTime);

    // Use a timer ID that was never created
    bool result = rtos.stopTimer(9999u);
    EXPECT_FALSE(result);

    rtos.setTimeMode(RTOSMock::TimeMode::kRealTime);
}

/**
 * @brief stopTimer() with a valid ID returns true and disables the timer.
 * Advancing time past the expiry after stop should NOT fire the callback.
 */
TEST(RTOSCoverageTest, StopTimerPreventsFiring) {
    RTOSMock& rtos = GetMock();
    rtos.setTimeMode(RTOSMock::TimeMode::kVirtualTime);

    std::atomic<int> fire_count{0};
    uint32_t timer_id =
        rtos.createTimer([&fire_count]() { fire_count++; }, 100, 0);

    // Stop it before it can fire
    bool stopped = rtos.stopTimer(timer_id);
    EXPECT_TRUE(stopped);

    // Advance past the original expiry time – callback must NOT fire
    rtos.advanceTime(200);
    EXPECT_EQ(fire_count.load(), 0) << "Stopped timer must not fire";

    rtos.setTimeMode(RTOSMock::TimeMode::kRealTime);
}

// ---------------------------------------------------------------------------
// SendToQueue() with full queue and timeout (lines 811-819)
// ---------------------------------------------------------------------------

/**
 * @brief SendToQueue() blocks briefly when the queue is full and a timeout>0
 * is given; it times out and returns kTimeout (covers lines 811-819).
 */
TEST(RTOSCoverageTest, SendToQueueFullQueueTimeout) {
    RTOSMock& rtos = GetMock();

    // Create a single-slot queue and fill it
    QueueHandle_t q = rtos.CreateQueue(1, sizeof(uint32_t));
    ASSERT_NE(q, nullptr);

    uint32_t item = 42u;
    // First send succeeds (queue was empty)
    EXPECT_EQ(rtos.SendToQueue(q, &item, 0), QueueResult::kOk);

    // Second send should fail immediately when timeout=0 and queue is full
    EXPECT_EQ(rtos.SendToQueue(q, &item, 0), QueueResult::kFull);

    // Third send with small non-zero timeout should time out (queue still full)
    auto start = std::chrono::steady_clock::now();
    auto result = rtos.SendToQueue(q, &item, 50u);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count();
    EXPECT_EQ(result, QueueResult::kTimeout);
    EXPECT_GE(elapsed, 40);  // Waited ~50ms

    rtos.DeleteQueue(q);
}

// ---------------------------------------------------------------------------
// WaitForNotify() — timeout=0 immediate return (line 1338-1340)
// ---------------------------------------------------------------------------

/**
 * @brief WaitForNotify(0) from a registered RTOS task returns kTimeout
 * immediately when no notification is pending (covers lines 1338-1340).
 */
TEST(RTOSCoverageTest, WaitForNotifyTimeout0ImmediateReturn) {
    RTOSMock& rtos = GetMock();

    std::atomic<QueueResult> observed_result{QueueResult::kOk};
    std::atomic<bool> done{false};

    auto task_fn = [](void* param) {
        auto* p = static_cast<
            std::pair<std::atomic<QueueResult>*, std::atomic<bool>*>*>(param);
        // No prior notification → timeout=0 should return kTimeout immediately
        QueueResult r = GetRTOS().WaitForNotify(0);
        p->first->store(r);
        p->second->store(true);
    };

    auto params = std::make_pair(&observed_result, &done);
    TaskHandle_t handle;
    ASSERT_TRUE(
        rtos.CreateTask(task_fn, "WFNTask0", 2048, &params, 1, &handle));

    for (int i = 0; i < 100 && !done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(done.load());
    EXPECT_EQ(observed_result.load(), QueueResult::kTimeout);

    rtos.DeleteTask(handle);
}

/**
 * @brief WaitForNotify() from the main test thread (not a registered task)
 * returns kError (covers lines 1298-1304 in rtos_mock.hpp).
 */
TEST(RTOSCoverageTest, WaitForNotifyFromNonTaskThreadReturnsError) {
    // Main thread is not in the tasks_ map → kError immediately
    QueueResult result = GetRTOS().WaitForNotify(10);
    EXPECT_EQ(result, QueueResult::kError);
}

// ---------------------------------------------------------------------------
// NotifyTask() via handle – covers lines 1260-1270
// ---------------------------------------------------------------------------

/**
 * @brief NotifyTask(handle, value) sets notification_pending and wakes the
 * task (covers lines 1239-1274 in rtos_mock.hpp).
 */
TEST(RTOSCoverageTest, NotifyTaskByHandle) {
    RTOSMock& rtos = GetMock();

    std::atomic<QueueResult> observed{QueueResult::kError};
    std::atomic<bool> done{false};

    auto task_fn = [](void* param) {
        auto* p = static_cast<
            std::pair<std::atomic<QueueResult>*, std::atomic<bool>*>*>(param);
        // Wait up to 500ms for a notification
        QueueResult r = GetRTOS().WaitForNotify(500);
        p->first->store(r);
        p->second->store(true);
    };

    auto params = std::make_pair(&observed, &done);
    TaskHandle_t handle;
    ASSERT_TRUE(rtos.CreateTask(task_fn, "NotifyTaskByHandleTask", 2048,
                                &params, 1, &handle));

    // Give the task time to enter WaitForNotify
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    // Notify via handle
    QueueResult notify_result = rtos.NotifyTask(handle, 0u);
    EXPECT_EQ(notify_result, QueueResult::kOk);

    for (int i = 0; i < 100 && !done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(done.load());
    EXPECT_EQ(observed.load(), QueueResult::kOk);

    rtos.DeleteTask(handle);
}

/**
 * @brief NotifyTask() with nullptr handle notifies the current task's own
 * entry (covers lines 1246-1258 in rtos_mock.hpp).
 */
TEST(RTOSCoverageTest, NotifyTaskNullHandleSelf) {
    RTOSMock& rtos = GetMock();

    std::atomic<bool> done{false};

    auto task_fn = [](void* param) {
        auto* d = static_cast<std::atomic<bool>*>(param);
        // Self-notify via nullptr handle
        QueueResult r = GetRTOS().NotifyTask(nullptr, 42u);
        EXPECT_EQ(r, QueueResult::kOk);
        d->store(true);
    };

    TaskHandle_t handle;
    ASSERT_TRUE(
        rtos.CreateTask(task_fn, "SelfNotifyTask", 2048, &done, 1, &handle));

    for (int i = 0; i < 100 && !done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(done.load());
    rtos.DeleteTask(handle);
}

/**
 * @brief NotifyTask() with a non-existent handle returns kError (covers
 * line 1273 in rtos_mock.hpp).
 */
TEST(RTOSCoverageTest, NotifyTaskNonExistentHandleReturnsError) {
    RTOSMock& rtos = GetMock();
    TaskHandle_t fake =
        reinterpret_cast<TaskHandle_t>(static_cast<uintptr_t>(0xDEAD0002));
    QueueResult result = rtos.NotifyTask(fake, 0u);
    EXPECT_EQ(result, QueueResult::kError);
}

// ---------------------------------------------------------------------------
// NotifyTaskFromISR() paths
// ---------------------------------------------------------------------------

/**
 * @brief NotifyTaskFromISR(handle) sets notification_pending on the target task.
 */
TEST(RTOSCoverageTest, NotifyTaskFromISRByHandle) {
    RTOSMock& rtos = GetMock();

    std::atomic<QueueResult> observed{QueueResult::kError};
    std::atomic<bool> done{false};

    auto task_fn = [](void* param) {
        auto* p = static_cast<
            std::pair<std::atomic<QueueResult>*, std::atomic<bool>*>*>(param);
        QueueResult r = GetRTOS().WaitForNotify(500);
        p->first->store(r);
        p->second->store(true);
    };

    auto params = std::make_pair(&observed, &done);
    TaskHandle_t handle;
    ASSERT_TRUE(
        rtos.CreateTask(task_fn, "ISRNotifyTask", 2048, &params, 1, &handle));

    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    rtos.NotifyTaskFromISR(handle);

    for (int i = 0; i < 100 && !done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(done.load());
    EXPECT_EQ(observed.load(), QueueResult::kOk);

    rtos.DeleteTask(handle);
}

// ---------------------------------------------------------------------------
// GetRandom() – verify non-crash and reasonable output
// ---------------------------------------------------------------------------

/**
 * @brief GetRandom() returns a uint32_t without crashing.
 */
TEST(RTOSCoverageTest, GetRandomNoCrash) {
    uint32_t r1 = GetRTOS().GetRandom();
    uint32_t r2 = GetRTOS().GetRandom();
    // Just verify it compiles and runs; the values may differ
    (void)r1;
    (void)r2;
    SUCCEED();
}

// ---------------------------------------------------------------------------
// delay() fallback for non-task thread (lines 983-988 in rtos_mock.hpp)
// Calling delay() from the main test thread (not in tasks_ map) hits the
// "could not find TaskInfo" warning path.
// NOTE: This path throws TaskTerminationException from inside a non-task thread
// which we cannot safely catch here; instead we exercise the real-time path via
// a registered task so that the interruptible wait_for branch is covered.
// ---------------------------------------------------------------------------

/**
 * @brief delay() in real-time mode uses an interruptible wait_for and wakes
 * when the task is deleted (covers lines 1001-1015 in rtos_mock.hpp).
 */
TEST(RTOSCoverageTest, DelayInterruptedByDeletion) {
    RTOSMock& rtos = GetMock();
    rtos.setTimeMode(RTOSMock::TimeMode::kRealTime);

    std::atomic<bool> started{false};

    auto task_fn = [](void* param) {
        auto* s = static_cast<std::atomic<bool>*>(param);
        s->store(true);
        try {
            GetRTOS().delay(
                10000);  // Long delay – will be interrupted by delete
        } catch (const os::TaskTerminationException&) {
            // Expected when task is deleted mid-delay
        }
    };

    TaskHandle_t handle;
    ASSERT_TRUE(rtos.CreateTask(task_fn, "DelayInterruptTask", 2048, &started,
                                1, &handle));

    // Wait for task to be running and inside delay()
    for (int i = 0; i < 50 && !started.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(started.load());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Delete the task – this sets delay_interrupted and notifies delay_cv
    rtos.DeleteTask(handle);
    // If we reach here without hanging, the test passed
    SUCCEED();
}

// ---------------------------------------------------------------------------
// ResumeTask() already-not-suspended branch (lines 609-613 in rtos_mock.hpp)
// ---------------------------------------------------------------------------

/**
 * @brief ResumeTask() on a task that is NOT suspended returns true immediately.
 */
TEST(RTOSCoverageTest, ResumeAlreadyRunningTask) {
    RTOSMock& rtos = GetMock();

    std::atomic<bool> should_exit{false};
    auto task_fn = [](void* param) {
        auto* exit_flag = static_cast<std::atomic<bool>*>(param);
        while (!exit_flag->load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    };

    TaskHandle_t handle;
    ASSERT_TRUE(rtos.CreateTask(task_fn, "NotSuspendedTask", 2048, &should_exit,
                                1, &handle));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Task is running (not suspended) – ResumeTask should return true immediately
    bool result = rtos.ResumeTask(handle);
    EXPECT_TRUE(result);

    should_exit.store(true);
    rtos.DeleteTask(handle);
}

// ---------------------------------------------------------------------------
// SuspendTask() already-suspended branch (lines 488-499 in rtos_mock.hpp)
// ---------------------------------------------------------------------------

/**
 * @brief SuspendTask() on an already-suspended task returns true immediately.
 */
TEST(RTOSCoverageTest, SuspendAlreadySuspendedTask) {
    RTOSMock& rtos = GetMock();

    std::atomic<bool> should_exit{false};
    auto task_fn = [](void* param) {
        auto* exit_flag = static_cast<std::atomic<bool>*>(param);
        while (!exit_flag->load()) {
            GetRTOS().ShouldStopOrPause();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    };

    TaskHandle_t handle;
    ASSERT_TRUE(rtos.CreateTask(task_fn, "SuspendTwiceTask", 2048, &should_exit,
                                1, &handle));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // First suspend
    EXPECT_TRUE(rtos.SuspendTask(handle));

    // Second suspend on already-suspended task
    EXPECT_TRUE(rtos.SuspendTask(handle));

    // Resume and clean up
    rtos.ResumeTask(handle);
    should_exit.store(true);
    rtos.DeleteTask(handle);
}

// ---------------------------------------------------------------------------
// SetCurrentTaskNodeAddress() and GetCurrentTaskNodeAddress()
// ---------------------------------------------------------------------------

/**
 * @brief SetCurrentTaskNodeAddress()/GetCurrentTaskNodeAddress() round-trip
 * from within a registered RTOS task (covers lines 1784-1835 in rtos_mock.hpp).
 */
TEST(RTOSCoverageTest, SetAndGetCurrentTaskNodeAddress) {
    RTOSMock& rtos = GetMock();

    std::atomic<bool> done{false};
    std::string observed_address;
    std::mutex obs_mutex;

    auto task_fn = [](void* param) {
        auto* p =
            static_cast<std::pair<std::string*, std::atomic<bool>*>*>(param);

        GetRTOS().SetCurrentTaskNodeAddress("0xABCD");
        const char* addr = GetRTOS().GetCurrentTaskNodeAddress();

        *p->first = addr;
        p->second->store(true);
    };

    auto params = std::make_pair(&observed_address, &done);
    TaskHandle_t handle;
    ASSERT_TRUE(
        rtos.CreateTask(task_fn, "AddrTask", 2048, &params, 1, &handle));

    for (int i = 0; i < 100 && !done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(done.load());
    EXPECT_EQ(observed_address, "0xABCD");

    rtos.DeleteTask(handle);
}

// ---------------------------------------------------------------------------
// GiveSystemSemaphore(nullptr) – returns false (line 1603-1604)
// ---------------------------------------------------------------------------

/**
 * @brief GiveSystemSemaphore(nullptr) returns false without crashing.
 */
TEST(RTOSCoverageTest, GiveSystemSemaphoreNullHandleReturnsFalse) {
    bool result = GetRTOS().GiveSystemSemaphore(nullptr);
    EXPECT_FALSE(result);
}

// ---------------------------------------------------------------------------
// getTaskStateString() — all switch branches
// ---------------------------------------------------------------------------

TEST(RTOSCoverageTest, GetTaskStateStringReady) {
    EXPECT_STREQ(RTOS::getTaskStateString(TaskState::kReady), "Ready");
}

TEST(RTOSCoverageTest, GetTaskStateStringBlocked) {
    EXPECT_STREQ(RTOS::getTaskStateString(TaskState::kBlocked), "Blocked");
}

TEST(RTOSCoverageTest, GetTaskStateStringSuspended) {
    EXPECT_STREQ(RTOS::getTaskStateString(TaskState::kSuspended), "Suspended");
}

TEST(RTOSCoverageTest, GetTaskStateStringDeleted) {
    EXPECT_STREQ(RTOS::getTaskStateString(TaskState::kDeleted), "Deleted");
}

TEST(RTOSCoverageTest, GetTaskStateStringUnknown) {
    EXPECT_STREQ(RTOS::getTaskStateString(TaskState::kUnknown), "Unknown");
}

TEST(RTOSCoverageTest, GetTaskStateStringDefault) {
    auto invalid = static_cast<TaskState>(99);
    EXPECT_STREQ(RTOS::getTaskStateString(invalid), "Unknown");
}

#endif  // ARDUINO
