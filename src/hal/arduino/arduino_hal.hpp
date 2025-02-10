// src/loramesher/hal/arduino_hal.hpp
#pragma once

#include "../loramesher_hal.hpp"
#include "build_options.hpp"

#ifdef LORAMESHER_BUILD_ARDUINO
#include "Arduino.h"

namespace loramesher {
namespace hal {

class LoraMesherArduinoHal : public ILoraMesherHal {
   public:
    uint32_t millis() override;
    void delay(uint32_t ms) override;
};

}  // namespace hal
}  // namespace loramesher

#endif