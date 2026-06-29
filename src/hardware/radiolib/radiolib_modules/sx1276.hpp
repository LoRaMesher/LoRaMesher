// src/hardware/radiolib/radiolib_modules/sx1276.hpp
#pragma once

#include "config/system_config.hpp"

#ifdef LORAMESHER_BUILD_ARDUINO

#include "radiolib_module_base.hpp"

namespace loramesher {
namespace radio {

/**
 * @brief IRadio implementation for the Semtech SX1276 (SX127x family)
 *
 * Classic sub-GHz LoRa transceiver. All behavior lives in
 * RadioLibSx127xModule / RadioLibModuleBase; this leaf supplies only identity.
 *
 * @see RadioLibModuleBase
 */
class LoraMesherSX1276 final : public RadioLibSx127xModule<SX1276> {
   public:
    LoraMesherSX1276(int8_t cs_pin, int8_t irq_pin, int8_t reset_pin,
                     int8_t busy_pin, SPIClass& spi)
        : RadioLibSx127xModule<SX1276>(cs_pin, irq_pin, reset_pin, busy_pin,
                                       spi, RadioType::kSx1276,
                                       "LoraMesherSX1276") {}
};

}  // namespace radio
}  // namespace loramesher

#endif  // LORAMESHER_BUILD_ARDUINO
