/**
 * @file test/hardware/test_hardware.cpp
 * @brief Hardware manager unit tests
 */
#include <gtest/gtest.h>
#include "os/rtos.hpp"
#include <memory>

#include "hardware/hardware_manager.hpp"
#include "mocks/mock_radio_test_helpers.hpp"

namespace loramesher {
namespace hardware {
namespace test {

class HardwareManagerTest : public ::testing::Test {
   protected:
    void SetUp() override {
        pin_config_.setNss(10);
        pin_config_.setDio0(11);
        pin_config_.setReset(12);
        pin_config_.setDio1(13);
    }

    std::shared_ptr<HardwareManager> CreateManager() {
        return std::make_shared<HardwareManager>(pin_config_, radio_config_);
    }

    PinConfig pin_config_;
    RadioConfig radio_config_;
};

// ---- Initialize ----

TEST_F(HardwareManagerTest, InitializeSuccess) {
    auto mgr = CreateManager();
    Result result = mgr->Initialize();
    EXPECT_TRUE(result) << result.GetErrorMessage();
}

TEST_F(HardwareManagerTest, InitializeIdempotent) {
    auto mgr = CreateManager();
    ASSERT_TRUE(mgr->Initialize());
    // Second Initialize() should succeed (idempotent)
    Result result = mgr->Initialize();
    EXPECT_TRUE(result) << result.GetErrorMessage();
}

// ---- Start ----

TEST_F(HardwareManagerTest, StartAfterInitialize) {
    auto mgr = CreateManager();
    ASSERT_TRUE(mgr->Initialize());
    Result result = mgr->Start();
    EXPECT_TRUE(result) << result.GetErrorMessage();
}

TEST_F(HardwareManagerTest, StartWithoutInitializeFails) {
    auto mgr = CreateManager();
    // Start() without Initialize() should fail
    Result result = mgr->Start();
    EXPECT_FALSE(result);
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kNotInitialized);
}

TEST_F(HardwareManagerTest, StartIdempotent) {
    auto mgr = CreateManager();
    ASSERT_TRUE(mgr->Initialize());
    ASSERT_TRUE(mgr->Start());
    // Second Start() is idempotent
    Result result = mgr->Start();
    EXPECT_TRUE(result) << result.GetErrorMessage();
}

// ---- Stop ----

TEST_F(HardwareManagerTest, StopAfterStart) {
    auto mgr = CreateManager();
    ASSERT_TRUE(mgr->Initialize());
    ASSERT_TRUE(mgr->Start());
    Result result = mgr->Stop();
    EXPECT_TRUE(result) << result.GetErrorMessage();
}

TEST_F(HardwareManagerTest, StopWithoutStartSucceeds) {
    auto mgr = CreateManager();
    ASSERT_TRUE(mgr->Initialize());
    // Stop when not running — should succeed
    Result result = mgr->Stop();
    EXPECT_TRUE(result) << result.GetErrorMessage();
}

TEST_F(HardwareManagerTest, StopWithoutInitializeFails) {
    auto mgr = CreateManager();
    Result result = mgr->Stop();
    EXPECT_FALSE(result);
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kNotInitialized);
}

// ---- StartReceive ----

TEST_F(HardwareManagerTest, StartReceiveAfterInitialize) {
    auto mgr = CreateManager();
    ASSERT_TRUE(mgr->Initialize());
    Result result = mgr->StartReceive();
    EXPECT_TRUE(result) << result.GetErrorMessage();
}

TEST_F(HardwareManagerTest, StartReceiveWithoutInitializeFails) {
    auto mgr = CreateManager();
    Result result = mgr->StartReceive();
    EXPECT_FALSE(result);
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kNotInitialized);
}

// ---- setActionReceive ----

TEST_F(HardwareManagerTest, SetActionReceiveWithoutInitFails) {
    auto mgr = CreateManager();
    Result result = mgr->setActionReceive(nullptr);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kNotInitialized);
}

TEST_F(HardwareManagerTest, SetActionReceiveAfterInit) {
    auto mgr = CreateManager();
    ASSERT_TRUE(mgr->Initialize());
    bool called = false;
    auto cb = [&called](std::unique_ptr<radio::RadioEvent>) {
        called = true;
    };
    Result result = mgr->setActionReceive(cb);
    EXPECT_TRUE(result) << result.GetErrorMessage();
}

// ---- SendMessage ----

TEST_F(HardwareManagerTest, SendMessageWithoutRunningFails) {
    auto mgr = CreateManager();
    ASSERT_TRUE(mgr->Initialize());
    // Not started → SendMessage should fail
    auto opt_msg = BaseMessage::Create(0x1234, 0x5678, MessageType::PING,
                                       std::vector<uint8_t>{0x01});
    ASSERT_TRUE(opt_msg.has_value());
    Result result = mgr->SendMessage(*opt_msg);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kInvalidState);
}

TEST_F(HardwareManagerTest, SendMessageAfterStartSucceeds) {
    auto mgr = CreateManager();
    ASSERT_TRUE(mgr->Initialize());
    ASSERT_TRUE(mgr->Start());
    auto opt_msg = BaseMessage::Create(0x1234, 0x5678, MessageType::PING,
                                       std::vector<uint8_t>{0x01});
    ASSERT_TRUE(opt_msg.has_value());
    Result result = mgr->SendMessage(*opt_msg);
    EXPECT_TRUE(result) << result.GetErrorMessage();
}

TEST_F(HardwareManagerTest, SendMessageWithCallbackInvokesCallback) {
    auto mgr = CreateManager();
    ASSERT_TRUE(mgr->Initialize());
    ASSERT_TRUE(mgr->Start());

    bool cb_called = false;
    auto cb = [&cb_called](std::unique_ptr<radio::RadioEvent> event) {
        if (event)
            cb_called = true;
    };
    ASSERT_TRUE(mgr->setActionReceive(cb));

    auto opt_msg = BaseMessage::Create(0x1234, 0x5678, MessageType::PING,
                                       std::vector<uint8_t>{0xAB});
    ASSERT_TRUE(opt_msg.has_value());
    Result result = mgr->SendMessage(*opt_msg);
    EXPECT_TRUE(result) << result.GetErrorMessage();
    EXPECT_TRUE(cb_called);
}

// ---- setPinConfig ----

TEST_F(HardwareManagerTest, SetPinConfigValidSucceeds) {
    auto mgr = CreateManager();
    PinConfig new_pins;
    new_pins.setNss(5);
    new_pins.setDio0(6);
    new_pins.setReset(7);
    new_pins.setDio1(8);
    Result result = mgr->setPinConfig(new_pins);
    EXPECT_TRUE(result) << result.GetErrorMessage();
}

// ---- setState ----

TEST_F(HardwareManagerTest, SetStateWithoutInitFails) {
    auto mgr = CreateManager();
    Result result = mgr->setState(radio::RadioState::kReceive);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kNotInitialized);
}

TEST_F(HardwareManagerTest, SetStateAfterInit) {
    auto mgr = CreateManager();
    ASSERT_TRUE(mgr->Initialize());
    Result result = mgr->setState(radio::RadioState::kReceive);
    EXPECT_TRUE(result) << result.GetErrorMessage();
}

// ---- updateRadioConfig ----

TEST_F(HardwareManagerTest, UpdateRadioConfigValidSucceeds) {
    auto mgr = CreateManager();
    RadioConfig new_config;
    Result result = mgr->updateRadioConfig(new_config);
    EXPECT_TRUE(result) << result.GetErrorMessage();
}

// ---- SetLocalAddress ----

TEST_F(HardwareManagerTest, SetLocalAddressAfterInit) {
    auto mgr = CreateManager();
    ASSERT_TRUE(mgr->Initialize());
    // Should not crash
    EXPECT_NO_THROW(mgr->SetLocalAddress(0x1234));
}

TEST_F(HardwareManagerTest, SetLocalAddressBeforeInit) {
    auto mgr = CreateManager();
    // radio_ is null — should not crash (has null guard)
    EXPECT_NO_THROW(mgr->SetLocalAddress(0x5678));
}

// ---- getTimeOnAir ----

TEST_F(HardwareManagerTest, GetTimeOnAirWithoutInitReturnsZero) {
    auto mgr = CreateManager();
    EXPECT_EQ(mgr->getTimeOnAir(10), 0u);
}

TEST_F(HardwareManagerTest, GetTimeOnAirAfterInit) {
    auto mgr = CreateManager();
    ASSERT_TRUE(mgr->Initialize());
    // With mock radio: should return a value (may be 0 on mock)
    uint32_t toa = mgr->getTimeOnAir(20);
    (void)toa;  // Just checking it doesn't crash
}

}  // namespace test
}  // namespace hardware
}  // namespace loramesher

#if defined(ARDUINO)
#include <Arduino.h>

void setup() {
    Serial.begin(115200);
    ::testing::InitGoogleTest();
}

void loop() {
    if (RUN_ALL_TESTS()) {}
    delay(1000);
}

#else
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    loramesher::os::RTOS::Init();
    if (RUN_ALL_TESTS()) {}
    return 0;
}
#endif
