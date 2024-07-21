#include "LM_SX1280.h"

#ifdef ARDUINO
LM_SX1280::LM_SX1280(uint8_t loraCs, uint8_t loraIrq, uint8_t loraRst, uint8_t loraIo1, SPIClass* spi) {
    module = new SX1280(new Module(loraCs, loraIrq, loraRst, loraIo1, *spi));
}
#else 
LM_SX1280::LM_SX1280(Module* mod) {
    module = new SX1280(mod);
}
#endif

int16_t LM_SX1280::begin(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t syncWord, int8_t power, int16_t preambleLength) {
    return module->begin(freq, bw, sf, cr, syncWord, power, preambleLength);
}

int16_t LM_SX1280::receive(uint8_t* data, size_t len) {
    return module->receive(data, len);
}

int16_t LM_SX1280::startReceive() {
    return module->startReceive();
}

int16_t LM_SX1280::scanChannel() {
    return module->scanChannel();
}

int16_t LM_SX1280::startChannelScan() {
    return module->startChannelScan();
}

int16_t LM_SX1280::standby() {
    return module->standby();
}

void LM_SX1280::reset() {
    module->reset();
}

int16_t LM_SX1280::setCRC(bool crc) {
    return module->setCRC(crc);
}

size_t LM_SX1280::getPacketLength() {
    return module->getPacketLength();
}

float LM_SX1280::getRSSI() {
    return module->getRSSI();
}

float LM_SX1280::getSNR() {
    return module->getSNR();
}

int16_t LM_SX1280::readData(uint8_t* buffer, size_t numBytes) {
    return module->readData(buffer, numBytes);
}

int16_t LM_SX1280::transmit(uint8_t* buffer, size_t length) {
    return module->transmit(buffer, length);
}

uint32_t LM_SX1280::getTimeOnAir(size_t length) {
    return module->getTimeOnAir(length);
}

void LM_SX1280::setDioActionForReceiving(void (*action)()) {
    module->setPacketReceivedAction(action);
}

void LM_SX1280::setDioActionForReceivingTimeout(void(*action)()) {
    module->setDio1Action(action);
}

void LM_SX1280::setDioActionForScanning(void (*action)()) {
    // TODO: Implement
    // module->setDio1Action(action);
}

void LM_SX1280::setDioActionForScanningTimeout(void(*action)()) {
    // TODO: Implement
    // module->setDio0Action(action, RISING);
}

void LM_SX1280::clearDioActions() {
    module->clearDio1Action();
}

int16_t LM_SX1280::setFrequency(float freq) {
    return module->setFrequency(freq);
}

int16_t LM_SX1280::setBandwidth(float bw) {
    return module->setBandwidth(bw);
}

int16_t LM_SX1280::setSpreadingFactor(uint8_t sf) {
    return module->setSpreadingFactor(sf);
}

int16_t LM_SX1280::setCodingRate(uint8_t cr) {
    return module->setCodingRate(cr);
}

int16_t LM_SX1280::setSyncWord(uint8_t syncWord) {
    return module->setSyncWord(syncWord);
}

int16_t LM_SX1280::setOutputPower(int8_t power) {
    return module->setOutputPower(power);
}

int16_t LM_SX1280::setPreambleLength(int16_t preambleLength) {
    return module->setPreambleLength(preambleLength);
}

int16_t LM_SX1280::setGain(uint8_t gain) {
    // TODO: Implement this
    return 0;
}

int16_t LM_SX1280::setOutputPower(int8_t power, int8_t useRfo) {
    return module->setOutputPower(power);
}