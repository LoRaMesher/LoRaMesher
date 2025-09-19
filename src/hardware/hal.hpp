// src/loramesher/hal/hal.hpp
#pragma once

#include <cstdint>

// Forward declaration of SPIClass to avoid including platform-specific headers here
class SPIClass;

namespace loramesher {
namespace hal {

/**
 * @brief Interface for LoraMesher hardware abstraction layer.
 */
class IHal {
   public:
    /**
     * @brief Default constructor.
     */
    IHal() = default;

    /**
     * @brief Virtual destructor.
     */
    virtual ~IHal() = default;

    /**
     * @brief Get the current time in milliseconds.
     * 
     * @return Current time in milliseconds.
     */
    virtual uint32_t millis() = 0;

    /**
     * @brief Delay execution for a specified number of milliseconds.
     * 
     * @param ms Number of milliseconds to delay.
     */
    virtual void delay(uint32_t ms) = 0;

    /**
     * @brief Get an instance of SPIClass for communication.
     *
     * This method provides platform-independent access to SPI functionality.
     * Implementations should handle the differences between hardware and
     * simulated environments.
     *
     * @param spiNum The SPI bus number (0 for primary SPI, 1-2 for additional buses if available)
     * @return Reference to an SPIClass instance.
     */
    virtual SPIClass& getSPI(uint8_t spiNum = 0) = 0;

    /**
     * @brief Get platform-specific hardware unique identifier
     *
     * This method provides access to hardware-based unique identifiers
     * such as MAC addresses, chip IDs, or other platform-specific unique values.
     * Used for automatic device address generation.
     *
     * @param id_buffer Buffer to store the unique ID (must be at least 6 bytes)
     * @param buffer_size Size of the provided buffer
     * @return bool True if unique ID was successfully retrieved
     */
    virtual bool GetHardwareUniqueId(uint8_t* id_buffer,
                                     size_t buffer_size) = 0;
};

}  // namespace hal
}  // namespace loramesher