#pragma once

#include <RadioLib.h>

#include "LM_SX1276.h"

/**
 * @brief LM_RFM95 class
 *
 * This class is a wrapper for the LM_SX1276 class, specifically for the RFM95 module.
 * It inherits all methods and properties from LM_SX1276.
 */
class LM_RFM95 : public LM_SX1276 {
public:
#ifdef ARDUINO
    LM_RFM95(
        uint8_t loraCs = 0, // LoRa chip select pin
        uint8_t loraIrq = 0, // LoRa IRQ pin
        uint8_t loraRst = 0, // LoRa reset pin
        SPIClass* spi = &SPI // SPI interface
    ) : LM_SX1276(loraCs, loraIrq, loraRst, spi) {}
#else
    LM_RFM95(Module* module) : LM_SX1276(module) {}
#endif
};