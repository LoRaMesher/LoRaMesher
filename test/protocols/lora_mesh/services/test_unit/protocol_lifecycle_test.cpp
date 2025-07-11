/**
 * @file test_protocol_lifecycle.cpp
 * @brief Simple test for LoRaMesh protocol creation and destruction
 */

#include <gtest/gtest.h>
#include <chrono>
#include <memory>
#include <thread>

#include "hardware/hardware_manager.hpp"
#include "hardware/radiolib/radiolib_radio.hpp"
#include "mocks/mock_radio_test_helpers.hpp"
#include "os/os_port.hpp"
#include "protocols/lora_mesh_protocol.hpp"
#include "types/configurations/protocol_configuration.hpp"

namespace loramesher {
namespace test {

/**
 * @brief Simple test for protocol lifecycle without network simulation
 */
class ProtocolLifecycleTest : public ::testing::Test {
   protected:
    void SetUp() override {
        // Create pin and radio configuration
        pin_config_.setNss(10);
        pin_config_.setDio0(11);
        pin_config_.setReset(12);
        pin_config_.setDio1(13);

        // Create hardware manager
        hardware_manager_ = std::make_shared<hardware::HardwareManager>(
            pin_config_, radio_config_);

        ASSERT_TRUE(hardware_manager_->Initialize());
    }

    void TearDown() override {
        // Ensure proper cleanup before resetting hardware manager
        if (hardware_manager_) {
            hardware_manager_.reset();
        }
    }

    PinConfig pin_config_;
    RadioConfig radio_config_;
    std::shared_ptr<hardware::HardwareManager> hardware_manager_;
};

/**
 * @brief Test basic protocol creation and destruction without starting
 */
TEST_F(ProtocolLifecycleTest, CreateAndDestroy) {
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();

    // Initialize protocol
    Result result = protocol->Init(hardware_manager_, 0x1001);
    EXPECT_TRUE(result) << "Protocol initialization failed: "
                        << result.GetErrorMessage();

    // Configure protocol
    LoRaMeshProtocolConfig config(0x1001);
    result = protocol->Configure(config);
    EXPECT_TRUE(result) << "Protocol configuration failed: "
                        << result.GetErrorMessage();

    // Call Stop to clean up properly (even though we didn't start)
    result = protocol->Stop();
    EXPECT_TRUE(result) << "Protocol stop failed: " << result.GetErrorMessage();

    // Give time for cleanup
    GetRTOS().delay(100);

    // Destroy protocol
    protocol.reset();
}

/**
 * @brief Test basic protocol start and stop
 */
TEST_F(ProtocolLifecycleTest, StartAndStop) {
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();

    // Initialize protocol
    Result result = protocol->Init(hardware_manager_, 0x1001);
    EXPECT_TRUE(result) << "Protocol initialization failed: "
                        << result.GetErrorMessage();

    // Configure protocol
    LoRaMeshProtocolConfig config(0x1001);
    result = protocol->Configure(config);
    EXPECT_TRUE(result) << "Protocol configuration failed: "
                        << result.GetErrorMessage();

    // Start protocol
    result = protocol->Start();
    EXPECT_TRUE(result) << "Protocol start failed: "
                        << result.GetErrorMessage();

    // Let it run briefly
    GetRTOS().delay(100);

    // Stop protocol
    result = protocol->Stop();
    EXPECT_TRUE(result) << "Protocol stop failed: " << result.GetErrorMessage();

    // Destroy protocol
    protocol.reset();
}

/**
 * @brief Test multiple start/stop cycles
 */
TEST_F(ProtocolLifecycleTest, MultipleStartStop) {
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();

    Result result = protocol->Init(hardware_manager_, 0x1001);
    ASSERT_TRUE(result);

    LoRaMeshProtocolConfig config(0x1001);
    result = protocol->Configure(config);
    ASSERT_TRUE(result);

    // Test multiple start/stop cycles
    for (int i = 0; i < 3; ++i) {
        result = protocol->Start();
        EXPECT_TRUE(result) << "Start failed on iteration " << i;

        GetRTOS().delay(100);

        result = protocol->Stop();
        EXPECT_TRUE(result) << "Stop failed on iteration " << i;
    }

    // Final cleanup delay
    GetRTOS().delay(100);
    protocol.reset();
}

/**
 * @brief Test pause/resume functionality
 */
TEST_F(ProtocolLifecycleTest, PauseResume) {
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();

    Result result = protocol->Init(hardware_manager_, 0x1001);
    ASSERT_TRUE(result);

    LoRaMeshProtocolConfig config(0x1001);
    result = protocol->Configure(config);
    ASSERT_TRUE(result);

    result = protocol->Start();
    ASSERT_TRUE(result);

    // Test pause
    result = protocol->Pause();
    EXPECT_TRUE(result) << "Pause failed: " << result.GetErrorMessage();

    GetRTOS().delay((50));

    // Test resume
    result = protocol->Resume();
    EXPECT_TRUE(result) << "Resume failed: " << result.GetErrorMessage();

    GetRTOS().delay((50));

    result = protocol->Stop();
    EXPECT_TRUE(result);

    protocol.reset();
}

}  // namespace test
}  // namespace loramesher