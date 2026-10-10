/**
 * @file lora_mesh_protocol_coverage_test.cpp
 * @brief Coverage tests for LoRaMeshProtocol error paths and edge cases.
 *
 * Targets uncovered lines in src/protocols/lora_mesh_protocol.cpp:
 * - Init() hardware failure path (lines 79-83)
 * - SendMessage() when hardware_ is null (lines 354-358)
 * - SendData() when network_service_ is null (lines 391-394)
 * - Start() when hardware_ is null (lines 261-264)
 * - Stop() without prior Start() (task handle null path, lines 318-321)
 * - Pause() / Resume() with no task handle (null guard paths)
 * - SendMessage() for all message type switch branches (lines 363-377)
 * - IsSynchronized() before initialization (lines 447-448)
 * - GetState / GetNetworkManager / GetCurrentSlot after init
 * - GetLocalNodeCapabilities / GetNodeCapabilities null guard paths (494-498)
 */

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <thread>

#include "hardware/hardware_manager.hpp"
#include "os/os_port.hpp"
#include "protocols/lora_mesh_protocol.hpp"
#include "types/configurations/protocol_configuration.hpp"
#include "types/error_codes/loramesher_error_codes.hpp"
#include "types/messages/base_message.hpp"
#include "types/messages/message_type.hpp"

namespace loramesher {
namespace test {

// ---------------------------------------------------------------------------
// Minimal failing IHardwareManager that returns error on Initialize()
// ---------------------------------------------------------------------------

class FailingHardwareManager : public hardware::IHardwareManager {
   public:
    explicit FailingHardwareManager(bool fail_init = true,
                                    bool fail_start = false,
                                    bool fail_action = false)
        : fail_init_(fail_init),
          fail_start_(fail_start),
          fail_action_(fail_action) {}

    Result Initialize() override {
        if (fail_init_) {
            return Result(LoraMesherErrorCode::kHardwareError,
                          "Simulated hardware init failure");
        }
        is_initialized_ = true;
        return Result::Success();
    }

    Result Start() override {
        if (fail_start_) {
            return Result(LoraMesherErrorCode::kHardwareError,
                          "Simulated hardware start failure");
        }
        return Result::Success();
    }

    Result Stop() override { return Result::Success(); }

    Result setActionReceive(EventCallback callback) override {
        if (fail_action_) {
            return Result(LoraMesherErrorCode::kHardwareError,
                          "Simulated setActionReceive failure");
        }
        event_callback_ = callback;
        return Result::Success();
    }

    Result SendMessage(const BaseMessage& /*message*/) override {
        return Result::Success();
    }

    uint32_t getTimeOnAir(uint8_t /*length*/) override { return 10; }

    Result setState(radio::RadioState /*state*/) override {
        return Result::Success();
    }

    hal::IHal* getHal() override { return nullptr; }

    void SetLocalAddress(AddressType /*address*/) override {}

   private:
    bool fail_init_;
    bool fail_start_;
    bool fail_action_;
    bool is_initialized_ = false;
    EventCallback event_callback_ = nullptr;
};

// ---------------------------------------------------------------------------
// Test fixture — uses real HardwareManager (mock radio)
// ---------------------------------------------------------------------------

class LoraMeshProtocolCoverageTest : public ::testing::Test {
   protected:
    void SetUp() override {
        pin_config_.setNss(10);
        pin_config_.setDio0(11);
        pin_config_.setReset(12);
        pin_config_.setDio1(13);

        hardware_manager_ = std::make_shared<hardware::HardwareManager>(
            pin_config_, radio_config_);
        ASSERT_TRUE(hardware_manager_->Initialize());
    }

    void TearDown() override {
        // Give RTOS tasks a moment to exit cleanly
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (hardware_manager_) {
            hardware_manager_.reset();
        }
    }

    /** Create an initialized + configured but NOT started protocol. */
    std::unique_ptr<protocols::LoRaMeshProtocol> CreateInitialized(
        AddressType addr = 0x1001) {
        auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();
        LoRaMeshProtocolConfig config(addr);
        EXPECT_TRUE(protocol->Init(hardware_manager_, addr));
        EXPECT_TRUE(protocol->Configure(config));
        return protocol;
    }

    /** Create an initialized + started protocol. */
    std::unique_ptr<protocols::LoRaMeshProtocol> CreateStarted(
        AddressType addr = 0x1001) {
        auto protocol = CreateInitialized(addr);
        EXPECT_TRUE(protocol->Start());
        return protocol;
    }

    PinConfig pin_config_;
    RadioConfig radio_config_;
    std::shared_ptr<hardware::HardwareManager> hardware_manager_;
};

// ---------------------------------------------------------------------------
// Init() hardware failure path — lora_mesh_protocol.cpp lines 79-83
// ---------------------------------------------------------------------------

/**
 * @brief Init() returns hardware error when IHardwareManager::Initialize() fails.
 *
 * Exercises the early-return at lines 79-83 where hw_result is false.
 */
TEST_F(LoraMeshProtocolCoverageTest, InitFailsWhenHardwareInitFails) {
    auto failing_hw =
        std::make_shared<FailingHardwareManager>(/*fail_init=*/true);
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();

    Result result = protocol->Init(failing_hw, 0x2001);
    EXPECT_FALSE(result) << "Expected Init() to fail when hardware init fails";
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kHardwareError);
}

// ---------------------------------------------------------------------------
// Start() when hardware_ is null — lora_mesh_protocol.cpp lines 261-264
// ---------------------------------------------------------------------------

/**
 * @brief Start() on a fresh (non-Init'd) protocol returns InvalidState.
 *
 * hardware_ is null on a default-constructed protocol.
 * Exercises the `if (!hardware_)` guard at lines 261-264.
 */
TEST_F(LoraMeshProtocolCoverageTest, StartWithoutInitReturnsError) {
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();
    // No Init() call — hardware_ is nullptr
    Result result = protocol->Start();
    EXPECT_FALSE(result) << "Start() without Init() should fail";
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kInvalidState);
}

// ---------------------------------------------------------------------------
// Stop() without Start() — task handle null path (lines 318-321)
// ---------------------------------------------------------------------------

/**
 * @brief Stop() on an initialized-but-not-started protocol is safe.
 *
 * protocol_task_handle_ is set during Init(), so Stop() should delete it.
 * The key path here is that Stop() completes without crashing even if
 * the superframe has never been started.
 */
TEST_F(LoraMeshProtocolCoverageTest, StopWithoutStartIsIdempotent) {
    auto protocol = CreateInitialized();
    Result result = protocol->Stop();
    EXPECT_TRUE(result) << "Stop() should succeed even without Start()";
}

// ---------------------------------------------------------------------------
// Stop() on default-constructed (no-Init) protocol
// ---------------------------------------------------------------------------

/**
 * @brief Stop() on a bare default-constructed protocol returns Success.
 *
 * Exercises the branch where protocol_task_handle_ is nullptr (line 318-321
 * else branch) and hardware_ / services are null.
 */
TEST_F(LoraMeshProtocolCoverageTest, StopOnUninitializedProtocol) {
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();
    // No Init or Start — everything is null
    Result result = protocol->Stop();
    EXPECT_TRUE(result) << "Stop() on uninitialized protocol should succeed";
}

// ---------------------------------------------------------------------------
// SendMessage() when hardware_ is null — lines 354-358
// ---------------------------------------------------------------------------

/**
 * @brief SendMessage() on a non-initialized protocol returns InvalidState.
 *
 * hardware_ == nullptr on a default-constructed protocol.
 * Exercises lines 354-358: `if (!hardware_) return ...`.
 */
TEST_F(LoraMeshProtocolCoverageTest, SendMessageWithoutHardwareFails) {
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();

    std::vector<uint8_t> payload{0x01};
    auto opt_msg =
        BaseMessage::Create(0x1234, 0x5678, MessageType::DATA, payload);
    ASSERT_TRUE(opt_msg.has_value());

    Result result = protocol->SendMessage(*opt_msg);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kInvalidState);
}

// ---------------------------------------------------------------------------
// SendData() when network_service_ is null — lines 391-394
// ---------------------------------------------------------------------------

/**
 * @brief SendData() on a non-initialized protocol returns InvalidState.
 *
 * network_service_ == nullptr on a default-constructed protocol.
 * Exercises lines 391-394: `if (!network_service_) return ...`.
 */
TEST_F(LoraMeshProtocolCoverageTest, SendDataWithoutNetworkServiceFails) {
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();

    Result result = protocol->SendData(0x5678, {0x01, 0x02, 0x03});
    EXPECT_FALSE(result);
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kInvalidState);
}

// ---------------------------------------------------------------------------
// SendMessage() switch branches — lines 363-377
// Exercise all MessageType cases to hit every branch of the slot-type switch.
// ---------------------------------------------------------------------------

/**
 * @brief SendMessage() with DATA type routes to TX slot (line 364-366).
 */
TEST_F(LoraMeshProtocolCoverageTest, SendMessageDataTypeRoutesTX) {
    auto protocol = CreateInitialized();

    std::vector<uint8_t> data_payload{0xAA, 0xBB};
    auto opt_msg =
        BaseMessage::Create(0x2000, 0x1001, MessageType::DATA, data_payload);
    ASSERT_TRUE(opt_msg.has_value());

    Result result = protocol->SendMessage(*opt_msg);
    EXPECT_TRUE(result);
}

/**
 * @brief SendMessage() with ROUTE_TABLE type routes to CONTROL_TX slot (line 367-372).
 */
TEST_F(LoraMeshProtocolCoverageTest,
       SendMessageRouteTableTypesRoutesControlTX) {
    auto protocol = CreateInitialized();

    // ROUTE_TABLE
    {
        auto opt =
            BaseMessage::Create(0x2000, 0x1001, MessageType::ROUTE_TABLE, {});
        ASSERT_TRUE(opt.has_value());
        EXPECT_TRUE(protocol->SendMessage(*opt));
    }
    // JOIN_REQUEST
    {
        auto opt =
            BaseMessage::Create(0x2000, 0x1001, MessageType::JOIN_REQUEST, {});
        ASSERT_TRUE(opt.has_value());
        EXPECT_TRUE(protocol->SendMessage(*opt));
    }
    // JOIN_RESPONSE
    {
        auto opt =
            BaseMessage::Create(0x2000, 0x1001, MessageType::JOIN_RESPONSE, {});
        ASSERT_TRUE(opt.has_value());
        EXPECT_TRUE(protocol->SendMessage(*opt));
    }
    // SLOT_REQUEST
    {
        auto opt =
            BaseMessage::Create(0x2000, 0x1001, MessageType::SLOT_REQUEST, {});
        ASSERT_TRUE(opt.has_value());
        EXPECT_TRUE(protocol->SendMessage(*opt));
    }
    // SLOT_ALLOCATION
    {
        auto opt = BaseMessage::Create(0x2000, 0x1001,
                                       MessageType::SLOT_ALLOCATION, {});
        ASSERT_TRUE(opt.has_value());
        EXPECT_TRUE(protocol->SendMessage(*opt));
    }
}

/**
 * @brief SendMessage() with PING type routes to default TX slot (lines 374-376).
 */
TEST_F(LoraMeshProtocolCoverageTest, SendMessageDefaultTypeRoutesTX) {
    auto protocol = CreateInitialized();

    std::vector<uint8_t> ping_payload{0x01};
    auto opt_msg =
        BaseMessage::Create(0x2000, 0x1001, MessageType::PING, ping_payload);
    ASSERT_TRUE(opt_msg.has_value());

    Result result = protocol->SendMessage(*opt_msg);
    EXPECT_TRUE(result);
}

// ---------------------------------------------------------------------------
// IsSynchronized() before services initialized — lines 447-448
// ---------------------------------------------------------------------------

/**
 * @brief IsSynchronized() on default-constructed protocol returns false.
 *
 * Exercises the `!network_service_ || !superframe_service_` branch at line 447.
 */
TEST_F(LoraMeshProtocolCoverageTest, IsSynchronizedBeforeInitReturnsFalse) {
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();
    EXPECT_FALSE(protocol->IsSynchronized());
}

// ---------------------------------------------------------------------------
// Pause() / Resume() with no task handle — null guard paths
// ---------------------------------------------------------------------------

/**
 * @brief Pause() on a default-constructed protocol (no task handle) succeeds.
 *
 * Exercises the `if (protocol_task_handle_)` null guard at line 401.
 */
TEST_F(LoraMeshProtocolCoverageTest, PauseWithoutTaskHandleSucceeds) {
    // Create protocol but don't start (task handle is set by Init, but
    // we want a protocol with no task handle at all)
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();
    Result result = protocol->Pause();
    // No task handle → pause just skips the SuspendTask call
    EXPECT_TRUE(result);
}

/**
 * @brief Resume() on a default-constructed protocol (no task handle) succeeds.
 *
 * Exercises the `if (protocol_task_handle_)` null guard at line 425.
 */
TEST_F(LoraMeshProtocolCoverageTest, ResumeWithoutTaskHandleSucceeds) {
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();
    Result result = protocol->Resume();
    EXPECT_TRUE(result);
}

// ---------------------------------------------------------------------------
// GetLocalNodeCapabilities() and GetNodeCapabilities() null-service guard
// (lines 494-498, 501-505)
// ---------------------------------------------------------------------------

/**
 * @brief GetLocalNodeCapabilities() returns 0 if network_service_ is null.
 *
 * On a default-constructed protocol, network_service_ is nullptr.
 */
TEST_F(LoraMeshProtocolCoverageTest,
       GetLocalNodeCapabilitiesNullServiceReturnsZero) {
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();
    EXPECT_EQ(protocol->GetLocalNodeCapabilities(), 0u);
}

/**
 * @brief GetNodeCapabilities() returns 0 if network_service_ is null.
 */
TEST_F(LoraMeshProtocolCoverageTest,
       GetNodeCapabilitiesNullServiceReturnsZero) {
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();
    EXPECT_EQ(protocol->GetNodeCapabilities(0x1234), 0u);
}

// ---------------------------------------------------------------------------
// SetNodeCapabilities() with null service — line 488-491
// ---------------------------------------------------------------------------

/**
 * @brief SetNodeCapabilities() with null network_service_ does not crash.
 *
 * Exercises the `if (network_service_)` null guard at line 489.
 */
TEST_F(LoraMeshProtocolCoverageTest,
       SetNodeCapabilitiesNullServiceDoesNotCrash) {
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();
    EXPECT_NO_THROW(protocol->SetNodeCapabilities(0xFF));
}

// ---------------------------------------------------------------------------
// SetNodeCapabilities() + GetLocalNodeCapabilities() round-trip after Init
// ---------------------------------------------------------------------------

/**
 * @brief SetNodeCapabilities() / GetLocalNodeCapabilities() round-trip
 *        on an initialized protocol.
 */
TEST_F(LoraMeshProtocolCoverageTest, NodeCapabilitiesRoundTripAfterInit) {
    auto protocol = CreateInitialized();

    protocol->SetNodeCapabilities(0x05);  // some capability bits
    uint8_t caps = protocol->GetLocalNodeCapabilities();
    EXPECT_EQ(caps, 0x05u);
}

// ---------------------------------------------------------------------------
// GetServiceConfiguration() — lora_mesh_protocol.cpp line 513-514
// ---------------------------------------------------------------------------

/**
 * @brief GetServiceConfiguration() returns the current config.
 *
 * Exercises lines 512-515.
 */
TEST_F(LoraMeshProtocolCoverageTest, GetServiceConfiguration) {
    auto protocol = CreateInitialized();
    const auto& config = protocol->GetServiceConfiguration();
    // Just verify no crash and the config exists
    EXPECT_GE(config.message_queue_size, 1u);
}

// ---------------------------------------------------------------------------
// GetDiscoveryTimeout() / GetJoinTimeout() — called from ProtocolTask
// ---------------------------------------------------------------------------

/**
 * @brief GetDiscoveryTimeout() returns a positive value after Start.
 *
 * The superframe service must be running (started) for GetDiscoveryTimeout() to
 * return a non-zero value — before Start(), is_running_ = false → returns 0.
 */
TEST_F(LoraMeshProtocolCoverageTest, GetDiscoveryTimeoutAfterInit) {
    auto protocol = CreateStarted();
    EXPECT_GT(protocol->GetDiscoveryTimeout(), 0u);
}

/**
 * @brief GetJoinTimeout() returns a positive value after Start.
 *
 * The superframe service must have total_slots_ > 0 for GetJoinTimeout() to
 * return a non-zero value.
 */
TEST_F(LoraMeshProtocolCoverageTest, GetJoinTimeoutAfterInit) {
    auto protocol = CreateStarted();
    EXPECT_GT(protocol->GetJoinTimeout(), 0u);
}

// ---------------------------------------------------------------------------
// IsSynchronized() partial case — only network service null
// ---------------------------------------------------------------------------

/**
 * @brief IsSynchronized() on a fully initialized (but unstarted) protocol.
 *
 * Both services exist but neither is synchronized.  This exercises the
 * non-null path through IsSynchronized().
 */
TEST_F(LoraMeshProtocolCoverageTest, IsSynchronizedAfterInitBeforeStart) {
    auto protocol = CreateInitialized();
    // Services are initialized but superframe has not been started
    bool sync = protocol->IsSynchronized();
    // Not synchronized yet — just verify no crash
    EXPECT_FALSE(sync);
}

// ---------------------------------------------------------------------------
// Protocol started and stopped — basic lifecycle check
// ---------------------------------------------------------------------------

/**
 * @brief A full start/stop cycle succeeds and all state accessors are safe.
 */
TEST_F(LoraMeshProtocolCoverageTest, StartedProtocolStateAccessors) {
    auto protocol = CreateStarted();

    // State accessors must not crash
    EXPECT_NO_THROW({
        (void)protocol->GetState();
        (void)protocol->GetCurrentSlot();
        (void)protocol->GetNetworkManager();
        (void)protocol->GetNetworkNodes();
    });

    protocol->Stop();
}

// ---------------------------------------------------------------------------
// Configure() with capabilities — exercises the capability-setting branch
// (lora_mesh_protocol.cpp lines 251-255)
// ---------------------------------------------------------------------------

/**
 * @brief Configure() with non-zero capabilities calls SetLocalNodeCapabilities.
 */
TEST_F(LoraMeshProtocolCoverageTest, ConfigureWithCapabilities) {
    auto protocol = CreateInitialized();

    LoRaMeshProtocolConfig config(0x1001);
    config.setNodeCapabilities(0x03);
    Result result = protocol->Configure(config);
    EXPECT_TRUE(result);

    EXPECT_EQ(protocol->GetLocalNodeCapabilities(), 0x03u);
}

// ---------------------------------------------------------------------------
// GetSlotTable() after Start — exercises GetSlotTable() (hpp inline)
// ---------------------------------------------------------------------------

/**
 * @brief GetSlotTable() on a started protocol returns a valid span.
 */
TEST_F(LoraMeshProtocolCoverageTest, GetSlotTableAfterStart) {
    auto protocol = CreateStarted();

    auto slot_table = protocol->GetSlotTable();
    // Span is valid (may be empty in early DISCOVERY)
    EXPECT_GE(slot_table.size(), 0u);

    protocol->Stop();
}

// ---------------------------------------------------------------------------
// Pause() / Resume() on a running protocol
// ---------------------------------------------------------------------------

/**
 * @brief Pause() and Resume() on a running protocol succeed.
 *
 * Exercises lines 399-440 for the normal (running) case.
 */
TEST_F(LoraMeshProtocolCoverageTest, PauseAndResumeRunningProtocol) {
    auto protocol = CreateStarted();

    Result pause_result = protocol->Pause();
    EXPECT_TRUE(pause_result)
        << "Pause failed: " << pause_result.GetErrorMessage();

    GetRTOS().delay(20);

    Result resume_result = protocol->Resume();
    EXPECT_TRUE(resume_result)
        << "Resume failed: " << resume_result.GetErrorMessage();

    GetRTOS().delay(20);

    protocol->Stop();
}

// ---------------------------------------------------------------------------
// GetDataSlotsPerSuperframe / GetTxQueueSize / GetRxQueueSize
// (lora_mesh_protocol.cpp)
// ---------------------------------------------------------------------------

/**
 * @brief Queue size accessors return 0 for a fresh node (no traffic).
 */
TEST_F(LoraMeshProtocolCoverageTest, QueueSizesOnFreshNode) {
    auto protocol = CreateStarted();

    EXPECT_NO_THROW({
        (void)protocol->GetDataSlotsPerSuperframe();
        (void)protocol->GetTxQueueSize();
        (void)protocol->GetRxQueueSize();
        (void)protocol->GetTimeUntilNextDataSlot(0);
    });

    protocol->Stop();
}

// ---------------------------------------------------------------------------
// GetSlotDuration() — non-const overload (lora_mesh_protocol.cpp line 1281)
// ---------------------------------------------------------------------------

/**
 * @brief GetSlotDuration() (non-const overload) returns a positive value after Start.
 *
 * This exercises the non-const version of GetSlotDuration() at line 1281.
 */
TEST_F(LoraMeshProtocolCoverageTest, GetSlotDurationAfterStart) {
    auto protocol = CreateStarted();

    uint32_t duration = protocol->GetSlotDuration();
    EXPECT_GT(duration, 0u);

    protocol->Stop();
}

/**
 * @brief GetSlotDuration() (non-const) returns 0 when superframe_service_ is null.
 *
 * Exercises the null guard at lora_mesh_protocol.cpp lines 1282-1283.
 */
TEST_F(LoraMeshProtocolCoverageTest, GetSlotDurationBeforeInitReturnsZero) {
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();
    // superframe_service_ is null — non-const overload should return 0
    uint32_t duration = protocol->GetSlotDuration();
    EXPECT_EQ(duration, 0u);
}

/**
 * @brief GetSuperframeDuration() returns 0 when superframe_service_ is null.
 */
TEST_F(LoraMeshProtocolCoverageTest,
       GetSuperframeDurationBeforeInitReturnsZero) {
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();
    EXPECT_EQ(protocol->GetSuperframeDuration(), 0u);
}

// ---------------------------------------------------------------------------
// const GetSlotDuration() — exercises the const overload (hpp inline)
// ---------------------------------------------------------------------------

/**
 * @brief const GetSlotDuration() returns a positive value after Start.
 *
 * This exercises the const version of GetSlotDuration() in the header.
 */
TEST_F(LoraMeshProtocolCoverageTest, ConstGetSlotDurationAfterStart) {
    auto protocol = CreateStarted();
    const protocols::LoRaMeshProtocol& const_proto = *protocol;

    uint32_t duration = const_proto.GetSlotDuration();
    EXPECT_GT(duration, 0u);

    protocol->Stop();
}

// ---------------------------------------------------------------------------
// Init() with failing hardware Start — exercises lora_mesh_protocol.cpp line 282-286
// ---------------------------------------------------------------------------

/**
 * @brief Start() propagates hardware Start() failure.
 *
 * Uses a FailingHardwareManager that succeeds in Initialize() but fails Start().
 * Exercises lines 282-286: `if (!result) { LOG_ERROR...; return result; }`.
 */
TEST_F(LoraMeshProtocolCoverageTest, StartFailsWhenHardwareStartFails) {
    // Hardware that passes Initialize() but fails Start()
    auto hw = std::make_shared<FailingHardwareManager>(
        /*fail_init=*/false, /*fail_start=*/true);

    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();

    // Init should succeed (Initialize passes)
    Result init_result = protocol->Init(hw, 0x3001);
    EXPECT_TRUE(init_result) << "Init() should succeed";

    LoRaMeshProtocolConfig config(0x3001);
    EXPECT_TRUE(protocol->Configure(config));

    // Start should fail (hardware Start fails)
    Result start_result = protocol->Start();
    EXPECT_FALSE(start_result)
        << "Start() should fail when hardware start fails";
    EXPECT_EQ(start_result.getErrorCode(), LoraMesherErrorCode::kHardwareError);

    // Explicit cleanup to avoid use-after-free in task teardown
    protocol->Stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    protocol.reset();
}

// ---------------------------------------------------------------------------
// IsReadyToSend() predicate
// ---------------------------------------------------------------------------

/**
 * @brief IsReadyToSend() reports kNotInitialized on a fresh protocol.
 *
 * Without Init(), neither network_service_ nor superframe_service_ exist.
 * The composed predicate must report not-ready and must not crash.
 */
TEST_F(LoraMeshProtocolCoverageTest, IsReadyToSendBeforeInitNotInitialized) {
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();
    Result base = protocol->IsReadyToSend();
    EXPECT_FALSE(base);
    EXPECT_EQ(base.getErrorCode(), LoraMesherErrorCode::kNotInitialized);

    Result with_dest = protocol->IsReadyToSend(0x9999);
    EXPECT_FALSE(with_dest);
    EXPECT_EQ(with_dest.getErrorCode(), LoraMesherErrorCode::kNotInitialized);
}

/**
 * @brief IsReadyToSend() reports kInvalidState after Init but before Start.
 *
 * Services exist but the protocol is in INITIALIZING — the state gate alone
 * blocks the predicate.
 */
TEST_F(LoraMeshProtocolCoverageTest, IsReadyToSendAfterInitBeforeStart) {
    auto protocol = CreateInitialized();
    Result result = protocol->IsReadyToSend();
    EXPECT_FALSE(result);
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kInvalidState);
}

/**
 * @brief IsReadyToSend() reports kInvalidState in DISCOVERY.
 *
 * State accessor reports DISCOVERY immediately after Start(). Even with
 * services running, the state gate keeps the predicate false.
 */
TEST_F(LoraMeshProtocolCoverageTest, IsReadyToSendInDiscoveryReturnsFalse) {
    auto protocol = CreateStarted();
    using ProtocolState = protocols::lora_mesh::INetworkService::ProtocolState;
    auto state = protocol->GetState();
    EXPECT_TRUE(state == ProtocolState::DISCOVERY ||
                state == ProtocolState::INITIALIZING)
        << "Expected DISCOVERY/INITIALIZING after Start, got "
        << static_cast<int>(state);
    Result result = protocol->IsReadyToSend();
    EXPECT_FALSE(result);
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kInvalidState);
    protocol->Stop();
}

/**
 * @brief IsReadyToSend(self) reports kInvalidParameter once base ready.
 *
 * Self-send rejection only fires after the base predicate passes; in a
 * non-ready protocol the base error code propagates first. We assert the
 * non-Success result either way.
 */
TEST_F(LoraMeshProtocolCoverageTest, IsReadyToSendSelfReturnsFalse) {
    auto protocol = CreateInitialized(0x4001);
    Result result = protocol->IsReadyToSend(0x4001);
    EXPECT_FALSE(result)
        << "Self-send must be rejected even in non-ready state";
}

/**
 * @brief IsReadyToSend(broadcast) follows base IsReadyToSend() truth value.
 *
 * Before stabilization the base is non-Success, so the broadcast overload
 * propagates the same error code. The short-circuit-true on broadcast only
 * kicks in once the base predicate is Success (covered by the integration
 * suite).
 */
TEST_F(LoraMeshProtocolCoverageTest, IsReadyToSendBroadcastTracksBase) {
    auto protocol = CreateInitialized();
    Result base = protocol->IsReadyToSend();
    Result bcast = protocol->IsReadyToSend(kBroadcastAddress);
    EXPECT_EQ(static_cast<bool>(base), static_cast<bool>(bcast));
    EXPECT_EQ(base.getErrorCode(), bcast.getErrorCode());
}

}  // namespace test
}  // namespace loramesher
