// src/loramesher/hal/hal.hpp
#pragma once

#include <cstdint>

namespace loramesher {
namespace hal {

/**
 * @brief Interface for LoraMesher hardware abstraction layer.
 */
class IHardwareManager {
   public:
    /**
     * @brief Default constructor.
     */
    IHardwareManager() = default;

    /**
     * @brief Virtual destructor.
     */
    virtual ~IHardwareManager() = default;

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
};

}  // namespace hal
}  // namespace loramesher