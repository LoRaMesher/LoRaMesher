// src/hardware/radiolib/radiolib_modules/sx1278.hpp
#pragma once

#include "config/system_config.hpp"

#ifdef LORAMESHER_BUILD_ARDUINO

#include "radiolib_module_base.hpp"

namespace loramesher {
namespace radio {

/**
 * @brief IRadio implementation for the Semtech SX1278 (SX127x family)
 *
 * Functionally identical control flow to the SX1276; differs only in the
 * RadioLib driver type and recommended current-limit profile. All behavior
 * lives in RadioLibSx127xModule / RadioLibModuleBase.
 *
 * @see RadioLibModuleBase
 */
class LoraMesherSX1278 final : public RadioLibSx127xModule<SX1278> {
   public:
    LoraMesherSX1278(int8_t cs_pin, int8_t irq_pin, int8_t reset_pin,
                     int8_t busy_pin, SPIClass& spi)
        : RadioLibSx127xModule<SX1278>(cs_pin, irq_pin, reset_pin, busy_pin,
                                       spi, RadioType::kSx1278,
                                       "LoraMesherSX1278") {}
};

}  // namespace radio
}  // namespace loramesher

#endif  // LORAMESHER_BUILD_ARDUINO
