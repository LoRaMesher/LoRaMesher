/**
 * @file network_service_resume_test.cpp
 * @brief Unit tests of a member resuming its membership after a deep sleep
 */

#include <gtest/gtest.h>

#include <memory>
#include <utility>
#include <vector>

#include "os/os_port.hpp"
#include "protocols/lora_mesh/services/message_queue_service.hpp"
#include "protocols/lora_mesh/services/network_service.hpp"
#include "protocols/lora_mesh/services/superframe_service.hpp"
#include "types/messages/loramesher/join_request_message.hpp"
#include "types/messages/loramesher/join_response_message.hpp"
#include "types/messages/loramesher/sync_beacon_message.hpp"
#include "types/storage/resume_snapshot_codec.hpp"

#ifdef ARDUINO

TEST(NetworkServiceResumeTest, SkipOnArduino) {
    GTEST_SKIP();
}

#else

#include "os/rtos_mock.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {
namespace test {

using ProtocolState = INetworkService::ProtocolState;
using types::protocols::lora_mesh::NetworkNodeRoute;
using types::protocols::lora_mesh::SlotAllocation;

class NetworkServiceResumeTest : public ::testing::Test {
   protected:
    static constexpr AddressType kManager = 0x1000;
    static constexpr AddressType kMember = 0x2001;
    static constexpr AddressType kNeighbour = 0x2002;
    static constexpr uint16_t kNetworkId = 0xBEEF;
    static constexpr uint8_t kControlSlot = 2;
    static constexpr uint8_t kMaxNodes = 20;

    void SetUp() override {
        mock_ = dynamic_cast<os::RTOSMock*>(&GetRTOS());
        ASSERT_NE(mock_, nullptr);
        mock_->SeedRandom(42);
        mock_->setTimeMode(os::RTOSMock::TimeMode::kVirtualTime);
    }

    void TearDown() override {
        nodes_.clear();
        mock_->setTimeMode(os::RTOSMock::TimeMode::kRealTime);
    }

    struct Node {
        std::shared_ptr<MessageQueueService> queue;
        std::shared_ptr<SuperframeService> superframe;
        std::unique_ptr<NetworkService> service;
    };

    Node& MakeNode(AddressType address, NodeRole role = NodeRole::NODE_ONLY) {
        auto node = std::make_unique<Node>();
        node->queue = std::make_shared<MessageQueueService>(10);
        node->superframe = std::make_shared<SuperframeService>();
        node->service = std::make_unique<NetworkService>(
            address, node->queue, node->superframe, nullptr);
        INetworkService::NetworkConfig cfg;
        cfg.node_address = address;
        cfg.node_role = role;
        cfg.max_hops = 5;
        cfg.max_packet_size = 255;
        cfg.default_data_slots = 2;
        cfg.max_network_nodes = kMaxNodes;
        EXPECT_TRUE(node->service->Configure(cfg));
        nodes_.push_back(std::move(node));
        return *nodes_.back();
    }

    static BaseMessage Beacon() {
        auto beacon = SyncBeaconMessage::CreateOriginal(
            0xFFFF, kManager, kNetworkId, 20, 1000, kManager, 0, 1, 3);
        EXPECT_TRUE(beacon.has_value());
        return beacon->ToBaseMessage();
    }

    /// A member that joined through the manager's beacon, heard a second
    /// beacon and knows a direct neighbour with link history
    Node& MakeJoinedMember() {
        Node& node = MakeNode(kMember);
        NetworkService& member = *node.service;
        EXPECT_TRUE(node.superframe->StartSuperframe());
        EXPECT_TRUE(member.StartDiscovery(5000));
        EXPECT_TRUE(
            member.ProcessReceivedMessage(Beacon(), GetRTOS().getTickCount()));
        EXPECT_EQ(member.GetState(), ProtocolState::JOINING);

        auto response = JoinResponseMessage::Create(
            kMember, kManager, kNetworkId, 2,
            JoinResponseHeader::ResponseStatus::ACCEPTED, {}, kMember, 0,
            kControlSlot);
        EXPECT_TRUE(response.has_value());
        EXPECT_TRUE(
            member.ProcessReceivedMessage(response->ToBaseMessage(), 0));
        EXPECT_EQ(member.GetState(), ProtocolState::NORMAL_OPERATION);

        mock_->advanceTime(20000);
        EXPECT_TRUE(
            member.ProcessReceivedMessage(Beacon(), GetRTOS().getTickCount()));

        member.UpdateRouteEntry(kNeighbour, kNeighbour, 1, 200, 2, 0);
        auto neighbour = member.GetRoutingTable()->FindNode(kNeighbour);
        EXPECT_TRUE(neighbour.has_value());
        for (int i = 0; i < 5; ++i) {
            neighbour->link_stats.ExpectMessage();
            neighbour->link_stats.ReceivedMessage(GetRTOS().getTickCount(),
                                                  -90.0f, 7.5f);
        }
        neighbour->link_stats.UpdateRemoteQuality(180);
        neighbour->path_rtt = {3000, 400};
        EXPECT_TRUE(member.GetRoutingTable()->AddNode(*neighbour));
        EXPECT_TRUE(member.UpdateSlotTable());
        return node;
    }

    /// A joined member that has heard beacons long enough to sleep
    Node& MakeSettledMember() {
        Node& node = MakeJoinedMember();
        for (int i = 0; i < 2; ++i) {
            EXPECT_TRUE(node.service->HandleSuperframeStart());
            mock_->advanceTime(20000);
            EXPECT_TRUE(node.service->ProcessReceivedMessage(
                Beacon(), GetRTOS().getTickCount()));
        }
        return node;
    }

    static void FillTiming(storage::ResumeSnapshot& snapshot,
                           const SuperframeService& superframe) {
        snapshot.timing.total_slots = superframe.GetTotalSlots();
        snapshot.timing.slot_duration_ms = superframe.GetSlotDuration();
    }

    static std::vector<std::pair<uint16_t, SlotAllocation::SlotType>> Slots(
        const NetworkService& service) {
        std::vector<std::pair<uint16_t, SlotAllocation::SlotType>> slots;
        service.ForEachSlot([&](const SlotAllocation& allocation) {
            slots.emplace_back(allocation.slot_number, allocation.type);
        });
        return slots;
    }

    os::RTOSMock* mock_ = nullptr;
    std::vector<std::unique_ptr<Node>> nodes_;
};

TEST_F(NetworkServiceResumeTest, ResumedMemberMatchesTheOriginal) {
    Node& original = MakeJoinedMember();
    auto snapshot = original.service->CaptureResumeSnapshot();
    ASSERT_TRUE(snapshot.has_value());
    FillTiming(*snapshot, *original.superframe);
    const auto blob = storage::ResumeSnapshotCodec::Encode(*snapshot);
    ASSERT_TRUE(blob.has_value());

    Node& resumed = MakeNode(kMember);
    ASSERT_TRUE(resumed.service->ApplyResumeSnapshot(*snapshot));

    EXPECT_EQ(resumed.service->GetState(), ProtocolState::NORMAL_OPERATION);
    EXPECT_EQ(resumed.service->GetNetworkManagerAddress(), kManager);
    EXPECT_EQ(resumed.service->GetNetworkId(), kNetworkId);
    EXPECT_EQ(resumed.service->GetMyControlSlotIndex(), kControlSlot);
    EXPECT_EQ(resumed.service->GetMissedSyncBeaconCount(), 0u);
    EXPECT_EQ(resumed.superframe->GetTotalSlots(),
              original.superframe->GetTotalSlots());
    EXPECT_EQ(resumed.superframe->GetSlotDuration(),
              original.superframe->GetSlotDuration());
    EXPECT_EQ(Slots(*resumed.service), Slots(*original.service));
    EXPECT_EQ(
        resumed.service->GetRoutingTable()->GetDirectLinkQuality(kNeighbour),
        original.service->GetRoutingTable()->GetDirectLinkQuality(kNeighbour));
    EXPECT_EQ(resumed.service->GetRoutingTable()->GetLinkQuality(kNeighbour),
              original.service->GetRoutingTable()->GetLinkQuality(kNeighbour));

    // Everything captured comes back unchanged
    auto recaptured = resumed.service->CaptureResumeSnapshot();
    ASSERT_TRUE(recaptured.has_value());
    recaptured->timing = snapshot->timing;
    EXPECT_EQ(storage::ResumeSnapshotCodec::Encode(*recaptured), blob);
}

TEST_F(NetworkServiceResumeTest, SnapshotHoldsMembershipAndLinkHistory) {
    Node& member = MakeJoinedMember();
    auto snapshot = member.service->CaptureResumeSnapshot();
    ASSERT_TRUE(snapshot.has_value());

    EXPECT_FALSE(snapshot->network.was_network_manager);
    EXPECT_EQ(snapshot->network.network_id, kNetworkId);
    EXPECT_EQ(snapshot->member.network_manager, kManager);
    EXPECT_EQ(snapshot->member.control_slot_index, kControlSlot);
    EXPECT_EQ(snapshot->member.slots_per_superframe, 20u);
    EXPECT_EQ(snapshot->member.beacon_node_count, 3u);

    EXPECT_EQ(snapshot->member.allocated_data_slots, 2u);

    const NetworkNodeRoute* neighbour = nullptr;
    for (const auto& route : snapshot->routes) {
        if (route.GetAddress() == kNeighbour) {
            neighbour = &route;
        }
    }
    ASSERT_NE(neighbour, nullptr);
    EXPECT_EQ(neighbour->link_stats.messages_received, 5u);
    EXPECT_EQ(neighbour->link_stats.remote_link_quality, 180u);
    EXPECT_EQ(neighbour->path_rtt.srtt_ms, 3000u);
}

TEST_F(NetworkServiceResumeTest, OnlyMembersInNormalOperationCapture) {
    Node& fresh = MakeNode(kMember);
    EXPECT_FALSE(fresh.service->CaptureResumeSnapshot().has_value());

    Node& joining = MakeNode(0x2005);
    ASSERT_TRUE(joining.superframe->StartSuperframe());
    ASSERT_TRUE(joining.service->StartDiscovery(5000));
    ASSERT_TRUE(joining.service->ProcessReceivedMessage(
        Beacon(), GetRTOS().getTickCount()));
    ASSERT_EQ(joining.service->GetState(), ProtocolState::JOINING);
    EXPECT_FALSE(joining.service->CaptureResumeSnapshot().has_value());

    Node& manager = MakeNode(kManager, NodeRole::NETWORK_MANAGER);
    ASSERT_TRUE(manager.superframe->StartSuperframe());
    ASSERT_TRUE(manager.service->CreateNetwork());
    EXPECT_FALSE(manager.service->CaptureResumeSnapshot().has_value());
}

TEST_F(NetworkServiceResumeTest, UnusableSnapshotChangesNothing) {
    Node& member = MakeJoinedMember();
    auto snapshot = member.service->CaptureResumeSnapshot();
    ASSERT_TRUE(snapshot.has_value());
    FillTiming(*snapshot, *member.superframe);

    const auto refused = [&](AddressType address, NodeRole role,
                             const storage::ResumeSnapshot& candidate) {
        Node& node = MakeNode(address, role);
        const Result result = node.service->ApplyResumeSnapshot(candidate);
        return !result &&
               node.service->GetState() == ProtocolState::INITIALIZING &&
               node.service->GetRoutingTable()->GetSize() == 0 &&
               node.service->GetNetworkId() == 0;
    };

    EXPECT_TRUE(refused(0x2009, NodeRole::NODE_ONLY, *snapshot));
    EXPECT_TRUE(refused(kMember, NodeRole::NETWORK_MANAGER, *snapshot));

    storage::ResumeSnapshot inconsistent = *snapshot;
    inconsistent.timing.slot_duration_ms += 1;
    EXPECT_TRUE(refused(kMember, NodeRole::NODE_ONLY, inconsistent));

    storage::ResumeSnapshot too_many_routes = *snapshot;
    for (AddressType i = 0; i < kMaxNodes; ++i) {
        too_many_routes.routes.emplace_back(
            static_cast<AddressType>(0x3000 + i), kNeighbour, 2, 100, 0);
    }
    EXPECT_TRUE(refused(kMember, NodeRole::NODE_ONLY, too_many_routes));

    storage::ResumeSnapshot bad_streams = *snapshot;
    bad_streams.delivery_streams = {{kNeighbour, false, 1, 2, 0}};
    EXPECT_TRUE(refused(kMember, NodeRole::NODE_ONLY, bad_streams));
}

TEST_F(NetworkServiceResumeTest, DeepSleepNeedsASettledMember) {
    Node& fresh = MakeNode(0x2007);
    EXPECT_STREQ(fresh.service->GetDeepSleepBlocker(),
                 "not in normal operation");

    Node& member = MakeSettledMember();
    EXPECT_EQ(member.service->GetDeepSleepBlocker(), nullptr);

    // A superframe without a beacon
    ASSERT_TRUE(member.service->HandleSuperframeStart());
    ASSERT_GT(member.service->GetMissedSyncBeaconCount(), 0u);
    EXPECT_STREQ(member.service->GetDeepSleepBlocker(), "missed sync beacons");

    mock_->advanceTime(20000);
    ASSERT_TRUE(member.service->ProcessReceivedMessage(
        Beacon(), GetRTOS().getTickCount()));
    EXPECT_EQ(member.service->GetDeepSleepBlocker(), nullptr);
}

TEST_F(NetworkServiceResumeTest, PendingScheduleChangeSurvivesTheSleep) {
    Node& original = MakeSettledMember();
    const auto slots_before = Slots(*original.service);
    // A routing change waits for the next rebuild
    ASSERT_TRUE(original.service->UpdateNetworkNode(kNeighbour, false, 3));
    ASSERT_EQ(Slots(*original.service), slots_before);
    EXPECT_EQ(original.service->GetDeepSleepBlocker(), nullptr);

    auto snapshot = original.service->CaptureResumeSnapshot();
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_TRUE(snapshot->schedule.rebuild_pending);
    FillTiming(*snapshot, *original.superframe);

    Node& resumed = MakeNode(kMember);
    ASSERT_TRUE(resumed.service->ApplyResumeSnapshot(*snapshot));
    EXPECT_EQ(Slots(*resumed.service), slots_before);
    auto recaptured = resumed.service->CaptureResumeSnapshot();
    ASSERT_TRUE(recaptured.has_value());
    EXPECT_TRUE(recaptured->schedule.rebuild_pending);

    // Both rebuild at the next beacon, to the same table
    mock_->advanceTime(20000);
    for (Node* node : {&original, &resumed}) {
        ASSERT_TRUE(node->service->ProcessReceivedMessage(
            Beacon(), GetRTOS().getTickCount()));
        auto after = node->service->CaptureResumeSnapshot();
        ASSERT_TRUE(after.has_value());
        EXPECT_FALSE(after->schedule.rebuild_pending);
    }
    EXPECT_EQ(Slots(*resumed.service), Slots(*original.service));
}

TEST_F(NetworkServiceResumeTest, ResumedMemberWaitsForABeacon) {
    Node& original = MakeJoinedMember();
    auto snapshot = original.service->CaptureResumeSnapshot();
    ASSERT_TRUE(snapshot.has_value());
    FillTiming(*snapshot, *original.superframe);
    EXPECT_FALSE(original.service->IsAwaitingResync());

    // The node sleeps through the rest of the superframe and part of the next
    mock_->advanceTime(25000);
    Node& resumed = MakeNode(kMember);
    ASSERT_TRUE(resumed.service->ApplyResumeSnapshot(*snapshot));
    ASSERT_TRUE(resumed.superframe->ResumeAt(
        snapshot->member.last_sync_beacon_ms, 20, 1000, 0));
    EXPECT_TRUE(resumed.service->IsAwaitingResync());
    EXPECT_FALSE(resumed.service->HeardSyncBeaconThisSuperframe());
    EXPECT_STREQ(resumed.service->GetDeepSleepBlocker(),
                 "no sync beacon in this superframe");

    mock_->advanceTime(15000);
    ASSERT_TRUE(resumed.service->ProcessReceivedMessage(
        Beacon(), GetRTOS().getTickCount()));
    EXPECT_FALSE(resumed.service->IsAwaitingResync());
    EXPECT_TRUE(resumed.service->HeardSyncBeaconThisSuperframe());
    EXPECT_EQ(resumed.service->GetDeepSleepBlocker(), nullptr);
}

TEST_F(NetworkServiceResumeTest, NodeThatHasNotJoinedStaysAwake) {
    Node& node = MakeNode(0x2007);
    ASSERT_TRUE(node.superframe->StartSuperframe());
    ASSERT_TRUE(node.service->StartDiscovery(5000));
    EXPECT_STREQ(node.service->GetSleepHold(), "not joined");

    ASSERT_TRUE(node.service->ProcessReceivedMessage(Beacon(),
                                                     GetRTOS().getTickCount()));
    ASSERT_EQ(node.service->GetState(), ProtocolState::JOINING);
    EXPECT_STREQ(node.service->GetSleepHold(), "not joined");
    EXPECT_STREQ(node.service->GetDeepSleepBlocker(),
                 "not in normal operation");
}

TEST_F(NetworkServiceResumeTest, JoinedMemberSettlesBeforeSleeping) {
    Node& member = MakeJoinedMember();  // One beacon heard since joining
    EXPECT_STREQ(member.service->GetSleepHold(), "settling after joining");
    EXPECT_STREQ(member.service->GetDeepSleepBlocker(),
                 "settling after joining");

    for (int i = 0; i < 2; ++i) {
        ASSERT_TRUE(member.service->HandleSuperframeStart());
        mock_->advanceTime(20000);
        ASSERT_TRUE(member.service->ProcessReceivedMessage(
            Beacon(), GetRTOS().getTickCount()));
    }
    EXPECT_EQ(member.service->GetSleepHold(), nullptr);
    EXPECT_EQ(member.service->GetDeepSleepBlocker(), nullptr);

    // A superframe without a beacon starts the settling again
    ASSERT_TRUE(member.service->HandleSuperframeStart());
    mock_->advanceTime(20000);
    ASSERT_TRUE(member.service->HandleSuperframeStart());
    mock_->advanceTime(1000);
    ASSERT_TRUE(member.service->ProcessReceivedMessage(
        Beacon(), GetRTOS().getTickCount()));
    EXPECT_STREQ(member.service->GetSleepHold(), "settling after joining");
}

TEST_F(NetworkServiceResumeTest, ResumedMemberIsAlreadySettled) {
    Node& original = MakeJoinedMember();
    auto snapshot = original.service->CaptureResumeSnapshot();
    ASSERT_TRUE(snapshot.has_value());
    FillTiming(*snapshot, *original.superframe);

    Node& resumed = MakeNode(kMember);
    ASSERT_TRUE(resumed.service->ApplyResumeSnapshot(*snapshot));
    EXPECT_EQ(resumed.service->GetSleepHold(), nullptr);
}

TEST_F(NetworkServiceResumeTest, SponsorStaysAwakeWhileRelayingAJoin) {
    Node& member = MakeSettledMember();
    ASSERT_EQ(member.service->GetSleepHold(), nullptr);

    constexpr AddressType kJoiner = 0x2009;
    auto request =
        JoinRequestMessage::Create(kManager, kJoiner, 2, {}, kMember, kMember);
    ASSERT_TRUE(request.has_value());
    ASSERT_TRUE(
        member.service->ProcessReceivedMessage(request->ToBaseMessage(), 0));
    EXPECT_STREQ(member.service->GetSleepHold(), "relaying a join");

    // Still held in the next superframe, for the response and a retry
    mock_->advanceTime(20000);
    EXPECT_STREQ(member.service->GetSleepHold(), "relaying a join");
    // Released once the superframe after it is over
    mock_->advanceTime(25000);
    EXPECT_EQ(member.service->GetSleepHold(), nullptr);
}

TEST_F(NetworkServiceResumeTest, RelayHoldEndsWhenTheJoinerIsReachable) {
    Node& member = MakeSettledMember();
    constexpr AddressType kJoiner = 0x2009;
    auto request =
        JoinRequestMessage::Create(kManager, kJoiner, 2, {}, kMember, kMember);
    ASSERT_TRUE(request.has_value());
    ASSERT_TRUE(
        member.service->ProcessReceivedMessage(request->ToBaseMessage(), 0));
    ASSERT_STREQ(member.service->GetSleepHold(), "relaying a join");

    member.service->UpdateRouteEntry(kJoiner, kJoiner, 1, 200, 2, 0);
    EXPECT_EQ(member.service->GetSleepHold(), nullptr);
}

TEST_F(NetworkServiceResumeTest, ManagerStaysAwakeUntilItAnswersAJoin) {
    Node& manager = MakeNode(kManager, NodeRole::NETWORK_MANAGER);
    ASSERT_TRUE(manager.superframe->StartSuperframe());
    ASSERT_TRUE(manager.service->CreateNetwork());
    EXPECT_EQ(manager.service->GetSleepHold(), nullptr);

    auto request =
        JoinRequestMessage::Create(kManager, 0x2009, 2, {}, kManager, 0);
    ASSERT_TRUE(request.has_value());
    ASSERT_TRUE(
        manager.service->ProcessReceivedMessage(request->ToBaseMessage(), 0));
    EXPECT_STREQ(manager.service->GetSleepHold(), "answering a join");

    // The response leaves in the discovery band; the join is applied at the
    // next superframe start
    ASSERT_NE(manager.queue->ExtractMessageOfType(
                  SlotAllocation::SlotType::DISCOVERY_TX),
              nullptr);
    EXPECT_STREQ(manager.service->GetSleepHold(), "answering a join");
    ASSERT_TRUE(manager.service->HandleSuperframeStart());
    EXPECT_EQ(manager.service->GetSleepHold(), nullptr);
}

TEST_F(NetworkServiceResumeTest, SleepsSinceTheLastBeaconAreSampledTogether) {
    Node& member = MakeSettledMember();
    const auto samples =
        member.service->GetSleepClockCalibration().GetSamples();

    // Two light sleeps in one superframe, then its beacon
    member.service->RecordSleepForCalibration(4000);
    member.service->RecordSleepForCalibration(6000);
    ASSERT_TRUE(member.service->HandleSuperframeStart());
    mock_->advanceTime(20000);
    ASSERT_TRUE(member.service->ProcessReceivedMessage(
        Beacon(), GetRTOS().getTickCount()));
    EXPECT_EQ(member.service->GetSleepClockCalibration().GetSamples(),
              samples + 1);

    // Nothing slept since: the next beacon adds no sample
    ASSERT_TRUE(member.service->HandleSuperframeStart());
    mock_->advanceTime(20000);
    ASSERT_TRUE(member.service->ProcessReceivedMessage(
        Beacon(), GetRTOS().getTickCount()));
    EXPECT_EQ(member.service->GetSleepClockCalibration().GetSamples(),
              samples + 1);
}

TEST_F(NetworkServiceResumeTest, SleepBeforeAMissedBeaconIsNotSampled) {
    Node& member = MakeSettledMember();
    const auto samples =
        member.service->GetSleepClockCalibration().GetSamples();

    // The beacon of the superframe is missed: the drift seen at the next one
    // includes a superframe awake and is not the sleep clock's error
    member.service->RecordSleepForCalibration(10000);
    ASSERT_TRUE(member.service->HandleSuperframeStart());
    mock_->advanceTime(20000);
    ASSERT_TRUE(member.service->HandleSuperframeStart());
    mock_->advanceTime(1000);
    ASSERT_TRUE(member.service->ProcessReceivedMessage(
        Beacon(), GetRTOS().getTickCount()));
    EXPECT_EQ(member.service->GetSleepClockCalibration().GetSamples(), samples);
}

TEST_F(NetworkServiceResumeTest, ReliableMessageInFlightBlocksDeepSleep) {
    Node& member = MakeSettledMember();
    ASSERT_EQ(member.service->GetDeepSleepBlocker(), nullptr);

    const auto id = member.service->SendReliable(kNeighbour, {1, 2, 3}, 2, 0);
    ASSERT_NE(id.source, 0u);
    EXPECT_STREQ(member.service->GetDeepSleepBlocker(),
                 "reliable messages in flight");
}

}  // namespace test
}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher

#endif  // ARDUINO
