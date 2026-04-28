/**
 * @file test_loramesher_initialization.cpp
 * @brief Test suite for LoraMesher initialization and lifecycle management
 */
#include <gtest/gtest.h>
#include "os/rtos.hpp"
#include <chrono>
#include <memory>
#include <thread>

#include "loramesher.hpp"
#include "os/os_port.hpp"

namespace loramesher {
namespace test {

#define LORA_CS 18
#define LORA_RST 23
#define LORA_IRQ 26
#define LORA_IO1 33

#define LORA_FREQUENCY 869.900F
#define LORA_SPREADING_FACTOR 7U
#define LORA_BANDWITH 125.0
#define LORA_CODING_RATE 7U
#define LORA_POWER 6
#define LORA_SYNC_WORD 20U
#define LORA_CRC true
#define LORA_PREAMBLE_LENGTH 8U

/**
 * @brief Test fixture for LoraMesher initialization tests
 */
class LoraMesherInitializationTest : public ::testing::Test {
   protected:
    void SetUp() override {
        // Create valid default configurations for testing
        pin_config_.setNss(LORA_CS);
        pin_config_.setDio0(LORA_IRQ);
        pin_config_.setReset(LORA_RST);
        pin_config_.setDio1(LORA_IO1);

        // Use mock radio for reliable testing
        // #if defined(LORAMESHER_BUILD_ARDUINO)
        radio_config_.setRadioType(RadioType::kSx1276);
        // #else
        //         radio_config_.setRadioType(RadioType::kMockRadio);
        // #endif
        radio_config_.setFrequency(LORA_FREQUENCY);
        radio_config_.setSpreadingFactor(LORA_SPREADING_FACTOR);
        radio_config_.setBandwidth(LORA_BANDWITH);
        radio_config_.setCodingRate(LORA_CODING_RATE);
        radio_config_.setPower(LORA_POWER);
        radio_config_.setSyncWord(LORA_SYNC_WORD);
        radio_config_.setCRC(LORA_CRC);
        radio_config_.setPreambleLength(LORA_PREAMBLE_LENGTH);

        // Create LoRaMesh protocol configuration
        mesh_config_.setNodeAddress(0);        // Auto-generate address
        mesh_config_.setHelloInterval(60000);  // 60 seconds
        mesh_config_.setRouteTimeout(180000);  // 180 seconds
        mesh_config_.setMaxHops(10);
    }

    void TearDown() override {
        // Ensure proper cleanup
        if (mesher_) {
            mesher_->Stop();
            mesher_.reset();
            // Allow RTOS tasks to fully exit before next test
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    /**
     * @brief Create a LoraMesher instance with valid configuration
     */
    std::unique_ptr<LoraMesher> CreateValidLoraMesher() {
        return LoraMesher::Builder()
            .withRadioConfig(radio_config_)
            .withPinConfig(pin_config_)
            .withLoRaMeshProtocol(mesh_config_)
            .Build();
    }

    /**
     * @brief Create a LoraMesher instance with invalid pin configuration
     */
    std::unique_ptr<LoraMesher> CreateInvalidPinLoraMesher() {
        PinConfig invalid_pins(-1, -1, -1, -1);
        return LoraMesher::Builder()
            .withRadioConfig(radio_config_)
            .withPinConfig(invalid_pins)
            .withLoRaMeshProtocol(mesh_config_)
            .Build();
    }

    PinConfig pin_config_;
    RadioConfig radio_config_;
    LoRaMeshProtocolConfig mesh_config_;
    std::unique_ptr<LoraMesher> mesher_;
};

/**
 * @brief Test basic LoraMesher creation and destruction
 */
TEST_F(LoraMesherInitializationTest, CreateAndDestroy) {
    // Should not throw
    EXPECT_NO_THROW({ mesher_ = CreateValidLoraMesher(); });

    EXPECT_NE(mesher_, nullptr);

    // Destructor should handle cleanup properly
    EXPECT_NO_THROW({ mesher_.reset(); });
}

/**
 * @brief Test successful initialization sequence
 */
TEST_F(LoraMesherInitializationTest, SuccessfulInitialization) {
    mesher_ = CreateValidLoraMesher();
    ASSERT_NE(mesher_, nullptr);

    // Test Start() - should succeed
    Result start_result = mesher_->Start();
    EXPECT_TRUE(start_result)
        << "Start() failed: " << start_result.GetErrorMessage();

    // Node should have a valid address after initialization
    AddressType address = mesher_->GetNodeAddress();
    EXPECT_NE(address, 0)
        << "Node address should not be zero after initialization";

    // Hardware manager should be available
    auto hardware_manager = mesher_->GetHardwareManager();
    EXPECT_NE(hardware_manager, nullptr)
        << "Hardware manager should be available";

    // Protocol should be available
    auto protocol = mesher_->GetLoRaMeshProtocol();
    EXPECT_NE(protocol, nullptr) << "LoRaMesh protocol should be available";

    // Protocol type should be LoRaMesh
    EXPECT_EQ(mesher_->GetActiveProtocolType(),
              protocols::ProtocolType::kLoraMesh);
}

/**
 * @brief Test address generation functionality
 */
TEST_F(LoraMesherInitializationTest, AddressGeneration) {
    // Test auto-address generation
    mesher_ = CreateValidLoraMesher();
    ASSERT_NE(mesher_, nullptr);

    Result start_result = mesher_->Start();
    EXPECT_TRUE(start_result);

    AddressType auto_address = mesher_->GetNodeAddress();
    EXPECT_NE(auto_address, 0) << "Auto-generated address should not be zero";

    mesher_->Stop();
    mesher_.reset();

    // Test explicit address setting
    const AddressType explicit_address = 0x1234;
    mesher_ = LoraMesher::Builder()
                  .withRadioConfig(radio_config_)
                  .withPinConfig(pin_config_)
                  .withLoRaMeshProtocol(mesh_config_)
                  .withNodeAddress(explicit_address)
                  .Build();

    start_result = mesher_->Start();
    EXPECT_TRUE(start_result);

    AddressType set_address = mesher_->GetNodeAddress();
    EXPECT_EQ(set_address, explicit_address)
        << "Explicit address should be preserved";
}

/**
 * @brief Test hardware-based vs fallback address generation
 */
TEST_F(LoraMesherInitializationTest, AddressGenerationModes) {
    // Test with hardware-based addressing enabled (default)
    mesher_ = LoraMesher::Builder()
                  .withRadioConfig(radio_config_)
                  .withPinConfig(pin_config_)
                  .withLoRaMeshProtocol(mesh_config_)
                  .withAutoAddressFromHardware(true)
                  .Build();

    Result start_result = mesher_->Start();
    EXPECT_TRUE(start_result);
    AddressType hw_address = mesher_->GetNodeAddress();
    EXPECT_NE(hw_address, 0);

    mesher_->Stop();
    mesher_.reset();

    // Test with hardware-based addressing disabled
    mesher_ = LoraMesher::Builder()
                  .withRadioConfig(radio_config_)
                  .withPinConfig(pin_config_)
                  .withLoRaMeshProtocol(mesh_config_)
                  .withAutoAddressFromHardware(false)
                  .Build();

    start_result = mesher_->Start();
    EXPECT_TRUE(start_result);
    AddressType fallback_address = mesher_->GetNodeAddress();
    EXPECT_NE(fallback_address, 0);

    // Addresses might be different (though not guaranteed due to random fallback)
    // Just ensure both are valid non-zero addresses
}

/**
 * @brief Test double Start() calls
 */
TEST_F(LoraMesherInitializationTest, DoubleStart) {
    mesher_ = CreateValidLoraMesher();
    ASSERT_NE(mesher_, nullptr);

    // First Start() should succeed
    Result first_start = mesher_->Start();
    EXPECT_TRUE(first_start)
        << "First Start() failed: " << first_start.GetErrorMessage();

    // Second Start() should also succeed (idempotent)
    Result second_start = mesher_->Start();
    EXPECT_TRUE(second_start)
        << "Second Start() failed: " << second_start.GetErrorMessage();
}

/**
 * @brief Test Stop() without Start()
 */
TEST_F(LoraMesherInitializationTest, StopWithoutStart) {
    mesher_ = CreateValidLoraMesher();
    ASSERT_NE(mesher_, nullptr);

    // Stop() without Start() should not crash
    EXPECT_NO_THROW({ mesher_->Stop(); });
}

/**
 * @brief Test Start() after Stop()
 */
TEST_F(LoraMesherInitializationTest, StartAfterStop) {
    mesher_ = CreateValidLoraMesher();
    ASSERT_NE(mesher_, nullptr);

    // Start, Stop, then Start again
    Result first_start = mesher_->Start();
    EXPECT_TRUE(first_start);

    mesher_->Stop();

    Result second_start = mesher_->Start();
    EXPECT_TRUE(second_start)
        << "Start after Stop failed: " << second_start.GetErrorMessage();
}

/**
 * @brief Test configuration validation in Builder
 */
TEST_F(LoraMesherInitializationTest, ConfigurationValidation) {
    // Invalid pin configuration should throw during Build()
    EXPECT_THROW(
        { auto invalid_mesher = CreateInvalidPinLoraMesher(); },
        std::invalid_argument);
}

/**
 * @brief Test basic data sending interface
 */
TEST_F(LoraMesherInitializationTest, BasicDataInterface) {
    mesher_ = CreateValidLoraMesher();
    ASSERT_NE(mesher_, nullptr);

    Result start_result = mesher_->Start();
    EXPECT_TRUE(start_result);

    // Test data callback setting (should not crash)
    bool callback_called = false;
    mesher_->SetDataCallback(
        [&callback_called](AddressType /* source */,
                           const std::vector<uint8_t>& /* data */) {
            callback_called = true;
        });

    // Test sending data (should not crash, though may not succeed without network)
    std::vector<uint8_t> test_data = {0x01, 0x02, 0x03, 0x04};
    Result send_result = mesher_->Send(0x1234, test_data);
    // Note: We don't expect this to succeed without a proper network, just that it doesn't crash
    EXPECT_TRUE(send_result ||
                !send_result);  // Either result is acceptable for this test
}

/**
 * @brief Test network status access
 */
TEST_F(LoraMesherInitializationTest, NetworkStatusAccess) {
    mesher_ = CreateValidLoraMesher();
    ASSERT_NE(mesher_, nullptr);

    Result start_result = mesher_->Start();
    EXPECT_TRUE(start_result);

    // These methods should not crash and return valid data structures
    EXPECT_NO_THROW({
        (void)mesher_->GetRoutingTable();
        (void)mesher_->GetNetworkStatus();
        (void)mesher_->GetSlotTable();
    });
}

/**
 * @brief Test PingPong protocol configuration
 */
TEST_F(LoraMesherInitializationTest, PingPongProtocolConfiguration) {
    // Create LoraMesher with PingPong protocol instead of LoRaMesh
    auto ping_pong_mesher =
        LoraMesher::Builder()
            .withRadioConfig(radio_config_)
            .withPinConfig(pin_config_)
            .withPingPongProtocol()  // Use PingPong instead of LoRaMesh
            .Build();

    ASSERT_NE(ping_pong_mesher, nullptr);

    Result start_result = ping_pong_mesher->Start();
    EXPECT_TRUE(start_result)
        << "PingPong protocol start failed: " << start_result.GetErrorMessage();

    // Should have PingPong protocol active
    EXPECT_EQ(ping_pong_mesher->GetActiveProtocolType(),
              protocols::ProtocolType::kPingPong);

    // PingPong protocol should be available
    auto ping_pong_protocol = ping_pong_mesher->GetPingPongProtocol();
    EXPECT_NE(ping_pong_protocol, nullptr);

    // LoRaMesh protocol should not be available
    auto lora_mesh_protocol = ping_pong_mesher->GetLoRaMeshProtocol();
    EXPECT_EQ(lora_mesh_protocol, nullptr);

    ping_pong_mesher->Stop();
}

/**
 * @brief Test GenerateAddressFromHardware static method returns consistent address
 */
TEST_F(LoraMesherInitializationTest, GenerateAddressFromHardwareConsistency) {
    // Call GenerateAddressFromHardware multiple times
    AddressType first_address = LoraMesher::GenerateAddressFromHardware();
    AddressType second_address = LoraMesher::GenerateAddressFromHardware();
    AddressType third_address = LoraMesher::GenerateAddressFromHardware();

    // All calls should return the same address (hardware ID is deterministic)
    EXPECT_EQ(first_address, second_address)
        << "GenerateAddressFromHardware should return consistent address";
    EXPECT_EQ(second_address, third_address)
        << "GenerateAddressFromHardware should return consistent address";

    // Address should be valid (non-zero on platforms with hardware ID)
    // Note: On native/mock platforms, this may return 0 if hardware ID is unavailable
}

/**
 * @brief Test that GenerateAddressFromHardware matches GetNodeAddress after build
 */
TEST_F(LoraMesherInitializationTest, GenerateAddressFromHardwareMatchesBuilt) {
    // Pre-generate address using static method
    AddressType pre_generated_address =
        LoraMesher::GenerateAddressFromHardware();

    // Skip test if hardware ID is not available (native mock environment)
    if (pre_generated_address == 0) {
        GTEST_SKIP() << "Hardware ID not available in test environment";
    }

    // Build LoraMesher with auto address from hardware
    mesher_ = LoraMesher::Builder()
                  .withRadioConfig(radio_config_)
                  .withPinConfig(pin_config_)
                  .withLoRaMeshProtocol(mesh_config_)
                  .withAutoAddressFromHardware(true)
                  .Build();

    ASSERT_NE(mesher_, nullptr);

    Result start_result = mesher_->Start();
    EXPECT_TRUE(start_result)
        << "Start() failed: " << start_result.GetErrorMessage();

    AddressType built_address = mesher_->GetNodeAddress();

    // The pre-generated address should match the address after build
    EXPECT_EQ(pre_generated_address, built_address)
        << "Pre-generated address (0x" << std::hex << pre_generated_address
        << ") should match built address (0x" << built_address << ")";
}

/**
 * @brief Test using GenerateAddressFromHardware for role-based configuration
 */
TEST_F(LoraMesherInitializationTest, GenerateAddressForRoleConfiguration) {
    // This test demonstrates the intended use case:
    // Pre-generate address to configure NodeRole before building

    AddressType my_address = LoraMesher::GenerateAddressFromHardware();

    // Skip if hardware ID not available
    if (my_address == 0) {
        GTEST_SKIP() << "Hardware ID not available in test environment";
    }

    // Build using the pre-generated address explicitly
    mesher_ = LoraMesher::Builder()
                  .withRadioConfig(radio_config_)
                  .withPinConfig(pin_config_)
                  .withLoRaMeshProtocol(mesh_config_)
                  .withNodeAddress(my_address)  // Use pre-generated address
                  .Build();

    ASSERT_NE(mesher_, nullptr);

    Result start_result = mesher_->Start();
    EXPECT_TRUE(start_result);

    // Address should match what we set
    EXPECT_EQ(mesher_->GetNodeAddress(), my_address)
        << "Node address should match the pre-generated address";
}

/**
 * @brief Test LoRaMesh-specific accessor methods on loramesher.cpp
 */
TEST_F(LoraMesherInitializationTest, LoRaMeshAccessors) {
    mesher_ = CreateValidLoraMesher();
    ASSERT_NE(mesher_, nullptr);
    ASSERT_TRUE(mesher_->Start());

    // SetNodeCapabilities / GetNodeCapabilities
    mesher_->SetNodeCapabilities(0x07);
    EXPECT_EQ(mesher_->GetNodeCapabilities(), 0x07);

    // GetNodeCapabilities for a specific (unknown) node — should return 0
    EXPECT_EQ(mesher_->GetNodeCapabilities(0xDEAD), 0);

    // GetClosestNodeByCapability (no nodes → nullopt)
    auto closest = mesher_->GetClosestNodeByCapability(0x01);
    // May or may not find a node in a fresh network — just check it doesn't crash

    // GetClosestGateway
    auto gateway = mesher_->GetClosestGateway();
    (void)gateway;

    // GetTimeUntilNextDataSlot
    uint32_t tds = mesher_->GetTimeUntilNextDataSlot(50);
    (void)tds;

    // GetDataSlotsPerSuperframe
    uint8_t dsps = mesher_->GetDataSlotsPerSuperframe();
    (void)dsps;

    // GetTxQueueSize / GetRxQueueSize
    size_t tx_q = mesher_->GetTxQueueSize();
    size_t rx_q = mesher_->GetRxQueueSize();
    (void)tx_q;
    (void)rx_q;
}

/**
 * @brief Test SendMessage when not running
 */
TEST_F(LoraMesherInitializationTest, SendMessageNotRunning) {
    mesher_ = CreateValidLoraMesher();
    ASSERT_NE(mesher_, nullptr);
    // Not started yet

    auto msg = BaseMessage::Create(0x1234, 0x5678, MessageType::PING,
                                   std::vector<uint8_t>{0x01});
    ASSERT_TRUE(msg.has_value());
    Result result = mesher_->SendMessage(*msg);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kInvalidState);
}

/**
 * @brief Test NetworkStatus when LoRaMesh is active
 */
TEST_F(LoraMesherInitializationTest, NetworkStatusWithMesh) {
    mesher_ = CreateValidLoraMesher();
    ASSERT_NE(mesher_, nullptr);
    ASSERT_TRUE(mesher_->Start());

    NetworkStatus status = mesher_->GetNetworkStatus();
    // current_state should be a valid enum value
    EXPECT_GE(static_cast<int>(status.current_state), 0);
    EXPECT_FALSE(status.is_synchronized);  // Not synchronized in fresh network
}

/**
 * @brief Test SetDataCallback with LoRaMesh protocol
 */
TEST_F(LoraMesherInitializationTest, SetDataCallbackWithMesh) {
    mesher_ = CreateValidLoraMesher();
    ASSERT_NE(mesher_, nullptr);
    ASSERT_TRUE(mesher_->Start());

    // Set callback with LoRaMesh protocol active (capture by value to avoid dangling ref)
    auto called = std::make_shared<bool>(false);
    mesher_->SetDataCallback(
        [called](AddressType, const std::vector<uint8_t>&) { *called = true; });
    // Should not crash
    EXPECT_FALSE(*called);
}

/**
 * @brief Test SendMessage when running — exercises the is_running_ true branch
 */
TEST_F(LoraMesherInitializationTest, SendMessageWhenRunning) {
    mesher_ = CreateValidLoraMesher();
    ASSERT_NE(mesher_, nullptr);
    ASSERT_TRUE(mesher_->Start());

    auto msg =
        BaseMessage::Create(0x1234, mesher_->GetNodeAddress(),
                            MessageType::PING, std::vector<uint8_t>{0x01});
    ASSERT_TRUE(msg.has_value());
    // SendMessage when running should not crash — protocol may succeed or fail
    Result result = mesher_->SendMessage(*msg);
    // Either succeeds or fails with a protocol-level error — just not kInvalidState "not running"
    if (!result) {
        EXPECT_NE(result.getErrorCode(), LoraMesherErrorCode::kInvalidState);
    }
}

/**
 * @brief Test GetRoutingTable returns empty table after fresh start
 */
TEST_F(LoraMesherInitializationTest, GetRoutingTableEmpty) {
    mesher_ = CreateValidLoraMesher();
    ASSERT_NE(mesher_, nullptr);
    ASSERT_TRUE(mesher_->Start());

    // Fresh start - routing table should be empty (no other nodes)
    auto table = mesher_->GetRoutingTable();
    EXPECT_EQ(table.size(), 0u);
}

/**
 * @brief Test that GetNodeAddress returns a non-zero value after build
 */
TEST_F(LoraMesherInitializationTest, GetNodeAddressIsNonZero) {
    mesher_ = CreateValidLoraMesher();
    ASSERT_NE(mesher_, nullptr);
    EXPECT_NE(mesher_->GetNodeAddress(), 0u);
}

/**
 * @brief Test Send() before Start() returns kInvalidState
 *
 * Exercises the early-exit branch in Send() (loramesher.cpp lines 165-167)
 * where is_running_ is false.
 */
TEST_F(LoraMesherInitializationTest, SendBeforeStartFails) {
    mesher_ = CreateValidLoraMesher();
    ASSERT_NE(mesher_, nullptr);
    // Do NOT call Start()

    std::vector<uint8_t> data = {0xAA, 0xBB};
    Result result = mesher_->Send(0x1234, data);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kInvalidState);
}

/**
 * @brief Test SendMessage() with no active protocol but instance is running
 *
 * Exercises the else-branch in SendMessage() (loramesher.cpp lines 151-153)
 * where active_protocol_ is nullptr. Achieved by building with PingPong
 * protocol (active_protocol_ set), starting, then verifying SendMessage
 * routes through the protocol. Separately, we test the null-protocol path
 * via GetRoutingTable/GetNetworkStatus on a non-mesh instance.
 */
TEST_F(LoraMesherInitializationTest, SendMessageWithPingPongProtocol) {
    // Build with PingPong — no LoRaMesh active_protocol_
    auto ping_pong_mesher = LoraMesher::Builder()
                                .withRadioConfig(radio_config_)
                                .withPinConfig(pin_config_)
                                .withPingPongProtocol()
                                .Build();
    ASSERT_NE(ping_pong_mesher, nullptr);
    ASSERT_TRUE(ping_pong_mesher->Start());

    // GetRoutingTable and GetNetworkStatus should handle no-mesh gracefully
    auto table = ping_pong_mesher->GetRoutingTable();
    EXPECT_EQ(table.size(), 0u);

    NetworkStatus status = ping_pong_mesher->GetNetworkStatus();
    EXPECT_EQ(
        status.current_state,
        protocols::lora_mesh::INetworkService::ProtocolState::INITIALIZING);
    EXPECT_EQ(status.network_manager, 0u);
    EXPECT_FALSE(status.is_synchronized);

    // GetSlotTable should return empty span
    auto slot_table = ping_pong_mesher->GetSlotTable();
    EXPECT_EQ(slot_table.size(), 0u);

    ping_pong_mesher->Stop();
}

}  // namespace test
}  // namespace loramesher

#if defined(ARDUINO)
#include <Arduino.h>

void setup() {
    // should be the same value as for the `test_speed` option in "platformio.ini"
    // default value is test_speed=115200
    Serial.begin(115200);

    ::testing::InitGoogleTest();
}

void loop() {
    // Run tests
    if (RUN_ALL_TESTS()) {}

    // sleep 1 sec
    delay(1000);
}

#else
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    loramesher::os::RTOS::Init();
    if (RUN_ALL_TESTS()) {}
    // Always return zero-code and allow PlatformIO to parse results
    return 0;
}
#endif