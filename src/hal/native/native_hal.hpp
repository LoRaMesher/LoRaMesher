
// src/loramesher/hal/natie_hal.hpp
#pragma once

#include "build_options.hpp"
#include "../loramesher_hal.hpp"

#ifdef LORAMESHER_BUILD_NATIVE

#include <chrono>
#include <thread>

namespace loramesher {
namespace hal {

class NativeHal : public LoraMesherHal {
   public:
    uint32_t millis() override;
    void delay(uint32_t ms) override;
};

}  // namespace hal
}  // namespace loramesher

#endif