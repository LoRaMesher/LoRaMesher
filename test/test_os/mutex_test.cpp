/**
 * @file mutex_test.cpp
 * @brief Unit tests for the portable mutex primitives (os::Mutex,
 *        os::LockGuard, os::UniqueLock).
 *
 * These exercise the RAII/ownership logic of the wrappers. On the native and
 * ESP32 toolchains os::Mutex aliases std::mutex; on bare-metal cores (e.g.
 * STM32) it is backed by a FreeRTOS mutex. The guard behavior under test is the
 * same on every platform.
 */
#include <gtest/gtest.h>

#ifdef ARDUINO

TEST(MutexTest, SkipOnArduino) {
    GTEST_SKIP();
}

#else

#include <atomic>
#include <thread>
#include <vector>

#include "os/mutex.hpp"

using namespace loramesher::os;

// A LockGuard holds the mutex for its scope and releases it on destruction.
TEST(MutexTest, LockGuardLocksAndReleases) {
    Mutex m;
    {
        LockGuard guard(m);
        // While held, another thread must not be able to acquire it.
        std::atomic<bool> acquired{false};
        std::thread t([&] {
            if (m.try_lock()) {
                acquired = true;
                m.unlock();
            }
        });
        t.join();
        EXPECT_FALSE(acquired.load());
    }
    // Released after the guard's scope.
    EXPECT_TRUE(m.try_lock());
    m.unlock();
}

// UniqueLock supports manual unlock()/lock() and reports ownership correctly.
TEST(MutexTest, UniqueLockManualUnlockRelock) {
    Mutex m;
    UniqueLock lock(m);
    EXPECT_TRUE(lock.owns_lock());

    lock.unlock();
    EXPECT_FALSE(lock.owns_lock());
    EXPECT_TRUE(m.try_lock());  // free now
    m.unlock();

    lock.lock();
    EXPECT_TRUE(lock.owns_lock());
}

// UniqueLock releases the mutex when destroyed while still owning it.
TEST(MutexTest, UniqueLockReleasesOnDestruction) {
    Mutex m;
    {
        UniqueLock lock(m);
        EXPECT_TRUE(lock.owns_lock());
    }
    EXPECT_TRUE(m.try_lock());
    m.unlock();
}

// UniqueLock destroyed after an explicit unlock() must not double-unlock.
TEST(MutexTest, UniqueLockNoDoubleUnlock) {
    Mutex m;
    {
        UniqueLock lock(m);
        lock.unlock();
        EXPECT_FALSE(lock.owns_lock());
    }
    // Still consistently lockable.
    EXPECT_TRUE(m.try_lock());
    m.unlock();
}

// Concurrent LockGuard use provides mutual exclusion (no lost updates).
TEST(MutexTest, MutualExclusionAcrossThreads) {
    Mutex m;
    long counter = 0;
    constexpr int kThreads = 8;
    constexpr int kIterations = 10000;

    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&] {
            for (int j = 0; j < kIterations; ++j) {
                LockGuard guard(m);
                ++counter;
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(counter, static_cast<long>(kThreads) * kIterations);
}

#endif  // ARDUINO
