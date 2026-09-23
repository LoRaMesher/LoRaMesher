/**
 * @file slot_scheduler_test.cpp
 * @brief Standalone unit tests for the SlotScheduler component.
 *
 * Drives SlotScheduler through a hand-built Context/Host harness (no
 * NetworkService), locking in the slot-shaping contract that the WS-5 ph.4
 * extraction relies on.
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <vector>

#include "protocols/lora_mesh/services/slot_scheduler.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {
namespace {

using SlotType = types::protocols::lora_mesh::SlotAllocation::SlotType;
using NetworkNodeRoute = types::protocols::lora_mesh::NetworkNodeRoute;
using SlotAllocation = types::protocols::lora_mesh::SlotAllocation;

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

    // One direct neighbor holding control index 1 → it gets RX data slots.
    NetworkNodeRoute neighbor(/*addr=*/2, /*time=*/0, /*is_manager=*/false,
                              /*caps=*/0, /*slots=*/2, /*hops=*/1);
    neighbor.is_active = true;
    neighbor.control_slot_index = 1;
    nodes_.push_back(neighbor);

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

/**
 * @brief One node's scheduler plus its local view of the network.
 *
 * Several rigs model different nodes of the same network so the data bands
 * they compute can be compared slot by slot.
 */
struct SchedulerRig {
    SchedulerRig(AddressType address, uint8_t control_index, uint8_t hops_to_nm)
        : self(address), index(control_index), hop(hops_to_nm) {
        SlotScheduler::Host host;
        host.get_routing_nodes = [this]() {
            return nodes;
        };
        host.get_hop_distance_to_nm = [this]() {
            return hop;
        };
        host.get_slot_duration = []() -> uint32_t {
            return 1000;
        };
        host.calculate_nm_tx_time = [](uint8_t, uint8_t) -> uint32_t {
            return 10;
        };
        host.notify_superframe = [](uint16_t) -> Result {
            return Result::Success();
        };
        scheduler = std::make_unique<SlotScheduler>(std::move(host));
    }

    /// Add a routing entry to this node's local view.
    NetworkNodeRoute& AddNode(AddressType address, uint8_t control_index,
                              uint8_t hops, bool is_manager = false,
                              uint8_t data_slots = kDataSlots) {
        NetworkNodeRoute node(address, /*time=*/0, is_manager, /*caps=*/0,
                              data_slots, hops);
        node.is_active = true;
        node.control_slot_index = control_index;
        nodes.push_back(node);
        return nodes.back();
    }

    /// Context of a synchronized (non-NM) node sharing the beacon parameters.
    SlotScheduler::Context Context(AddressType network_manager) const {
        SlotScheduler::Context ctx;
        ctx.node_address = self;
        ctx.network_manager = network_manager;
        ctx.current_network_depth = kDepth;
        ctx.number_of_slots_per_superframe = kFrameSlots;
        ctx.beacon_node_count = kControlSlots;
        ctx.my_control_slot_index = index;
        ctx.max_network_nodes = 50;
        ctx.max_data_slots = 100;
        ctx.default_data_slots = kDataSlots;
        return ctx;
    }

    std::span<const SlotAllocation> Build(AddressType network_manager) {
        EXPECT_TRUE(
            scheduler->UpdateSlotTableIfDirty(Context(network_manager), true)
                .IsSuccess());
        return scheduler->GetSlotTable();
    }

    static constexpr uint8_t kDataSlots = 2;
    static constexpr uint8_t kDepth = 3;
    static constexpr uint8_t kControlSlots = 6;
    static constexpr uint8_t kFrameSlots = 60;
    /// First data slot: sync band (depth + 1) followed by the control band.
    static constexpr size_t kDataStart = kDepth + 1 + kControlSlots;

    AddressType self;
    uint8_t index;
    uint8_t hop;
    std::vector<NetworkNodeRoute> nodes;
    std::unique_ptr<SlotScheduler> scheduler;
};

/**
 * @brief Assert that @p sender's data TX slots are data RX slots for
 *        @p sender in its neighbour @p listener's table.
 */
void ExpectListenerHearsSender(std::span<const SlotAllocation> sender_table,
                               AddressType sender,
                               std::span<const SlotAllocation> listener_table,
                               AddressType listener) {
    ASSERT_EQ(sender_table.size(), listener_table.size());
    size_t tx_count = 0;
    for (size_t i = 0; i < sender_table.size(); i++) {
        if (sender_table[i].type != SlotType::TX) {
            continue;
        }
        tx_count++;
        EXPECT_EQ(listener_table[i].type, SlotType::RX)
            << "slot " << i << ": 0x" << std::hex << sender
            << " transmits but 0x" << listener << " is not listening";
        EXPECT_EQ(listener_table[i].target_address, sender)
            << "slot " << i << ": 0x" << std::hex << listener
            << " listens for another node while 0x" << sender << " transmits";
    }
    EXPECT_EQ(tx_count, SchedulerRig::kDataSlots)
        << "0x" << std::hex << sender << " has no data TX slots";
}

// Network used by the alignment tests (control-slot index in brackets):
//   NM 0x1003[0] -- B 0x1004[2] -- A 0x1002[1]
//   plus far nodes 0x1001[3], 0x1005[4], 0x1006[5] that only some nodes know.
constexpr AddressType kNm = 0x1003;
constexpr AddressType kNodeA = 0x1002;
constexpr AddressType kNodeB = 0x1004;

TEST(SlotSchedulerAlignmentTest, DataBandAlignedWhenNmFlagKnownOnlyByOneNode) {
    SchedulerRig a(kNodeA, /*control_index=*/1, /*hops_to_nm=*/2);
    a.AddNode(kNodeB, 2, /*hops=*/1);
    a.AddNode(kNm, 0, /*hops=*/2);  // learned via routing: no NM flag

    SchedulerRig b(kNodeB, /*control_index=*/2, /*hops_to_nm=*/1);
    b.AddNode(kNodeA, 1, /*hops=*/1);
    b.AddNode(kNm, 0, /*hops=*/1, /*is_manager=*/true);  // heard NM beacon

    auto table_a = a.Build(kNm);
    auto table_b = b.Build(kNm);

    ExpectListenerHearsSender(table_a, kNodeA, table_b, kNodeB);
    ExpectListenerHearsSender(table_b, kNodeB, table_a, kNodeA);
}

TEST(SlotSchedulerAlignmentTest, DataBandAlignedWhenOneNodeMissesFarNode) {
    SchedulerRig a(kNodeA, 1, 2);
    a.AddNode(kNodeB, 2, 1);
    a.AddNode(kNm, 0, 2, /*is_manager=*/true);

    SchedulerRig b(kNodeB, 2, 1);
    b.AddNode(kNodeA, 1, 1);
    b.AddNode(kNm, 0, 1, /*is_manager=*/true);
    // Only B knows a far node that sorts before both A and B.
    b.AddNode(0x1001, 3, /*hops=*/4);

    auto table_a = a.Build(kNm);
    auto table_b = b.Build(kNm);

    ExpectListenerHearsSender(table_a, kNodeA, table_b, kNodeB);
    ExpectListenerHearsSender(table_b, kNodeB, table_a, kNodeA);
}

TEST(SlotSchedulerAlignmentTest, DataBandAlignedWithStaleRemoteAllocation) {
    SchedulerRig a(kNodeA, 1, 2);
    a.AddNode(kNodeB, 2, 1);
    a.AddNode(kNm, 0, 2, /*is_manager=*/true);
    a.AddNode(0x1001, 3, 4, false, /*data_slots=*/5);  // stale count

    SchedulerRig b(kNodeB, 2, 1);
    b.AddNode(kNodeA, 1, 1);
    b.AddNode(kNm, 0, 1, /*is_manager=*/true);
    b.AddNode(0x1001, 3, 4);

    auto table_a = a.Build(kNm);
    auto table_b = b.Build(kNm);

    ExpectListenerHearsSender(table_a, kNodeA, table_b, kNodeB);
    ExpectListenerHearsSender(table_b, kNodeB, table_a, kNodeA);
}

TEST(SlotSchedulerAlignmentTest, DataSlotsIndexedByControlIndex) {
    // Control indices deliberately do not follow address order.
    SchedulerRig b(kNodeB, /*control_index=*/2, /*hops_to_nm=*/1);
    b.AddNode(kNm, 0, 1, /*is_manager=*/true);
    b.AddNode(0x1005, 1, 1);
    b.AddNode(kNodeA, 3, 1);
    b.AddNode(0x1006, 4, 3);  // not a direct neighbour

    auto table = b.Build(kNm);
    constexpr size_t d = SchedulerRig::kDataSlots;
    constexpr size_t start = SchedulerRig::kDataStart;

    // Index c owns data slots [start + c*d, start + (c+1)*d).
    for (size_t j = 0; j < d; j++) {
        EXPECT_EQ(table[start + 0 * d + j].type, SlotType::RX);
        EXPECT_EQ(table[start + 0 * d + j].target_address, kNm);
        EXPECT_EQ(table[start + 1 * d + j].type, SlotType::RX);
        EXPECT_EQ(table[start + 1 * d + j].target_address, 0x1005);
        EXPECT_EQ(table[start + 2 * d + j].type, SlotType::TX);
        EXPECT_EQ(table[start + 3 * d + j].type, SlotType::RX);
        EXPECT_EQ(table[start + 3 * d + j].target_address, kNodeA);
        EXPECT_EQ(table[start + 4 * d + j].type, SlotType::SLEEP);
        // Index 5 is unassigned: sleep.
        EXPECT_EQ(table[start + 5 * d + j].type, SlotType::SLEEP);
    }
}

TEST(SlotSchedulerAlignmentTest, NeighbourNmWithUnknownIndexOwnsIndexZero) {
    SchedulerRig b(kNodeB, 2, 1);
    b.AddNode(kNm, /*control_index=*/0xFF, 1, /*is_manager=*/true);

    auto table = b.Build(kNm);
    for (size_t j = 0; j < SchedulerRig::kDataSlots; j++) {
        EXPECT_EQ(table[SchedulerRig::kDataStart + j].type, SlotType::RX);
        EXPECT_EQ(table[SchedulerRig::kDataStart + j].target_address, kNm);
    }
}

TEST(SlotSchedulerAlignmentTest, InactiveNeighbourIsSleep) {
    SchedulerRig b(kNodeB, 2, 1);
    b.AddNode(kNm, 0, 1, /*is_manager=*/true);
    b.AddNode(kNodeA, 1, 1).is_active = false;

    auto table = b.Build(kNm);
    constexpr size_t d = SchedulerRig::kDataSlots;
    for (size_t j = 0; j < d; j++) {
        EXPECT_EQ(table[SchedulerRig::kDataStart + d + j].type,
                  SlotType::SLEEP);
    }
}

TEST(SlotSchedulerAlignmentTest, DuplicateIndexPrefersMostRecentlySeen) {
    SchedulerRig b(kNodeB, 2, 1);
    b.AddNode(kNm, 0, 1, /*is_manager=*/true);
    b.AddNode(kNodeA, 1, 1).last_seen = 100;
    b.AddNode(0x1005, 1, 1).last_seen = 200;

    auto table = b.Build(kNm);
    constexpr size_t d = SchedulerRig::kDataSlots;
    for (size_t j = 0; j < d; j++) {
        EXPECT_EQ(table[SchedulerRig::kDataStart + d + j].target_address,
                  0x1005);
    }
}

TEST(SlotSchedulerAlignmentTest, IndexBeyondDataBudgetGetsNoDataSlots) {
    // Budget of 4 data slots covers indices 0 and 1 only.
    SchedulerRig b(kNodeB, /*control_index=*/2, 1);
    b.AddNode(kNm, 0, 1, /*is_manager=*/true);
    SlotScheduler::Context ctx = b.Context(kNm);
    ctx.max_data_slots = 4;

    ASSERT_TRUE(b.scheduler->UpdateSlotTableIfDirty(ctx, true).IsSuccess());
    for (const auto& slot : b.scheduler->GetSlotTable()) {
        EXPECT_NE(slot.type, SlotType::TX);
    }
}

TEST_F(SlotSchedulerTest, NmDataBandIsControlSlotsTimesDataSlots) {
    SlotScheduler::Context ctx = NmContext();
    ctx.max_data_slots = 100;
    for (uint8_t index = 1; index <= 3; index++) {
        NetworkNodeRoute node(static_cast<AddressType>(0x10 + index), 0, false,
                              0, 2, /*hops=*/1);
        node.is_active = true;
        node.control_slot_index = index;
        nodes_.push_back(node);
    }

    ASSERT_TRUE(scheduler_->UpdateSlotTableIfDirty(ctx, true).IsSuccess());
    ASSERT_EQ(scheduler_->GetAllocatedControlSlots(), 4u);

    size_t tx = 0;
    size_t rx = 0;
    for (const auto& slot : scheduler_->GetSlotTable()) {
        tx += slot.type == SlotType::TX;
        rx += slot.type == SlotType::RX;
    }
    EXPECT_EQ(tx, ctx.default_data_slots);
    EXPECT_EQ(rx, 3u * ctx.default_data_slots);
}

}  // namespace
}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher
