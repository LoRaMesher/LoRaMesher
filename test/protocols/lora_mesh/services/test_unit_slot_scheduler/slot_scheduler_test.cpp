/**
 * @file slot_scheduler_test.cpp
 * @brief Standalone unit tests for the SlotScheduler component.
 *
 * Drives SlotScheduler through a hand-built Context/Host harness (no
 * NetworkService), locking in the slot-shaping contract that the WS-5 ph.4
 * extraction relies on.
 */
#include <gtest/gtest.h>

#include <vector>

#include "protocols/lora_mesh/services/slot_scheduler.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {
namespace {

using SlotType = types::protocols::lora_mesh::SlotAllocation::SlotType;
using NetworkNodeRoute = types::protocols::lora_mesh::NetworkNodeRoute;

/// Test harness owning the scheduler's injected dependencies.
class SlotSchedulerTest : public ::testing::Test {
   protected:
    void SetUp() override { scheduler_ = MakeScheduler(); }

    std::unique_ptr<SlotScheduler> MakeScheduler() {
        SlotScheduler::Host host;
        host.get_routing_nodes = [this]() {
            return nodes_;
        };
        host.get_hop_distance_to_nm = [this]() {
            return hop_distance_;
        };
        host.get_allocated_data_slots = [this]() {
            return allocated_data_slots_;
        };
        host.get_slot_duration = []() -> uint32_t {
            return 1000;
        };
        host.calculate_nm_tx_time = [this](uint8_t, uint8_t) {
            return nm_tx_time_ms_;
        };
        host.notify_superframe = [this](uint16_t total_slots) -> Result {
            last_notified_slots_ = total_slots;
            ++notify_count_;
            return Result::Success();
        };
        return std::make_unique<SlotScheduler>(std::move(host));
    }

    /// Context for a freshly created Network Manager (node 1).
    SlotScheduler::Context NmContext() const {
        SlotScheduler::Context ctx;
        ctx.node_address = 1;
        ctx.network_manager = 1;
        ctx.in_network_manager_state = true;
        ctx.network_creator = true;
        ctx.current_network_depth = 0;
        ctx.number_of_slots_per_superframe = 0;
        ctx.beacon_node_count = 1;
        ctx.my_control_slot_index = 0;
        ctx.local_allocated_data_slots = 1;
        ctx.local_capabilities = 0;
        ctx.no_received_sync_beacon_count = 0;
        ctx.max_network_nodes = 20;
        ctx.max_data_slots = 10;
        ctx.default_data_slots = 2;
        ctx.target_duty_cycle = 0.01f;
        ctx.min_sleep_fraction = 0.30f;
        ctx.churn_margin_slots = 2;
        return ctx;
    }

    std::unique_ptr<SlotScheduler> scheduler_;
    std::vector<NetworkNodeRoute> nodes_;
    uint8_t hop_distance_ = 0;
    uint8_t allocated_data_slots_ = 0;
    uint32_t nm_tx_time_ms_ = 10;
    uint16_t last_notified_slots_ = 0;
    int notify_count_ = 0;
};

TEST_F(SlotSchedulerTest, SetDiscoverySlotsLaysOutDiscoveryBand) {
    EXPECT_TRUE(scheduler_->SetDiscoverySlots().IsSuccess());

    // Defaults to ISuperframeService::DEFAULT_DISCOVERY_SLOT_COUNT (10).
    EXPECT_EQ(scheduler_->GetSlotCount(), 10u);
    auto table = scheduler_->GetSlotTable();
    ASSERT_EQ(table.size(), 10u);
    for (const auto& slot : table) {
        EXPECT_EQ(slot.type, SlotType::DISCOVERY_RX);
        EXPECT_EQ(slot.target_address, kBroadcastAddress);
    }
}

TEST_F(SlotSchedulerTest, UpdateSlotTableProducesValidSuperframe) {
    SlotScheduler::Context ctx = NmContext();
    allocated_data_slots_ = 1;  // NM's own data slot

    EXPECT_TRUE(scheduler_->UpdateSlotTableIfDirty(ctx, true).IsSuccess());

    EXPECT_EQ(notify_count_, 1);
    EXPECT_GE(scheduler_->GetSlotCount(), kMinSlots);
    EXPECT_EQ(scheduler_->GetSlotCount(), last_notified_slots_);
    EXPECT_GE(scheduler_->GetAllocatedControlSlots(), 1u);

    auto table = scheduler_->GetSlotTable();
    ASSERT_FALSE(table.empty());
    // NM transmits the sync beacon in slot 0.
    EXPECT_EQ(table[0].type, SlotType::SYNC_BEACON_TX);
    // Discovery slots always close the superframe.
    EXPECT_EQ(table.back().type, SlotType::DISCOVERY_RX);
}

TEST_F(SlotSchedulerTest, DirtyGatingSkipsRebuildWhenClean) {
    SlotScheduler::Context ctx = NmContext();

    ASSERT_TRUE(scheduler_->UpdateSlotTableIfDirty(ctx, true).IsSuccess());
    EXPECT_EQ(notify_count_, 1);

    // Clean after a successful build: a non-forced call is a no-op.
    EXPECT_TRUE(scheduler_->UpdateSlotTableIfDirty(ctx, false).IsSuccess());
    EXPECT_EQ(notify_count_, 1);

    // Marking dirty makes the next non-forced call rebuild.
    scheduler_->MarkDirty();
    EXPECT_TRUE(scheduler_->UpdateSlotTableIfDirty(ctx, false).IsSuccess());
    EXPECT_EQ(notify_count_, 2);
}

TEST_F(SlotSchedulerTest, IsTDMANeighborReflectsRxSlots) {
    SlotScheduler::Context ctx = NmContext();
    ctx.local_allocated_data_slots = 1;

    // One direct neighbor (addr 2, 1 data slot) → it gets an RX data slot.
    NetworkNodeRoute neighbor(/*addr=*/2, /*time=*/0, /*is_manager=*/false,
                              /*caps=*/0, /*slots=*/1, /*hops=*/1);
    neighbor.is_active = true;
    nodes_.push_back(neighbor);
    allocated_data_slots_ = 2;  // self (1) + neighbor (1)

    ASSERT_TRUE(scheduler_->UpdateSlotTableIfDirty(ctx, true).IsSuccess());

    EXPECT_TRUE(scheduler_->IsTDMANeighbor(2));
    EXPECT_FALSE(scheduler_->IsTDMANeighbor(0x1234));
}

TEST_F(SlotSchedulerTest, SetJoiningSlotsConvertsToListenOnly) {
    SlotScheduler::Context ctx = NmContext();
    ASSERT_TRUE(scheduler_->SetJoiningSlots(ctx).IsSuccess());

    bool discovery_tx_seen = false;
    for (const auto& slot : scheduler_->GetSlotTable()) {
        // No transmit slots remain except the single join DISCOVERY_TX.
        EXPECT_NE(slot.type, SlotType::SYNC_BEACON_TX);
        EXPECT_NE(slot.type, SlotType::CONTROL_TX);
        EXPECT_NE(slot.type, SlotType::TX);
        if (slot.type == SlotType::DISCOVERY_TX) {
            discovery_tx_seen = true;
            EXPECT_EQ(slot.target_address, ctx.network_manager);
        }
    }
    EXPECT_TRUE(discovery_tx_seen);
}

TEST_F(SlotSchedulerTest, ScheduleDiscoverySlotForwardingFlipsRxToTx) {
    ASSERT_TRUE(scheduler_->SetDiscoverySlots().IsSuccess());

    EXPECT_TRUE(scheduler_->ScheduleDiscoverySlotForwarding(0x0099));

    auto table = scheduler_->GetSlotTable();
    ASSERT_FALSE(table.empty());
    // The first DISCOVERY_RX slot is flipped to a TX toward the NM.
    EXPECT_EQ(table[0].type, SlotType::DISCOVERY_TX);
    EXPECT_EQ(table[0].target_address, 0x0099);
}

}  // namespace
}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher
