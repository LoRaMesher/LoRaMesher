/**
 * @file mutex.hpp
 * @brief Portable mutex primitives.
 *
 * The native and ESP32 toolchains provide std::mutex (ESP32 gets it through the
 * FreeRTOS gthreads layer). Bare-metal Arduino cores such as STM32 ship a
 * libstdc++ built without gthreads, so std::mutex is not defined there; on those
 * targets the mutex is backed by a FreeRTOS mutex instead. Availability is
 * detected via `_GLIBCXX_HAS_GTHREADS` rather than by enumerating cores.
 *
 * The RAII guards below are defined once against os::Mutex's lock()/unlock() so
 * every call site is identical regardless of platform.
 */
#pragma once

#include <mutex>  // pulls in _GLIBCXX_HAS_GTHREADS; defines std::mutex iff available

#include "config/system_config.hpp"

#if defined(LORAMESHER_BUILD_ARDUINO) && !defined(_GLIBCXX_HAS_GTHREADS)

#include "os/freertos_includes.hpp"

namespace loramesher {
namespace os {

/**
 * @brief FreeRTOS-backed mutex for toolchains whose libstdc++ lacks std::mutex.
 */
class Mutex {
   public:
    Mutex() : handle_(xSemaphoreCreateMutex()) {}

    ~Mutex() {
        if (handle_ != nullptr) {
            vSemaphoreDelete(handle_);
        }
    }

    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    void lock() { xSemaphoreTake(handle_, portMAX_DELAY); }

    void unlock() { xSemaphoreGive(handle_); }

    bool try_lock() { return xSemaphoreTake(handle_, 0) == pdTRUE; }

   private:
    SemaphoreHandle_t handle_;
};

}  // namespace os
}  // namespace loramesher

#else  // std::mutex is available

namespace loramesher {
namespace os {
using Mutex = std::mutex;
}  // namespace os
}  // namespace loramesher

#endif

namespace loramesher {
namespace os {

/**
 * @brief RAII scoped lock; drop-in for std::lock_guard<os::Mutex>.
 */
class LockGuard {
   public:
    explicit LockGuard(Mutex& mutex) : mutex_(mutex) { mutex_.lock(); }

    ~LockGuard() { mutex_.unlock(); }

    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;

   private:
    Mutex& mutex_;
};

/**
 * @brief Scoped lock supporting manual unlock()/lock(); drop-in for the
 *        std::unique_lock<os::Mutex> uses in this codebase.
 */
class UniqueLock {
   public:
    explicit UniqueLock(Mutex& mutex) : mutex_(&mutex), owns_(true) {
        mutex_->lock();
    }

    ~UniqueLock() {
        if (owns_) {
            mutex_->unlock();
        }
    }

    UniqueLock(const UniqueLock&) = delete;
    UniqueLock& operator=(const UniqueLock&) = delete;

    void lock() {
        if (!owns_) {
            mutex_->lock();
            owns_ = true;
        }
    }

    void unlock() {
        if (owns_) {
            mutex_->unlock();
            owns_ = false;
        }
    }

    bool owns_lock() const { return owns_; }

   private:
    Mutex* mutex_;
    bool owns_;
};

}  // namespace os
}  // namespace loramesher
