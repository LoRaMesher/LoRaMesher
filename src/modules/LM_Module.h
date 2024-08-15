#pragma once

#include <RadioLib.h>

#include "BuildOptions.h"

class LM_Module {
public:
    virtual ~LM_Module() {}
    virtual int16_t begin(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t syncWord,
        int8_t power, int16_t preambleLength) = 0;

    virtual int16_t receive(uint8_t* data, size_t len) = 0;
    virtual int16_t startReceive() = 0;
    virtual int16_t scanChannel() = 0;
    virtual int16_t startChannelScan() = 0;
    virtual int16_t standby() = 0;
    virtual void reset() = 0;
    virtual int16_t setCRC(bool crc) = 0;
    virtual size_t getPacketLength() = 0;
    virtual float getRSSI() = 0;
    virtual float getSNR() = 0;
    virtual int16_t readData(uint8_t* buffer, size_t numBytes) = 0;
    virtual int16_t transmit(uint8_t* buffer, size_t length) = 0;
    virtual uint32_t getTimeOnAir(size_t length) = 0;

    virtual void setDioActionForReceiving(void (*action)()) = 0;
    virtual void setDioActionForReceivingTimeout(void (*action)()) = 0;
    virtual void setDioActionForScanning(void (*action)()) = 0;
    virtual void setDioActionForScanningTimeout(void (*action)()) = 0;
    virtual void clearDioActions() = 0;

    virtual int16_t setFrequency(float freq) = 0;
    virtual int16_t setBandwidth(float bw) = 0;
    virtual int16_t setSpreadingFactor(uint8_t sf) = 0;
    virtual int16_t setCodingRate(uint8_t cr) = 0;
    virtual int16_t setSyncWord(uint8_t syncWord) = 0;
    virtual int16_t setOutputPower(int8_t power) = 0;
    virtual int16_t setPreambleLength(int16_t preambleLength) = 0;
    virtual int16_t setGain(uint8_t gain) = 0;
    virtual int16_t setOutputPower(int8_t power, int8_t useRfo) = 0;
};