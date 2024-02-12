#include "LM_SX1278.h"

#ifdef ARDUINO
LM_SX1278::LM_SX1278(uint8_t loraCs, uint8_t loraIrq, uint8_t loraRst, uint8_t loraIo1, SPIClass* spi) {
    module = new SX1278(new Module(loraCs, loraIrq, loraRst, loraIo1, *spi));
}
#else 
LM_SX1278::LM_SX1278(Module* mod) {
    module=new SX1278(mod);
}
#endif

int16_t LM_SX1278::begin(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t syncWord, int8_t power, int16_t preambleLength) {
    return module->begin(freq, bw, sf, cr, syncWord, power, preambleLength);
}

int16_t LM_SX1278::receive(uint8_t* data, size_t len) {
    return module->receive(data, len);
}

int16_t LM_SX1278::startReceive() {
    return module->startReceive();
}

int16_t LM_SX1278::scanChannel() {
    return module->scanChannel();
}

int16_t LM_SX1278::startChannelScan() {
    return module->startChannelScan();
}

int16_t LM_SX1278::standby() {
    return module->standby();
}

void LM_SX1278::reset() {
    module->reset();
}

int16_t LM_SX1278::setCRC(bool crc) {
    return module->setCRC(crc);
}

size_t LM_SX1278::getPacketLength() {
    return module->getPacketLength();
}

float LM_SX1278::getRSSI() {
    return module->getRSSI();
}

float LM_SX1278::getSNR() {
    return module->getSNR();
}

int16_t LM_SX1278::readData(uint8_t* buffer, size_t numBytes) {
    return module->readData(buffer, numBytes);
}

int16_t LM_SX1278::transmit(uint8_t* buffer, size_t length) {
    return module->transmit(buffer, length);
}

uint32_t LM_SX1278::getTimeOnAir(size_t length) {
    return module->getTimeOnAir(length);
}

void LM_SX1278::setDioActionForReceiving(void (*action)()) {
    module->setDio0Action(action, RISING);
}

void LM_SX1278::setDioActionForReceivingTimeout(void(*action)()) {
    module->setDio1Action(action, RISING);
}

void LM_SX1278::setDioActionForScanning(void (*action)()) {
    module->setDio1Action(action, RISING);
}

void LM_SX1278::setDioActionForScanningTimeout(void(*action)()) {
    module->setDio0Action(action, RISING);
}

void LM_SX1278::clearDioActions() {
    module->clearDio0Action();
    module->clearDio1Action();
}

int16_t LM_SX1278::setFrequency(float freq) {
    return module->setFrequency(freq);
}

int16_t LM_SX1278::setBandwidth(float bw) {
    return module->setBandwidth(bw);
}

int16_t LM_SX1278::setSpreadingFactor(uint8_t sf) {
    return module->setSpreadingFactor(sf);
}

int16_t LM_SX1278::setCodingRate(uint8_t cr) {
    return module->setCodingRate(cr);
}

int16_t LM_SX1278::setSyncWord(uint8_t syncWord) {
    return module->setSyncWord(syncWord);
}

int16_t LM_SX1278::setOutputPower(int8_t power) {
    return module->setOutputPower(power);
}

int16_t LM_SX1278::setPreambleLength(int16_t preambleLength) {
    return module->setPreambleLength(preambleLength);
}

int16_t LM_SX1278::setGain(uint8_t gain) {
    return module->setGain(gain);
}

int16_t LM_SX1278::setOutputPower(int8_t power, int8_t useRfo) {
    return module->setOutputPower(power, useRfo);
}