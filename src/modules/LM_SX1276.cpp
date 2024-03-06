#include "LM_SX1276.h"

#ifdef ARDUINO
LM_SX1276::LM_SX1276(uint8_t loraCs, uint8_t loraIrq, uint8_t loraRst, SPIClass* spi) {
    module = new SX1276(new Module(loraCs, loraIrq, loraRst, RADIOLIB_NC, *spi));
}
#else 
LM_SX1276::LM_SX1276(Module* mod) {
    module=new SX1276(mod);
}
#endif

int16_t LM_SX1276::begin(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t syncWord, int8_t power, int16_t preambleLength) {
    return module->begin(freq, bw, sf, cr, syncWord, power, preambleLength);
}

int16_t LM_SX1276::receive(uint8_t* data, size_t len) {
    return module->receive(data, len);
}

int16_t LM_SX1276::startReceive() {
    return module->startReceive();
}

int16_t LM_SX1276::scanChannel() {
    return module->scanChannel();
}

int16_t LM_SX1276::startChannelScan() {
    return module->startChannelScan();
}

int16_t LM_SX1276::standby() {
    return module->standby();
}

void LM_SX1276::reset() {
    module->reset();
}

int16_t LM_SX1276::setCRC(bool crc) {
    return module->setCRC(crc);
}

size_t LM_SX1276::getPacketLength() {
    return module->getPacketLength();
}

float LM_SX1276::getRSSI() {
    return module->getRSSI();
}

float LM_SX1276::getSNR() {
    return module->getSNR();
}

int16_t LM_SX1276::readData(uint8_t* buffer, size_t numBytes) {
    return module->readData(buffer, numBytes);
}

int16_t LM_SX1276::transmit(uint8_t* buffer, size_t length) {
    return module->transmit(buffer, length);
}

uint32_t LM_SX1276::getTimeOnAir(size_t length) {
    return module->getTimeOnAir(length);
}

void LM_SX1276::setDioActionForReceiving(void (*action)()) {
    module->setDio0Action(action, RISING);
}

void LM_SX1276::setDioActionForReceivingTimeout(void(*action)()) {
    module->setDio1Action(action, RISING);
}

void LM_SX1276::setDioActionForScanning(void (*action)()) {
    module->setDio1Action(action, RISING);
}

void LM_SX1276::setDioActionForScanningTimeout(void(*action)()) {
    module->setDio0Action(action, RISING);
}

void LM_SX1276::clearDioActions() {
    module->clearDio0Action();
    module->clearDio1Action();
}

int16_t LM_SX1276::setFrequency(float freq) {
    return module->setFrequency(freq);
}

int16_t LM_SX1276::setBandwidth(float bw) {
    return module->setBandwidth(bw);
}

int16_t LM_SX1276::setSpreadingFactor(uint8_t sf) {
    return module->setSpreadingFactor(sf);
}

int16_t LM_SX1276::setCodingRate(uint8_t cr) {
    return module->setCodingRate(cr);
}

int16_t LM_SX1276::setSyncWord(uint8_t syncWord) {
    return module->setSyncWord(syncWord);
}

int16_t LM_SX1276::setOutputPower(int8_t power) {
    return module->setOutputPower(power);
}

int16_t LM_SX1276::setPreambleLength(int16_t preambleLength) {
    return module->setPreambleLength(preambleLength);
}

int16_t LM_SX1276::setGain(uint8_t gain) {
    return module->setGain(gain);
}

int16_t LM_SX1276::setOutputPower(int8_t power, int8_t useRfo) {
    return module->setOutputPower(power, useRfo);
}