#include "LM_SX1262.h"

#ifdef ARDUINO
LM_SX1262::LM_SX1262(uint8_t loraCs, uint8_t loraIrq, uint8_t loraRst, uint8_t loraIo1, SPIClass* spi) {
    module = new SX1262(new Module(loraCs, loraIrq, loraRst, loraIo1, *spi));
}
#else 
LM_SX1262::LM_SX1262(Module* mod) {
    module = new SX1262(mod);
}
#endif

int16_t LM_SX1262::begin(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t syncWord, int8_t power, int16_t preambleLength) {
    int16_t state = module->begin(freq, bw, sf, cr, syncWord, power, preambleLength);
    if (state == -706 || state == -707)
        // tcxoVoltage – TCXO reference voltage to be set on DIO3. Defaults to 1.6 V. 
        // If you are seeing -706/-707 error codes, it likely means you are using non-0 value for module with XTAL. 
        // To use XTAL, either set this value to 0, or set SX126x::XTAL to true.
        state = module->begin(freq, bw, sf, cr, syncWord, power, preambleLength, 0);

    return state;
}

int16_t LM_SX1262::receive(uint8_t* data, size_t len) {
    return module->receive(data, len);
}

int16_t LM_SX1262::startReceive() {
    return module->startReceive();
}

int16_t LM_SX1262::scanChannel() {
    return module->scanChannel();
}

int16_t LM_SX1262::startChannelScan() {
    return module->startChannelScan();
}

int16_t LM_SX1262::standby() {
    return module->standby();
}

void LM_SX1262::reset() {
    module->reset();
}

int16_t LM_SX1262::setCRC(bool crc) {
    if (crc)
        return module->setCRC(2);
    else
        return module->setCRC(0);
}

size_t LM_SX1262::getPacketLength() {
    return module->getPacketLength();
}

float LM_SX1262::getRSSI() {
    return module->getRSSI();
}

float LM_SX1262::getSNR() {
    return module->getSNR();
}

int16_t LM_SX1262::readData(uint8_t* buffer, size_t numBytes) {
    return module->readData(buffer, numBytes);
}

int16_t LM_SX1262::transmit(uint8_t* buffer, size_t length) {
    return module->transmit(buffer, length);
}

uint32_t LM_SX1262::getTimeOnAir(size_t length) {
    return module->getTimeOnAir(length);
}

void LM_SX1262::setDioActionForReceiving(void (*action)()) {
    module->setDio1Action(action);
}

void LM_SX1262::setDioActionForReceivingTimeout(void(*action)()) {
    return;
}

void LM_SX1262::setDioActionForScanning(void(*action)()) {
    return;
}

void LM_SX1262::setDioActionForScanningTimeout(void(*action)()) {
    return;
}

void LM_SX1262::clearDioActions() {
    module->clearDio1Action();
}

int16_t LM_SX1262::setFrequency(float freq) {
    return module->setFrequency(freq);
}

int16_t LM_SX1262::setBandwidth(float bw) {
    return module->setBandwidth(bw);
}

int16_t LM_SX1262::setSpreadingFactor(uint8_t sf) {
    return module->setSpreadingFactor(sf);
}

int16_t LM_SX1262::setCodingRate(uint8_t cr) {
    return module->setCodingRate(cr);
}

int16_t LM_SX1262::setSyncWord(uint8_t syncWord) {
    return module->setSyncWord(syncWord);
}

int16_t LM_SX1262::setOutputPower(int8_t power) {
    return module->setOutputPower(power);
}

int16_t LM_SX1262::setPreambleLength(int16_t preambleLength) {
    return module->setPreambleLength(preambleLength);
}

int16_t LM_SX1262::setGain(uint8_t gain) {
    return 0;
}

int16_t LM_SX1262::setOutputPower(int8_t power, int8_t useRfo) {
    return module->setOutputPower(power);
}