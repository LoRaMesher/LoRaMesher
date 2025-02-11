// test/types/test_radio/mock_radio.hpp
#pragma once

#include <gmock/gmock.h>

#include "types/radio/radio.hpp"

namespace loramesher {
namespace radio {
namespace test {

class MockRadio : public IRadio {
   public:
    MOCK_METHOD(Result, configure, (const RadioConfig& config), (override));
    MOCK_METHOD(Result, send, (const uint8_t* data, size_t len), (override));
    MOCK_METHOD(Result, startReceive, (), (override));
    MOCK_METHOD(Result, sleep, (), (override));
    MOCK_METHOD(int8_t, getRSSI, (), (override));
    MOCK_METHOD(int8_t, getSNR, (), (override));
    MOCK_METHOD(void, setReceiveCallback, (std::function<void(RadioEvent&)>),
                (override));
};

}  // namespace test
}  // namespace radio
}  // namespace loramesher