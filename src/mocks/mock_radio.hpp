#pragma once

#include "config/system_config.hpp"
#include "types/radio/radio.hpp"

namespace loramesher {
namespace radio {

// Forward declaration
namespace test {
class MockRadio;
}

/**
 * @brief MockRadio implementation for testing
 * 
 * This class provides a mock implementation of the IRadio interface
 * for testing purposes. It's only available in LORAMESHER_BUILD_NATIVE mode.
 */
class MockRadio : public IRadio {
   public:
    /**
     * @brief Construct a new MockRadio object
     */
    MockRadio();

    /**
     * @brief Destroy the MockRadio object
     */
    ~MockRadio() override;

    // IRadio interface implementation
    Result Configure(const RadioConfig& config) override;
    Result Begin(const RadioConfig& config) override;
    Result Send(const uint8_t* data, size_t len) override;
    Result readData(uint8_t* data, size_t len) override;
    Result StartReceive() override;
    Result Sleep() override;

    // Parameter Configuration
    Result setFrequency(float frequency) override;
    Result setSpreadingFactor(uint8_t sf) override;
    Result setBandwidth(float bandwidth) override;
    Result setCodingRate(uint8_t codingRate) override;
    Result setPower(int8_t power) override;
    Result setSyncWord(uint8_t syncWord) override;
    Result setCRC(bool enable) override;
    Result setPreambleLength(uint16_t length) override;

    // Radio Status
    int8_t getRSSI() override;
    int8_t getSNR() override;
    int8_t getLastPacketRSSI() override;
    int8_t getLastPacketSNR() override;
    bool IsTransmitting() override;
    float getFrequency() override;
    uint8_t getSpreadingFactor() override;
    float getBandwidth() override;
    uint8_t getCodingRate() override;
    uint8_t getPower() override;
    uint8_t getPacketLength() override;

    // Event Handling
    Result setActionReceive(void (*callback)(void)) override;
    Result setActionReceive(
        std::function<void(std::unique_ptr<RadioEvent>)> callback) override;
    Result setState(RadioState state) override;
    RadioState getState() override;
    Result ClearActionReceive() override;

    // Friend declaration for the test helper function
    friend test::MockRadio& GetMockForTesting(MockRadio& radio);

   private:
    // Forward declaration of implementation
    class Impl;
    std::unique_ptr<Impl> pimpl_;
};

/**
 * @brief Get the underlying test mock from a MockRadio instance
 * 
 * This function allows tests to set expectations on the mock radio.
 * 
 * @param radio The MockRadio instance
 * @return test::MockRadio& Reference to the underlying test mock
 */
test::MockRadio& GetMockForTesting(MockRadio& radio);

}  // namespace radio
}  // namespace loramesher