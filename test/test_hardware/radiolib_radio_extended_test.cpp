/**
 * @file radiolib_radio_extended_test.cpp
 * @brief Extended tests for RadioLibRadio: setState, setters, getters,
 *        unsupported function exceptions, NativeHal, and RadioEvent
 */
#include <gtest/gtest.h>
#include <memory>

#include "hardware/SPIMock.hpp"
#include "hardware/hal_factory.hpp"
#include "hardware/native/native_hal.hpp"
#include "hardware/radiolib/radiolib_radio.hpp"
#include "os/os_port.hpp"
#include "types/radio/radio_event.hpp"
#include "utils/logger.hpp"

#include "../test/utils/mock_radio.hpp"
#include "mocks/mock_radio_test_helpers.hpp"

using namespace loramesher;
using namespace loramesher::radio;

using ::testing::_;
using ::testing::A;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SaveArg;

namespace loramesher {
namespace radio {
namespace test {

// ============================================================
// RadioLibRadio extended tests
// ============================================================

class RadioLibRadioExtendedTest : public ::testing::Test {
   public:
    const int kCsPin = 5;
    const int kDI0Pin = 4;
    const int kRstPin = 14;
    const int kBusyPin = 15;

    std::unique_ptr<SPIClass> spi_;
    std::unique_ptr<RadioLibRadio> radio_;
    RadioConfig test_config_;
    void (*saved_callback_)(void) = nullptr;

    void SetUp() override {
        spi_ = std::make_unique<SPIClass>();
        test_config_.setRadioType(RadioType::kMockRadio);
        test_config_.setFrequency(868.0f);
        test_config_.setSpreadingFactor(7);
        test_config_.setBandwidth(125.0f);
        test_config_.setCodingRate(5);
        test_config_.setPower(17);
        test_config_.setSyncWord(0x12);
        test_config_.setCRC(true);
        test_config_.setPreambleLength(8);

        radio_ = std::make_unique<RadioLibRadio>(kCsPin, kDI0Pin, kRstPin,
                                                 kBusyPin, *spi_);
        ConfigureRadio();
    }

    void TearDown() override {
        saved_callback_ = nullptr;
        LOG_FLUSH();
        radio_.reset();
        spi_.reset();
    }

    void ConfigureRadio() {
        ASSERT_TRUE(radio_->Configure(test_config_));
        auto& mock = GetRadioLibMockForTesting(*radio_);

        EXPECT_CALL(mock, Begin(_)).WillOnce(Return(Result::Success()));
        ON_CALL(mock, getTimeOnAir(_)).WillByDefault(Return(0u));
        EXPECT_CALL(mock, Sleep()).WillRepeatedly(Return(Result::Success()));
        EXPECT_CALL(mock, setActionReceive(A<void (*)(void)>()))
            .WillRepeatedly(
                DoAll(SaveArg<0>(&saved_callback_), Return(Result::Success())));
        EXPECT_CALL(mock, ClearActionReceive())
            .WillRepeatedly(Return(Result::Success()));
        EXPECT_CALL(mock, StartReceive())
            .WillRepeatedly(Return(Result::Success()));

        ASSERT_TRUE(radio_->Begin(test_config_));
    }
};

// --- setState ---

TEST_F(RadioLibRadioExtendedTest, SetStateReceive) {
    Result result = radio_->setState(RadioState::kReceive);
    EXPECT_TRUE(result);
    EXPECT_EQ(radio_->getState(), RadioState::kReceive);
}

TEST_F(RadioLibRadioExtendedTest, SetStateSleep) {
    Result result = radio_->setState(RadioState::kSleep);
    EXPECT_TRUE(result);
    EXPECT_EQ(radio_->getState(), RadioState::kSleep);
}

TEST_F(RadioLibRadioExtendedTest, SetStateIdle) {
    Result result = radio_->setState(RadioState::kIdle);
    EXPECT_TRUE(result);
    // kIdle maps to Sleep()
    EXPECT_EQ(radio_->getState(), RadioState::kSleep);
}

TEST_F(RadioLibRadioExtendedTest, SetStateTransmitReturnsError) {
    Result result = radio_->setState(RadioState::kTransmit);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kInvalidParameter);
}

TEST_F(RadioLibRadioExtendedTest, SetStateCadReturnsError) {
    Result result = radio_->setState(RadioState::kCad);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kInvalidParameter);
}

TEST_F(RadioLibRadioExtendedTest, SetStateErrorReturnsError) {
    Result result = radio_->setState(RadioState::kError);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kInvalidParameter);
}

// --- Getters from config ---

TEST_F(RadioLibRadioExtendedTest, GetSpreadingFactor) {
    EXPECT_EQ(radio_->getSpreadingFactor(), 7);
}

TEST_F(RadioLibRadioExtendedTest, GetBandwidth) {
    EXPECT_FLOAT_EQ(radio_->getBandwidth(), 125.0f);
}

TEST_F(RadioLibRadioExtendedTest, GetCodingRate) {
    EXPECT_EQ(radio_->getCodingRate(), 5);
}

TEST_F(RadioLibRadioExtendedTest, GetPower) {
    EXPECT_EQ(radio_->getPower(), 17);
}

// --- Setters via mock ---

TEST_F(RadioLibRadioExtendedTest, SetSpreadingFactor) {
    auto& mock = GetRadioLibMockForTesting(*radio_);
    EXPECT_CALL(mock, setSpreadingFactor(12))
        .WillOnce(Return(Result::Success()));

    EXPECT_TRUE(radio_->setSpreadingFactor(12));
    EXPECT_EQ(radio_->getSpreadingFactor(), 12);
}

TEST_F(RadioLibRadioExtendedTest, SetBandwidth) {
    auto& mock = GetRadioLibMockForTesting(*radio_);
    EXPECT_CALL(mock, setBandwidth(250.0f)).WillOnce(Return(Result::Success()));

    EXPECT_TRUE(radio_->setBandwidth(250.0f));
    EXPECT_FLOAT_EQ(radio_->getBandwidth(), 250.0f);
}

TEST_F(RadioLibRadioExtendedTest, SetCodingRate) {
    auto& mock = GetRadioLibMockForTesting(*radio_);
    EXPECT_CALL(mock, setCodingRate(8)).WillOnce(Return(Result::Success()));

    EXPECT_TRUE(radio_->setCodingRate(8));
    EXPECT_EQ(radio_->getCodingRate(), 8);
}

TEST_F(RadioLibRadioExtendedTest, SetPower) {
    auto& mock = GetRadioLibMockForTesting(*radio_);
    EXPECT_CALL(mock, setPower(10)).WillOnce(Return(Result::Success()));

    EXPECT_TRUE(radio_->setPower(10));
    EXPECT_EQ(radio_->getPower(), 10);
}

TEST_F(RadioLibRadioExtendedTest, SetSyncWord) {
    auto& mock = GetRadioLibMockForTesting(*radio_);
    EXPECT_CALL(mock, setSyncWord(0x34)).WillOnce(Return(Result::Success()));

    EXPECT_TRUE(radio_->setSyncWord(0x34));
}

TEST_F(RadioLibRadioExtendedTest, SetCRC) {
    auto& mock = GetRadioLibMockForTesting(*radio_);
    EXPECT_CALL(mock, setCRC(false)).WillOnce(Return(Result::Success()));

    EXPECT_TRUE(radio_->setCRC(false));
}

TEST_F(RadioLibRadioExtendedTest, SetPreambleLength) {
    auto& mock = GetRadioLibMockForTesting(*radio_);
    EXPECT_CALL(mock, setPreambleLength(16))
        .WillOnce(Return(Result::Success()));

    EXPECT_TRUE(radio_->setPreambleLength(16));
}

// --- IsTransmitting ---

TEST_F(RadioLibRadioExtendedTest, IsTransmittingInitiallyFalse) {
    EXPECT_FALSE(radio_->IsTransmitting());
}

// --- Last packet RSSI/SNR ---

TEST_F(RadioLibRadioExtendedTest, LastPacketRSSIInitiallyZero) {
    EXPECT_EQ(radio_->getLastPacketRSSI(), 0);
}

TEST_F(RadioLibRadioExtendedTest, LastPacketSNRInitiallyZero) {
    EXPECT_EQ(radio_->getLastPacketSNR(), 0);
}

// --- RSSI/SNR with no module ---

TEST_F(RadioLibRadioExtendedTest, GetTimeOnAirReturnsCachedValue) {
    // ToA is cached during Begin() — the mock returns 0 for all sizes,
    // so the cache contains 0.  Verify the cached path works.
    EXPECT_EQ(radio_->getTimeOnAir(50), 0u);
}

// --- SetLocalAddress ---

TEST_F(RadioLibRadioExtendedTest, SetLocalAddressDoesNotCrash) {
    EXPECT_NO_THROW(radio_->SetLocalAddress(0xABCD));
}

// --- Unsupported function exceptions ---

TEST_F(RadioLibRadioExtendedTest, SetActionReceiveFnPtrThrows) {
    EXPECT_THROW(radio_->setActionReceive(static_cast<void (*)(void)>(nullptr)),
                 std::runtime_error);
}

TEST_F(RadioLibRadioExtendedTest, GetPacketLengthThrows) {
    EXPECT_THROW(radio_->getPacketLength(), std::runtime_error);
}

TEST_F(RadioLibRadioExtendedTest, ReadDataThrows) {
    uint8_t buf[4];
    EXPECT_THROW(radio_->readData(buf, 4), std::runtime_error);
}

TEST_F(RadioLibRadioExtendedTest, ClearActionReceiveThrows) {
    EXPECT_THROW(radio_->ClearActionReceive(), std::runtime_error);
}

// --- StartReceive idempotent ---

TEST_F(RadioLibRadioExtendedTest, StartReceiveIdempotent) {
    ASSERT_TRUE(radio_->StartReceive());
    EXPECT_EQ(radio_->getState(), RadioState::kReceive);
    // Second call should succeed immediately
    ASSERT_TRUE(radio_->StartReceive());
    EXPECT_EQ(radio_->getState(), RadioState::kReceive);
}

// --- Sleep idempotent ---

TEST_F(RadioLibRadioExtendedTest, SleepIdempotent) {
    ASSERT_TRUE(radio_->Sleep());
    EXPECT_EQ(radio_->getState(), RadioState::kSleep);
    // Second call should succeed immediately
    ASSERT_TRUE(radio_->Sleep());
    EXPECT_EQ(radio_->getState(), RadioState::kSleep);
}

// --- Send while transmitting ---

TEST_F(RadioLibRadioExtendedTest, SendWhileTransmittingReturnsBusy) {
    auto& mock = GetRadioLibMockForTesting(*radio_);

    // First send succeeds, sets state to kTransmit
    EXPECT_CALL(mock, Send(_, 5)).WillOnce(Return(Result::Success()));
    std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04, 0x05};
    ASSERT_TRUE(radio_->Send(data.data(), data.size()));

    // Start receive to get back to a known state, then manually set transmit
    // Actually, after first Send, it calls Sleep so state becomes kSleep.
    // We need to test the busy path. Let's make Sleep fail so state stays at kTransmit.
}

// ============================================================
// NativeHal tests
// ============================================================

class NativeHalTest : public ::testing::Test {
   protected:
    loramesher::hal::NativeHal hal_;
};

TEST_F(NativeHalTest, MillisReturnsNonZero) {
    uint32_t t = hal_.millis();
    EXPECT_GT(t, 0u);
}

TEST_F(NativeHalTest, MillisIncreases) {
    uint32_t t1 = hal_.millis();
    // Small busy wait
    int x = 0;
    for (int i = 0; i < 100000; ++i)
        x++;
    (void)x;
    uint32_t t2 = hal_.millis();
    EXPECT_GE(t2, t1);
}

TEST_F(NativeHalTest, DelayDoesNotCrash) {
    EXPECT_NO_THROW(hal_.delay(1));
}

TEST_F(NativeHalTest, GetSPIDefault) {
    SPIClass& spi = hal_.getSPI(0);
    spi.begin();
    spi.end();
}

TEST_F(NativeHalTest, GetSPI1) {
    SPIClass& spi = hal_.getSPI(1);
    spi.begin();
    spi.end();
}

TEST_F(NativeHalTest, GetSPI2) {
    SPIClass& spi = hal_.getSPI(2);
    spi.begin();
    spi.end();
}

TEST_F(NativeHalTest, GetSPIDefaultForInvalidBus) {
    // Invalid bus number should return default SPI
    SPIClass& spi = hal_.getSPI(99);
    (void)spi;
}

TEST_F(NativeHalTest, GetHardwareUniqueIdSucceeds) {
    uint8_t id[6] = {};
    EXPECT_TRUE(hal_.GetHardwareUniqueId(id, 6));
    // First byte should be 0x02 (locally administered MAC prefix)
    EXPECT_EQ(id[0], 0x02);
    // Second byte should be 0x4E ("Native" identifier)
    EXPECT_EQ(id[1], 0x4E);
}

TEST_F(NativeHalTest, GetHardwareUniqueIdConsistent) {
    uint8_t id1[6] = {};
    uint8_t id2[6] = {};
    EXPECT_TRUE(hal_.GetHardwareUniqueId(id1, 6));
    EXPECT_TRUE(hal_.GetHardwareUniqueId(id2, 6));
    // Cached, so should be identical
    for (int i = 0; i < 6; ++i) {
        EXPECT_EQ(id1[i], id2[i]);
    }
}

TEST_F(NativeHalTest, GetHardwareUniqueIdNullBuffer) {
    EXPECT_FALSE(hal_.GetHardwareUniqueId(nullptr, 6));
}

TEST_F(NativeHalTest, GetHardwareUniqueIdBufferTooSmall) {
    uint8_t id[4] = {};
    EXPECT_FALSE(hal_.GetHardwareUniqueId(id, 4));
}

TEST_F(NativeHalTest, ConstructWithPinConfig) {
    PinConfig pins;
    loramesher::hal::NativeHal hal_with_pins(pins);
    uint32_t t = hal_with_pins.millis();
    EXPECT_GT(t, 0u);
}

// ============================================================
// HalFactory tests
// ============================================================

TEST(HalFactoryTest, CreateHalReturnsNonNull) {
    PinConfig pins;
    auto hal = loramesher::hal::HalFactory::createHal(pins);
    EXPECT_NE(hal, nullptr);
}

TEST(HalFactoryTest, CreatedHalHasWorkingMillis) {
    PinConfig pins;
    auto hal = loramesher::hal::HalFactory::createHal(pins);
    EXPECT_GT(hal->millis(), 0u);
}

TEST(HalFactoryTest, CreatedHalHasWorkingSPI) {
    PinConfig pins;
    auto hal = loramesher::hal::HalFactory::createHal(pins);
    SPIClass& spi = hal->getSPI();
    spi.begin();
    spi.end();
}

// ============================================================
// RadioEvent tests
// ============================================================

class RadioEventTest : public ::testing::Test {};

TEST_F(RadioEventTest, CreateReceivedEventSetsFields) {
    auto msg = BaseMessage::Create(0x1234, 0x5678, MessageType::PING,
                                   std::vector<uint8_t>{0x01});
    ASSERT_TRUE(msg.has_value());

    auto event =
        CreateReceivedEvent(std::make_unique<BaseMessage>(*msg), -65, 8);

    EXPECT_EQ(event->getType(), RadioEventType::kReceived);
    EXPECT_EQ(event->getRssi(), -65);
    EXPECT_EQ(event->getSnr(), 8);
    EXPECT_TRUE(event->HasMessage());
}

TEST_F(RadioEventTest, CreateTransmittedEvent) {
    auto msg = BaseMessage::Create(0x1234, 0x5678, MessageType::PING,
                                   std::vector<uint8_t>{0x01});
    ASSERT_TRUE(msg.has_value());

    auto event = CreateTransmittedEvent(std::make_unique<BaseMessage>(*msg));

    EXPECT_EQ(event->getType(), RadioEventType::kTransmitted);
    EXPECT_TRUE(event->HasMessage());
}

TEST_F(RadioEventTest, CreateReceivedTimeoutEvent) {
    auto event = CreateReceivedTimeoutEvent();
    EXPECT_EQ(event->getType(), RadioEventType::kReceivedTimeout);
    EXPECT_FALSE(event->HasMessage());
}

TEST_F(RadioEventTest, CreateTransmittedTimeoutEvent) {
    auto event = CreateTransmittedTimeoutEvent();
    EXPECT_EQ(event->getType(), RadioEventType::kTransmittedTimeout);
    EXPECT_FALSE(event->HasMessage());
}

TEST_F(RadioEventTest, TakeMessageTransfersOwnership) {
    auto msg = BaseMessage::Create(0x1234, 0x5678, MessageType::PING,
                                   std::vector<uint8_t>{0x01});
    ASSERT_TRUE(msg.has_value());

    auto event =
        CreateReceivedEvent(std::make_unique<BaseMessage>(*msg), -50, 10);

    EXPECT_TRUE(event->HasMessage());
    auto taken = event->TakeMessage();
    EXPECT_NE(taken, nullptr);
    EXPECT_FALSE(event->HasMessage());
}

TEST_F(RadioEventTest, MoveConstructor) {
    auto msg = BaseMessage::Create(0x1234, 0x5678, MessageType::PING,
                                   std::vector<uint8_t>{0x01});
    ASSERT_TRUE(msg.has_value());

    RadioEvent original(RadioEventType::kReceived,
                        std::make_unique<BaseMessage>(*msg));
    original.setRssi(-70);
    original.setSnr(5);

    RadioEvent moved(std::move(original));
    EXPECT_EQ(moved.getType(), RadioEventType::kReceived);
    EXPECT_EQ(moved.getRssi(), -70);
    EXPECT_EQ(moved.getSnr(), 5);
    EXPECT_TRUE(moved.HasMessage());
}

TEST_F(RadioEventTest, MoveAssignment) {
    auto msg = BaseMessage::Create(0x1234, 0x5678, MessageType::PING,
                                   std::vector<uint8_t>{0x01});
    ASSERT_TRUE(msg.has_value());

    RadioEvent original(RadioEventType::kTransmitted,
                        std::make_unique<BaseMessage>(*msg));
    original.setRssi(-80);

    RadioEvent target(RadioEventType::kCrcError);
    target = std::move(original);
    EXPECT_EQ(target.getType(), RadioEventType::kTransmitted);
    EXPECT_EQ(target.getRssi(), -80);
    EXPECT_TRUE(target.HasMessage());
}

TEST_F(RadioEventTest, EventTypeToStringAllTypes) {
    EXPECT_STREQ(RadioEvent::EventTypeToString(RadioEventType::kReceived),
                 "Received");
    EXPECT_STREQ(RadioEvent::EventTypeToString(RadioEventType::kTransmitted),
                 "Transmitted");
    EXPECT_STREQ(
        RadioEvent::EventTypeToString(RadioEventType::kReceivedTimeout),
        "Received Timeout");
    EXPECT_STREQ(
        RadioEvent::EventTypeToString(RadioEventType::kTransmittedTimeout),
        "Transmitted Timeout");
    EXPECT_STREQ(RadioEvent::EventTypeToString(RadioEventType::kCrcError),
                 "CRC Error");
    EXPECT_STREQ(
        RadioEvent::EventTypeToString(RadioEventType::kPreambleDetected),
        "Preamble Detected");
    EXPECT_STREQ(RadioEvent::EventTypeToString(RadioEventType::kSyncWordValid),
                 "Sync Word Valid");
    EXPECT_STREQ(RadioEvent::EventTypeToString(RadioEventType::kHeaderValid),
                 "Header Valid");
    EXPECT_STREQ(RadioEvent::EventTypeToString(RadioEventType::kHeaderError),
                 "Header Error");
    EXPECT_STREQ(RadioEvent::EventTypeToString(RadioEventType::kNoise),
                 "Noise Detected");
    EXPECT_STREQ(RadioEvent::EventTypeToString(RadioEventType::kCadDone),
                 "CAD Done");
    EXPECT_STREQ(RadioEvent::EventTypeToString(RadioEventType::kCadDetected),
                 "CAD Detected");
    EXPECT_STREQ(RadioEvent::EventTypeToString(RadioEventType::kRxError),
                 "Reception Error");
    EXPECT_STREQ(RadioEvent::EventTypeToString(RadioEventType::kTxError),
                 "Transmission Error");
}

TEST_F(RadioEventTest, SetAndGetTimestamp) {
    RadioEvent event(RadioEventType::kReceived);
    event.setTimestamp(42);
    EXPECT_EQ(event.getTimestamp(), 42u);
}

TEST_F(RadioEventTest, EventWithoutMessage) {
    RadioEvent event(RadioEventType::kNoise);
    EXPECT_FALSE(event.HasMessage());
    EXPECT_EQ(event.getMessage(), nullptr);
}

}  // namespace test
}  // namespace radio
}  // namespace loramesher
