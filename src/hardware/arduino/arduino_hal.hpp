// src/loramesher/hal/arduino_hal.hpp
#pragma once

#ifdef LORAMESHER_BUILD_ARDUINO

#include "../hal.hpp"
#include "build_options.hpp"

namespace loramesher {
namespace hal {

class LoraMesherArduinoHal : public IHal {
   public:
    uint32_t millis() override;
    void delay(uint32_t ms) override;
};

}  // namespace hal
}  // namespace loramesher

#endif  // LORAMESHER_BUILD_ARDUINO