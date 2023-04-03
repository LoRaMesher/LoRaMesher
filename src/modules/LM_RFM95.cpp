#include "LM_RFM95.h"

LM_RFM95::LM_RFM95() {
    module = new RFM95(new Module(LORA_CS, LORA_IRQ, LORA_RST));
}

int16_t LM_RFM95::begin(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t syncWord, int8_t power, int16_t preambleLength) {
    return module->begin(freq, bw, sf, cr, syncWord, power, preambleLength);
}

int16_t LM_RFM95::receive(uint8_t* data, size_t len) {
    return module->receive(data, len);
}

int16_t LM_RFM95::startReceive() {
    return module->startReceive();
}

int16_t LM_RFM95::scanChannel() {
    return module->scanChannel();
}

int16_t LM_RFM95::startChannelScan() {
    return module->startChannelScan();
}

int16_t LM_RFM95::standby() {
    return module->standby();
}

void LM_RFM95::reset() {
    module->reset();
}

int16_t LM_RFM95::setCRC(bool crc) {
    return module->setCRC(crc);
}

size_t LM_RFM95::getPacketLength() {
    return module->getPacketLength();
}

float LM_RFM95::getRSSI() {
    return module->getRSSI();
}

float LM_RFM95::getSNR() {
    return module->getSNR();
}

int16_t LM_RFM95::readData(uint8_t* buffer, size_t numBytes) {
    return module->readData(buffer, numBytes);
}

int16_t LM_RFM95::transmit(uint8_t* buffer, size_t length) {
    return module->transmit(buffer, length);
}

uint32_t LM_RFM95::getTimeOnAir(size_t length) {
    return module->getTimeOnAir(length);
}

void LM_RFM95::setDioActionForReceiving(void (*action)()) {
    module->setDio0Action(action);
}

void LM_RFM95::setDioActionForReceivingTimeout(void(*action)()) {
    module->setDio1Action(action);
}

void LM_RFM95::setDioActionForScanning(void (*action)()) {
    module->setDio1Action(action);
}

void LM_RFM95::setDioActionForScanningTimeout(void(*action)()) {
    module->setDio0Action(action);
}

void LM_RFM95::clearDioActions() {
    module->clearDio0Action();
    module->clearDio1Action();
}

int16_t LM_RFM95::setFrequency(float freq) {
    return module->setFrequency(freq);
}

int16_t LM_RFM95::setBandwidth(float bw) {
    return module->setBandwidth(bw);
}

int16_t LM_RFM95::setSpreadingFactor(uint8_t sf) {
    return module->setSpreadingFactor(sf);
}

int16_t LM_RFM95::setCodingRate(uint8_t cr) {
    return module->setCodingRate(cr);
}

int16_t LM_RFM95::setSyncWord(uint8_t syncWord) {
    return module->setSyncWord(syncWord);
}

int16_t LM_RFM95::setOutputPower(int8_t power) {
    return module->setOutputPower(power);
}

int16_t LM_RFM95::setPreambleLength(int16_t preambleLength) {
    return module->setPreambleLength(preambleLength);
}

int16_t LM_RFM95::setGain(uint8_t gain) {
    return module->setGain(gain);
}

int16_t LM_RFM95::setOutputPower(int8_t power, int8_t useRfo) {
    return module->setOutputPower(power, useRfo);
}


