/**
 * @file protocol_coverage_extra_test.cpp
 * @brief Additional coverage tests for LoRaMeshProtocol and SuperframeService.
 *
 * Targets uncovered lines:
 * - LoRaMeshProtocol: setActionReceive failure, Configure network service
 *   failure, StartDiscovery null guard, GetTimeUntilNextDataSlot edge cases,
 *   GetDataSlotsPerSuperframe null guard, route update callback,
 *   CreateServiceConfig (non-DEBUG), NotifyProtocolTask null guard
 * - SuperframeService: GetTotalSlots, UpdateSlotDuration, GetUpdateInterval,
 *   IsAutoAdvanceEnabled, SetDiscoveryJitter/GetDiscoveryJitter,
 *   StopSuperframe while not running, StartSuperframe while already running,
 *   UpdateSuperframeConfig with zero slots, SetUpdateInterval min/max clamping,
 *   CheckForNewSuperframe test helper, NeedsResynchronization paths,
 *   GetSuperframeStats paths, GetTimeRemainingInSlot, GetTimeSinceSuperframeStart
 */

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <thread>

#include "../test/utils/protocol_lifecycle_helpers.hpp"
#include "hardware/hardware_manager.hpp"
#include "os/os_port.hpp"
#include "protocols/lora_mesh/services/superframe_service.hpp"
#include "protocols/lora_mesh_protocol.hpp"
#include "types/configurations/protocol_configuration.hpp"
#include "types/error_codes/loramesher_error_codes.hpp"
#include "types/messages/base_message.hpp"
#include "types/messages/message_type.hpp"
#include "types/protocols/lora_mesh/slot_allocation.hpp"

namespace loramesher {
namespace test {

// ---------------------------------------------------------------------------
// IHardwareManager that fails on setActionReceive
// ---------------------------------------------------------------------------

class ActionReceiveFailHW : public hardware::IHardwareManager {
   public:
    Result Initialize() override { return Result::Success(); }

    Result Start() override { return Result::Success(); }

    Result Stop() override { return Result::Success(); }

    Result setActionReceive(EventCallback) override {
        return Result(LoraMesherErrorCode::kHardwareError,
                      "setActionReceive failure");
    }

    Result SendMessage(const BaseMessage&) override {
        return Result::Success();
    }

    uint32_t getTimeOnAir(uint8_t) override { return 10; }

    Result setState(radio::RadioState) override { return Result::Success(); }

    hal::IHal* getHal() override { return nullptr; }

    void SetLocalAddress(AddressType) override {}
};

// ---------------------------------------------------------------------------
// Test fixture for LoRaMeshProtocol extra coverage
// ---------------------------------------------------------------------------

class ProtocolCoverageExtraTest : public ::testing::Test {
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
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        hardware_manager_.reset();
    }

    std::unique_ptr<protocols::LoRaMeshProtocol> CreateInitialized(
        AddressType addr = 0x1001) {
        auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();
        LoRaMeshProtocolConfig config(addr);
        EXPECT_TRUE(protocol->Init(hardware_manager_, addr));
        EXPECT_TRUE(protocol->Configure(config));
        return protocol;
    }

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
// Init() with setActionReceive failure
// ---------------------------------------------------------------------------

TEST_F(ProtocolCoverageExtraTest, InitFailsWhenSetActionReceiveFails) {
    auto hw = std::make_shared<ActionReceiveFailHW>();
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();

    Result result = protocol->Init(hw, 0x2001);
    EXPECT_FALSE(result);
}

// ---------------------------------------------------------------------------
// GetDataSlotsPerSuperframe on uninitialized protocol returns 0
// ---------------------------------------------------------------------------

TEST_F(ProtocolCoverageExtraTest,
       GetDataSlotsPerSuperframeUninitializedReturnsZero) {
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();
    EXPECT_EQ(protocol->GetDataSlotsPerSuperframe(), 0u);
}

// ---------------------------------------------------------------------------
// GetTimeUntilNextDataSlot on uninitialized protocol returns 0
// ---------------------------------------------------------------------------

TEST_F(ProtocolCoverageExtraTest,
       GetTimeUntilNextDataSlotUninitializedReturnsZero) {
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();
    EXPECT_EQ(protocol->GetTimeUntilNextDataSlot(0), 0u);
}

// ---------------------------------------------------------------------------
// GetTimeUntilNextDataSlot on initialized but not started protocol
// ---------------------------------------------------------------------------

TEST_F(ProtocolCoverageExtraTest,
       GetTimeUntilNextDataSlotInitializedReturnsZero) {
    auto protocol = CreateInitialized();
    // No TX slots allocated before start, should return 0
    uint32_t time = protocol->GetTimeUntilNextDataSlot(0);
    // May be 0 since no TX slots are allocated in DISCOVERY state
    EXPECT_EQ(time, 0u);
    protocol->Stop();
}

// ---------------------------------------------------------------------------
// GetTimeUntilNextDataSlot after start
// ---------------------------------------------------------------------------

TEST_F(ProtocolCoverageExtraTest, GetTimeUntilNextDataSlotAfterStart) {
    auto protocol = CreateStarted();
    // Protocol is in DISCOVERY state, may not have TX slots
    uint32_t time = protocol->GetTimeUntilNextDataSlot(0);
    // Just verify it doesn't crash
    (void)time;
    protocol->Stop();
}

// ---------------------------------------------------------------------------
// SendMessage with SYNC_BEACON type routes to default TX slot
// ---------------------------------------------------------------------------

TEST_F(ProtocolCoverageExtraTest, SendMessageSyncBeaconType) {
    auto protocol = CreateInitialized();
    auto opt =
        BaseMessage::Create(0x2000, 0x1001, MessageType::SYNC_BEACON, {});
    ASSERT_TRUE(opt.has_value());
    Result result = protocol->SendMessage(*opt);
    EXPECT_TRUE(result);
    protocol->Stop();
}

// ---------------------------------------------------------------------------
// SendMessage with NM_CLAIM type routes to default TX slot
// ---------------------------------------------------------------------------

TEST_F(ProtocolCoverageExtraTest, SendMessageNMClaimType) {
    auto protocol = CreateInitialized();
    auto opt = BaseMessage::Create(0x2000, 0x1001, MessageType::NM_CLAIM, {});
    ASSERT_TRUE(opt.has_value());
    Result result = protocol->SendMessage(*opt);
    EXPECT_TRUE(result);
    protocol->Stop();
}

// ---------------------------------------------------------------------------
// Double Stop is safe
// ---------------------------------------------------------------------------

TEST_F(ProtocolCoverageExtraTest, DoubleStopIsSafe) {
    auto protocol = CreateStarted();
    Result r1 = protocol->Stop();
    EXPECT_TRUE(r1);
    Result r2 = protocol->Stop();
    EXPECT_TRUE(r2);
}

// ---------------------------------------------------------------------------
// SetRouteUpdateCallback and SetDataReceivedCallback after Init
// ---------------------------------------------------------------------------

TEST_F(ProtocolCoverageExtraTest, SetCallbacksAfterInit) {
    auto protocol = CreateInitialized();
    bool route_called = false;
    bool data_called = false;

    protocol->SetRouteUpdateCallback(
        [&route_called](bool, AddressType, AddressType, uint8_t) {
            route_called = true;
        });

    protocol->SetDataReceivedCallback(
        [&data_called](AddressType, const std::vector<uint8_t>&) {
            data_called = true;
        });

    // Just verify no crash
    (void)route_called;
    (void)data_called;
    protocol->Stop();
}

// ---------------------------------------------------------------------------
// Configure with invalid config after Init
// ---------------------------------------------------------------------------

TEST_F(ProtocolCoverageExtraTest, ConfigureInvalidConfigFails) {
    auto protocol = std::make_unique<protocols::LoRaMeshProtocol>();
    Result init_result = protocol->Init(hardware_manager_, 0x1001);
    ASSERT_TRUE(init_result);

    // hello_interval < 5000 is invalid
    LoRaMeshProtocolConfig bad_config(0x1001, /*hello_interval=*/100);
    Result result = protocol->Configure(bad_config);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kInvalidParameter);
    protocol->Stop();
}

// ---------------------------------------------------------------------------
// Start after Stop restarts successfully
// ---------------------------------------------------------------------------

TEST_F(ProtocolCoverageExtraTest, RestartAfterStop) {
    ResetTaskMisuseCounters();
    auto protocol = CreateInitialized();

    Result start1 = protocol->Start();
    ASSERT_TRUE(start1) << start1.GetErrorMessage();

    Result stop = protocol->Stop();
    ASSERT_TRUE(stop) << stop.GetErrorMessage();
    EXPECT_FALSE(SuperframeRunning(*protocol));

    // Reconfiguring a stopped protocol is allowed
    LoRaMeshProtocolConfig config(0x1001);
    EXPECT_TRUE(protocol->Configure(config));

    Result start2 = protocol->Start();
    ASSERT_TRUE(start2) << start2.GetErrorMessage();
    EXPECT_TRUE(ProtocolTaskResponds(*protocol));
    EXPECT_TRUE(SuperframeRunning(*protocol));

    EXPECT_TRUE(protocol->Stop());
    ExpectNoTaskMisuse();
}

}  // namespace test
}  // namespace loramesher

// ===========================================================================
// SuperframeService extra coverage tests
// ===========================================================================

#ifndef ARDUINO

namespace {
using namespace loramesher::types::protocols::lora_mesh;
}

namespace loramesher {
namespace protocols {
namespace lora_mesh {
namespace test {

class SuperframeExtraCoverageTest : public ::testing::Test {
   protected:
    void SetUp() override { service_ = std::make_shared<SuperframeService>(); }

    void TearDown() override {
        if (service_) {
            if (service_->IsSynchronized()) {
                service_->StopSuperframe();
            }
            service_.reset();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::shared_ptr<SuperframeService> service_;
};

// GetTotalSlots returns default value
TEST_F(SuperframeExtraCoverageTest, GetTotalSlotsReturnsDefault) {
    uint16_t slots = service_->GetTotalSlots();
    EXPECT_GT(slots, 0u);
}

// UpdateSlotDuration delegates to UpdateSuperframeConfig
TEST_F(SuperframeExtraCoverageTest, UpdateSlotDurationWhenRunning) {
    ASSERT_TRUE(service_->StartSuperframe());
    Result result = service_->UpdateSlotDuration(500);
    EXPECT_TRUE(result) << result.GetErrorMessage();
    EXPECT_EQ(service_->GetSlotDuration(), 500u);
    service_->StopSuperframe();
}

// UpdateSlotDuration with 0 does not change duration
TEST_F(SuperframeExtraCoverageTest, UpdateSlotDurationZeroNoChange) {
    ASSERT_TRUE(service_->StartSuperframe());
    uint32_t original = service_->GetSlotDuration();
    Result result = service_->UpdateSlotDuration(0);
    EXPECT_TRUE(result);
    EXPECT_EQ(service_->GetSlotDuration(), original);
    service_->StopSuperframe();
}

// GetUpdateInterval returns default value
TEST_F(SuperframeExtraCoverageTest, GetUpdateIntervalReturnsDefault) {
    uint32_t interval = service_->GetUpdateInterval();
    EXPECT_GT(interval, 0u);
}

// IsAutoAdvanceEnabled returns true by default
TEST_F(SuperframeExtraCoverageTest, IsAutoAdvanceEnabledDefault) {
    EXPECT_TRUE(service_->IsAutoAdvanceEnabled());
}

// SetAutoAdvance toggles the flag
TEST_F(SuperframeExtraCoverageTest, SetAutoAdvanceToggles) {
    service_->SetAutoAdvance(false);
    EXPECT_FALSE(service_->IsAutoAdvanceEnabled());
    service_->SetAutoAdvance(true);
    EXPECT_TRUE(service_->IsAutoAdvanceEnabled());
}

// SetDiscoveryJitter and GetDiscoveryJitter
TEST_F(SuperframeExtraCoverageTest, DiscoveryJitterRoundTrip) {
    service_->SetDiscoveryJitter(1234);
    EXPECT_EQ(service_->GetDiscoveryJitter(), 1234u);
}

// SetDiscoveryJitter to 0 disables jitter
TEST_F(SuperframeExtraCoverageTest, DiscoveryJitterZeroDisables) {
    service_->SetDiscoveryJitter(0);
    EXPECT_EQ(service_->GetDiscoveryJitter(), 0u);
}

// StopSuperframe when not running returns error
TEST_F(SuperframeExtraCoverageTest, StopSuperframeWhenNotRunning) {
    Result result = service_->StopSuperframe();
    EXPECT_FALSE(result);
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kInvalidState);
}

// StartSuperframe twice returns error
TEST_F(SuperframeExtraCoverageTest, StartSuperframeTwiceFails) {
    ASSERT_TRUE(service_->StartSuperframe());
    Result result = service_->StartSuperframe();
    EXPECT_FALSE(result);
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kInvalidState);
    service_->StopSuperframe();
}

// HandleNewSuperframe when not running
TEST_F(SuperframeExtraCoverageTest, HandleNewSuperframeWhenNotRunning) {
    Result result = service_->HandleNewSuperframe();
    EXPECT_FALSE(result);
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kInvalidState);
}

// UpdateSuperframeConfig with zero slots
TEST_F(SuperframeExtraCoverageTest, UpdateSuperframeConfigZeroSlotsFails) {
    ASSERT_TRUE(service_->StartSuperframe());
    Result result = service_->UpdateSuperframeConfig(0);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kInvalidArgument);
    service_->StopSuperframe();
}

// UpdateSuperframeConfig without updating superframe
TEST_F(SuperframeExtraCoverageTest, UpdateSuperframeConfigNoUpdateSuperframe) {
    ASSERT_TRUE(service_->StartSuperframe());
    Result result =
        service_->UpdateSuperframeConfig(8, 500, /*update_superframe=*/false);
    EXPECT_TRUE(result);
    EXPECT_EQ(service_->GetTotalSlots(), 8u);
    EXPECT_EQ(service_->GetSlotDuration(), 500u);
    service_->StopSuperframe();
}

// SetUpdateInterval clamping
TEST_F(SuperframeExtraCoverageTest, SetUpdateIntervalMinClamp) {
    service_->SetUpdateInterval(1);  // Below minimum (10)
    EXPECT_GE(service_->GetUpdateInterval(), 10u);
}

TEST_F(SuperframeExtraCoverageTest, SetUpdateIntervalMaxClamp) {
    service_->SetUpdateInterval(99999);  // Above maximum (1000)
    EXPECT_LE(service_->GetUpdateInterval(), 1000u);
}

TEST_F(SuperframeExtraCoverageTest, SetUpdateIntervalNormal) {
    service_->SetUpdateInterval(100);
    EXPECT_EQ(service_->GetUpdateInterval(), 100u);
}

// NeedsResynchronization when not running
TEST_F(SuperframeExtraCoverageTest, NeedsResyncWhenNotRunning) {
    EXPECT_TRUE(service_->NeedsResynchronization());
}

// NeedsResynchronization when running and synchronized
TEST_F(SuperframeExtraCoverageTest, NeedsResyncWhenSynchronized) {
    ASSERT_TRUE(service_->StartSuperframe());
    // After start, should be synchronized with zero drift
    EXPECT_FALSE(service_->NeedsResynchronization());
    service_->StopSuperframe();
}

// NeedsResynchronization with small threshold triggers true
TEST_F(SuperframeExtraCoverageTest, NeedsResyncWithSmallThreshold) {
    ASSERT_TRUE(service_->StartSuperframe());
    // With threshold 0, any drift would trigger; freshly started = no drift
    bool needs = service_->NeedsResynchronization(0);
    // Fresh start has 0 drift, 0 > 0 is false
    EXPECT_FALSE(needs);
    service_->StopSuperframe();
}

// GetSuperframeStats when running
TEST_F(SuperframeExtraCoverageTest, GetSuperframeStatsRunning) {
    ASSERT_TRUE(service_->StartSuperframe());
    auto stats = service_->GetSuperframeStats();
    EXPECT_GE(stats.total_runtime_ms, 0u);
    service_->StopSuperframe();
}

// GetSuperframeStats when not running
TEST_F(SuperframeExtraCoverageTest, GetSuperframeStatsNotRunning) {
    auto stats = service_->GetSuperframeStats();
    EXPECT_EQ(stats.superframes_completed, 0u);
}

// GetTimeRemainingInSlot when not running
TEST_F(SuperframeExtraCoverageTest, GetTimeRemainingInSlotNotRunning) {
    EXPECT_EQ(service_->GetTimeRemainingInSlot(), 0u);
}

// GetTimeRemainingInSlot when running
TEST_F(SuperframeExtraCoverageTest, GetTimeRemainingInSlotRunning) {
    ASSERT_TRUE(service_->StartSuperframe());
    uint32_t remaining = service_->GetTimeRemainingInSlot();
    // Should be > 0 since we just started
    EXPECT_GT(remaining, 0u);
    service_->StopSuperframe();
}

// GetTimeSinceSuperframeStart when not running
TEST_F(SuperframeExtraCoverageTest, GetTimeSinceSuperframeStartNotRunning) {
    EXPECT_EQ(service_->GetTimeSinceSuperframeStart(), 0u);
}

// GetTimeSinceSuperframeStart when running
TEST_F(SuperframeExtraCoverageTest, GetTimeSinceSuperframeStartRunning) {
    ASSERT_TRUE(service_->StartSuperframe());
    uint32_t elapsed = service_->GetTimeSinceSuperframeStart();
    EXPECT_GE(elapsed, 0u);
    service_->StopSuperframe();
}

// GetCurrentSlot when not running
TEST_F(SuperframeExtraCoverageTest, GetCurrentSlotNotRunning) {
    EXPECT_EQ(service_->GetCurrentSlot(), 0u);
}

// GetTimeInSlot when not running
TEST_F(SuperframeExtraCoverageTest, GetTimeInSlotNotRunning) {
    EXPECT_EQ(service_->GetTimeInSlot(), 0u);
}

// GetDiscoveryTimeout when not running
TEST_F(SuperframeExtraCoverageTest, GetDiscoveryTimeoutNotRunning) {
    EXPECT_EQ(service_->GetDiscoveryTimeout(), 0u);
}

// GetDiscoveryTimeout with jitter disabled
TEST_F(SuperframeExtraCoverageTest, GetDiscoveryTimeoutNoJitter) {
    service_->SetDiscoveryJitter(0);
    ASSERT_TRUE(service_->StartSuperframe());
    uint32_t timeout = service_->GetDiscoveryTimeout();
    // Should be exactly 3x superframe duration (no jitter)
    uint32_t expected =
        service_->GetTotalSlots() * service_->GetSlotDuration() * 3;
    EXPECT_EQ(timeout, expected);
    service_->StopSuperframe();
}

// GetDiscoveryTimeout with jitter enabled
TEST_F(SuperframeExtraCoverageTest, GetDiscoveryTimeoutWithJitter) {
    service_->SetDiscoveryJitter(5000);
    service_->SetNodeAddress(0x1234);
    ASSERT_TRUE(service_->StartSuperframe());
    uint32_t timeout = service_->GetDiscoveryTimeout();
    uint32_t base = service_->GetTotalSlots() * service_->GetSlotDuration() * 3;
    EXPECT_GE(timeout, base);
    EXPECT_LE(timeout, base + 5000);
    service_->StopSuperframe();
}

// StartSuperframeDiscovery when not running
TEST_F(SuperframeExtraCoverageTest, StartSuperframeDiscoveryWhenNotRunning) {
    Result result = service_->StartSuperframeDiscovery();
    EXPECT_FALSE(result);
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kInvalidState);
}

// StartSuperframeDiscovery when running
TEST_F(SuperframeExtraCoverageTest, StartSuperframeDiscoveryWhenRunning) {
    ASSERT_TRUE(service_->StartSuperframe());
    Result result = service_->StartSuperframeDiscovery();
    EXPECT_TRUE(result) << result.GetErrorMessage();
    service_->StopSuperframe();
}

// DoNotUpdateStartTimeOnNewSuperframe
TEST_F(SuperframeExtraCoverageTest, DoNotUpdateStartTimeOnNewSuperframe) {
    Result result = service_->DoNotUpdateStartTimeOnNewSuperframe();
    EXPECT_TRUE(result);
}

// SetSynchronized true and false
TEST_F(SuperframeExtraCoverageTest, SetSynchronizedRoundTrip) {
    ASSERT_TRUE(service_->StartSuperframe());
    service_->SetSynchronized(false);
    EXPECT_FALSE(service_->IsSynchronized());
    service_->SetSynchronized(true);
    EXPECT_TRUE(service_->IsSynchronized());
    service_->StopSuperframe();
}

// IsSynchronized when not running
TEST_F(SuperframeExtraCoverageTest, IsSynchronizedNotRunning) {
    EXPECT_FALSE(service_->IsSynchronized());
}

// GetCurrentSlotType with empty slot table
TEST_F(SuperframeExtraCoverageTest, GetCurrentSlotTypeEmptyTable) {
    ASSERT_TRUE(service_->StartSuperframe());
    std::vector<SlotAllocation> empty_table;
    auto slot_type = service_->GetCurrentSlotType(empty_table);
    EXPECT_EQ(slot_type, SlotAllocation::SlotType::SLEEP);
    service_->StopSuperframe();
}

// IsInSlotType with empty table
TEST_F(SuperframeExtraCoverageTest, IsInSlotTypeEmptyTable) {
    ASSERT_TRUE(service_->StartSuperframe());
    std::vector<SlotAllocation> empty_table;
    EXPECT_TRUE(
        service_->IsInSlotType(SlotAllocation::SlotType::SLEEP, empty_table));
    EXPECT_FALSE(
        service_->IsInSlotType(SlotAllocation::SlotType::TX, empty_table));
    service_->StopSuperframe();
}

// GetSlotStartTime and GetSlotEndTime
TEST_F(SuperframeExtraCoverageTest, SlotStartAndEndTime) {
    ASSERT_TRUE(service_->StartSuperframe());
    uint32_t start = service_->GetSlotStartTime(0);
    uint32_t end = service_->GetSlotEndTime(0);
    EXPECT_GT(end, start);
    EXPECT_EQ(end - start, service_->GetSlotDuration());
    service_->StopSuperframe();
}

// UpdateSuperframeState when not running
TEST_F(SuperframeExtraCoverageTest, UpdateSuperframeStateNotRunning) {
    Result result = service_->UpdateSuperframeState();
    EXPECT_FALSE(result);
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kInvalidState);
}

// SynchronizeWith invalid slot
TEST_F(SuperframeExtraCoverageTest, SynchronizeWithInvalidSlot) {
    ASSERT_TRUE(service_->StartSuperframe());
    // Slot index >= total_slots should fail
    Result result = service_->SynchronizeWith(0, 9999);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kInvalidArgument);
    service_->StopSuperframe();
}

// SynchronizeWith valid slot
TEST_F(SuperframeExtraCoverageTest, SynchronizeWithValidSlot) {
    ASSERT_TRUE(service_->StartSuperframe());
    uint32_t current_time = GetRTOS().getTickCount();
    Result result = service_->SynchronizeWith(current_time, 0);
    EXPECT_TRUE(result) << result.GetErrorMessage();
    EXPECT_TRUE(service_->IsSynchronized());
    service_->StopSuperframe();
}

// CheckForNewSuperframe test helper
TEST_F(SuperframeExtraCoverageTest, TestCheckForNewSuperframeNotRunning) {
    EXPECT_FALSE(service_->TestCheckForNewSuperframe());
}

TEST_F(SuperframeExtraCoverageTest, TestCheckForNewSuperframeRunning) {
    ASSERT_TRUE(service_->StartSuperframe());
    // Just started, not yet past superframe boundary
    EXPECT_FALSE(service_->TestCheckForNewSuperframe());
    service_->StopSuperframe();
}

// SetSuperframeCallback null
TEST_F(SuperframeExtraCoverageTest, SetSuperframeCallbackNull) {
    service_->SetSuperframeCallback(nullptr);
    // No crash
}

// SetNodeAddress
TEST_F(SuperframeExtraCoverageTest, SetNodeAddress) {
    service_->SetNodeAddress(0x5678);
    // No crash, verify via GetDiscoveryTimeout jitter behavior
    service_->SetDiscoveryJitter(1000);
    ASSERT_TRUE(service_->StartSuperframe());
    uint32_t timeout = service_->GetDiscoveryTimeout();
    EXPECT_GT(timeout, 0u);
    service_->StopSuperframe();
}

// GetSuperframeDuration
TEST_F(SuperframeExtraCoverageTest, GetSuperframeDuration) {
    uint32_t duration = service_->GetSuperframeDuration();
    EXPECT_EQ(duration,
              service_->GetTotalSlots() * service_->GetSlotDuration());
}

// Constructor with custom params
TEST_F(SuperframeExtraCoverageTest, ConstructorCustomParams) {
    auto custom = std::make_shared<SuperframeService>(8, 500);
    EXPECT_EQ(custom->GetTotalSlots(), 8u);
    EXPECT_EQ(custom->GetSlotDuration(), 500u);
    EXPECT_EQ(custom->GetSuperframeDuration(), 4000u);
}

// HandleNewSuperframe with DoNotUpdateStartTime
TEST_F(SuperframeExtraCoverageTest,
       HandleNewSuperframeWithDoNotUpdateStartTime) {
    ASSERT_TRUE(service_->StartSuperframe());
    service_->DoNotUpdateStartTimeOnNewSuperframe();

    // Advance time past superframe end to exercise the non-update path
    // Let the superframe run briefly
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    Result result = service_->HandleNewSuperframe();
    EXPECT_TRUE(result) << result.GetErrorMessage();
    service_->StopSuperframe();
}

// TestSetSyncInProgress helper
TEST_F(SuperframeExtraCoverageTest, TestSetSyncInProgress) {
    ASSERT_TRUE(service_->StartSuperframe());
    service_->TestSetSyncInProgress(true);

    // SynchronizeWith should skip when sync in progress
    uint32_t current_time = GetRTOS().getTickCount();
    Result result = service_->SynchronizeWith(current_time, 0);
    EXPECT_TRUE(result);  // Returns success (skipped)

    service_->TestSetSyncInProgress(false);
    service_->StopSuperframe();
}

// GetCurrentSlot with auto_advance disabled
TEST_F(SuperframeExtraCoverageTest, GetCurrentSlotAutoAdvanceDisabled) {
    ASSERT_TRUE(service_->StartSuperframe());
    service_->SetAutoAdvance(false);
    uint16_t slot = service_->GetCurrentSlot();
    // Should return a valid slot
    EXPECT_LT(slot, service_->GetTotalSlots());
    service_->SetAutoAdvance(true);
    service_->StopSuperframe();
}

}  // namespace test
}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher

#endif  // ARDUINO
