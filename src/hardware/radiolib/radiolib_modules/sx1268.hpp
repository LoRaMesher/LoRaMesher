// src/hardware/radiolib/radiolib_modules/sx1268.hpp
#pragma once

#include "config/system_config.hpp"

#ifdef LORAMESHER_BUILD_ARDUINO

#include "radiolib_module_base.hpp"

namespace loramesher {
namespace radio {

/**
 * @brief IRadio implementation for the Semtech SX1268 (SX126x family)
 *
 * Functionally identical control flow to the SX1262; differs only in the
 * RadioLib driver type and recommended current-limit profile. All behavior
 * lives in RadioLibSx126xModule / RadioLibModuleBase.
 *
 * @see RadioLibModuleBase
 */
class LoraMesherSX1268 final : public RadioLibSx126xModule<SX1268> {
   public:
    LoraMesherSX1268(int8_t cs_pin, int8_t irq_pin, int8_t reset_pin,
                     int8_t busy_pin, SPIClass& spi)
        : RadioLibSx126xModule<SX1268>(cs_pin, irq_pin, reset_pin, busy_pin,
                                       spi, RadioType::kSx1268,
                                       "LoraMesherSX1268") {}
};

}  // namespace radio
}  // namespace loramesher

#endif  // LORAMESHER_BUILD_ARDUINO
