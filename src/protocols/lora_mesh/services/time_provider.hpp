
/**
 * @file time_provider.hpp
 * @brief Time provider interface and implementation for LoRaMesh protocol
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include "os/os_port.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {

/**
 * @brief Interface for time provider
 * 
 * Abstracts time operations to enable testing and different time sources
 */
class ITimeProvider {
   public:
    virtual ~ITimeProvider() = default;

    /**
    * @brief Get the current time in milliseconds
    * 
    * @return uint32_t Current time
    */
    virtual uint32_t GetCurrentTime() const = 0;

    /**
    * @brief Sleep for the specified duration
    * 
    * @param ms Sleep duration in milliseconds
    */
    virtual void Sleep(uint32_t ms) const = 0;

    /**
    * @brief Get elapsed time since a reference point
    * 
    * @param reference_time Reference time to compare against
    * @return uint32_t Elapsed time in milliseconds
    */
    virtual uint32_t GetElapsedTime(uint32_t reference_time) const = 0;
};

/**
 * @brief Real-time implementation of ITimeProvider
 * 
 * Uses system clock for time operations
 */
class TimeProvider : public ITimeProvider {
   public:
    TimeProvider() = default;
    virtual ~TimeProvider() = default;

    uint32_t GetCurrentTime() const override {
        return GetRTOS().getTickCount();
    }

    void Sleep(uint32_t ms) const override { GetRTOS().delay(ms); }

    uint32_t GetElapsedTime(uint32_t reference_time) const override {
        uint32_t current_time = GetCurrentTime();
        if (current_time < reference_time) {
            return (UINT32_MAX - reference_time) + current_time + 1;
        }
        return current_time - reference_time;
    }
};

/**
 * @brief Configurable time provider for testing
 * 
 * Allows setting custom time functions and simulating time advancement
 */
class ConfigurableTimeProvider : public ITimeProvider {
   public:
    /**
    * @brief Constructor with optional custom time function
    * 
    * @param time_func Custom function to get current time (optional)
    */
    ConfigurableTimeProvider(std::function<uint32_t()> time_func = nullptr)
        : time_function_(time_func), simulated_time_(0), use_simulated_(false) {

        if (!time_function_) {
            // Default to real time if no custom function provided
            time_function_ = []() {
                return static_cast<uint32_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count());
            };
        }
    }

    uint32_t GetCurrentTime() const override {
        if (use_simulated_) {
            return simulated_time_;
        }
        return time_function_();
    }

    void Sleep(uint32_t ms) const override {
        if (use_simulated_) {
            // In simulation mode, just advance simulated time
            const_cast<ConfigurableTimeProvider*>(this)->simulated_time_ += ms;
        } else {
            GetRTOS().delay(ms);
        }
    }

    uint32_t GetElapsedTime(uint32_t reference_time) const override {
        uint32_t current_time = GetCurrentTime();
        if (current_time < reference_time) {
            return (UINT32_MAX - reference_time) + current_time + 1;
        }
        return current_time - reference_time;
    }

    /**
    * @brief Enable simulated time mode
    * 
    * @param initial_time Starting simulated time
    */
    void EnableSimulatedTime(uint32_t initial_time = 0) {
        use_simulated_ = true;
        simulated_time_ = initial_time;
    }

    /**
    * @brief Disable simulated time mode (use real time)
    */
    void DisableSimulatedTime() { use_simulated_ = false; }

    /**
    * @brief Advance simulated time
    * 
    * @param ms Milliseconds to advance
    */
    void AdvanceTime(uint32_t ms) {
        if (use_simulated_) {
            simulated_time_ += ms;
        }
    }

    /**
    * @brief Set simulated time to specific value
    * 
    * @param time New simulated time
    */
    void SetSimulatedTime(uint32_t time) {
        if (use_simulated_) {
            simulated_time_ = time;
        }
    }

    /**
    * @brief Check if using simulated time
    * 
    * @return bool True if using simulated time
    */
    bool IsUsingSimulatedTime() const { return use_simulated_; }

   private:
    std::function<uint32_t()> time_function_;
    uint32_t simulated_time_;
    bool use_simulated_;
};

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher