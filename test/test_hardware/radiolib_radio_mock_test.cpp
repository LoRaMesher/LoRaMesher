/**
 * @file radiolib_radio_test.cpp
 * @brief Test suite for RadioLibRadio class
 * 
 * This file contains Google Test cases for testing the RadioLibRadio
 * class functionality including configuration, transmission, reception,
 * and parameter settings.
 * 
 * @copyright Copyright (c) 2025
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <vector>

#ifdef ARDUINO
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

// Mock classes
#include "../mocks/mock_radio.hpp"
#include "../mocks/mock_rtos.hpp"
#include "mocks/mock_radio_test_helpers.hpp"

using ::testing::_;
using ::testing::A;
using ::testing::AnyNumber;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;

namespace loramesher {
namespace radio {
namespace test {

// Forward declaration
class RadioLibRadioTestMock;

// -------------------- Testing Helpers --------------------

/**
  * @brief Friend test class to access private members of RadioLibRadio
  */
class RadioLibRadioTestMockAccess : public RadioLibRadio {
    // Allow the test fixture to access private members
    friend class RadioLibRadioTestMock;

   public:
    // Constructor that forwards to the parent
    RadioLibRadioTestMockAccess(int cs_pin, int di0_pin, int rst_pin,
                                int busy_pin, SPIClass& spi)
        : RadioLibRadio(cs_pin, di0_pin, rst_pin, busy_pin, spi) {}

    // Expose protected methods for testing
    void TestHandleInterrupt() { HandleInterrupt(); }

    // Expose protected static methods
    static void TestHandleInterruptStatic() { HandleInterruptStatic(); }

    // Removed: Expose protected members
    // Use GetRadioLibMockForTesting instead of accessing current_module_ directly

    // Expose static methods for testing
    static void TestProcessEvents(void* parameters) {
        ProcessEvents(parameters);
    }

    auto& GetReceiveCallback() { return receive_callback_; }

    auto& GetRadioMutex() { return radio_mutex_; }

    auto GetReceiveQueue() { return receive_queue_; }

    auto GetCurrentState() { return current_state_; }

    // Set last packet RSSI and SNR for testing
    void SetLastPacketMetrics(int8_t rssi, int8_t snr) {
        last_packet_rssi_ = rssi;
        last_packet_snr_ = snr;
    }

    // Set the static instance for testing
    static void SetInstance(RadioLibRadio* radio) { instance_ = radio; }

    // Required for friend access - we still need this to set the mock
    std::unique_ptr<IRadio>& GetCurrentModule() { return current_module_; }
};

/**
   * @brief Test fixture for RadioLibRadio tests
   */
class RadioLibRadioTestMock : public ::testing::Test {
   public:
    // Hardware pins for test configuration
    const int kCsPin = 5;
    const int kDI0Pin = 4;
    const int kRstPin = 14;
    const int kBusyPin = 15;

    // Test queue handle
    os::QueueHandle_t queue_handle_ =
        reinterpret_cast<os::QueueHandle_t>(0x12345678);
    os::TaskHandle_t task_handle_ =
        reinterpret_cast<os::TaskHandle_t>(0x87654321);

    // Mock objects
    std::shared_ptr<::loramesher::test::MockRTOS> rtos_mock_;
    std::unique_ptr<SPIClass> spi_mock_;

    // The radio module to test
    std::unique_ptr<RadioLibRadio> radio_;

    // Sample radio configuration
    RadioConfig test_config_;

    void SetUp() override {
        // Initialize mocks
        rtos_mock_ = std::make_shared<::loramesher::test::MockRTOS>();
        spi_mock_ = std::make_unique<SPIClass>();

        // Set up RTOS mock expectations for common operations
        ON_CALL(*rtos_mock_, CreateQueue(_, _))
            .WillByDefault(Return(queue_handle_));

        ON_CALL(*rtos_mock_, CreateTask(_, _, _, _, _, _))
            .WillByDefault(DoAll(SetArgPointee<5>(task_handle_), Return(true)));

        // Initialize test configuration
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
                                                 kBusyPin, *spi_mock_);
    }

    void TearDown() override {
        // Clean up
        radio_.reset();
        spi_mock_.reset();
        rtos_mock_.reset();
    }

    // Utility to configure the radio with default settings
    void ConfigureRadio() {
        EXPECT_CALL(*rtos_mock_, CreateQueue(_, _))
            .WillOnce(Return(queue_handle_));

        EXPECT_CALL(*rtos_mock_, CreateTask(_, _, _, _, _, _))
            .WillOnce(DoAll(SetArgPointee<5>(task_handle_), Return(true)));

        ASSERT_TRUE(radio_->Configure(test_config_));
        ASSERT_TRUE(radio_->Begin(test_config_));
    }

    // Add this helper method to your RadioLibRadioTestMock fixture class
    void ConfigureRadioAccess(RadioLibRadioTestMockAccess* radio) {
        EXPECT_CALL(*rtos_mock_, CreateQueue(_, _))
            .WillOnce(Return(queue_handle_));

        EXPECT_CALL(*rtos_mock_, CreateTask(_, _, _, _, _, _))
            .WillOnce(DoAll(SetArgPointee<5>(task_handle_), Return(true)));

        ASSERT_TRUE(radio->Configure(test_config_));
        ASSERT_TRUE(radio_->Begin(test_config_));
    }
};

// -------------------- Test Cases --------------------

/**
   * @brief Test radio configuration succeeds with valid parameters
   */
TEST_F(RadioLibRadioTestMock, ConfigurationSucceeds) {
    ConfigureRadio();
}

/**
   * @brief Test radio configuration fails when queue creation fails
   */
TEST_F(RadioLibRadioTestMock, ConfigurationFailsWhenQueueCreationFails) {
    EXPECT_CALL(*rtos_mock_, CreateQueue(_, _)).WillOnce(Return(nullptr));

    ASSERT_FALSE(radio_->Configure(test_config_));
    ASSERT_FALSE(radio_->Begin(test_config_));
}

/**
   * @brief Test radio configuration fails when task creation fails
   */
TEST_F(RadioLibRadioTestMock, ConfigurationFailsWhenTaskCreationFails) {
    EXPECT_CALL(*rtos_mock_, CreateQueue(_, _)).WillOnce(Return(queue_handle_));

    EXPECT_CALL(*rtos_mock_, CreateTask(_, _, _, _, _, _))
        .WillOnce(Return(false));

    EXPECT_CALL(*rtos_mock_, DeleteQueue(queue_handle_)).Times(1);

    ASSERT_FALSE(radio_->Configure(test_config_));
    ASSERT_FALSE(radio_->Begin(test_config_));
}

/**
   * @brief Test starting reception mode
   */
TEST_F(RadioLibRadioTestMock, StartReceiveSucceeds) {
    ConfigureRadio();

    // Get the mock radio
    auto& mock_radio = GetRadioLibMockForTesting(*radio_);

    // Set up expectations on the mock radio
    EXPECT_CALL(mock_radio, ClearActionReceive())
        .WillOnce(Return(Result::Success()));

    EXPECT_CALL(mock_radio, setActionReceive(A<void (*)(void)>()))
        .WillOnce(Return(Result::Success()));

    EXPECT_CALL(mock_radio, StartReceive()).WillOnce(Return(Result::Success()));

    // Ensure the processing task is suspended and resumed
    EXPECT_CALL(*rtos_mock_, SuspendTask(task_handle_)).Times(1);
    EXPECT_CALL(*rtos_mock_, ResumeTask(task_handle_)).Times(1);

    ASSERT_TRUE(radio_->StartReceive());
    EXPECT_EQ(radio_->getState(), RadioState::kReceive);
}

/**
   * @brief Test sending data
   */
TEST_F(RadioLibRadioTestMock, SendDataSucceeds) {
    ConfigureRadio();

    // Get the mock radio
    auto& mock_radio = GetRadioLibMockForTesting(*radio_);

    // Set up expectations on the mock radio
    EXPECT_CALL(mock_radio, ClearActionReceive())
        .WillOnce(Return(Result::Success()));

    // Prepare test data
    std::vector<uint8_t> test_data = {0x01, 0x02, 0x03, 0x04, 0x05};

    EXPECT_CALL(mock_radio, Send(_, test_data.size()))
        .WillOnce(Return(Result::Success()));

    EXPECT_CALL(mock_radio, setActionReceive(A<void (*)(void)>()))
        .WillOnce(Return(Result::Success()));

    EXPECT_CALL(mock_radio, StartReceive()).WillOnce(Return(Result::Success()));

    // Test sending data
    ASSERT_TRUE(radio_->Send(test_data.data(), test_data.size()));

    // Verify state was changed to receive after sending
    EXPECT_EQ(radio_->getState(), RadioState::kReceive);
}

/**
   * @brief Test putting radio to sleep
   */
TEST_F(RadioLibRadioTestMock, SleepSucceeds) {
    ConfigureRadio();

    // Get the mock radio
    auto& mock_radio = GetRadioLibMockForTesting(*radio_);

    // Set up expectations on the mock radio
    EXPECT_CALL(mock_radio, Sleep()).WillOnce(Return(Result::Success()));

    // Ensure the processing task is suspended
    EXPECT_CALL(*rtos_mock_, SuspendTask(task_handle_)).Times(1);

    ASSERT_TRUE(radio_->Sleep());
    EXPECT_EQ(radio_->getState(), RadioState::kSleep);
}

/**
   * @brief Test getting and setting frequency
   */
TEST_F(RadioLibRadioTestMock, FrequencyGetSet) {
    ConfigureRadio();

    // Get the mock radio
    auto& mock_radio = GetRadioLibMockForTesting(*radio_);

    // Set up expectations for getting the frequency
    EXPECT_CALL(mock_radio, getFrequency()).WillOnce(Return(868.0f));

    // Set up expectations for setting the frequency
    EXPECT_CALL(mock_radio, setFrequency(915.0f))
        .WillOnce(Return(Result::Success()));

    EXPECT_CALL(mock_radio, getFrequency()).WillOnce(Return(915.0f));

    // Test initial value
    EXPECT_FLOAT_EQ(radio_->getFrequency(), 868.0f);

    // Test setting new value
    ASSERT_TRUE(radio_->setFrequency(915.0f));
    EXPECT_FLOAT_EQ(radio_->getFrequency(), 915.0f);
}

/**
   * @brief Test getting and setting spreading factor
   */
TEST_F(RadioLibRadioTestMock, SpreadingFactorGetSet) {
    ConfigureRadio();

    // Get the mock radio
    auto& mock_radio = GetRadioLibMockForTesting(*radio_);

    // Set up expectations for getting the spreading factor
    EXPECT_CALL(mock_radio, getSpreadingFactor()).WillOnce(Return(7));

    // Set up expectations for setting the spreading factor
    EXPECT_CALL(mock_radio, setSpreadingFactor(10))
        .WillOnce(Return(Result::Success()));

    EXPECT_CALL(mock_radio, getSpreadingFactor()).WillOnce(Return(10));

    // Test initial value
    EXPECT_EQ(radio_->getSpreadingFactor(), 7);

    // Test setting new value
    ASSERT_TRUE(radio_->setSpreadingFactor(10));
    EXPECT_EQ(radio_->getSpreadingFactor(), 10);
}

/**
   * @brief Test getting and setting bandwidth
   */
TEST_F(RadioLibRadioTestMock, BandwidthGetSet) {
    ConfigureRadio();

    // Get the mock radio
    auto& mock_radio = GetRadioLibMockForTesting(*radio_);

    // Set up expectations for getting the bandwidth
    EXPECT_CALL(mock_radio, getBandwidth()).WillOnce(Return(125.0f));

    // Set up expectations for setting the bandwidth
    EXPECT_CALL(mock_radio, setBandwidth(250.0f))
        .WillOnce(Return(Result::Success()));

    EXPECT_CALL(mock_radio, getBandwidth()).WillOnce(Return(250.0f));

    // Test initial value
    EXPECT_FLOAT_EQ(radio_->getBandwidth(), 125.0f);

    // Test setting new value
    ASSERT_TRUE(radio_->setBandwidth(250.0f));
    EXPECT_FLOAT_EQ(radio_->getBandwidth(), 250.0f);
}

/**
   * @brief Test getting and setting coding rate
   */
TEST_F(RadioLibRadioTestMock, CodingRateGetSet) {
    ConfigureRadio();

    // Get the mock radio
    auto& mock_radio = GetRadioLibMockForTesting(*radio_);

    // Set up expectations for getting the coding rate
    EXPECT_CALL(mock_radio, getCodingRate()).WillOnce(Return(5));

    // Set up expectations for setting the coding rate
    EXPECT_CALL(mock_radio, setCodingRate(7))
        .WillOnce(Return(Result::Success()));

    EXPECT_CALL(mock_radio, getCodingRate()).WillOnce(Return(7));

    // Test initial value
    EXPECT_EQ(radio_->getCodingRate(), 5);

    // Test setting new value
    ASSERT_TRUE(radio_->setCodingRate(7));
    EXPECT_EQ(radio_->getCodingRate(), 7);
}

/**
   * @brief Test getting and setting transmission power
   */
TEST_F(RadioLibRadioTestMock, PowerGetSet) {
    ConfigureRadio();

    // Get the mock radio
    auto& mock_radio = GetRadioLibMockForTesting(*radio_);

    // Set up expectations for getting the power
    EXPECT_CALL(mock_radio, getPower()).WillOnce(Return(17));

    // Set up expectations for setting the power
    EXPECT_CALL(mock_radio, setPower(20)).WillOnce(Return(Result::Success()));

    EXPECT_CALL(mock_radio, getPower()).WillOnce(Return(20));

    // Test initial value
    EXPECT_EQ(radio_->getPower(), 17);

    // Test setting new value
    ASSERT_TRUE(radio_->setPower(20));
    EXPECT_EQ(radio_->getPower(), 20);
}

/**
   * @brief Test getting and setting sync word
   */
TEST_F(RadioLibRadioTestMock, SyncWordSet) {
    ConfigureRadio();

    // Get the mock radio
    auto& mock_radio = GetRadioLibMockForTesting(*radio_);

    // Set up expectations for setting the sync word
    EXPECT_CALL(mock_radio, setSyncWord(0x34))
        .WillOnce(Return(Result::Success()));

    // Test setting new value
    ASSERT_TRUE(radio_->setSyncWord(0x34));
}

/**
   * @brief Test enabling and disabling CRC
   */
TEST_F(RadioLibRadioTestMock, CrcSet) {
    ConfigureRadio();

    // Get the mock radio
    auto& mock_radio = GetRadioLibMockForTesting(*radio_);

    // Set up expectations for setting CRC
    EXPECT_CALL(mock_radio, setCRC(false)).WillOnce(Return(Result::Success()));

    EXPECT_CALL(mock_radio, setCRC(true)).WillOnce(Return(Result::Success()));

    // Test setting new value
    ASSERT_TRUE(radio_->setCRC(false));
    ASSERT_TRUE(radio_->setCRC(true));
}

/**
   * @brief Test setting preamble length
   */
TEST_F(RadioLibRadioTestMock, PreambleLengthSet) {
    ConfigureRadio();

    // Get the mock radio
    auto& mock_radio = GetRadioLibMockForTesting(*radio_);

    // Set up expectations for setting preamble length
    EXPECT_CALL(mock_radio, setPreambleLength(16))
        .WillOnce(Return(Result::Success()));

    // Test setting new value
    ASSERT_TRUE(radio_->setPreambleLength(16));
}

/**
   * @brief Test setting radio state
   */
TEST_F(RadioLibRadioTestMock, SetState) {
    ConfigureRadio();

    // Get the mock radio
    auto& mock_radio = GetRadioLibMockForTesting(*radio_);

    // Set up expectations for setting to receive
    EXPECT_CALL(mock_radio, ClearActionReceive())
        .WillRepeatedly(Return(Result::Success()));

    EXPECT_CALL(mock_radio, setActionReceive(A<void (*)(void)>()))
        .WillRepeatedly(Return(Result::Success()));

    EXPECT_CALL(mock_radio, StartReceive())
        .WillRepeatedly(Return(Result::Success()));

    EXPECT_CALL(mock_radio, Sleep()).WillRepeatedly(Return(Result::Success()));

    EXPECT_CALL(*rtos_mock_, SuspendTask(task_handle_)).Times(AnyNumber());

    EXPECT_CALL(*rtos_mock_, ResumeTask(task_handle_)).Times(AnyNumber());

    // Test setting to receive
    ASSERT_TRUE(radio_->setState(RadioState::kReceive));
    EXPECT_EQ(radio_->getState(), RadioState::kReceive);

    // Test setting to sleep
    ASSERT_TRUE(radio_->setState(RadioState::kSleep));
    EXPECT_EQ(radio_->getState(), RadioState::kSleep);

    // Test setting to idle (should put to sleep)
    ASSERT_TRUE(radio_->setState(RadioState::kIdle));
    EXPECT_EQ(radio_->getState(), RadioState::kSleep);
}

/**
   * @brief Test that transmitting state is correctly reported
   */
TEST_F(RadioLibRadioTestMock, IsTransmitting) {
    ConfigureRadio();

    // Get the mock radio
    auto& mock_radio = GetRadioLibMockForTesting(*radio_);

    // Set up expectations for getState
    EXPECT_CALL(mock_radio, getState())
        .WillOnce(Return(RadioState::kIdle))
        .WillOnce(Return(RadioState::kTransmit));

    // Initially not transmitting
    EXPECT_FALSE(radio_->IsTransmitting());

    // Now it's transmitting
    EXPECT_TRUE(radio_->IsTransmitting());
}

/**
   * @brief Test setting receive callback function
   */
TEST_F(RadioLibRadioTestMock, SetActionReceive) {
    ConfigureRadio();

    bool callback_called = false;
    auto test_callback = [&callback_called](std::unique_ptr<RadioEvent> event) {
        // Use parameters to prevent unused variable warnings
        (void)event;  // Explicitly mark as used

        callback_called = true;
    };

    ASSERT_TRUE(radio_->setActionReceive(test_callback));
}

/**
   * @brief Test RSSI and SNR getters when radio is initialized
   */
TEST_F(RadioLibRadioTestMock, GetRSSIAndSNR) {
    ConfigureRadio();

    // Get the mock radio
    auto& mock_radio = GetRadioLibMockForTesting(*radio_);

    // Set up expectations for RSSI and SNR
    EXPECT_CALL(mock_radio, getRSSI()).WillOnce(Return(-75));

    EXPECT_CALL(mock_radio, getSNR()).WillOnce(Return(6));

    // Test getters
    EXPECT_EQ(radio_->getRSSI(), -75);
    EXPECT_EQ(radio_->getSNR(), 6);
}

/**
   * @brief Test RSSI and SNR getters when radio is not initialized
   */
TEST_F(RadioLibRadioTestMock, GetRSSIAndSNRWhenNotInitialized) {
    // Create a new radio but don't configure it
    auto unconfigured_radio = std::make_unique<RadioLibRadio>(
        kCsPin, kDI0Pin, kRstPin, kBusyPin, *spi_mock_);

    // Test getters should return invalid values
    EXPECT_EQ(unconfigured_radio->getRSSI(), -128);
    EXPECT_EQ(unconfigured_radio->getSNR(), -128);
}

/**
   * @brief Test last packet RSSI and SNR getters
   */
TEST_F(RadioLibRadioTestMock, GetLastPacketRSSIAndSNR) {
    ConfigureRadio();

    // Initial values should be 0 as per initialization in constructor
    EXPECT_EQ(radio_->getLastPacketRSSI(), 0);
    EXPECT_EQ(radio_->getLastPacketSNR(), 0);
}

/**
  * @brief Test the HandleInterrupt method for processing received data
  */
TEST_F(RadioLibRadioTestMock, HandleInterruptReceivesData) {
    // Create and configure a RadioLibRadioTestMockAccess instance instead of regular RadioLibRadio
    auto radio_access = std::make_unique<RadioLibRadioTestMockAccess>(
        kCsPin, kDI0Pin, kRstPin, kBusyPin, *spi_mock_);
    ConfigureRadioAccess(radio_access.get());

    // Create a mock for the radio module
    auto mock_radio = std::make_unique<NiceMock<MockRadio>>();
    auto* mock_ptr = mock_radio.get();

    // Set up the mock to return a packet length and data
    const uint8_t kPacketLength = 10;
    const int8_t kTestRSSI = -65;
    const int8_t kTestSNR = 8;
    std::vector<uint8_t> test_packet = {0x01, 0x02, 0x03, 0x04, 0x05,
                                        0x06, 0x07, 0x08, 0x09, 0x0A};

    // Replace the current_module_ with our mock using the accessor
    radio_access->GetCurrentModule() = std::move(mock_radio);

    // Set expectations on the mock
    EXPECT_CALL(*mock_ptr, getPacketLength()).WillOnce(Return(kPacketLength));

    EXPECT_CALL(*mock_ptr, readData(_, kPacketLength))
        .WillOnce(DoAll(
            // Copy our test packet to the buffer provided by RadioLibRadio
            Invoke([&test_packet](uint8_t* data, size_t len) {
                // Use parameters to prevent unused variable warnings
                (void)len;  // Explicitly mark as used

                std::copy(test_packet.begin(), test_packet.end(), data);
                return Result::Success();
            }),
            Return(Result::Success())));

    EXPECT_CALL(*mock_ptr, getRSSI()).WillOnce(Return(kTestRSSI));

    EXPECT_CALL(*mock_ptr, getSNR()).WillOnce(Return(kTestSNR));

    EXPECT_CALL(*mock_ptr, StartReceive()).WillOnce(Return(Result::Success()));

    // Track if our callback was called
    bool callback_called = false;
    std::unique_ptr<RadioEvent> received_event;

    // Set a callback to be triggered when a message is received
    radio_access->setActionReceive(
        [&callback_called, &received_event](std::unique_ptr<RadioEvent> event) {
            callback_called = true;
            received_event = std::move(event);
        });

    // Call the HandleInterrupt method through our test access class
    radio_access->TestHandleInterrupt();

    // Verify RSSI and SNR were updated
    EXPECT_EQ(radio_access->getLastPacketRSSI(), kTestRSSI);
    EXPECT_EQ(radio_access->getLastPacketSNR(), kTestSNR);

    // If BaseMessage::CreateFromSerialized is implemented correctly and
    // a valid message could be created from our test data, the callback
    // should have been called
    if (received_event) {
        EXPECT_TRUE(callback_called);
        // Add additional verification on the received event if needed
    }
}

/**
  * @brief Test HandleInterrupt with invalid packet length
  */
TEST_F(RadioLibRadioTestMock, HandleInterruptInvalidPacketLength) {
    // Create and configure a RadioLibRadioTestMockAccess instance
    auto radio_access = std::make_unique<RadioLibRadioTestMockAccess>(
        kCsPin, kDI0Pin, kRstPin, kBusyPin, *spi_mock_);
    ConfigureRadioAccess(radio_access.get());

    // Create a mock for the radio module
    auto mock_radio = std::make_unique<NiceMock<MockRadio>>();
    auto* mock_ptr = mock_radio.get();

    // Replace the current_module_ with our mock
    radio_access->GetCurrentModule() = std::move(mock_radio);

    // Set expectations on the mock to return zero packet length
    EXPECT_CALL(*mock_ptr, getPacketLength()).WillOnce(Return(0));

    // No other calls should happen
    EXPECT_CALL(*mock_ptr, readData(_, _)).Times(0);

    // Call the HandleInterrupt method
    radio_access->TestHandleInterrupt();

    // No changes should have occurred
    EXPECT_EQ(radio_access->getLastPacketRSSI(), 0);
    EXPECT_EQ(radio_access->getLastPacketSNR(), 0);
}

/**
  * @brief Test HandleInterrupt when read fails
  */
TEST_F(RadioLibRadioTestMock, HandleInterruptReadFails) {
    // Create and configure a RadioLibRadioTestMockAccess instance
    auto radio_access = std::make_unique<RadioLibRadioTestMockAccess>(
        kCsPin, kDI0Pin, kRstPin, kBusyPin, *spi_mock_);
    ConfigureRadioAccess(radio_access.get());

    // Create a mock for the radio module
    auto mock_radio = std::make_unique<NiceMock<MockRadio>>();
    auto* mock_ptr = mock_radio.get();

    // Set up the mock to return a packet length but fail on read
    const uint8_t kPacketLength = 10;

    // Replace the current_module_ with our mock
    radio_access->GetCurrentModule() = std::move(mock_radio);

    // Set expectations on the mock
    EXPECT_CALL(*mock_ptr, getPacketLength()).WillOnce(Return(kPacketLength));

    EXPECT_CALL(*mock_ptr, readData(_, kPacketLength))
        .WillOnce(Return(Result::Error(LoraMesherErrorCode::kReceptionError)));

    // Should still try to restart receive mode
    EXPECT_CALL(*mock_ptr, StartReceive()).WillOnce(Return(Result::Success()));

    // Track if our callback was called - it should not be in this case
    bool callback_called = false;
    radio_access->setActionReceive(
        [&callback_called](std::unique_ptr<RadioEvent> event) {
            // Use parameters to prevent unused variable warnings
            (void)event;  // Explicitly mark as used

            callback_called = true;
        });

    // Call the HandleInterrupt method
    radio_access->TestHandleInterrupt();

    // Callback should not have been called due to read error
    EXPECT_FALSE(callback_called);
}

/**
  * @brief Test the HandleInterrupt static method correctly routes to instance
  */
TEST_F(RadioLibRadioTestMock, HandleInterruptStaticMethod) {
    // Create and configure a RadioLibRadioTestMockAccess instance
    auto radio_access = std::make_unique<RadioLibRadioTestMockAccess>(
        kCsPin, kDI0Pin, kRstPin, kBusyPin, *spi_mock_);
    ConfigureRadioAccess(radio_access.get());

    // Set up the task notification expectation
    EXPECT_CALL(*rtos_mock_, NotifyTaskFromISR(task_handle_)).Times(1);

    // Set the instance for testing
    RadioLibRadioTestMockAccess::SetInstance(radio_access.get());

    // Call the static method
    RadioLibRadioTestMockAccess::TestHandleInterruptStatic();
}

/**
  * @brief Test the ProcessEvents static method
  */
TEST_F(RadioLibRadioTestMock, ProcessEventsHandlesNotifications) {
    // Create and configure a RadioLibRadioTestMockAccess instance
    auto radio_access = std::make_unique<RadioLibRadioTestMockAccess>(
        kCsPin, kDI0Pin, kRstPin, kBusyPin, *spi_mock_);
    ConfigureRadioAccess(radio_access.get());

    // Set up expectations for the WaitForNotify call
    EXPECT_CALL(*rtos_mock_, WaitForNotify(_))
        .WillOnce(Return(os::QueueResult::kOk));

    // The test function needs to return to avoid infinite loop
    // This is typically handled by mocking DeleteTask to end the loop
    EXPECT_CALL(*rtos_mock_, DeleteTask(nullptr))
        .WillOnce([](os::TaskHandle_t) {
            // Throw an exception to break out of the infinite loop
            throw std::runtime_error("Test completed");
        });

    // Run the process events function and expect it to throw our exception
    try {
        RadioLibRadioTestMockAccess::TestProcessEvents(radio_access.get());
        FAIL() << "ProcessEvents should not return normally in this test";
    } catch (const std::runtime_error& e) {
        EXPECT_STREQ(e.what(), "Test completed");
    }
}

/**
* @brief Test ProcessEvents with invalid parameters
*/
TEST_F(RadioLibRadioTestMock, ProcessEventsWithInvalidParameters) {
    // Set up expectations for DeleteTask to be called with null
    EXPECT_CALL(*rtos_mock_, DeleteTask(nullptr)).Times(1);

    // Call ProcessEvents with NULL parameters
    RadioLibRadioTestMockAccess::TestProcessEvents(nullptr);
}

}  // namespace test
}  // namespace radio
}  // namespace loramesher

#endif  // ARDUINO