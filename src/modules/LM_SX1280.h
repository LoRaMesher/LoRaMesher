#pragma once

#include <RadioLib.h>

#include "LM_Module.h"

class LM_SX1280: public LM_Module {
public:
#ifdef ARDUINO
    LM_SX1280(
        uint8_t loraCs = 0, // LoRa chip select pin
        uint8_t loraIrq = 0, // LoRa IRQ pin
        uint8_t loraRst = 0, // LoRa reset pin
        uint8_t loraIo1 = 0, // LoRa DIO1 pin 
        SPIClass* spi = &SPI // SPI interface
    );
#else
    LM_SX1280(Module* module);
#endif

    int16_t begin(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t syncWord,
        int8_t power, int16_t preambleLength) override;

    int16_t receive(uint8_t* data, size_t len) override;
    int16_t startReceive() override;
    int16_t scanChannel() override;
    int16_t startChannelScan() override;
    int16_t standby() override;
    void reset() override;
    int16_t setCRC(bool crc) override;
    size_t getPacketLength() override;
    float getRSSI() override;
    float getSNR() override;
    int16_t readData(uint8_t* buffer, size_t numBytes) override;
    int16_t transmit(uint8_t* buffer, size_t length) override;
    uint32_t getTimeOnAir(size_t length) override;

    void setDioActionForReceiving(void (*action)()) override;
    void setDioActionForReceivingTimeout(void (*action)()) override;
    void setDioActionForScanning(void (*action)()) override;
    void setDioActionForScanningTimeout(void (*action)()) override;
    void clearDioActions() override;

    int16_t setFrequency(float freq) override;
    int16_t setBandwidth(float bw) override;
    int16_t setSpreadingFactor(uint8_t sf) override;
    int16_t setCodingRate(uint8_t cr) override;
    int16_t setSyncWord(uint8_t syncWord) override;
    int16_t setOutputPower(int8_t power) override;
    int16_t setPreambleLength(int16_t preambleLength) override;
    int16_t setGain(uint8_t gain) override;
    int16_t setOutputPower(int8_t power, int8_t useRfo) override;

private:

    /**
    * @brief RadioLib SX1280 Module
    *
    */
    SX1280* module;
};