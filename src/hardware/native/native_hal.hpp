
// src/loramesher/hal/natie_hal.hpp
#pragma once

#include "../hal.hpp"
#include "config/system_config.hpp"

#ifdef LORAMESHER_BUILD_NATIVE
#include <chrono>
#include <thread>

#include "hardware/SPIMock.hpp"  // Include the SPI mock implementation

namespace loramesher {
namespace hal {

/**
 * @brief Hardware abstraction layer implementation for native platform.
 */
class NativeHal : public IHal {
   public:
    /**
     * @brief Default constructor.
     */
    NativeHal() = default;

    /**
     * @brief Get the current time in milliseconds.
     * 
     * @return Current time in milliseconds using std::chrono.
     */
    uint32_t millis() override {
        auto now = std::chrono::steady_clock::now();
        return static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch())
                .count());
    }

    /**
     * @brief Delay execution for a specified number of milliseconds.
     * 
     * @param ms Number of milliseconds to delay using std::this_thread::sleep_for.
     */
    void delay(uint32_t ms) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }

    /**
     * @brief Get the mocked SPIClass instance for the specified SPI bus.
     * 
     * @param spiNum SPI bus number (0-2)
     * @return Reference to a mocked SPIClass instance.
     */
    SPIClass& getSPI(uint8_t spiNum = 0) override {
        switch (spiNum) {
            case 1:
                return SPI1;
            case 2:
                return SPI2;
            case 0:
            default:
                return SPI;
        }
    }
};

}  // namespace hal
}  // namespace loramesher

#endif  // LORAMESHER_BUILD_NATIVE