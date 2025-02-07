// src/loramesher/hal/arduino_hal.hpp
#pragma once

#include "build_options.hpp"
#include "../loramesher_hal.hpp"

#ifdef LORAMESHER_BUILD_ARDUINO
#include "Arduino.h"

namespace loramesher {
namespace hal {

class LoraMesherArduinoHal : public LoraMesherHal {
   public:
    uint32_t millis() override;
    void delay(uint32_t ms) override;
};

}  // namespace hal
}  // namespace loramesher

#endif