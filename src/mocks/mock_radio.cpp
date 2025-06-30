#include "mocks/mock_radio.hpp"

#include "../test/utils/mock_radio.hpp"

namespace loramesher {
namespace radio {

// Implementation class using the PIMPL idiom to hide test::MockRadio
class MockRadio::Impl {
   public:
    test::MockRadio mock;
};

// Constructor
MockRadio::MockRadio() : pimpl_(std::make_unique<Impl>()) {}

// Destructor needs to be defined here where Impl is complete
MockRadio::~MockRadio() = default;

// Forward all IRadio methods to the test mock
Result MockRadio::Configure(const RadioConfig& config) {
    return pimpl_->mock.Configure(config);
}

Result MockRadio::Begin(const RadioConfig& config) {
    return pimpl_->mock.Begin(config);
}

Result MockRadio::Send(const uint8_t* data, size_t len) {
    return pimpl_->mock.Send(data, len);
}

Result MockRadio::readData(uint8_t* data, size_t len) {
    return pimpl_->mock.readData(data, len);
}

Result MockRadio::StartReceive() {
    return pimpl_->mock.StartReceive();
}

Result MockRadio::Sleep() {
    return pimpl_->mock.Sleep();
}

Result MockRadio::setFrequency(float frequency) {
    return pimpl_->mock.setFrequency(frequency);
}

Result MockRadio::setSpreadingFactor(uint8_t sf) {
    return pimpl_->mock.setSpreadingFactor(sf);
}

Result MockRadio::setBandwidth(float bandwidth) {
    return pimpl_->mock.setBandwidth(bandwidth);
}

Result MockRadio::setCodingRate(uint8_t codingRate) {
    return pimpl_->mock.setCodingRate(codingRate);
}

Result MockRadio::setPower(int8_t power) {
    return pimpl_->mock.setPower(power);
}

Result MockRadio::setSyncWord(uint8_t syncWord) {
    return pimpl_->mock.setSyncWord(syncWord);
}

Result MockRadio::setCRC(bool enable) {
    return pimpl_->mock.setCRC(enable);
}

Result MockRadio::setPreambleLength(uint16_t length) {
    return pimpl_->mock.setPreambleLength(length);
}

int8_t MockRadio::getRSSI() {
    return pimpl_->mock.getRSSI();
}

int8_t MockRadio::getSNR() {
    return pimpl_->mock.getSNR();
}

int8_t MockRadio::getLastPacketRSSI() {
    return pimpl_->mock.getLastPacketRSSI();
}

int8_t MockRadio::getLastPacketSNR() {
    return pimpl_->mock.getLastPacketSNR();
}

bool MockRadio::IsTransmitting() {
    return pimpl_->mock.IsTransmitting();
}

float MockRadio::getFrequency() {
    return pimpl_->mock.getFrequency();
}

uint8_t MockRadio::getSpreadingFactor() {
    return pimpl_->mock.getSpreadingFactor();
}

float MockRadio::getBandwidth() {
    return pimpl_->mock.getBandwidth();
}

uint8_t MockRadio::getCodingRate() {
    return pimpl_->mock.getCodingRate();
}

uint8_t MockRadio::getPower() {
    return pimpl_->mock.getPower();
}

uint8_t MockRadio::getPacketLength() {
    return pimpl_->mock.getPacketLength();
}

uint32_t MockRadio::getTimeOnAir(uint8_t length) {
    return pimpl_->mock.getTimeOnAir(length);
}

Result MockRadio::setActionReceive(void (*callback)(void)) {
    return pimpl_->mock.setActionReceive(callback);
}

Result MockRadio::setActionReceive(
    std::function<void(std::unique_ptr<RadioEvent>)> callback) {
    return pimpl_->mock.setActionReceive(std::move(callback));
}

Result MockRadio::setState(RadioState state) {
    return pimpl_->mock.setState(state);
}

RadioState MockRadio::getState() {
    return pimpl_->mock.getState();
}

Result MockRadio::ClearActionReceive() {
    return pimpl_->mock.ClearActionReceive();
}

// Implementation of the test helper function
test::MockRadio& GetMockForTesting(MockRadio& radio) {
    return radio.pimpl_->mock;
}

}  // namespace radio
}  // namespace loramesher
