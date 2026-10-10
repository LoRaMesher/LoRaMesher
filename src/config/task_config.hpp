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

    /// Radio event task. Measured peak ~2.7 KB on a 13-node ESP32 network.
    static constexpr size_t kRadioEventStackSize = 4096;

    /// Main protocol task. It also runs the application data callbacks.
    /// Measured peak ~3.6 KB on a 13-node ESP32 network with data traffic.
    static constexpr size_t kProtocolMainStackSize = 6144;

    /// Superframe update task. Measured peak ~1.9 KB on a 13-node ESP32
    /// network.
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