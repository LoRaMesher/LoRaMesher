
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

    /**
     * @brief Get platform-specific hardware unique identifier (native/testing implementation)
     *
     * @param id_buffer Buffer to store the unique ID (must be at least 6 bytes)
     * @param buffer_size Size of the provided buffer
     * @return bool True if unique ID was successfully retrieved
     */
    bool GetHardwareUniqueId(uint8_t* id_buffer, size_t buffer_size) override {
        if (!id_buffer || buffer_size < 6) {
            return false;
        }

        // Generate a simple test unique ID for native builds
        // This is deterministic but different for each run for testing purposes
        static bool initialized = false;
        static uint8_t cached_id[6];

        if (!initialized) {
            auto now = std::chrono::steady_clock::now();
            auto time_count = now.time_since_epoch().count();

            cached_id[0] = 0x02;  // Locally administered MAC prefix
            cached_id[1] = 0x4E;  // "Native" identifier
            cached_id[2] = static_cast<uint8_t>((time_count >> 24) & 0xFF);
            cached_id[3] = static_cast<uint8_t>((time_count >> 16) & 0xFF);
            cached_id[4] = static_cast<uint8_t>((time_count >> 8) & 0xFF);
            cached_id[5] = static_cast<uint8_t>(time_count & 0xFF);

            initialized = true;
        }

        // Copy to output buffer
        for (size_t i = 0; i < 6 && i < buffer_size; i++) {
            id_buffer[i] = cached_id[i];
        }

        return true;
    }
};

}  // namespace hal
}  // namespace loramesher

#endif  // LORAMESHER_BUILD_NATIVE