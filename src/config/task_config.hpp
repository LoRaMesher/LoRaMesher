// src/config/task_config.hpp
#pragma once

#include "system_config.hpp"

namespace loramesher {
namespace config {

/**
 * @brief Configuration for FreeRTOS task stack sizes.
 *
 * Every value here is in bytes. The RTOS layer converts to the port's
 * stack-depth unit.
 */
struct TaskConfig {
    /// Minimum reasonable usable stack size (bytes).
    static constexpr size_t kMinStackWatermark = 512;

    /// Periodic-monitor warning threshold (bytes free).
    static constexpr size_t kStackWarnBytes = 1024;

    /// Radio event task. Measured peak ~2.4 KB at the heaviest tested
    /// radio-IRQ load.
    static constexpr size_t kRadioEventStackSize = 3280;

    /// Main protocol task. Measured peak ~1.75 KB during single-node boot.
    static constexpr size_t kProtocolMainStackSize = 4096;

    /// Superframe update task. Measured peak ~1.8 KB.
    static constexpr size_t kSuperframeStackSize = 3072;

    /// PingPong message-processing task.
    static constexpr size_t kPingPongProcessStackSize = 2048;

    /// PingPong timeout task.
    static constexpr size_t kPingPongTimeoutStackSize = 2048;
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