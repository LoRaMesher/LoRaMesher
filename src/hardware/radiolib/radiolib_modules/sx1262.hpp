// src/hardware/radiolib/radiolib_modules/sx1262.hpp
#pragma once

#include "config/system_config.hpp"

#ifdef LORAMESHER_BUILD_ARDUINO

#include "radiolib_module_base.hpp"

namespace loramesher {
namespace radio {

/**
 * @brief IRadio implementation for the Semtech SX1262 (SX126x family)
 *
 * Lower power consumption and up to +22 dBm TX power. Requires a BUSY pin in
 * addition to CS, IRQ (DIO1), and RESET. All behavior lives in
 * RadioLibSx126xModule / RadioLibModuleBase; this leaf supplies only identity.
 *
 * @see RadioLibModuleBase
 */
class LoraMesherSX1262 final : public RadioLibSx126xModule<SX1262> {
   public:
    LoraMesherSX1262(int8_t cs_pin, int8_t irq_pin, int8_t reset_pin,
                     int8_t busy_pin, SPIClass& spi)
        : RadioLibSx126xModule<SX1262>(cs_pin, irq_pin, reset_pin, busy_pin,
                                       spi, RadioType::kSx1262,
                                       "LoraMesherSX1262") {}
};

}  // namespace radio
}  // namespace loramesher

#endif  // LORAMESHER_BUILD_ARDUINO
