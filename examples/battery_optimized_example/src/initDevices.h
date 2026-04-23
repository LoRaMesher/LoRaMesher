/**
 * @file initDevices.h
 * @brief T-Beam power management interface
 *
 * Provides initialization and sleep control for TTGO T-Beam boards
 * with AXP192 or AXP2101 power management chips.
 */

#ifndef INIT_DEVICES_H
#define INIT_DEVICES_H

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
     * Disables measurements and peripheral power to minimize
     * current draw during sleep. Call before entering light sleep.
     *
     * @return true if sleep preparation succeeded, false if no PMU
     */
    static bool prepareSleep();

   private:
    static bool beginPower();
};

#endif  // INIT_DEVICES_H
