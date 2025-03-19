/**
 * @file radiolib_radio_esp32_mock_test.cpp
 * @brief Test suite for RadioLibRadio class on ESP32 using mock radio
 * 
 * This file contains Google Test cases for testing the RadioLibRadio
 * class functionality on ESP32 hardware but using the mock radio module
 * to avoid dependencies on actual radio hardware.
 * 
 * @copyright Copyright (c) 2025
 */

#include <gtest/gtest.h>
#include <memory>
#include <vector>

#ifndef ARDUINO
/**
  * @brief Test radio configuration succeeds with valid parameters
  */
TEST(RadioLibRadioTestMock, IgnoredTest) {
    // This test is ignored because it requires hardware
    // and cannot be run in the CI environment
    GTEST_SKIP();
}
#else

#include "hardware/SPIMock.hpp"
#include "hardware/radiolib/radiolib_radio.hpp"
#include "os/rtos.hpp"
#include "utils/logger.hpp"

// Include the mocks
#include "../test/mocks/mock_radio.hpp"
#include "mocks/mock_radio_test_helpers.hpp"

using namespace loramesher;
using namespace loramesher::radio;

using ::testing::_;
using ::testing::A;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::SaveArg;

namespace loramesher {
namespace radio {
namespace test {

/**
  * @brief Test fixture for RadioLibRadio tests on ESP32 using mock radio
  */
class RadioLibRadioESP32MockTest : public ::testing::Test {
   public:
    // Hardware pins for ESP32 configuration
    const int kCsPin = 5;  // GPIO pins for ESP32
    const int kDI0Pin = 4;
    const int kRstPin = 14;
    const int kBusyPin = 15;

    static constexpr AddressType kDest = 0x1234;
    static constexpr AddressType kSrc = 0x5678;

    std::unique_ptr<BaseMessage> msg_ptr;

    // SPI interface
    std::unique_ptr<SPIClass> spi_;

    // The radio module to test
    std::unique_ptr<RadioLibRadio> radio_;

    // Sample radio configuration
    RadioConfig test_config_;

    // Callback for testing
    void (*saved_callback_)(void) = nullptr;

    void SetUp() override {
        // Initialize SPI
        spi_ = std::make_unique<SPIClass>();
        if (!spi_) {
            FAIL() << "Failed to create SPIClass instance";
        }

        // Initialize test configuration - use MockRadio
        test_config_.setRadioType(RadioType::kMockRadio);
        test_config_.setFrequency(868.0f);
        test_config_.setSpreadingFactor(7);
        test_config_.setBandwidth(125.0f);
        test_config_.setCodingRate(5);  // 4/5
        test_config_.setPower(17);      // 17 dBm
        test_config_.setSyncWord(0x12);
        test_config_.setCRC(true);
        test_config_.setPreambleLength(8);

        // Create the radio module
        radio_ = std::make_unique<RadioLibRadio>(kCsPin, kDI0Pin, kRstPin,
                                                 kBusyPin, *spi_);
        if (!radio_) {
            FAIL() << "Failed to create RadioLibRadio instance";
        }

        ConfigureRadio();
    }

    void TearDown() override {
        // Clean up
        radio_->Sleep();
        saved_callback_ = nullptr;

        // Clear the message pointer
        msg_ptr.reset();

        // Consider fully destroying and recreating radio instance
        radio_.reset();
        spi_.reset();
        LOG.Reset();
    }

    void ConfigureRadio() {
        // Configure the radio - on ESP32 this will use the actual RTOS
        ASSERT_TRUE(radio_->Configure(test_config_));

        // When using the mock radio, we should be able to access it
        auto& mock_radio = GetRadioLibMockForTesting(*radio_);

        EXPECT_CALL(mock_radio, Begin(_)).WillOnce(Return(Result::Success()));
        EXPECT_CALL(mock_radio, Sleep())
            .WillRepeatedly(Return(Result::Success()));
        EXPECT_CALL(mock_radio, setActionReceive(A<void (*)(void)>()))
            .WillRepeatedly(
                DoAll(SaveArg<0>(&saved_callback_), Return(Result::Success())));
        // Set up the expectation to clear the callback
        EXPECT_CALL(mock_radio, ClearActionReceive())
            .WillRepeatedly(
                DoAll(Invoke([this]() { this->saved_callback_ = nullptr; }),
                      Return(Result::Success())));
        EXPECT_CALL(mock_radio, Sleep())
            .WillRepeatedly(Return(Result::Success()));

        EXPECT_CALL(mock_radio, StartReceive())
            .WillRepeatedly(Return(Result::Success()));

        ASSERT_TRUE(radio_->Begin(test_config_));
    }

    void CreateMessage() {
        std::vector<uint8_t> kPayload{0x01, 0x02, 0x03};
        auto opt_msg =
            BaseMessage::Create(kDest, kSrc, MessageType::DATA, kPayload);
        ASSERT_TRUE(opt_msg.has_value()) << "Failed to create test message";
        msg_ptr = std::make_unique<BaseMessage>(*opt_msg);
    }

    void CreateMaxSizeMessage() {
        // Create a message with the maximum payload size
        std::vector<uint8_t> max_payload = std::vector<uint8_t>(
            255 - BaseHeader::size(), 0x55);  // Arbitrary data

        auto opt_msg =
            BaseMessage::Create(kDest, kSrc, MessageType::DATA, max_payload);
        ASSERT_TRUE(opt_msg.has_value()) << "Failed to create test message";
        msg_ptr = std::make_unique<BaseMessage>(*opt_msg);
    }
};

/**
  * @brief Test radio configuration succeeds with valid parameters
  */
TEST_F(RadioLibRadioESP32MockTest, ConfigurationSucceeds) {
    EXPECT_FLOAT_EQ(radio_->getFrequency(), 868.0f);
}

/**
  * @brief Test starting reception mode
  */
TEST_F(RadioLibRadioESP32MockTest, StartReceiveSucceeds) {
    // Test starting reception
    ASSERT_TRUE(radio_->StartReceive());

    // Verify the radio state has changed
    EXPECT_EQ(radio_->getState(), RadioState::kReceive);
}

/**
  * @brief Test sending data
  */
TEST_F(RadioLibRadioESP32MockTest, SendDataSucceeds) {
    // Get access to the mock radio
    auto& mock_radio = GetRadioLibMockForTesting(*radio_);

    // Prepare test data
    std::vector<uint8_t> test_data = {0x01, 0x02, 0x03, 0x04, 0x05};

    EXPECT_CALL(mock_radio, Send(_, test_data.size()))
        .WillOnce(Return(Result::Success()));

    // Test sending data
    ASSERT_TRUE(radio_->Send(test_data.data(), test_data.size()));

    // Verify state was changed to receive after sending
    EXPECT_EQ(radio_->getState(), RadioState::kReceive);
}

/**
  * @brief Test putting radio to sleep
  */
TEST_F(RadioLibRadioESP32MockTest, SleepSucceeds) {
    // Test putting radio to sleep
    ASSERT_TRUE(radio_->Sleep());

    // Verify state has changed
    EXPECT_EQ(radio_->getState(), RadioState::kSleep);
}

/**
  * @brief Test setting frequency
  */
TEST_F(RadioLibRadioESP32MockTest, FrequencyGetSet) {
    // Get access to the mock radio
    auto& mock_radio = GetRadioLibMockForTesting(*radio_);

    // Test initial value
    EXPECT_FLOAT_EQ(radio_->getFrequency(), 868.0f);

    // Set up expectations for setting the frequency
    EXPECT_CALL(mock_radio, setFrequency(915.0f))
        .WillOnce(Return(Result::Success()));

    // Test setting new value
    ASSERT_TRUE(radio_->setFrequency(915.0f));
    EXPECT_FLOAT_EQ(radio_->getFrequency(), 915.0f);
}

/**
  * @brief Test RSSI and SNR getters
  */
TEST_F(RadioLibRadioESP32MockTest, GetRSSIAndSNR) {
    // Get access to the mock radio
    auto& mock_radio = GetRadioLibMockForTesting(*radio_);

    // Set up expectations for RSSI and SNR
    EXPECT_CALL(mock_radio, getRSSI()).WillOnce(Return(-75));

    EXPECT_CALL(mock_radio, getSNR()).WillOnce(Return(6));

    // Test getters
    EXPECT_EQ(radio_->getRSSI(), -75);
    EXPECT_EQ(radio_->getSNR(), 6);
}

/**
  * @brief Test setting receive callback function
  */
TEST_F(RadioLibRadioESP32MockTest, SetActionReceive) {
    // Create a semaphore for callback synchronization
    // TODO: Change it to an GetRTOS().CreateSemaphore() call
    SemaphoreHandle_t callback_semaphore = xSemaphoreCreateBinary();
    ASSERT_NE(callback_semaphore, nullptr);

    // Start receiving
    radio_->StartReceive();

    // Get access to the mock radio
    auto& mock_radio = GetRadioLibMockForTesting(*radio_);

    // Prepare test data
    CreateMessage();

    const int8_t kTestRSSI = -65;
    const int8_t kTestSNR = 8;

    std::vector<uint8_t> test_packet = msg_ptr->Serialize().value();

    // Set expectations on the mock
    EXPECT_CALL(mock_radio, getPacketLength())
        .WillOnce(Return(test_packet.size()));

    // Set up expectations for RSSI and SNR
    EXPECT_CALL(mock_radio, getRSSI()).WillOnce(Return(kTestRSSI));

    EXPECT_CALL(mock_radio, getSNR()).WillOnce(Return(kTestSNR));

    EXPECT_CALL(mock_radio, readData(_, _))
        .WillOnce(DoAll(
            // Copy our test packet to the buffer provided by RadioLibRadio
            Invoke([&test_packet](uint8_t* data, size_t len) {
                // Use parameters to prevent unused variable warnings
                (void)len;  // Explicitly mark as used

                std::copy(test_packet.begin(), test_packet.end(), data);
                return Result::Success();
            }),
            Return(Result::Success())));

    // Create a test callback
    bool callback_called = false;
    auto test_callback = [&callback_called, callback_semaphore, test_packet,
                          kTestSNR,
                          kTestRSSI](std::unique_ptr<RadioEvent> event) {
        callback_called = true;

        // Check the received event
        ASSERT_NE(event, nullptr);
        ASSERT_EQ(event->getType(), RadioEventType::kReceived);
        ASSERT_EQ(event->getSnr(), kTestSNR);
        ASSERT_EQ(event->getRssi(), kTestRSSI);

        ASSERT_TRUE(event->HasMessage());
        std::unique_ptr<BaseMessage> message = event->TakeMessage();
        ASSERT_NE(message, nullptr);

        std::optional<std::vector<uint8_t>> serialized_message =
            message->Serialize();
        ASSERT_TRUE(serialized_message.has_value());

        std::vector<uint8_t> actual_message = serialized_message.value();

        // Check the received data
        ASSERT_EQ(actual_message.size(), test_packet.size());
        for (size_t i = 0; i < test_packet.size(); i++) {
            EXPECT_EQ(actual_message[i], test_packet[i]);
        }

        // Signal completion via semaphore
        xSemaphoreGive(callback_semaphore);
    };

    // Test setting the callback
    ASSERT_TRUE(radio_->setActionReceive(test_callback));

    // Verify the callback was set
    EXPECT_NE(saved_callback_, nullptr);

    // Call the callback
    saved_callback_();

    // Wait for the callback to complete with a timeout
    const TickType_t timeout_ticks = pdMS_TO_TICKS(1000);  // 1 second timeout
    BaseType_t result = xSemaphoreTake(callback_semaphore, timeout_ticks);

    // Clean up
    vSemaphoreDelete(callback_semaphore);

    // Check if callback was executed within timeout
    ASSERT_EQ(result, pdTRUE) << "Callback did not complete within timeout";

    // Verify the callback was called
    EXPECT_TRUE(callback_called);

    delay(100);
}

/**
 * @brief Test handling an empty packet
 * 
 * This test verifies that the radio correctly handles an empty packet
 * without calling message processing.
 */
TEST_F(RadioLibRadioESP32MockTest, EmptyPacketHandling) {
    Serial.println("EmptyPacketHandling");
    // Start receiving
    radio_->StartReceive();

    Serial.println("StartReceive");

    // Get access to the mock radio
    auto& mock_radio = GetRadioLibMockForTesting(*radio_);

    // Empty packet
    const size_t kEmptyPacketSize = 0;

    // Test RSSI and SNR values
    const int8_t kTestRSSI = -70;
    const int8_t kTestSNR = 5;

    // Set expectations on the mock
    EXPECT_CALL(mock_radio, getPacketLength())
        .WillOnce(Return(kEmptyPacketSize));

    // Set up expectations for RSSI and SNR
    EXPECT_CALL(mock_radio, getRSSI()).Times(0);
    EXPECT_CALL(mock_radio, getSNR()).Times(0);

    // readData should not be called for empty packets
    EXPECT_CALL(mock_radio, readData(_, _)).Times(0);

    // Track if callback was called
    bool callback_called = false;
    auto test_callback = [&callback_called](std::unique_ptr<RadioEvent> event) {
        // event should not be present
        ASSERT_EQ(event, nullptr);

        callback_called = true;
    };

    // Test setting the callback
    ASSERT_TRUE(radio_->setActionReceive(test_callback));

    // Verify the callback was set
    EXPECT_NE(saved_callback_, nullptr);

    // Call the callback
    saved_callback_();

    // // Verify the callback was called
    EXPECT_FALSE(callback_called);

    delay(100);
}

/**
 * @brief Test handling a maximum-sized packet
 * 
 * This test verifies that the radio correctly processes the largest packet
 * that the system can handle.
 */
TEST_F(RadioLibRadioESP32MockTest, MaxSizePacketHandling) {
    // Create a semaphore for callback synchronization
    SemaphoreHandle_t callback_semaphore = xSemaphoreCreateBinary();
    ASSERT_NE(callback_semaphore, nullptr);

    // Start receiving
    radio_->StartReceive();

    // Get access to the mock radio
    auto& mock_radio = GetRadioLibMockForTesting(*radio_);

    // Define maximum packet size (adjust based on your protocol's limitations)
    const size_t kMaxPacketSize = 255;  // Typical max for many radio protocols

    // Create a maximum-sized message
    CreateMaxSizeMessage();  // Assuming this helper method exists or needs to be created

    const int8_t kTestRSSI = -45;  // Strong signal
    const int8_t kTestSNR = 12;    // Good SNR

    std::vector<uint8_t> max_packet = msg_ptr->Serialize().value();

    // Ensure our test packet is actually the maximum size
    ASSERT_EQ(max_packet.size(), kMaxPacketSize)
        << "Test packet is not maximum size";

    // Set expectations on the mock
    EXPECT_CALL(mock_radio, getPacketLength())
        .WillOnce(Return(max_packet.size()));

    // Set up expectations for RSSI and SNR
    EXPECT_CALL(mock_radio, getRSSI()).WillOnce(Return(kTestRSSI));
    EXPECT_CALL(mock_radio, getSNR()).WillOnce(Return(kTestSNR));

    EXPECT_CALL(mock_radio, readData(_, _))
        .WillOnce(DoAll(
            // Copy our test packet to the buffer provided by RadioLibRadio
            Invoke([&max_packet](uint8_t* data, size_t len) {
                // Ensure buffer is large enough
                EXPECT_GE(len, max_packet.size())
                    << "Buffer is too small for max packet";

                std::copy(max_packet.begin(), max_packet.end(), data);
                return Result::Success();
            }),
            Return(Result::Success())));

    // Create a test callback
    bool callback_called = false;
    auto test_callback = [&callback_called, callback_semaphore, &max_packet,
                          kTestSNR,
                          kTestRSSI](std::unique_ptr<RadioEvent> event) {
        callback_called = true;

        // Check the received event
        ASSERT_NE(event, nullptr);
        ASSERT_EQ(event->getType(), RadioEventType::kReceived);
        ASSERT_EQ(event->getSnr(), kTestSNR);
        ASSERT_EQ(event->getRssi(), kTestRSSI);

        ASSERT_TRUE(event->HasMessage());
        std::unique_ptr<BaseMessage> message = event->TakeMessage();
        ASSERT_NE(message, nullptr);

        std::optional<std::vector<uint8_t>> serialized_message =
            message->Serialize();
        ASSERT_TRUE(serialized_message.has_value());

        std::vector<uint8_t> actual_message = serialized_message.value();

        // Check the received data
        ASSERT_EQ(actual_message.size(), max_packet.size());

        // Verify each byte matches (for large packets, report only first mismatch)
        bool all_bytes_match = true;

        for (size_t i = 0; i < max_packet.size(); i++) {
            if (actual_message[i] != max_packet[i]) {
                all_bytes_match = false;
                break;
            }
        }

        EXPECT_TRUE(all_bytes_match) << "Byte mismatch at index";

        // Signal completion via semaphore
        xSemaphoreGive(callback_semaphore);
    };

    // Test setting the callback
    ASSERT_TRUE(radio_->setActionReceive(test_callback));

    // Verify the callback was set
    EXPECT_NE(saved_callback_, nullptr);

    // Call the callback
    saved_callback_();

    // Wait for the callback to complete with a timeout
    const TickType_t timeout_ticks =
        pdMS_TO_TICKS(2000);  // 2 second timeout for max packet
    BaseType_t result = xSemaphoreTake(callback_semaphore, timeout_ticks);

    // Clean up
    vSemaphoreDelete(callback_semaphore);

    // Check if callback was executed within timeout
    ASSERT_EQ(result, pdTRUE) << "Callback did not complete within timeout";

    // Verify the callback was called
    EXPECT_TRUE(callback_called);

    delay(100);
}

/**
 * @brief Test handling multiple maximum-sized packets in sequence
 * 
 * This test verifies that the radio correctly processes multiple consecutive
 * maximum-sized packets without memory leaks, task collapses, or other issues.
 * It sends the same maximum-sized packet 10 times and verifies proper handling.
 */
TEST_F(RadioLibRadioESP32MockTest, RepeatedMaxSizePacketHandling) {
    // Create a semaphore for callback synchronization
    SemaphoreHandle_t callback_semaphore = xSemaphoreCreateBinary();
    ASSERT_NE(callback_semaphore, nullptr);

    // Start receiving
    radio_->StartReceive();

    // Get access to the mock radio
    auto& mock_radio = GetRadioLibMockForTesting(*radio_);

    // Define maximum packet size (adjust based on your protocol's limitations)
    const size_t kMaxPacketSize = 255;  // Typical max for many radio protocols

    // Define number of repetitions for stress testing
    const int kNumRepetitions = 10;

    // Create a maximum-sized message
    CreateMaxSizeMessage();  // Assuming this helper method exists

    const int8_t kTestRSSI = -45;  // Strong signal
    const int8_t kTestSNR = 12;    // Good SNR

    std::vector<uint8_t> max_packet = msg_ptr->Serialize().value();

    // Ensure our test packet is actually the maximum size
    ASSERT_EQ(max_packet.size(), kMaxPacketSize)
        << "Test packet is not maximum size";

    // Create atomic counters for tracking callbacks
    std::atomic<int> callback_count{0};
    std::atomic<bool> all_packets_valid{true};

    // Set expectations on the mock - will be called multiple times
    EXPECT_CALL(mock_radio, getPacketLength())
        .Times(kNumRepetitions)
        .WillRepeatedly(Return(max_packet.size()));

    // Set up expectations for RSSI and SNR
    EXPECT_CALL(mock_radio, getRSSI())
        .Times(kNumRepetitions)
        .WillRepeatedly(Return(kTestRSSI));

    EXPECT_CALL(mock_radio, getSNR())
        .Times(kNumRepetitions)
        .WillRepeatedly(Return(kTestSNR));

    EXPECT_CALL(mock_radio, readData(_, _))
        .Times(kNumRepetitions)
        .WillRepeatedly(DoAll(
            // Copy our test packet to the buffer provided by RadioLibRadio
            Invoke([&max_packet](uint8_t* data, size_t len) {
                // Ensure buffer is large enough
                EXPECT_GE(len, max_packet.size())
                    << "Buffer is too small for max packet";

                std::copy(max_packet.begin(), max_packet.end(), data);
                return Result::Success();
            }),
            Return(Result::Success())));

    // Create a test callback that will be called for each reception
    auto test_callback = [&callback_count, &all_packets_valid,
                          callback_semaphore, &max_packet, kTestSNR, kTestRSSI,
                          kNumRepetitions](std::unique_ptr<RadioEvent> event) {
        // Check the received event
        if (event == nullptr || event->getType() != RadioEventType::kReceived ||
            event->getSnr() != kTestSNR || event->getRssi() != kTestRSSI) {
            all_packets_valid = false;
        }

        if (!event->HasMessage()) {
            all_packets_valid = false;
        } else {
            std::unique_ptr<BaseMessage> message = event->TakeMessage();
            if (message == nullptr) {
                all_packets_valid = false;
            } else {
                std::optional<std::vector<uint8_t>> serialized_message =
                    message->Serialize();
                if (!serialized_message.has_value()) {
                    all_packets_valid = false;
                } else {
                    std::vector<uint8_t> actual_message =
                        serialized_message.value();

                    // Check size and content
                    if (actual_message.size() != max_packet.size()) {
                        all_packets_valid = false;
                    } else {
                        // Verify each byte matches
                        for (size_t i = 0; i < max_packet.size(); i++) {
                            if (actual_message[i] != max_packet[i]) {
                                all_packets_valid = false;
                                break;
                            }
                        }
                    }
                }
            }
        }

        // Increment the callback counter
        int current_count = ++callback_count;

        // Signal completion via semaphore on the last callback
        if (current_count >= kNumRepetitions) {
            xSemaphoreGive(callback_semaphore);
        }
    };

    // Set the callback
    ASSERT_TRUE(radio_->setActionReceive(test_callback));

    // Verify the callback was set
    EXPECT_NE(saved_callback_, nullptr);

    // Call the callback multiple times to simulate multiple packet receptions
    for (int i = 0; i < kNumRepetitions; i++) {
        saved_callback_();

        // Add a small delay between simulated receptions
        delay(100);

        // Monitor heap usage to detect memory leaks
        if (i % 2 == 0) {
            LOG_DEBUG("Heap free after %d packets: %d bytes\n", i + 1,
                      esp_get_free_heap_size());
        }
    }

    // Wait for all callbacks to complete with a timeout
    const TickType_t timeout_ticks = pdMS_TO_TICKS(5000);  // 5 second timeout
    BaseType_t result = xSemaphoreTake(callback_semaphore, timeout_ticks);

    // Clean up
    vSemaphoreDelete(callback_semaphore);

    // Check if callbacks were executed within timeout
    ASSERT_EQ(result, pdTRUE) << "Callbacks did not complete within timeout";

    // Verify all callbacks were called
    EXPECT_EQ(callback_count, kNumRepetitions)
        << "Expected " << kNumRepetitions << " callbacks, got "
        << callback_count;

    // Verify all packets were valid
    EXPECT_TRUE(all_packets_valid) << "Some packets had validation errors";

    // Add a delay to ensure all cleanup is complete
    delay(100);

    // Log final heap status
    LOG_DEBUG("Final heap free: %d bytes\n", esp_get_free_heap_size());
}

}  // namespace test
}  // namespace radio
}  // namespace loramesher

#endif  // ARDUINO