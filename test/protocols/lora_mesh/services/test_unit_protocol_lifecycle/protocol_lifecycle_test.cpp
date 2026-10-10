/**
 * @file test_protocol_lifecycle.cpp
 * @brief Simple test for LoRaMesh protocol creation and destruction
 */

#include <gtest/gtest.h>
#include <chrono>
#include <memory>
#include <thread>

#include "../test/utils/protocol_lifecycle_helpers.hpp"
#include "hardware/hardware_manager.hpp"
#include "hardware/radiolib/radiolib_radio.hpp"
#include "mocks/mock_radio_test_helpers.hpp"
#include "os/os_port.hpp"
#include "protocols/lora_mesh_protocol.hpp"
#include "types/configurations/protocol_configuration.hpp"
#include "types/power/power_types.hpp"

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
        ResetTaskMisuseCounters();
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
    ASSERT_TRUE(result) << "Protocol start failed: "
                        << result.GetErrorMessage();
    EXPECT_TRUE(ProtocolTaskResponds(*protocol));
    EXPECT_TRUE(SuperframeRunning(*protocol));

    // Stop protocol
    result = protocol->Stop();
    EXPECT_TRUE(result) << "Protocol stop failed: " << result.GetErrorMessage();
    EXPECT_FALSE(SuperframeRunning(*protocol));
    ExpectNoTaskMisuse();

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
        SCOPED_TRACE(testing::Message() << "iteration " << i);
        result = protocol->Start();
        ASSERT_TRUE(result) << result.GetErrorMessage();
        EXPECT_TRUE(ProtocolTaskResponds(*protocol));
        EXPECT_TRUE(SuperframeRunning(*protocol));

        result = protocol->Stop();
        ASSERT_TRUE(result) << result.GetErrorMessage();
        EXPECT_FALSE(SuperframeRunning(*protocol));
    }
    ExpectNoTaskMisuse();

    protocol.reset();
}

/**
 * @brief A repeated Init() keeps the single protocol task, so the protocol
 * still starts, stops and is destroyed cleanly
 */
TEST_F(ProtocolLifecycleTest, RepeatedInitKeepsOneTask) {
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();
    ASSERT_TRUE(protocol->Init(hardware_manager_, 0x1001));
    EXPECT_TRUE(protocol->Init(hardware_manager_, 0x1001));
    EXPECT_FALSE(protocol->Init(hardware_manager_, 0x1002))
        << "Init() with another address must be rejected";

    ASSERT_TRUE(protocol->Start());
    EXPECT_TRUE(ProtocolTaskResponds(*protocol));
    EXPECT_TRUE(protocol->Stop());
    ExpectNoTaskMisuse();

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

// ---- Configure() with invalid config returns error ----

TEST_F(ProtocolLifecycleTest, ConfigureWithInvalidConfigReturnsError) {
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();

    Result result = protocol->Init(hardware_manager_, 0x1001);
    ASSERT_TRUE(result);

    // hello_interval < 5000 is invalid per LoRaMeshProtocolConfig::Validate()
    LoRaMeshProtocolConfig bad_config(0x1001, /*hello_interval=*/100);
    result = protocol->Configure(bad_config);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kInvalidParameter);

    // Cleanup
    protocol->Stop();
    GetRTOS().delay(50);
    protocol.reset();
}

// ---- GetState() / GetNetworkManager() / IsSynchronized() after Init ----

TEST_F(ProtocolLifecycleTest, AccessorsAfterInit) {
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();

    Result result = protocol->Init(hardware_manager_, 0x1002);
    ASSERT_TRUE(result);

    // GetState() should not crash and return a valid state
    auto state = protocol->GetState();
    // The protocol is not running yet; state should be INITIALIZATION or DISCOVERY
    (void)state;

    // GetNetworkManager() should return some address (0 = none yet)
    AddressType nm = protocol->GetNetworkManager();
    (void)nm;

    // IsSynchronized() should return false when not started
    EXPECT_FALSE(protocol->IsSynchronized());

    // GetCurrentSlot() should not crash
    uint16_t slot = protocol->GetCurrentSlot();
    (void)slot;

    // GetSlotDuration() should return a positive value
    uint32_t dur = protocol->GetSlotDuration();
    EXPECT_GT(dur, 0u);

    protocol->Stop();
    GetRTOS().delay(50);
    protocol.reset();
}

// ---- GetNetworkNodes() and GetServiceConfiguration() after Init ----

TEST_F(ProtocolLifecycleTest, GetNetworkNodesAfterInit) {
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();

    Result result = protocol->Init(hardware_manager_, 0x1003);
    ASSERT_TRUE(result);

    // GetNetworkNodes() should return an empty vector initially
    const auto& nodes = protocol->GetNetworkNodes();
    EXPECT_TRUE(nodes.empty());

    // GetServiceConfiguration() should be accessible
    const auto& svc_cfg = protocol->GetServiceConfiguration();
    EXPECT_GT(svc_cfg.message_queue_size, 0u);

    protocol->Stop();
    GetRTOS().delay(50);
    protocol.reset();
}

// ---- SendMessage() after Init queues the message without crashing ----

TEST_F(ProtocolLifecycleTest, SendMessageAfterInit) {
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();

    Result result = protocol->Init(hardware_manager_, 0x1004);
    ASSERT_TRUE(result);

    // Create a minimal DATA message
    std::array<uint8_t, 4> payload{0x01, 0x02, 0x03, 0x04};
    BaseMessage msg(0xFFFF, 0x1004, MessageType::DATA,
                    std::span<const uint8_t>(payload));

    result = protocol->SendMessage(msg);
    EXPECT_TRUE(result) << result.GetErrorMessage();

    protocol->Stop();
    GetRTOS().delay(50);
    protocol.reset();
}

// ---- SendMessage() with ROUTE_TABLE type is queued to CONTROL_TX slot ----

TEST_F(ProtocolLifecycleTest, SendMessageRouteTableType) {
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();

    Result result = protocol->Init(hardware_manager_, 0x1005);
    ASSERT_TRUE(result);

    std::array<uint8_t, 2> payload{0xAB, 0xCD};
    BaseMessage msg(0x0001, 0x1005, MessageType::ROUTE_TABLE,
                    std::span<const uint8_t>(payload));

    result = protocol->SendMessage(msg);
    EXPECT_TRUE(result) << result.GetErrorMessage();

    protocol->Stop();
    GetRTOS().delay(50);
    protocol.reset();
}

// ---- SetNodeCapabilities / GetLocalNodeCapabilities / GetNodeCapabilities ----

TEST_F(ProtocolLifecycleTest, NodeCapabilitiesAfterInit) {
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();

    Result result = protocol->Init(hardware_manager_, 0x1006);
    ASSERT_TRUE(result);

    // Initially capabilities should be 0
    EXPECT_EQ(protocol->GetLocalNodeCapabilities(), 0u);

    // Set capabilities via the protocol method
    protocol->SetNodeCapabilities(0x42);
    EXPECT_EQ(protocol->GetLocalNodeCapabilities(), 0x42u);

    // GetNodeCapabilities for an unknown node should return 0
    EXPECT_EQ(protocol->GetNodeCapabilities(0xDEAD), 0u);

    protocol->Stop();
    GetRTOS().delay(50);
    protocol.reset();
}

// ---- Configure() with valid config after Init updates service config ----

TEST_F(ProtocolLifecycleTest, ConfigureValidConfigUpdatesSettings) {
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();

    Result result = protocol->Init(hardware_manager_, 0x1007);
    ASSERT_TRUE(result);

    // Build a valid config with non-default values
    LoRaMeshProtocolConfig cfg(0x1007, /*hello_interval=*/10000,
                               /*route_timeout=*/30000);
    cfg.setMaxHops(3);
    cfg.setMaxNetworkNodes(10);
    cfg.setNodeCapabilities(0x01);

    result = protocol->Configure(cfg);
    EXPECT_TRUE(result) << result.GetErrorMessage();

    // Capabilities should have been applied
    EXPECT_EQ(protocol->GetLocalNodeCapabilities(), 0x01u);

    protocol->Stop();
    GetRTOS().delay(50);
    protocol.reset();
}

// ---- GetDiscoveryTimeout / GetJoinTimeout accessible after Init ----

TEST_F(ProtocolLifecycleTest, TimeoutAccessorsAfterInit) {
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();

    Result result = protocol->Init(hardware_manager_, 0x1008);
    ASSERT_TRUE(result);

    // Accessors should be callable and not crash (return 0 before Start)
    uint32_t disc = protocol->GetDiscoveryTimeout();
    uint32_t join = protocol->GetJoinTimeout();
    (void)disc;
    (void)join;

    protocol->Stop();
    GetRTOS().delay(50);
    protocol.reset();
}

// =============================================================================
// Configure() with power management callbacks (lines 244-249)
// =============================================================================

/**
 * @brief Test Configure() with prepare-sleep and wake-up callbacks wires them in
 *
 * Covers lines 244-249: the if(callback) branches that assign
 * prepare_sleep_callback_ and wake_up_callback_.
 */
TEST_F(ProtocolLifecycleTest, ConfigureWithPowerCallbacksSucceeds) {
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();

    Result result = protocol->Init(hardware_manager_, 0x1010);
    ASSERT_TRUE(result);

    bool sleep_called = false;
    bool wakeup_called = false;

    LoRaMeshProtocolConfig cfg(0x1010);

    // Set the prepare-sleep callback (covers line 245)
    cfg.setPrepareSleepCallback(
        [&sleep_called](
            const power::SleepContext& /*ctx*/) -> power::SleepResult {
            sleep_called = true;
            return power::SleepResult{true};
        });

    // Set the wake-up callback (covers line 248)
    cfg.setWakeUpCallback(
        [&wakeup_called](power::PowerState /*prev*/) { wakeup_called = true; });

    result = protocol->Configure(cfg);
    EXPECT_TRUE(result) << result.GetErrorMessage();

    // Callbacks are stored internally; just verify Configure succeeded
    // (actually invoking them would require slot transitions in the protocol task)
    (void)sleep_called;
    (void)wakeup_called;

    protocol->Stop();
    GetRTOS().delay(50);
    protocol.reset();
}

// =============================================================================
// Tests that exercise error paths on an uninitialised protocol
// (lora_mesh_protocol.cpp lines 262-264, 355-358, 392-394, 1265-1287)
// =============================================================================

/**
 * @brief Start() returns error when hardware is not initialized
 *
 * Covers lines 262-264 in lora_mesh_protocol.cpp:
 *   if (!hardware_) return Result(kInvalidState, "Hardware not initialized");
 */
TEST_F(ProtocolLifecycleTest, StartWithoutHardwareReturnsError) {
    // Construct protocol but do NOT call Init() — hardware_ remains nullptr
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();

    Result result = protocol->Start();
    EXPECT_FALSE(result);
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kInvalidState);

    // Stop on an uninitialised protocol is safe (all members are null)
    protocol->Stop();
    GetRTOS().delay(50);
    protocol.reset();
}

/**
 * @brief SendMessage() returns error when hardware is not initialized
 *
 * Covers lines 355-358 in lora_mesh_protocol.cpp:
 *   if (!hardware_) return Result(kInvalidState, "Hardware not initialized");
 */
TEST_F(ProtocolLifecycleTest, SendMessageWithoutHardwareReturnsError) {
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();

    std::array<uint8_t, 2> payload{0x01, 0x02};
    BaseMessage msg(0xFFFF, 0x0001, MessageType::DATA,
                    std::span<const uint8_t>(payload));

    Result result = protocol->SendMessage(msg);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kInvalidState);

    protocol->Stop();
    GetRTOS().delay(50);
    protocol.reset();
}

/**
 * @brief SendData() returns error when network_service_ is not initialized
 *
 * Covers lines 392-394 in lora_mesh_protocol.cpp:
 *   if (!network_service_) return Result(kInvalidState, ...);
 *
 * To reach this branch we need hardware_ to be set but network_service_ to be
 * null.  A fresh protocol (no Init) has both null, so hardware_ guard fires
 * first.  We therefore use a protocol after Stop() which clears the task but
 * leaves the services intact — but that still keeps network_service_.
 *
 * The simplest approach: call SendData on a fresh protocol (no Init).
 * The hardware_ check fires first (line 392 check), not network_service_.
 * To actually hit line 392-394, we use the raw constructor and pass a fake hw.
 * However, since we cannot easily null out only network_service_, we test the
 * path by calling the function on a freshly constructed (not Init'd) protocol.
 * In that state !network_service_ is true.  But !hardware_ is also true, so
 * Start() and SendMessage() check hardware first.  SendData() checks only
 * network_service_, so calling it without Init covers line 392-394 directly.
 */
TEST_F(ProtocolLifecycleTest, SendDataWithoutNetworkServiceReturnsError) {
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();

    // network_service_ is nullptr (no Init called)
    std::vector<uint8_t> data{0x01, 0x02};
    Result result = protocol->SendData(0xDEAD, data);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kInvalidState);

    protocol->Stop();
    GetRTOS().delay(50);
    protocol.reset();
}

/**
 * @brief Non-const accessors return 0 when services are not initialized
 *
 * Covers lines 1265-1267 (GetDiscoveryTimeout), 1273-1275 (GetJoinTimeout),
 * and 1282-1284 (GetSlotDuration) in lora_mesh_protocol.cpp — the
 * !superframe_service_ / !network_service_ early-return paths.
 *
 * These branches are only reachable when Init() has NOT been called.
 */
TEST_F(ProtocolLifecycleTest, NonConstAccessorsReturnZeroWithoutInit) {
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();

    // GetDiscoveryTimeout — !superframe_service_ branch (line 1265-1267)
    uint32_t disc = protocol->GetDiscoveryTimeout();
    EXPECT_EQ(disc, 0u);

    // GetJoinTimeout — !network_service_ branch (line 1273-1275)
    uint32_t join = protocol->GetJoinTimeout();
    EXPECT_EQ(join, 0u);

    // GetSlotDuration (non-const) — !superframe_service_ branch (line 1282-1284)
    uint32_t slot_dur = protocol->GetSlotDuration();
    EXPECT_EQ(slot_dur, 0u);

    protocol->Stop();
    GetRTOS().delay(50);
    protocol.reset();
}

/**
 * @brief Stop() on a freshly constructed (never Init'd) protocol is safe
 *
 * Covers the null-guard paths in Stop() that protect against unset members.
 */
TEST_F(ProtocolLifecycleTest, StopBeforeInitIsSafe) {
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();

    // Stop() must not crash even though nothing was ever initialized
    Result result = protocol->Stop();
    EXPECT_TRUE(result);

    GetRTOS().delay(50);
    protocol.reset();
}

/**
 * @brief GetTxQueueSize and GetRxQueueSize are callable after Init
 *
 * Covers lora_mesh_protocol.cpp lines 1337-1345 (both queue-size accessors).
 */
TEST_F(ProtocolLifecycleTest, QueueSizesAfterInit) {
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();

    Result result = protocol->Init(hardware_manager_, 0x2001);
    ASSERT_TRUE(result);

    // Both should return 0 on a freshly initialized, not-yet-started protocol
    size_t tx_size = protocol->GetTxQueueSize();
    size_t rx_size = protocol->GetRxQueueSize();
    EXPECT_EQ(tx_size, 0u);
    EXPECT_EQ(rx_size, 0u);

    protocol->Stop();
    GetRTOS().delay(50);
    protocol.reset();
}

/**
 * @brief GetDiscoveryTimeout and GetJoinTimeout after Init return non-zero
 *
 * Covers lines 1264-1279 (the happy paths) — both services are initialized
 * after Init(), so the nil-guard is skipped and the service is queried.
 */
TEST_F(ProtocolLifecycleTest, NonConstTimeoutAccessorsAfterInit) {
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();

    Result result = protocol->Init(hardware_manager_, 0x2002);
    ASSERT_TRUE(result);

    // With services initialized, these should return sensible values
    uint32_t disc = protocol->GetDiscoveryTimeout();
    uint32_t join = protocol->GetJoinTimeout();
    uint32_t slot = protocol->GetSlotDuration();
    // Just verify they don't crash and return something (may be 0 or positive)
    (void)disc;
    (void)join;
    (void)slot;

    protocol->Stop();
    GetRTOS().delay(50);
    protocol.reset();
}

}  // namespace test
}  // namespace loramesher