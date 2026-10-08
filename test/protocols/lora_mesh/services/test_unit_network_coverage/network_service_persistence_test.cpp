/**
 * @file network_service_persistence_test.cpp
 * @brief Unit tests of the NetworkService warm restart (snapshot capture and
 *        restore) and of the election and join rules it relies on
 */

#include <gtest/gtest.h>

#include <memory>

#include "os/os_port.hpp"
#include "protocols/lora_mesh/services/message_queue_service.hpp"
#include "protocols/lora_mesh/services/network_service.hpp"
#include "protocols/lora_mesh/services/superframe_service.hpp"
#include "types/messages/loramesher/join_request_message.hpp"
#include "types/messages/loramesher/join_response_message.hpp"
#include "types/messages/loramesher/nm_claim_message.hpp"
#include "types/messages/loramesher/sync_beacon_message.hpp"

#ifdef ARDUINO

TEST(NetworkServicePersistenceTest, SkipOnArduino) {
    GTEST_SKIP();
}

#else

#include "os/rtos_mock.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {
namespace test {

using ProtocolState = INetworkService::ProtocolState;

class NetworkServicePersistenceTest : public ::testing::Test {
   protected:
    static constexpr AddressType kManager = 0x1000;
    static constexpr uint16_t kNetworkId = 0xBEEF;
    static constexpr uint32_t kNodeTimeoutMs = 60000;

    void SetUp() override {
        mock_ = dynamic_cast<os::RTOSMock*>(&GetRTOS());
        ASSERT_NE(mock_, nullptr);
        mock_->SeedRandom(42);
        mock_->setTimeMode(os::RTOSMock::TimeMode::kVirtualTime);
    }

    void TearDown() override {
        services_.clear();
        mock_->setTimeMode(os::RTOSMock::TimeMode::kRealTime);
    }

    struct Node {
        std::shared_ptr<MessageQueueService> queue;
        std::shared_ptr<SuperframeService> superframe;
        std::unique_ptr<NetworkService> service;
    };

    NetworkService& MakeService(AddressType address, NodeRole role) {
        Node node;
        node.queue = std::make_shared<MessageQueueService>(10);
        node.superframe = std::make_shared<SuperframeService>();
        node.service = std::make_unique<NetworkService>(
            address, node.queue, node.superframe, nullptr);
        INetworkService::NetworkConfig cfg;
        cfg.node_address = address;
        cfg.node_role = role;
        cfg.max_hops = 5;
        cfg.max_packet_size = 255;
        cfg.default_data_slots = 2;
        cfg.max_network_nodes = 20;
        cfg.node_timeout_ms = kNodeTimeoutMs;
        EXPECT_TRUE(node.service->Configure(cfg));
        EXPECT_TRUE(node.superframe->StartSuperframe());
        services_.push_back(std::move(node));
        return *services_.back().service;
    }

    MessageQueueService& QueueOf(const NetworkService& service) {
        for (auto& node : services_) {
            if (node.service.get() == &service) {
                return *node.queue;
            }
        }
        ADD_FAILURE() << "Unknown service";
        return *services_.front().queue;
    }

    /// Manager snapshot with members 0x2001 (slot 1) and 0x2003 (slot 3)
    static storage::NetworkSnapshot ManagerSnapshot(
        uint32_t superframe_ms = 0) {
        storage::NetworkSnapshot snapshot;
        snapshot.node_address = kManager;
        snapshot.was_network_manager = true;
        snapshot.network_id = kNetworkId;
        snapshot.last_sequence = 10;
        snapshot.network_depth = 1;
        snapshot.superframe_duration_ms = superframe_ms;
        snapshot.reservations = {{0x2001, 1}, {0x2003, 3}};
        return snapshot;
    }

    /// Process a join request from @p source addressed to the manager
    void Join(NetworkService& manager, AddressType source) {
        auto request =
            JoinRequestMessage::Create(kManager, source, 2, {}, kManager, 0);
        ASSERT_TRUE(request.has_value());
        ASSERT_TRUE(
            manager.ProcessReceivedMessage(request->ToBaseMessage(), 0));
    }

    static uint8_t ControlSlotOf(NetworkService& service, AddressType node) {
        auto route = service.GetRoutingTable()->FindNode(node);
        return route ? route->control_slot_index : 0xFF;
    }

    static BaseMessage Beacon(AddressType manager, uint16_t network_id) {
        auto beacon = SyncBeaconMessage::CreateOriginal(
            0xFFFF, manager, network_id, 20, 1000, manager, 0, 1, 2);
        EXPECT_TRUE(beacon.has_value());
        return beacon->ToBaseMessage();
    }

    static BaseMessage Claim(AddressType claimant, uint8_t priority,
                             uint16_t network_id) {
        auto claim = NMClaimMessage::Create(claimant, priority, 1, network_id);
        EXPECT_TRUE(claim.has_value());
        return claim->ToBaseMessage();
    }

    os::RTOSMock* mock_ = nullptr;
    std::vector<Node> services_;
};

TEST_F(NetworkServicePersistenceTest, MemberSnapshotCarriesNoReservations) {
    auto& member = MakeService(0x2001, NodeRole::AUTO);
    member.SetState(ProtocolState::NORMAL_OPERATION);

    const auto snapshot = member.CaptureSnapshot();

    EXPECT_EQ(snapshot.node_address, 0x2001);
    EXPECT_FALSE(snapshot.was_network_manager);
    EXPECT_TRUE(snapshot.reservations.empty());
}

TEST_F(NetworkServicePersistenceTest, ManagerSnapshotRecordsMemberSlots) {
    auto& manager = MakeService(kManager, NodeRole::NETWORK_MANAGER);
    ASSERT_TRUE(manager.StartDiscovery(5000));
    ASSERT_EQ(manager.GetState(), ProtocolState::NETWORK_MANAGER);
    Join(manager, 0x2001);
    Join(manager, 0x2002);

    const auto snapshot = manager.CaptureSnapshot();

    EXPECT_TRUE(snapshot.was_network_manager);
    EXPECT_EQ(snapshot.network_id, manager.GetNetworkId());
    EXPECT_NE(snapshot.network_id, 0u);
    ASSERT_EQ(snapshot.reservations.size(), 2u);
    for (const auto& reservation : snapshot.reservations) {
        EXPECT_EQ(reservation.control_slot_index,
                  ControlSlotOf(manager, reservation.address));
    }
    EXPECT_GT(snapshot.superframe_duration_ms, 0u);
}

TEST_F(NetworkServicePersistenceTest, SnapshotOfAnotherNodeIsRejected) {
    auto& node = MakeService(0x3000, NodeRole::NETWORK_MANAGER);
    EXPECT_EQ(node.ApplySnapshot(ManagerSnapshot()).getErrorCode(),
              LoraMesherErrorCode::kInvalidParameter);
    ASSERT_TRUE(node.StartDiscovery(5000));
    EXPECT_NE(node.GetNetworkId(), kNetworkId);
}

TEST_F(NetworkServicePersistenceTest, SequenceContinuesPastCachedNumbers) {
    auto& node = MakeService(0x2001, NodeRole::AUTO);
    storage::NetworkSnapshot snapshot;
    snapshot.node_address = 0x2001;
    snapshot.last_sequence = 240;

    ASSERT_TRUE(node.ApplySnapshot(snapshot));

    // 240 + 32 wraps to 16: no number a neighbour may still cache is reused
    EXPECT_EQ(node.CaptureSnapshot().last_sequence, 16u);
}

TEST_F(NetworkServicePersistenceTest, ManagerResumesNetworkWithReservedSlots) {
    for (NodeRole role : {NodeRole::NETWORK_MANAGER, NodeRole::AUTO}) {
        SCOPED_TRACE(static_cast<int>(role));
        auto& manager = MakeService(kManager, role);
        ASSERT_TRUE(manager.ApplySnapshot(ManagerSnapshot()));

        ASSERT_TRUE(manager.StartDiscovery(5000));

        EXPECT_EQ(manager.GetState(), ProtocolState::NETWORK_MANAGER);
        EXPECT_EQ(manager.GetNetworkId(), kNetworkId);
        // Control band covers the highest reserved slot
        EXPECT_EQ(manager.GetAllocatedControlSlots(), 4u);
        services_.clear();
    }
}

TEST_F(NetworkServicePersistenceTest, NodeOnlyIgnoresManagerSnapshot) {
    auto& node = MakeService(kManager, NodeRole::NODE_ONLY);
    ASSERT_TRUE(node.ApplySnapshot(ManagerSnapshot()));

    ASSERT_TRUE(node.StartDiscovery(5000));

    EXPECT_EQ(node.GetState(), ProtocolState::DISCOVERY);
    EXPECT_EQ(node.GetNetworkId(), 0u);
    EXPECT_EQ(node.GetManagerResumeDelayRemaining(), 0u);
}

TEST_F(NetworkServicePersistenceTest, ManagerListensBeforeResuming) {
    auto& manager = MakeService(kManager, NodeRole::NETWORK_MANAGER);
    ASSERT_TRUE(manager.ApplySnapshot(ManagerSnapshot(10000)));

    ASSERT_TRUE(manager.StartDiscovery(5000));
    EXPECT_EQ(manager.GetState(), ProtocolState::DISCOVERY);
    EXPECT_EQ(manager.GetManagerResumeDelayRemaining(), 15000u);

    // Beacons of other networks do not stop the wait
    ASSERT_TRUE(manager.ProcessReceivedMessage(Beacon(0x5000, 0x1111), 0));
    EXPECT_EQ(manager.GetState(), ProtocolState::DISCOVERY);

    mock_->advanceTime(14999);
    ASSERT_TRUE(manager.PerformDiscovery(5000));
    EXPECT_EQ(manager.GetState(), ProtocolState::DISCOVERY);

    mock_->advanceTime(1);
    ASSERT_TRUE(manager.PerformDiscovery(5000));
    EXPECT_EQ(manager.GetState(), ProtocolState::NETWORK_MANAGER);
    EXPECT_EQ(manager.GetNetworkId(), kNetworkId);
}

TEST_F(NetworkServicePersistenceTest, ManagerJoinsSuccessorOfItsNetwork) {
    auto& manager = MakeService(kManager, NodeRole::NETWORK_MANAGER);
    ASSERT_TRUE(manager.ApplySnapshot(ManagerSnapshot(10000)));
    ASSERT_TRUE(manager.StartDiscovery(5000));

    ASSERT_TRUE(manager.ProcessReceivedMessage(Beacon(0x2001, kNetworkId), 0));

    EXPECT_EQ(manager.GetState(), ProtocolState::JOINING);
    EXPECT_EQ(manager.GetNetworkManagerAddress(), 0x2001);
    EXPECT_EQ(manager.GetManagerResumeDelayRemaining(), 0u);
    EXPECT_TRUE(manager.CaptureSnapshot().reservations.empty());
}

TEST_F(NetworkServicePersistenceTest, ElectionForItsNetworkExtendsTheWait) {
    auto& manager = MakeService(kManager, NodeRole::NETWORK_MANAGER);
    ASSERT_TRUE(manager.ApplySnapshot(ManagerSnapshot(10000)));
    ASSERT_TRUE(manager.StartDiscovery(5000));
    mock_->advanceTime(14000);

    ASSERT_TRUE(
        manager.ProcessReceivedMessage(Claim(0x2001, 64, kNetworkId), 0));

    EXPECT_GT(manager.GetManagerResumeDelayRemaining(), 15000u);
    mock_->advanceTime(1000);
    ASSERT_TRUE(manager.PerformDiscovery(5000));
    EXPECT_EQ(manager.GetState(), ProtocolState::DISCOVERY);
}

TEST_F(NetworkServicePersistenceTest, RejoiningMemberGetsReservedSlot) {
    auto& manager = MakeService(kManager, NodeRole::NETWORK_MANAGER);
    ASSERT_TRUE(manager.ApplySnapshot(ManagerSnapshot()));
    ASSERT_TRUE(manager.StartDiscovery(5000));

    Join(manager, 0x2003);
    Join(manager, 0x2009);

    EXPECT_EQ(ControlSlotOf(manager, 0x2003), 3u);
    // A new member skips the slots still held for members expected back
    EXPECT_EQ(ControlSlotOf(manager, 0x2009), 2u);
}

TEST_F(NetworkServicePersistenceTest, ReservationsLapseAfterNodeTimeout) {
    auto& manager = MakeService(kManager, NodeRole::NETWORK_MANAGER);
    ASSERT_TRUE(manager.ApplySnapshot(ManagerSnapshot()));
    ASSERT_TRUE(manager.StartDiscovery(5000));
    Join(manager, 0x2001);
    ASSERT_TRUE(manager.HandleSuperframeStart());
    ASSERT_EQ(manager.GetAllocatedControlSlots(), 4u);

    mock_->advanceTime(kNodeTimeoutMs);
    ASSERT_TRUE(manager.HandleSuperframeStart());

    // 0x2001 rejoined and keeps slot 1; the slot of silent 0x2003 is released
    EXPECT_EQ(manager.GetAllocatedControlSlots(), 2u);
    const auto snapshot = manager.CaptureSnapshot();
    ASSERT_EQ(snapshot.reservations.size(), 1u);
    EXPECT_EQ(snapshot.reservations.front().address, 0x2001);
}

TEST_F(NetworkServicePersistenceTest, MemberWaitsLongerForItsNetwork) {
    auto& member = MakeService(0x2001, NodeRole::AUTO);
    storage::NetworkSnapshot snapshot;
    snapshot.node_address = 0x2001;
    snapshot.network_id = kNetworkId;
    snapshot.network_depth = 1;
    snapshot.superframe_duration_ms = 10000;
    ASSERT_TRUE(member.ApplySnapshot(snapshot));
    ASSERT_TRUE(member.StartDiscovery(5000));

    // (depth + 2) hops * 4 superframes of 10 s on top of the 5 s timeout
    mock_->advanceTime(5000 + 119999);
    ASSERT_TRUE(member.PerformDiscovery(5000));
    EXPECT_EQ(member.GetState(), ProtocolState::DISCOVERY);

    mock_->advanceTime(1);
    ASSERT_TRUE(member.PerformDiscovery(5000));
    EXPECT_EQ(member.GetState(), ProtocolState::NETWORK_MANAGER);
    // A member never takes the old network id with it
    EXPECT_NE(member.GetNetworkId(), kNetworkId);
}

TEST_F(NetworkServicePersistenceTest, FormerManagerNeverGetsManagerSlot) {
    auto& successor = MakeService(0x2001, NodeRole::AUTO);
    ASSERT_TRUE(successor.CreateNetwork());
    // The old manager is still recorded with its own control slot 0
    successor.UpdateRouteEntry(kManager, kManager, 1, 200, 2, 0);
    successor.GetRoutingTable()->SetControlSlotIndex(kManager, 0);

    auto request =
        JoinRequestMessage::Create(0x2001, kManager, 2, {}, 0x2001, 0);
    ASSERT_TRUE(request.has_value());
    ASSERT_TRUE(successor.ProcessReceivedMessage(request->ToBaseMessage(), 0));

    const uint8_t slot = ControlSlotOf(successor, kManager);
    EXPECT_NE(slot, 0u);
    EXPECT_NE(slot, 0xFFu);
}

TEST_F(NetworkServicePersistenceTest, StrongerNodeClaimsWhenWeakerNodeClaims) {
    auto& node = MakeService(0x2001, NodeRole::AUTO);
    node.SetState(ProtocolState::FAULT_RECOVERY);
    node.StartElectionBackoff();
    ASSERT_TRUE(node.IsElectionPending());

    ASSERT_TRUE(node.ProcessReceivedMessage(Claim(0x2003, 0xF0, 0x1234), 0));

    EXPECT_EQ(node.GetState(), ProtocolState::NM_ELECTION);
    EXPECT_TRUE(QueueOf(node).HasMessage(MessageType::NM_CLAIM));
}

TEST_F(NetworkServicePersistenceTest, ElectionWaitsForSeveralSuperframes) {
    auto& node = MakeService(0x2001, NodeRole::AUTO);
    const uint32_t superframe_ms =
        services_.back().superframe->GetSuperframeDuration();
    ASSERT_GT(superframe_ms, 0u);
    node.SetState(ProtocolState::FAULT_RECOVERY);

    node.StartElectionBackoff();

    // A manager that only reset must be heard before anyone replaces it
    EXPECT_GE(node.GetElectionBackoffRemaining(), 2 * superframe_ms);
}

}  // namespace test
}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher

#endif  // ARDUINO
