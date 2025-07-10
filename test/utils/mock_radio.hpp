// test/utils/mock_radio.hpp
#pragma once

#include <gmock/gmock.h>
#include "os/os_port.hpp"
#include "types/radio/radio.hpp"

namespace loramesher {
namespace radio {

// Forward declaration
class RadioLibRadio;

namespace test {

class MockRadio : public IRadio {
   public:
    MOCK_METHOD(Result, Configure, (const RadioConfig& config), (override));
    MOCK_METHOD(Result, Begin, (const RadioConfig& config), (override));
    MOCK_METHOD(Result, Send, (const uint8_t* data, size_t len), (override));
    // clang-format off
    MOCK_METHOD(Result, readData, (uint8_t* data, size_t len), (override));
    // clang-format on
    MOCK_METHOD(Result, StartReceive, (), (override));
    MOCK_METHOD(Result, Sleep, (), (override));

    // Parameter Configuration
    MOCK_METHOD(Result, setFrequency, (float frequency), (override));
    MOCK_METHOD(Result, setSpreadingFactor, (uint8_t sf), (override));
    MOCK_METHOD(Result, setBandwidth, (float bandwidth), (override));
    MOCK_METHOD(Result, setCodingRate, (uint8_t codingRate), (override));
    MOCK_METHOD(Result, setPower, (int8_t power), (override));
    MOCK_METHOD(Result, setSyncWord, (uint8_t syncWord), (override));
    MOCK_METHOD(Result, setCRC, (bool enable), (override));
    MOCK_METHOD(Result, setPreambleLength, (uint16_t length), (override));

    // Radio Status
    MOCK_METHOD(int8_t, getRSSI, (), (override));
    MOCK_METHOD(int8_t, getSNR, (), (override));
    MOCK_METHOD(int8_t, getLastPacketRSSI, (), (override));
    MOCK_METHOD(int8_t, getLastPacketSNR, (), (override));
    MOCK_METHOD(bool, IsTransmitting, (), (override));
    MOCK_METHOD(float, getFrequency, (), (override));
    MOCK_METHOD(uint8_t, getSpreadingFactor, (), (override));
    MOCK_METHOD(float, getBandwidth, (), (override));
    MOCK_METHOD(uint8_t, getCodingRate, (), (override));
    MOCK_METHOD(uint8_t, getPower, (), (override));
    MOCK_METHOD(uint8_t, getPacketLength, (), (override));
    MOCK_METHOD(uint32_t, getTimeOnAir, (uint8_t length), (override));

    // Event Handling
    MOCK_METHOD(Result, setActionReceive, (void (*callback)(void)), (override));
    MOCK_METHOD(Result, setActionReceive,
                (std::function<void(std::unique_ptr<RadioEvent>)>), (override));
    MOCK_METHOD(Result, setState, (RadioState state), (override));
    MOCK_METHOD(RadioState, getState, (), (override));
    MOCK_METHOD(Result, ClearActionReceive, (), (override));

    // Test-specific methods for RadioLibRadio instance awareness
    /**
     * @brief Set the associated RadioLibRadio instance for this mock
     * @param instance Pointer to the RadioLibRadio instance
     */
    void SetRadioLibInstance(RadioLibRadio* instance) {
        radio_lib_instance_ = instance;
    }

    /**
     * @brief Notify the processing task of the associated RadioLibRadio instance
     * This bypasses the singleton pattern issue by directly notifying the correct instance
     */
    void NotifyProcessingTask();

   private:
    RadioLibRadio* radio_lib_instance_ =
        nullptr;  ///< Associated RadioLibRadio instance
};

}  // namespace test
}  // namespace radio
}  // namespace loramesher
