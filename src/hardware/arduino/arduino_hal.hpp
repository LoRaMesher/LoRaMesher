// src/loramesher/hal/arduino_hal.hpp
#pragma once

#include "config/system_config.hpp"

#ifdef LORAMESHER_BUILD_ARDUINO

#include <SPI.h>

#ifdef ARDUINO_ARCH_ESP32
#include <WiFi.h>
#include <esp_system.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#endif

#include "../hal.hpp"

namespace loramesher {
namespace hal {

/**
 * @brief Hardware abstraction layer implementation for Arduino.
 */
class LoraMesherArduinoHal : public IHal {
   public:
    /**
     * @brief Default constructor.
     */
    LoraMesherArduinoHal() = default;

    /**
     * @brief Get the current time in milliseconds.
     * 
     * @return Current time in milliseconds from Arduino millis().
     */
    uint32_t millis() override { return ::millis(); }

    /**
     * @brief Delay execution for a specified number of milliseconds.
     * 
     * @param ms Number of milliseconds to delay using Arduino delay().
     */
    void delay(uint32_t ms) override { ::delay(ms); }

    /**
     * @brief Get the Arduino SPIClass instance for the specified SPI bus.
     *
     * @param spiNum SPI bus number (0-2 on ESP32)
     * @return Reference to an SPIClass instance.
     */
    SPIClass& getSPI(uint8_t spiNum = 0) override {
        switch (spiNum) {
            default:
                return SPI;
        }
    }

    /**
     * @brief Get platform-specific hardware unique identifier
     *
     * @param id_buffer Buffer to store the unique ID (must be at least 6 bytes)
     * @param buffer_size Size of the provided buffer
     * @return bool True if unique ID was successfully retrieved
     */
    bool GetHardwareUniqueId(uint8_t* id_buffer, size_t buffer_size) override {
        if (!id_buffer || buffer_size < 6) {
            return false;
        }

#ifdef ARDUINO_ARCH_ESP32
        // Use ESP32 eFuse MAC address
        uint64_t efuse_mac = ESP.getEfuseMac();

        // Convert 64-bit MAC to 6-byte array (lower 48 bits)
        for (int i = 0; i < 6; i++) {
            id_buffer[i] = static_cast<uint8_t>((efuse_mac >> (i * 8)) & 0xFF);
        }
        return true;

#elif defined(ESP8266)
        // Use ESP8266 WiFi MAC address
        uint8_t mac[6];
        WiFi.macAddress(mac);

        // Copy MAC to output buffer
        for (int i = 0; i < 6; i++) {
            id_buffer[i] = mac[i];
        }
        return true;

#else
        // Fallback for other Arduino platforms
        // Generate a simple pseudo-unique ID
        id_buffer[0] = 0x02;  // Locally administered MAC prefix
        id_buffer[1] = 0x41;  // "Arduino" identifier

        // Use millis() and some pseudo-random values
        uint32_t time_val = millis();
        id_buffer[2] = static_cast<uint8_t>((time_val >> 24) & 0xFF);
        id_buffer[3] = static_cast<uint8_t>((time_val >> 16) & 0xFF);
        id_buffer[4] = static_cast<uint8_t>((time_val >> 8) & 0xFF);
        id_buffer[5] = static_cast<uint8_t>(time_val & 0xFF);

        return true;
#endif
    }
};

}  // namespace hal
}  // namespace loramesher

#endif  // LORAMESHER_BUILD_ARDUINO