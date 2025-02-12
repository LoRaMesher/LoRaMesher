#pragma once

#include <gmock/gmock.h>
#include "types/radio/radio.hpp"

namespace loramesher {
namespace radio {
namespace test {

class MockRadio : public IRadio {
   public:
    MOCK_METHOD(Result, configure, (const RadioConfig& config), (override));
    MOCK_METHOD(Result, begin, (const RadioConfig& config), (override));
    MOCK_METHOD(Result, send, (const uint8_t* data, size_t len), (override));
    MOCK_METHOD(Result, startReceive, (), (override));
    MOCK_METHOD(Result, sleep, (), (override));

    // Parameter Configuration
    MOCK_METHOD(Result, setFrequency, (float frequency), (override));
    MOCK_METHOD(Result, setSpreadingFactor, (uint8_t sf), (override));
    MOCK_METHOD(Result, setBandwidth, (float bandwidth), (override));
    MOCK_METHOD(Result, setCodingRate, (uint8_t codingRate), (override));
    MOCK_METHOD(Result, setPower, (uint8_t power), (override));
    MOCK_METHOD(Result, setSyncWord, (uint8_t syncWord), (override));
    MOCK_METHOD(Result, setCRC, (bool enable), (override));
    MOCK_METHOD(Result, setPreambleLength, (uint16_t length), (override));

    // Radio Status
    MOCK_METHOD(int8_t, getRSSI, (), (override));
    MOCK_METHOD(int8_t, getSNR, (), (override));
    MOCK_METHOD(int8_t, getLastPacketRSSI, (), (override));
    MOCK_METHOD(int8_t, getLastPacketSNR, (), (override));
    MOCK_METHOD(bool, isTransmitting, (), (override));
    MOCK_METHOD(float, getFrequency, (), (override));
    MOCK_METHOD(uint8_t, getSpreadingFactor, (), (override));
    MOCK_METHOD(float, getBandwidth, (), (override));
    MOCK_METHOD(uint8_t, getCodingRate, (), (override));
    MOCK_METHOD(uint8_t, getPower, (), (override));

    // Event Handling
    MOCK_METHOD(void, setReceiveCallback, (std::function<void(RadioEvent&)>),
                (override));
    MOCK_METHOD(Result, setState, (RadioState state), (override));
    MOCK_METHOD(RadioState, getState, (), (override));
};

}  // namespace test
}  // namespace radio
}  // namespace loramesher