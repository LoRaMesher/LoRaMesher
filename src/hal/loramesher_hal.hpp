// src/loramesher/hal/loramesher_hal.hpp
#pragma once

#include <cstdint>

#include <RadioLib.h>

// Common interface that works for both platforms
namespace loramesher {
namespace hal {

class LoraMesherHal {
   public:
    virtual ~LoraMesherHal() = default;
    virtual uint32_t millis() = 0;
    virtual void delay(uint32_t ms) = 0;
    // Add other platform-specific functions as needed
};

}  // namespace hal
}  // namespace loramesher