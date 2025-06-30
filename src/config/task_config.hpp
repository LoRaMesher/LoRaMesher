// src/config/task_config.hpp
#pragma once

#include "system_config.hpp"

namespace loramesher {
namespace config {

/**
 * @brief Configuration for FreeRTOS task stack sizes
 * 
 * Centralizes all task stack size configurations for the system.
 * Values determined through stack usage analysis and testing.
 */
struct TaskConfig {
    /**
     * @brief Radio event task stack size in bytes. After some testing,
     * this value was determined to be the minimum stack size required
     * for the radio event task to run without stack overflow.
     * After testing we get 3664 extra bytes of stack space, that's a
     * 28% of margin.
     */
    static constexpr size_t kRadioEventStackSize = 13120;
    static constexpr size_t kMinStackWatermark =
        512;  ///< Minimum free stack threshold in bytes
    // Add other task stack sizes here as needed
};

/**
 * @brief System-wide task priority definitions
 * 
 * Defines priority levels for all system tasks to ensure proper
 * task scheduling and prevent priority conflicts.
 */
struct TaskPriorities {
    static constexpr uint32_t kIdleTaskPriority = 0;
    static constexpr uint32_t kLowPriority = 5;
    static constexpr uint32_t kNormalPriority = 10;
    static constexpr uint32_t kHighPriority = 15;
    static constexpr uint32_t kRadioEventPriority = kHighPriority;

    // Runtime checks for priority relationships
    static_assert(kRadioEventPriority > kNormalPriority,
                  "Radio events must have higher priority than normal tasks");
};

}  // namespace config
}  // namespace loramesher