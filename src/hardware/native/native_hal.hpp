
// src/loramesher/hal/natie_hal.hpp
#pragma once

#include "../hal.hpp"
#include "config/system_config.hpp"

#ifdef LORAMESHER_BUILD_NATIVE

namespace loramesher {
namespace hal {

class NativeHal : public IHal {
   public:
    uint32_t millis() override;
    void delay(uint32_t ms) override;
};

}  // namespace hal
}  // namespace loramesher

#endif  // LORAMESHER_BUILD_NATIVE