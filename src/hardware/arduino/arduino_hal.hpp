// src/loramesher/hal/arduino_hal.hpp
#pragma once

#include "config/system_config.hpp"

#ifdef LORAMESHER_BUILD_ARDUINO

#include <SPI.h>

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
};

}  // namespace hal
}  // namespace loramesher

#endif  // LORAMESHER_BUILD_ARDUINO