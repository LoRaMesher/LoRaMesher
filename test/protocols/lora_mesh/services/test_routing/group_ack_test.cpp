/**
 * @file group_ack_test.cpp
 * @brief Integration tests for reliable (acknowledged) delivery and groups.
 */

#include <gtest/gtest.h>

#include <set>

#include "routing_test_fixture.hpp"

namespace loramesher {
namespace test {

class GroupAckTests : public RoutingTestFixture {
   protected:
    using DeliveryResult = protocols::reliability::DeliveryResult;
    using Outcome = protocols::reliability::Outcome;

    protocols::lora_mesh::NetworkService* Net(TestNode& node) {
        return node.protocol->GetNetworkServiceForTest();
    }

    size_t CountOutcomes(const TestNode& node, Outcome outcome) {
        size_t count = 0;
        for (const auto& r : node.delivery_outcomes) {
            if (r.outcome == outcome) {
                count++;
            }
        }
        return count;
    }
};

// Scenario 3 — reliable unicast success fires Delivered with RTT.
TEST_F(GroupAckTests, ReliableUnicastSucceedsWithRttCallback) {
    auto nodes = GenerateLineTopology(2, 0x1000, "Node", 0);
    for (auto* node : nodes) {
        ASSERT_TRUE(StartNode(*node)) << "Failed to start " << node->name;
    }
    ASSERT_TRUE(WaitForNetworkFormation(nodes, 1))
        << "Network formation failed";
    ASSERT_TRUE(WaitForRoutingStabilization(nodes)) << "Routing unstable";

    ClearAllReceivedMessages();
    nodes[0]->delivery_outcomes.clear();

    std::vector<uint8_t> payload = {0xDE, 0xAD, 0xBE, 0xEF};
    auto id = Net(*nodes[0])->SendReliable(nodes[1]->address, payload,
                                           /*max_retries=*/3);
    ASSERT_EQ(id.source, nodes[0]->address);

    auto superframe = GetSuperframeDuration(*nodes.front());
    bool done = AdvanceTime(superframe * 6, superframe * 6, 50u, 0, [&]() {
        return !nodes[0]->delivery_outcomes.empty() &&
               HasReceivedMessageFrom(*nodes[1], nodes[0]->address,
                                      MessageType::DATA);
    });
    ASSERT_TRUE(done) << "Reliable delivery did not complete";

    EXPECT_EQ(
        CountReceivedMessages(*nodes[1], nodes[0]->address, MessageType::DATA),
        1u);
    ASSERT_EQ(nodes[0]->delivery_outcomes.size(), 1u);
    const DeliveryResult& r = nodes[0]->delivery_outcomes.front();
    EXPECT_EQ(r.outcome, Outcome::Delivered);
    EXPECT_EQ(r.by, nodes[1]->address);
    EXPECT_GT(r.rtt_ms, 0u);
    EXPECT_EQ(Net(*nodes[0])->GetReliablePendingCount(), 0u);
}

// Scenario 4 — reliable unicast retransmits under loss, then succeeds.
TEST_F(GroupAckTests, ReliableUnicastRetransmitsThenSucceeds) {
    auto nodes = GenerateLineTopology(2, 0x1000, "Node", 0);
    for (auto* node : nodes) {
        ASSERT_TRUE(StartNode(*node)) << "Failed to start " << node->name;
    }
    ASSERT_TRUE(WaitForNetworkFormation(nodes, 1))
        << "Network formation failed";
    ASSERT_TRUE(WaitForRoutingStabilization(nodes)) << "Routing unstable";

    ClearAllReceivedMessages();
    nodes[0]->delivery_outcomes.clear();

    auto superframe = GetSuperframeDuration(*nodes.front());

    // Drop the first data attempts toward the receiver, forcing retransmission.
    SetDirectionalLinkLoss(*nodes[0], *nodes[1], 1.0f);

    std::vector<uint8_t> payload = {0x11, 0x22};
    auto id = Net(*nodes[0])->SendReliable(nodes[1]->address, payload,
                                           /*max_retries=*/5,
                                           /*timeout_override_ms=*/superframe);
    ASSERT_EQ(id.source, nodes[0]->address);

    // Let at least one attempt be lost, then restore the link.
    AdvanceTime(superframe * 2);
    EXPECT_FALSE(HasReceivedMessageFrom(*nodes[1], nodes[0]->address,
                                        MessageType::DATA));
    SetDirectionalLinkLoss(*nodes[0], *nodes[1], 0.0f);

    bool done = AdvanceTime(superframe * 8, superframe * 8, 50u, 0, [&]() {
        return !nodes[0]->delivery_outcomes.empty();
    });
    ASSERT_TRUE(done) << "Reliable delivery never completed after retransmit";

    EXPECT_EQ(CountOutcomes(*nodes[0], Outcome::Delivered), 1u);
    EXPECT_EQ(CountOutcomes(*nodes[0], Outcome::Failed), 0u);
    EXPECT_EQ(Net(*nodes[0])->GetReliablePendingCount(), 0u);
}

// Scenario 5 — reliable unicast fails after max retries, leaving no leak.
TEST_F(GroupAckTests, ReliableUnicastFailsAfterMaxRetries) {
    auto nodes = GenerateLineTopology(2, 0x1000, "Node", 0);
    for (auto* node : nodes) {
        ASSERT_TRUE(StartNode(*node)) << "Failed to start " << node->name;
    }
    ASSERT_TRUE(WaitForNetworkFormation(nodes, 1))
        << "Network formation failed";
    ASSERT_TRUE(WaitForRoutingStabilization(nodes)) << "Routing unstable";

    ClearAllReceivedMessages();
    nodes[0]->delivery_outcomes.clear();

    auto superframe = GetSuperframeDuration(*nodes.front());

    // Sever the link so no acknowledgement can return.
    SetLinkStatus(*nodes[0], *nodes[1], false);

    std::vector<uint8_t> payload = {0x99};
    auto id = Net(*nodes[0])->SendReliable(nodes[1]->address, payload,
                                           /*max_retries=*/2,
                                           /*timeout_override_ms=*/superframe);
    ASSERT_EQ(id.source, nodes[0]->address);

    bool failed = AdvanceTime(superframe * 10, superframe * 10, 50u, 0, [&]() {
        return CountOutcomes(*nodes[0], Outcome::Failed) > 0;
    });
    ASSERT_TRUE(failed) << "Reliable delivery did not fail after max retries";

    EXPECT_EQ(CountOutcomes(*nodes[0], Outcome::Delivered), 0u);
    EXPECT_EQ(Net(*nodes[0])->GetReliablePendingCount(), 0u);
}

// Scenario 1 — group delivery to members only; non-members relay but do not
// deliver. Line N0(NM)-N1-N2-N3; members N1 & N3, non-member N2, sender N0.
// N3 (member) receiving proves the non-member N2 relayed the flood.
TEST_F(GroupAckTests, GroupDeliversToMembersOnly) {
    constexpr AddressType kGroup = 0x8001;
    auto nodes = GenerateLineTopology(4, 0x1000, "Node", 0);
    for (auto* node : nodes) {
        ASSERT_TRUE(StartNode(*node)) << "Failed to start " << node->name;
    }
    ASSERT_TRUE(WaitForNetworkFormation(nodes, 3))
        << "Network formation failed";
    ASSERT_TRUE(WaitForRoutingStabilization(nodes)) << "Routing unstable";

    ASSERT_TRUE(Net(*nodes[1])->JoinGroup(kGroup));
    ASSERT_TRUE(Net(*nodes[3])->JoinGroup(kGroup));

    ClearAllReceivedMessages();

    std::vector<uint8_t> payload = {0x47, 0x52, 0x50};
    ASSERT_TRUE(Net(*nodes[0])->SendGroup(
        kGroup, std::span<const uint8_t>(payload.data(), payload.size())));

    auto superframe = GetSuperframeDuration(*nodes.front());
    bool done = AdvanceTime(superframe * 8, superframe * 8, 50u, 0, [&]() {
        return HasReceivedMessageFrom(*nodes[1], nodes[0]->address,
                                      MessageType::DATA) &&
               HasReceivedMessageFrom(*nodes[3], nodes[0]->address,
                                      MessageType::DATA);
    });
    ASSERT_TRUE(done) << "Group members did not all receive the message";

    EXPECT_EQ(
        CountReceivedMessages(*nodes[1], nodes[0]->address, MessageType::DATA),
        1u);
    EXPECT_EQ(
        CountReceivedMessages(*nodes[3], nodes[0]->address, MessageType::DATA),
        1u);
    // Non-member relayed (N3 reachable only via N2) but never delivered.
    EXPECT_EQ(
        CountReceivedMessages(*nodes[2], nodes[0]->address, MessageType::DATA),
        0u);
}

// Scenario 2 — group ids do not leak: a member of one group does not receive a
// send to a different group.
TEST_F(GroupAckTests, GroupDoesNotLeakAcrossIds) {
    auto nodes = GenerateLineTopology(3, 0x1000, "Node", 0);
    for (auto* node : nodes) {
        ASSERT_TRUE(StartNode(*node)) << "Failed to start " << node->name;
    }
    ASSERT_TRUE(WaitForNetworkFormation(nodes, 2))
        << "Network formation failed";
    ASSERT_TRUE(WaitForRoutingStabilization(nodes)) << "Routing unstable";

    ASSERT_TRUE(Net(*nodes[1])->JoinGroup(0x8001));
    ASSERT_TRUE(Net(*nodes[2])->JoinGroup(0x8001));

    ClearAllReceivedMessages();

    std::vector<uint8_t> payload = {0xAB};
    ASSERT_TRUE(Net(*nodes[0])->SendGroup(
        0x8002, std::span<const uint8_t>(payload.data(), payload.size())));

    auto superframe = GetSuperframeDuration(*nodes.front());
    // Negative assertion: advance enough time (coarse steps) to confirm the
    // group send never reaches members of a different group.
    AdvanceTime(superframe * 2, superframe * 2, 500u, 0, nullptr);

    EXPECT_EQ(
        CountReceivedMessages(*nodes[1], nodes[0]->address, MessageType::DATA),
        0u);
    EXPECT_EQ(
        CountReceivedMessages(*nodes[2], nodes[0]->address, MessageType::DATA),
        0u);
}

// Scenario 8 — no duplicate delivery under flooding with multiple paths.
TEST_F(GroupAckTests, GroupNoDuplicateDeliveryUnderFlood) {
    constexpr AddressType kGroup = 0x8001;
    auto nodes = GenerateFullMeshTopology(4, 0x1000, "Node", 0);
    for (auto* node : nodes) {
        ASSERT_TRUE(StartNode(*node)) << "Failed to start " << node->name;
    }
    ASSERT_TRUE(WaitForNetworkFormation(nodes, 3))
        << "Network formation failed";
    ASSERT_TRUE(WaitForRoutingStabilization(nodes)) << "Routing unstable";

    ASSERT_TRUE(Net(*nodes[1])->JoinGroup(kGroup));

    ClearAllReceivedMessages();

    std::vector<uint8_t> payload = {0x01, 0x02, 0x03};
    ASSERT_TRUE(Net(*nodes[0])->SendGroup(
        kGroup, std::span<const uint8_t>(payload.data(), payload.size())));

    auto superframe = GetSuperframeDuration(*nodes.front());
    bool done = AdvanceTime(superframe * 8, superframe * 8, 50u, 0, [&]() {
        return HasReceivedMessageFrom(*nodes[1], nodes[0]->address,
                                      MessageType::DATA);
    });
    ASSERT_TRUE(done) << "Group member did not receive the message";

    EXPECT_EQ(
        CountReceivedMessages(*nodes[1], nodes[0]->address, MessageType::DATA),
        1u)
        << "Member must receive exactly one copy under flood";
}

// Scenario 6 — reliable group send reports each responder, then the window
// closes with the total acknowledgement count.
TEST_F(GroupAckTests, ReliableGroupReportsEachResponder) {
    constexpr AddressType kGroup = 0x8001;
    // Star: center N0 (NM) with three leaf members.
    auto nodes = GenerateStarTopology(4, /*central=*/0, 0x1000, "Node",
                                      /*manager_index=*/0);
    for (auto* node : nodes) {
        ASSERT_TRUE(StartNode(*node)) << "Failed to start " << node->name;
    }
    ASSERT_TRUE(WaitForNetworkFormation(nodes, 3))
        << "Network formation failed";
    ASSERT_TRUE(WaitForRoutingStabilization(nodes)) << "Routing unstable";

    ASSERT_TRUE(Net(*nodes[1])->JoinGroup(kGroup));
    ASSERT_TRUE(Net(*nodes[2])->JoinGroup(kGroup));
    ASSERT_TRUE(Net(*nodes[3])->JoinGroup(kGroup));

    ClearAllReceivedMessages();
    nodes[0]->delivery_outcomes.clear();

    auto superframe = GetSuperframeDuration(*nodes.front());
    std::vector<uint8_t> payload = {0x55};
    auto id = Net(*nodes[0])->SendGroupReliable(
        kGroup, std::span<const uint8_t>(payload.data(), payload.size()),
        /*max_retries=*/0, /*window_ms=*/superframe * 6);
    ASSERT_EQ(id.source, nodes[0]->address);

    bool closed = AdvanceTime(superframe * 12, superframe * 12, 50u, 0, [&]() {
        return CountOutcomes(*nodes[0], Outcome::GroupWindowClosed) > 0;
    });
    ASSERT_TRUE(closed) << "Group acknowledgement window did not close";

    // Each member acknowledged exactly once (distinct responders).
    std::set<AddressType> responders;
    uint8_t closed_count = 0;
    for (const auto& r : nodes[0]->delivery_outcomes) {
        if (r.outcome == Outcome::Delivered) {
            responders.insert(r.by);
        } else if (r.outcome == Outcome::GroupWindowClosed) {
            closed_count = r.ack_count;
        }
    }
    EXPECT_EQ(responders.size(), 3u);
    EXPECT_TRUE(responders.count(nodes[1]->address));
    EXPECT_TRUE(responders.count(nodes[2]->address));
    EXPECT_TRUE(responders.count(nodes[3]->address));
    EXPECT_EQ(closed_count, 3u);
    EXPECT_EQ(Net(*nodes[0])->GetReliablePendingCount(), 0u);
}

// Scenario 7 — message ids are stable and observable at the receiver, and
// sequence numbers increase per source.
TEST_F(GroupAckTests, MessageIdsAreStableAndObservable) {
    auto nodes = GenerateLineTopology(2, 0x1000, "Node", 0);
    for (auto* node : nodes) {
        ASSERT_TRUE(StartNode(*node)) << "Failed to start " << node->name;
    }
    ASSERT_TRUE(WaitForNetworkFormation(nodes, 1))
        << "Network formation failed";
    ASSERT_TRUE(WaitForRoutingStabilization(nodes)) << "Routing unstable";

    std::vector<protocols::reliability::MessageId> received_ids;
    Net(*nodes[1])->SetDataReceivedExCallback(
        [&received_ids](AddressType, protocols::reliability::MessageId id,
                        uint8_t hops, const std::vector<uint8_t>&) {
            EXPECT_GE(hops, 1u);
            received_ids.push_back(id);
        });

    ClearAllReceivedMessages();
    auto superframe = GetSuperframeDuration(*nodes.front());

    std::vector<uint8_t> payload = {0x01};
    auto id1 = Net(*nodes[0])->SendReliable(nodes[1]->address, payload, 3);
    ASSERT_EQ(id1.source, nodes[0]->address);
    ASSERT_TRUE(AdvanceTime(superframe * 6, superframe * 6, 50u, 0,
                            [&]() { return !received_ids.empty(); }));

    auto id2 = Net(*nodes[0])->SendReliable(nodes[1]->address, payload, 3);
    ASSERT_TRUE(AdvanceTime(superframe * 6, superframe * 6, 50u, 0,
                            [&]() { return received_ids.size() >= 2; }));

    ASSERT_GE(received_ids.size(), 2u);
    // The id observed at the receiver matches the id returned to the sender.
    EXPECT_EQ(received_ids[0].source, id1.source);
    EXPECT_EQ(received_ids[0].seq, id1.seq);
    EXPECT_EQ(received_ids[1].seq, id2.seq);
    // Sequence numbers increase per source.
    EXPECT_NE(id1.seq, id2.seq);
}

}  // namespace test
}  // namespace loramesher
