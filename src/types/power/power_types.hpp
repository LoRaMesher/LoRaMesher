/**
 * @file power_types.hpp
 * @brief Power management types and callbacks for user-provided sleep control
 *
 * This file defines the types used for power management callbacks that allow
 * users to implement device-specific sleep behavior without adding hardware
 * dependencies to the library.
 *
 * The power management system integrates with the TDMA slot structure:
 * - During SLEEP slots, the PrepareSleepCallback is invoked to allow
 *   application-level preparation (disable peripherals, configure PMU, etc.)
 * - When transitioning from a SLEEP slot to an active slot, the WakeUpCallback
 *   is invoked to restore application state
 *
 * Example usage:
 * @code
 * auto mesher = LoraMesher::Builder()
 *     .withLoRaMeshProtocol()
 *     .withPrepareSleepCallback([](const power::SleepContext& ctx) {
 *         // Disable peripherals, configure PMU
 *         disableGPS();
 *         disableSensors();
 *
 *         // Optionally veto sleep if critical operation in progress
 *         if (hasUrgentData()) {
 *             return power::SleepResult{false};  // Veto sleep
 *         }
 *         return power::SleepResult{true};  // Allow sleep
 *     })
 *     .withWakeUpCallback([](power::PowerState previous) {
 *         // Restore peripherals after sleep
 *         enableGPS();
 *         enableSensors();
 *     })
 *     .Build();
 * @endcode
 */

#pragma once

#include <cstdint>
#include <functional>
#include <limits>

namespace loramesher {
namespace power {

/**
 * @brief Power states for the system
 *
 * These states represent the power consumption levels of the device during
 * different phases of TDMA operation.
 */
enum class PowerState : uint8_t {
    ACTIVE,       ///< Full operation - all systems active (TX/RX slots)
    RADIO_SLEEP,  ///< Radio off, MCU active - used between operations
    LIGHT_SLEEP,  ///< Radio + user peripherals off, MCU maintains timing (SLEEP slots)
};

/**
 * @brief Context information provided to the sleep callback
 *
 * Contains all information the user needs to decide whether and how to
 * enter sleep mode. This allows intelligent power management decisions
 * based on network state.
 */
struct SleepContext {
    /**
     * @brief The power state being requested (typically LIGHT_SLEEP)
     */
    PowerState requested_state;

    /**
     * @brief How long until the next scheduled activity (ms)
     *
     * This is the slot duration - the maximum time the device can sleep
     * before the next slot transition. User callbacks can use this to
     * decide whether sleep is worthwhile for short durations.
     */
    uint32_t sleep_duration_ms;

    /**
     * @brief Current slot number in the superframe (0-based)
     *
     * Useful for slot-specific behavior (e.g., always stay awake on
     * certain slots for external communication).
     */
    uint16_t current_slot;

    /**
     * @brief Whether there are messages waiting to be sent
     *
     * If true, the device may want to stay in a higher power state
     * to transmit pending messages sooner.
     */
    bool has_pending_messages;
};

/**
 * @brief Result returned from the prepare sleep callback
 *
 * Allows the user to veto sleep or adjust the maximum sleep duration.
 * This provides fine-grained control over power management.
 */
struct SleepResult {
    /**
     * @brief Set to false to veto sleep
     *
     * When false, the radio will still be set to sleep mode (to save power),
     * but the current_power_state_ will remain ACTIVE, and peripherals
     * will not be disabled. The wake-up callback will not be invoked on
     * the next slot transition.
     */
    bool allow_sleep = true;

    /**
     * @brief Maximum allowed sleep time (ms)
     *
     * Reserved for future use with advanced power management modes.
     * Currently informational only.
     */
    uint32_t max_sleep_duration_ms = std::numeric_limits<uint32_t>::max();
};

/**
 * @brief Callback type for preparing to enter sleep mode
 *
 * This callback is invoked before the system enters a SLEEP slot in the
 * TDMA superframe. The user can perform device-specific power management
 * operations and optionally veto the sleep request.
 *
 * @param context Information about the sleep request including duration,
 *                current slot, and pending message status
 * @return SleepResult indicating whether to allow sleep and max duration
 *
 * @note  This callback should be fast — avoid blocking operations.
 *        The callback runs in the protocol task context.
 * @note  Returning SleepResult{true} causes the protocol to:
 *        1. Put the radio to sleep.
 *        2. Put the MCU to light sleep until the next slot (ESP32 only).
 *        Use this callback to power down user peripherals (GPS, sensors) before sleep.
 * @note  Returning SleepResult{false} vetoes MCU sleep. The radio still sleeps for
 *        power savings, but the MCU stays running and OnWakeUp will not fire.
 * @warning Do not perform lengthy operations in this callback as it
 *          will delay slot processing and may cause timing issues
 */
using PrepareSleepCallback = std::function<SleepResult(const SleepContext&)>;

/**
 * @brief Callback type for waking up from sleep mode
 *
 * This callback is invoked when transitioning from a SLEEP slot to an
 * active slot (TX, RX, CONTROL_*, SYNC_*, DISCOVERY_*). The user can
 * restore peripherals and resume normal operation.
 *
 * @param previous_state The power state we are waking from (typically LIGHT_SLEEP)
 *
 * @note This callback should be fast - avoid blocking operations
 * @note The callback runs in the protocol task context
 * @note Only invoked if the device actually entered sleep (prepare callback
 *       returned allow_sleep=true)
 *
 * @warning Do not perform lengthy operations in this callback as it
 *          will delay slot processing and may cause timing issues
 */
using WakeUpCallback = std::function<void(PowerState previous_state)>;

}  // namespace power
}  // namespace loramesher
