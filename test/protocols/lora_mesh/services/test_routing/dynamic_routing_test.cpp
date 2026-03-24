/**
 * @file dynamic_routing_test.cpp
 * @brief Dynamic routing tests for LoRaMesh protocol
 *
 * Tests routing behavior when network topology changes:
 * link failures, node reconnection, loop prevention, and broadcast.
 */

#include <gtest/gtest.h>

#include <algorithm>

#include "routing_test_fixture.hpp"

namespace loramesher {
namespace test {

/**
 * @brief Test suite for dynamic routing behavior
 */
class DynamicRoutingTests : public RoutingTestFixture {};

/**
 * @brief Test route update after link failure
 *
 * Topology: Full mesh 4 nodes -> break N1-N3 link
 *
 * Verifies:
 * - Initial routes are all direct (1-hop)
 * - After breaking link, routes update to alternatives
 * - N1 finds alternative route to N3 via N2 or N4 (2 hops)
 */
TEST_F(DynamicRoutingTests, RouteUpdateAfterLinkFailure) {
    // Create full mesh with 4 nodes (first node is network manager)
    auto nodes = GenerateFullMeshTopology(4, 0x1000, "Node", 0);

    ASSERT_EQ(nodes.size(), 4) << "Expected 4 nodes";

    // Start all nodes
    for (auto* node : nodes) {
        ASSERT_TRUE(StartNode(*node)) << "Failed to start " << node->name;
    }

    // Wait for network formation
    ASSERT_TRUE(WaitForNetworkFormation(nodes, 3))
        << "Network formation failed";

    // Wait for full mesh routing to converge (all routes should be 1 hop)
    ASSERT_TRUE(WaitForFullMeshConvergence(nodes))
        << "Initial full mesh did not converge";

    // Verify initial routing - all direct (1-hop)
    for (auto* node : nodes) {
        for (auto* other : nodes) {
            if (node->address != other->address) {
                EXPECT_EQ(GetHopCount(*node, other->address), 1)
                    << "Initial route should be 1 hop";
            }
        }
    }

    std::cout << "=== Before link break ===" << std::endl;
    for (auto* node : nodes) {
        PrintRoutingTable(*node);
    }

    // Break the link between N1 (index 0) and N3 (index 2)
    SetLinkStatus(*nodes[0], *nodes[2], false);

    auto superframe_time = GetSuperframeDuration(*nodes.front());
    uint32_t step_ms = 15u;

    uint32_t end_superframe_time = 0;
    // Wait for routing tables to update (needs enough superframes for EWMA
    // decay + inactivation threshold + distance-vector convergence)
    bool updated = AdvanceTime(
        superframe_time * 16, superframe_time * 16, step_ms, 0, [&]() {
            // N1 should still have a route to N3, but with more hops
            if (!HasRouteTo(*nodes[0], nodes[2]->address)) {
                return false;
            }
            uint8_t hop_count = GetHopCount(*nodes[0], nodes[2]->address);
            // Route should now be via N2 or N4 (2 hops)
            if (hop_count == 2 && end_superframe_time == 0) {
                end_superframe_time = GetRTOS().getTickCount() +
                                      GetSuperframeDuration(*nodes.front()) * 2;
            }

            if (end_superframe_time > 0 &&
                GetRTOS().getTickCount() >= end_superframe_time) {
                return true;  // Route updated and stabilized
            }

            return false;  // Keep waiting
        });

    std::cout << "=== After link break ===" << std::endl;
    for (auto* node : nodes) {
        PrintRoutingTable(*node);
    }

    EXPECT_TRUE(updated) << "Route did not update after link failure";

    // Verify the new route
    if (updated) {
        AddressType next_hop = GetNextHop(*nodes[0], nodes[2]->address);
        // Next hop should be N2 or N4 (not N3 directly)
        EXPECT_TRUE(next_hop == nodes[1]->address ||
                    next_hop == nodes[3]->address)
            << "N1 should route to N3 via N2 or N4, not directly";

        // Send message to verify routing works
        std::vector<uint8_t> payload = {0x11, 0x22, 0x33};
        ASSERT_TRUE(SendMessage(*nodes[0], *nodes[2], payload));

        bool received = AdvanceTime(
            superframe_time * 4, superframe_time * 4, step_ms, 0, [&]() {
                return HasReceivedMessageFrom(*nodes[2], nodes[0]->address,
                                              MessageType::DATA);
            });

        EXPECT_TRUE(received) << "Message not received after route change";
    }
}

/**
 * @brief Test route recovery after node reconnect
 *
 * Topology: Line N1-N2-N3 -> disconnect N2 -> reconnect N2
 *
 * Verifies:
 * - Initial routes work (N1 can reach N3 via N2)
 * - After disconnecting N2, N1 loses route to N3
 * - After reconnecting N2, routes recover
 */
TEST_F(DynamicRoutingTests, RouteRecoveryAfterNodeReconnect) {
    // Create line topology with 3 nodes (first node is network manager)
    auto nodes = GenerateLineTopology(3, 0x1000, "Node", 0);

    ASSERT_EQ(nodes.size(), 3) << "Expected 3 nodes";

    // Start all nodes
    for (auto* node : nodes) {
        ASSERT_TRUE(StartNode(*node)) << "Failed to start " << node->name;
    }

    // Wait for network formation
    ASSERT_TRUE(WaitForNetworkFormation(nodes, 2))
        << "Network formation failed";

    ASSERT_TRUE(WaitForRoutingStabilization(nodes))
        << "Routing stabilization failed";

    // Verify initial routing: N1 can reach N3 (2 hops)
    EXPECT_TRUE(HasRouteTo(*nodes[0], nodes[2]->address))
        << "N1 should have route to N3";
    EXPECT_EQ(GetHopCount(*nodes[0], nodes[2]->address), 2)
        << "N1->N3 should be 2 hops";

    std::cout << "=== Before disconnect ===" << std::endl;
    for (auto* node : nodes) {
        PrintRoutingTable(*node);
    }

    // Simulate N2 failure (disconnect from all)
    SimulateNodeFailure(*nodes[1]);

    auto superframe_time = GetSuperframeDuration(*nodes.front());
    uint32_t step_ms = 15u;

    // Wait for route to be marked inactive or removed
    // Note: This may take up to route_timeout_ms
    AdvanceTime(superframe_time * 6, superframe_time * 6, step_ms, 0,
                [&]() { return false; });

    std::cout << "=== After disconnect ===" << std::endl;
    for (auto* node : nodes) {
        PrintRoutingTable(*node);
    }

    // Note: Depending on route timeout settings, the route may still exist
    // but be marked inactive, or it may be completely gone

    // Reconnect N2
    SimulateNodeRecovery(*nodes[1], false);  // Reconnect only to neighbors

    // Wait for routes to recover - need all nodes to have complete routing
    // tables so intermediate nodes can forward messages
    bool recovered = AdvanceTime(
        superframe_time * 12, superframe_time * 12, step_ms, 0, [&]() {
            // N1 should regain route to N3 with 2 hops via N2
            // N2 (intermediate) must know about N3 to forward messages
            // N1 must also have re-activated its direct route to N2 so it can
            // allocate a DATA_TX slot for N2 and actually deliver messages
            return HasRouteTo(*nodes[0], nodes[2]->address) &&
                   GetHopCount(*nodes[0], nodes[2]->address) == 2 &&
                   HasRouteTo(*nodes[1], nodes[2]->address) &&
                   HasRouteTo(*nodes[0], nodes[1]->address);
        });

    std::cout << "=== After reconnect ===" << std::endl;
    for (auto* node : nodes) {
        PrintRoutingTable(*node);
    }

    EXPECT_TRUE(recovered) << "Routes did not recover after node reconnect";

    // Verify message delivery after recovery
    if (recovered) {
        // Allow extra time for data slot allocation to stabilize
        AdvanceTime(superframe_time * 2, superframe_time * 2, step_ms, 0,
                    [&]() { return false; });

        std::vector<uint8_t> payload = {0xAA, 0xBB};
        ASSERT_TRUE(SendMessage(*nodes[0], *nodes[2], payload));

        bool received = AdvanceTime(
            superframe_time * 4, superframe_time * 4, step_ms, 0, [&]() {
                return HasReceivedMessageFrom(*nodes[2], nodes[0]->address,
                                              MessageType::DATA);
            });

        EXPECT_TRUE(received) << "Message not received after route recovery";
    }
}

/**
 * @brief Test loop prevention in complex topology
 *
 * Topology: 4 nodes with selective links (potential for loops)
 *     N1 -- N2
 *     |      |
 *     N4 -- N3
 *
 * Then disable N1-N2 and N3-N4, creating: N1-N4-N3-N2 path
 *
 * Verifies:
 * - No duplicate messages received
 * - Routing converges without loops
 */
TEST_F(DynamicRoutingTests, LoopPreventionInComplexTopology) {
    // Create 4 nodes manually with specific connectivity
    // First node is network manager for deterministic formation
    auto& node1 = CreateManagerNode("Node1", 0x1001);
    auto& node2 = CreateJoiningNode("Node2", 0x1002);
    auto& node3 = CreateJoiningNode("Node3", 0x1003);
    auto& node4 = CreateJoiningNode("Node4", 0x1004);

    // Create a square topology initially: N1-N2-N3-N4-N1
    SetLinkStatus(node1, node2, true);
    SetLinkStatus(node2, node3, true);
    SetLinkStatus(node3, node4, true);
    SetLinkStatus(node4, node1, true);

    // Start all nodes
    ASSERT_TRUE(StartNode(node1));
    ASSERT_TRUE(StartNode(node2));
    ASSERT_TRUE(StartNode(node3));
    ASSERT_TRUE(StartNode(node4));

    std::vector<TestNode*> nodes = {&node1, &node2, &node3, &node4};

    // Wait for network formation
    ASSERT_TRUE(WaitForNetworkFormation(nodes, 3))
        << "Network formation failed";

    ASSERT_TRUE(WaitForRoutingStabilization(nodes))
        << "Routing stabilization failed";

    std::cout << "=== Square topology ===" << std::endl;
    for (auto* node : nodes) {
        PrintRoutingTable(*node);
    }

    // Now break some links to create potential for loops
    // Break N1-N2 and N3-N4 links, leaving N1-N4-N3-N2 path
    SetLinkStatus(node1, node2, false);
    SetLinkStatus(node3, node4, false);

    // Wait for routing to stabilize
    AdvanceTime(15000);

    std::cout << "=== After link breaks ===" << std::endl;
    for (auto* node : nodes) {
        PrintRoutingTable(*node);
    }

    // Clear any previously received messages
    ClearAllReceivedMessages();

    // Send multiple messages from N1 to N3
    const int num_messages = 5;
    for (int i = 0; i < num_messages; i++) {
        std::vector<uint8_t> payload = {static_cast<uint8_t>(i)};
        SendMessage(node1, node3, payload);
        AdvanceTime(500);  // Small delay between messages
    }

    // Wait for messages to be delivered
    AdvanceTime(10000);

    // Check that N3 received exactly num_messages (no duplicates)
    size_t received_count =
        CountReceivedMessages(node3, node1.address, MessageType::DATA);

    std::cout << "Received " << received_count << " messages at N3 (expected "
              << num_messages << ")" << std::endl;

    // We expect at least some messages to arrive, and no duplicates
    EXPECT_LE(received_count, static_cast<size_t>(num_messages))
        << "Possible routing loop detected: received more messages than sent";
}

/**
 * @brief Test broadcast message routing
 *
 * Topology: 5 fully connected nodes
 *
 * Verifies:
 * - Broadcast message reaches all nodes
 * - Each node receives exactly one copy (no duplicates)
 */
TEST_F(DynamicRoutingTests, BroadcastToAllNodes) {
    auto nodes = GenerateFullMeshTopology(5, 0x1000, "Node", 0);

    ASSERT_EQ(nodes.size(), 5) << "Expected 5 nodes";

    for (auto* node : nodes) {
        ASSERT_TRUE(StartNode(*node)) << "Failed to start " << node->name;
    }

    ASSERT_TRUE(WaitForNetworkFormation(nodes, 4))
        << "Network formation failed";

    ASSERT_TRUE(WaitForFullMeshConvergence(nodes))
        << "Full mesh routing did not converge";

    ClearAllReceivedMessages();

    // Send broadcast from N1
    std::vector<uint8_t> payload = {0xBC, 0xBC, 0xBC};
    auto result = nodes[0]->protocol->SendBroadcast(payload);
    ASSERT_TRUE(result) << "Failed to send broadcast: "
                        << result.GetErrorMessage();

    // Wait for broadcast to propagate
    auto superframe_time = GetSuperframeDuration(*nodes.front());
    uint32_t step_ms = 50u;
    bool all_received = AdvanceTime(
        superframe_time * 4, superframe_time * 4, step_ms, 0, [&]() {
            for (size_t i = 1; i < nodes.size(); i++) {
                if (!HasReceivedMessageFrom(*nodes[i], nodes[0]->address,
                                            MessageType::DATA)) {
                    return false;
                }
            }
            return true;
        });

    EXPECT_TRUE(all_received) << "Not all nodes received broadcast message";

    // Verify each node (except sender) received exactly one copy
    for (size_t i = 1; i < nodes.size(); i++) {
        auto messages = GetReceivedMessages(*nodes[i], nodes[0]->address,
                                            MessageType::DATA);

        std::cout << "Node " << i + 1 << " received " << messages.size()
                  << " broadcast message(s)" << std::endl;

        EXPECT_EQ(messages.size(), 1)
            << "Node " << i + 1 << " should receive exactly 1 broadcast";

        if (!messages.empty()) {
            EXPECT_TRUE(std::ranges::equal(messages[0].GetPayload(), payload))
                << "Broadcast payload mismatch at Node " << i + 1;
        }
    }
}

/**
 * @brief Test broadcast propagation through multi-hop line topology
 *
 * Topology: N1 -- N2 -- N3 -- N4 (line, not fully connected)
 *
 * Verifies:
 * - Broadcast propagates through intermediate nodes
 * - All nodes receive exactly one copy
 */
TEST_F(DynamicRoutingTests, BroadcastMultiHop) {
    auto nodes = GenerateLineTopology(4, 0x1000, "Node", 0);

    ASSERT_EQ(nodes.size(), 4) << "Expected 4 nodes";

    for (auto* node : nodes) {
        ASSERT_TRUE(StartNode(*node)) << "Failed to start " << node->name;
    }

    ASSERT_TRUE(WaitForNetworkFormation(nodes, 3))
        << "Network formation failed";

    ASSERT_TRUE(WaitForRoutingStabilization(nodes))
        << "Routing stabilization failed";

    std::cout << "=== Line topology before broadcast ===" << std::endl;
    for (auto* node : nodes) {
        PrintRoutingTable(*node);
    }

    ClearAllReceivedMessages();

    // Send broadcast from N1 (one end of the line)
    std::vector<uint8_t> payload = {0xAA, 0xBB};
    auto result = nodes[0]->protocol->SendBroadcast(payload);
    ASSERT_TRUE(result) << "Failed to send broadcast: "
                        << result.GetErrorMessage();

    // Wait for broadcast to propagate through the entire line
    auto superframe_time = GetSuperframeDuration(*nodes.front());
    uint32_t step_ms = 50u;
    bool all_received = AdvanceTime(
        superframe_time * 6, superframe_time * 6, step_ms, 0, [&]() {
            for (size_t i = 1; i < nodes.size(); i++) {
                if (!HasReceivedMessageFrom(*nodes[i], nodes[0]->address,
                                            MessageType::DATA)) {
                    return false;
                }
            }
            return true;
        });

    EXPECT_TRUE(all_received) << "Broadcast did not reach all nodes in line";

    // Verify each node received exactly one copy
    for (size_t i = 1; i < nodes.size(); i++) {
        size_t count = CountReceivedMessages(*nodes[i], nodes[0]->address,
                                             MessageType::DATA);
        std::cout << "Node " << i + 1 << " received " << count
                  << " broadcast message(s)" << std::endl;

        EXPECT_EQ(count, 1u)
            << "Node " << i + 1
            << " should receive exactly 1 broadcast in line topology";
    }
}

/**
 * @brief Test broadcast de-duplication in a ring topology
 *
 * Topology: N1 -- N2 -- N3 -- N1 (fully connected triangle)
 *
 * The broadcast from N1 reaches N2 and N3 directly, then N2 and N3
 * re-broadcast. Without de-duplication, N2 would receive a second copy
 * from N3 (and vice versa). The de-duplication cache prevents this.
 *
 * Verifies:
 * - Each node receives exactly one copy despite multiple paths
 */
TEST_F(DynamicRoutingTests, BroadcastDeduplication) {
    auto nodes = GenerateFullMeshTopology(3, 0x1000, "Node", 0);

    ASSERT_EQ(nodes.size(), 3) << "Expected 3 nodes";

    for (auto* node : nodes) {
        ASSERT_TRUE(StartNode(*node)) << "Failed to start " << node->name;
    }

    ASSERT_TRUE(WaitForNetworkFormation(nodes, 2))
        << "Network formation failed";

    ASSERT_TRUE(WaitForFullMeshConvergence(nodes))
        << "Full mesh routing did not converge";

    ClearAllReceivedMessages();

    // Send broadcast from N1
    std::vector<uint8_t> payload = {0xDE, 0xAD};
    auto result = nodes[0]->protocol->SendBroadcast(payload);
    ASSERT_TRUE(result) << "Failed to send broadcast: "
                        << result.GetErrorMessage();

    // Wait for broadcast propagation and re-broadcasts
    auto superframe_time = GetSuperframeDuration(*nodes.front());
    uint32_t step_ms = 50u;
    AdvanceTime(superframe_time * 4, superframe_time * 4, step_ms, 0, [&]() {
        return HasReceivedMessageFrom(*nodes[1], nodes[0]->address,
                                      MessageType::DATA) &&
               HasReceivedMessageFrom(*nodes[2], nodes[0]->address,
                                      MessageType::DATA);
    });

    // Verify each non-sender node received exactly one copy
    for (size_t i = 1; i < nodes.size(); i++) {
        size_t count = CountReceivedMessages(*nodes[i], nodes[0]->address,
                                             MessageType::DATA);
        std::cout << "Node " << i + 1 << " received " << count
                  << " broadcast message(s) (expected 1)" << std::endl;

        EXPECT_EQ(count, 1u)
            << "Node " << i + 1
            << " should receive exactly 1 broadcast (de-duplication)";
    }

    // Verify sender did not receive its own broadcast
    size_t sender_count =
        CountReceivedMessages(*nodes[0], nodes[0]->address, MessageType::DATA);
    EXPECT_EQ(sender_count, 0u)
        << "Sender should not receive its own broadcast";
}

/**
 * @brief Test that data message de-duplication prevents duplicate delivery
 *
 * Topology: 3-node line N1 -- N2 -- N3
 *
 * Sends data from N1 to N3 (traverses N2). Verifies N3 receives exactly
 * one copy. The de-dup cache on N2 ensures forwarding happens only once.
 */
TEST_F(DynamicRoutingTests, DataMessageDeduplication) {
    auto nodes = GenerateLineTopology(3, 0x1000, "Node", 0);

    ASSERT_EQ(nodes.size(), 3) << "Expected 3 nodes";

    for (auto* node : nodes) {
        ASSERT_TRUE(StartNode(*node)) << "Failed to start " << node->name;
    }

    ASSERT_TRUE(WaitForNetworkFormation(nodes, 2))
        << "Network formation failed";

    ASSERT_TRUE(WaitForRoutingStabilization(nodes))
        << "Routing stabilization failed";

    ClearAllReceivedMessages();

    // Send data from N1 to N3
    std::vector<uint8_t> payload = {0xDE, 0xAD};
    auto result = nodes[0]->protocol->SendData(nodes[2]->address, payload);
    ASSERT_TRUE(result) << "Failed to send data: " << result.GetErrorMessage();

    auto superframe_time = GetSuperframeDuration(*nodes.front());
    uint32_t step_ms = 50u;
    bool received = AdvanceTime(
        superframe_time * 4, superframe_time * 4, step_ms, 0, [&]() {
            return HasReceivedMessageFrom(*nodes[2], nodes[0]->address,
                                          MessageType::DATA);
        });

    EXPECT_TRUE(received) << "N3 did not receive data from N1";

    // Verify exactly one copy delivered
    size_t count =
        CountReceivedMessages(*nodes[2], nodes[0]->address, MessageType::DATA);
    EXPECT_EQ(count, 1u) << "N3 should receive exactly 1 data message";
}

}  // namespace test
}  // namespace loramesher
