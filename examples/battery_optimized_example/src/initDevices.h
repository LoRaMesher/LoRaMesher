/**
 * @file initDevices.h
 * @brief T-Beam power management interface
 *
 * Provides initialization and sleep control for TTGO T-Beam boards
 * with AXP192 or AXP2101 power management chips.
 */

#ifndef INIT_DEVICES_H
#define INIT_DEVICES_H

#include <cstdint>

class InitDevices {
   public:
    /**
     * @brief Initialize the T-Beam power management unit
     *
     * Detects and configures the PMU (AXP192 or AXP2101),
     * enabling power to LoRa, GPS, and other peripherals.
     */
    static void init();

    /**
     * @brief Prepare the device for light sleep
     *
     * Disables PMU measurements and, for long sleeps, the GPS rail. The LoRa
     * rail stays on: the protocol puts the radio into its own sleep mode.
     *
     * @param sleep_ms Planned sleep duration in milliseconds
     * @return true if sleep preparation succeeded, false if no PMU
     */
    static bool prepareSleep(uint32_t sleep_ms);

    /**
     * @brief Undo prepareSleep() after waking up
     */
    static void wakeUp();

   private:
    static bool beginPower();
};

#endif  // INIT_DEVICES_H
