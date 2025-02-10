
// src/loramesher/hal/natie_hal.hpp
#pragma once

#include "../loramesher_hal.hpp"
#include "build_options.hpp"

#ifdef LORAMESHER_BUILD_NATIVE

#include <chrono>
#include <thread>

namespace loramesher {
namespace hal {

class NativeHal : public ILoraMesherHal {
   public:
    uint32_t millis() override;
    void delay(uint32_t ms) override;
};

}  // namespace hal
}  // namespace loramesher

#endif