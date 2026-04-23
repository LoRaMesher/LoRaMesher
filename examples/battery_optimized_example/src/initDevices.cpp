/**
 * @file initDevices.cpp
 * @brief T-Beam power management initialization using XPowersLib
 *
 * Supports both AXP192 (T-Beam v1.0/v1.1) and AXP2101 (T-Beam v1.2+)
 * power management chips with automatic detection.
 */

#include "initDevices.h"

#include <Arduino.h>
#include <XPowersLib.h>

#define PMU_IRQ 35

XPowersLibInterface* PMU = nullptr;

void InitDevices::init() {
    Wire.begin(SDA, SCL);
    beginPower();
}

bool InitDevices::beginPower() {
    // Try AXP2101 first (T-Beam v1.2+)
    PMU = new XPowersAXP2101(Wire);
    if (!PMU->init()) {
        delete PMU;
        // Fall back to AXP192 (T-Beam v1.0/v1.1)
        PMU = new XPowersAXP192(Wire);
        if (!PMU->init()) {
            delete PMU;
            PMU = nullptr;
            Serial.println("Error: No PMU found");
            return false;
        }
    }

    Serial.printf(
        "PMU: %s initialized\n",
        PMU->getChipModel() == XPOWERS_AXP2101 ? "AXP2101" : "AXP192");

    // Common settings
    PMU->setChargingLedMode(XPOWERS_CHG_LED_OFF);
    pinMode(PMU_IRQ, INPUT_PULLUP);

    // Configure power channels based on chip type
    if (PMU->getChipModel() == XPOWERS_AXP192) {
        // T-Beam v1.0/v1.1 power mapping:
        //   LDO2  = LoRa radio (SX1276/SX1262)
        //   LDO3  = GPS module
        //   DCDC1 = OLED display
        //   DCDC3 = ESP32 (protected)
        PMU->setPowerChannelVoltage(XPOWERS_LDO2, 3300);
        PMU->enablePowerOutput(XPOWERS_LDO2);
        PMU->setPowerChannelVoltage(XPOWERS_LDO3, 3300);
        PMU->enablePowerOutput(XPOWERS_LDO3);
        PMU->setPowerChannelVoltage(XPOWERS_DCDC1, 3300);
        PMU->enablePowerOutput(XPOWERS_DCDC1);
        PMU->setProtectedChannel(XPOWERS_DCDC1);
        PMU->setProtectedChannel(XPOWERS_DCDC3);

        // Disable unused channel
        PMU->disablePowerOutput(XPOWERS_DCDC2);

        // Configure interrupts
        PMU->disableIRQ(XPOWERS_AXP192_ALL_IRQ);
        PMU->enableIRQ(
            XPOWERS_AXP192_VBUS_REMOVE_IRQ | XPOWERS_AXP192_VBUS_INSERT_IRQ |
            XPOWERS_AXP192_BAT_CHG_DONE_IRQ | XPOWERS_AXP192_BAT_CHG_START_IRQ |
            XPOWERS_AXP192_BAT_REMOVE_IRQ | XPOWERS_AXP192_BAT_INSERT_IRQ |
            XPOWERS_AXP192_PKEY_SHORT_IRQ);
    } else if (PMU->getChipModel() == XPOWERS_AXP2101) {
        // T-Beam v1.2+ power mapping:
        //   ALDO2   = LoRa radio
        //   ALDO3   = GPS module
        //   VBACKUP = GPS RTC backup
        //   DCDC1   = ESP32 (protected)
        PMU->setPowerChannelVoltage(XPOWERS_ALDO2, 3300);
        PMU->enablePowerOutput(XPOWERS_ALDO2);
        PMU->setPowerChannelVoltage(XPOWERS_ALDO3, 3300);
        PMU->enablePowerOutput(XPOWERS_ALDO3);
        PMU->setPowerChannelVoltage(XPOWERS_VBACKUP, 3300);
        PMU->enablePowerOutput(XPOWERS_VBACKUP);
        PMU->setProtectedChannel(XPOWERS_DCDC1);

        // Disable unused channels
        PMU->disablePowerOutput(XPOWERS_DCDC2);
        PMU->disablePowerOutput(XPOWERS_DCDC3);
        PMU->disablePowerOutput(XPOWERS_DCDC4);
        PMU->disablePowerOutput(XPOWERS_DCDC5);
        PMU->disablePowerOutput(XPOWERS_ALDO1);
        PMU->disablePowerOutput(XPOWERS_ALDO4);
        PMU->disablePowerOutput(XPOWERS_BLDO1);
        PMU->disablePowerOutput(XPOWERS_BLDO2);
        PMU->disablePowerOutput(XPOWERS_DLDO1);
        PMU->disablePowerOutput(XPOWERS_DLDO2);
    }

    // Enable battery voltage monitoring
    PMU->enableBattVoltageMeasure();

    // Set power button hold time for shutdown
    PMU->setPowerKeyPressOffTime(XPOWERS_POWEROFF_4S);

    return true;
}

bool InitDevices::prepareSleep() {
    if (!PMU) {
        return false;
    }

    // Disable measurements to save power
    PMU->disableTemperatureMeasure();
    PMU->disableBattDetection();
    PMU->disableVbusVoltageMeasure();
    PMU->disableBattVoltageMeasure();
    PMU->disableSystemVoltageMeasure();

    PMU->enableSleep();

    // Disable peripherals based on chip type
    if (PMU->getChipModel() == XPOWERS_AXP192) {
        PMU->disablePowerOutput(XPOWERS_LDO2);  // LoRa off
        PMU->disablePowerOutput(XPOWERS_LDO3);  // GPS off
        PMU->disablePowerOutput(XPOWERS_DCDC2);
    } else {
        PMU->disablePowerOutput(XPOWERS_ALDO2);  // LoRa off
        PMU->disablePowerOutput(XPOWERS_ALDO3);  // GPS off
    }

    PMU->clearIrqStatus();
    return true;
}
