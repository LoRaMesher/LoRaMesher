/**
 * @file topology_routing_test.cpp
 * @brief Multi-hop topology routing tests for LoRaMesh protocol
 *
 * Tests routing functionality across various network topologies:
 * line, full mesh, star, and ring.
 */

#include <gtest/gtest.h>

#include <algorithm>

#include "routing_test_fixture.hpp"

namespace loramesher {
namespace test {

/**
 * @brief Test suite for topology-based routing tests
 */
class TopologyRoutingTests : public RoutingTestFixture {};

/**
 * @brief Test four-node line topology
 *
 * Topology: N1 <-> N2 <-> N3 <-> N4
 *
 * Verifies:
 * - Multi-hop routing (3 hops from N1 to N4)
 * - Correct hop counts in routing tables
 * - Message delivery across 3 hops
 */
TEST_F(TopologyRoutingTests, FourNodeLineTopology) {
    // Create line topology with 4 nodes (first node is network manager)
    auto nodes = GenerateLineTopology(4, 0x1000, "Node", 0);

    ASSERT_EQ(nodes.size(), 4) << "Expected 4 nodes";

    // Start all nodes
    for (auto* node : nodes) {
        ASSERT_TRUE(StartNode(*node)) << "Failed to start " << node->name;
    }

    // Wait for network formation (3 normal nodes + 1 network manager)
    ASSERT_TRUE(WaitForNetworkFormation(nodes, 3))
        << "Network formation failed";

    // Wait longer for routing tables to propagate through the line
    ASSERT_TRUE(WaitForRoutingStabilization(nodes))
        << "Routing stabilization failed";

    // Distance-vector needs additional rounds to converge to shortest paths.
    // With min_sleep_fraction each superframe is longer, so allow 2 extra.
    auto superframe_ms_conv = GetSuperframeDuration(*nodes.front());
    uint32_t step_ms_conv = 15u;
    AdvanceTime(superframe_ms_conv * 2, superframe_ms_conv * 2, step_ms_conv, 0,
                [&]() { return false; });

    // Print routing tables for debugging
    for (auto* node : nodes) {
        PrintRoutingTable(*node);
    }

    // Verify N1's routing table
    EXPECT_EQ(GetHopCount(*nodes[0], nodes[1]->address), 1)
        << "N1->N2 should be 1 hop";
    EXPECT_EQ(GetHopCount(*nodes[0], nodes[2]->address), 2)
        << "N1->N3 should be 2 hops";
    EXPECT_EQ(GetHopCount(*nodes[0], nodes[3]->address), 3)
        << "N1->N4 should be 3 hops";

    // Verify N4's routing table
    EXPECT_EQ(GetHopCount(*nodes[3], nodes[2]->address), 1)
        << "N4->N3 should be 1 hop";
    EXPECT_EQ(GetHopCount(*nodes[3], nodes[1]->address), 2)
        << "N4->N2 should be 2 hops";
    EXPECT_EQ(GetHopCount(*nodes[3], nodes[0]->address), 3)
        << "N4->N1 should be 3 hops";

    // Verify next hop chain from N1 to N4
    EXPECT_EQ(GetNextHop(*nodes[0], nodes[3]->address), nodes[1]->address)
        << "N1 should route to N4 via N2";
    EXPECT_EQ(GetNextHop(*nodes[1], nodes[3]->address), nodes[2]->address)
        << "N2 should route to N4 via N3";
    EXPECT_EQ(GetNextHop(*nodes[2], nodes[3]->address), nodes[3]->address)
        << "N3 should route to N4 directly";

    // Verify next hop chain from N4 to N1 (reverse direction)
    EXPECT_EQ(GetNextHop(*nodes[3], nodes[0]->address), nodes[2]->address)
        << "N4 should route to N1 via N3";
    EXPECT_EQ(GetNextHop(*nodes[2], nodes[0]->address), nodes[1]->address)
        << "N3 should route to N1 via N2";
    EXPECT_EQ(GetNextHop(*nodes[1], nodes[0]->address), nodes[0]->address)
        << "N2 should route to N1 directly";

    // Send message from N1 to N4 (requires 3-hop routing)
    std::vector<uint8_t> payload = {0xAA, 0xBB, 0xCC, 0xDD};
    auto error_msg = SendMessage(*nodes[0], *nodes[3], payload);
    ASSERT_TRUE(error_msg) << "Failed to send message from N1 to N4: "
                           << error_msg.GetErrorMessage();

    auto superframe_duration = GetSuperframeDuration(*nodes[0]);
    uint32_t step_ms = 15u;
    // Wait for message to be routed
    bool received =
        AdvanceTime(5000, superframe_duration * 5, step_ms, 0, [&]() {
            return HasReceivedMessageFrom(*nodes[3], nodes[0]->address,
                                          MessageType::DATA);
        });

    EXPECT_TRUE(received) << "N4 did not receive message from N1";

    if (received) {
        auto messages = GetReceivedMessages(*nodes[3], nodes[0]->address,
                                            MessageType::DATA);
        ASSERT_EQ(messages.size(), 1);
        EXPECT_TRUE(std::ranges::equal(messages[0].GetPayload(), payload));
    }
}

/**
 * @brief Test full mesh topology with 4 nodes
 *
 * Topology: All nodes connected to all others
 *     N1
 *    /|\
 *   N2-+-N3
 *    \|/
 *     N4
 *
 * Verifies:
 * - All routes are direct (1-hop)
 * - No multi-hop routes in full mesh
 */
TEST_F(TopologyRoutingTests, FullMeshFourNodes) {
    // Create full mesh topology with 4 nodes (first node is network manager)
    auto nodes = GenerateFullMeshTopology(4, 0x1000, "Node", 0);

    ASSERT_EQ(nodes.size(), 4) << "Expected 4 nodes";

    // Start all nodes
    for (auto* node : nodes) {
        ASSERT_TRUE(StartNode(*node)) << "Failed to start " << node->name;
    }

    // Wait for network formation (3 normal nodes + 1 network manager)
    ASSERT_TRUE(WaitForNetworkFormation(nodes, 3))
        << "Network formation failed";

    // Wait for full mesh routing to converge (all routes should be 1 hop)
    // This ensures the distance-vector algorithm has fully converged
    ASSERT_TRUE(WaitForFullMeshConvergence(nodes))
        << "Full mesh routing did not converge";

    // Print routing tables for debugging
    for (auto* node : nodes) {
        PrintRoutingTable(*node);
    }

    // Verify all routes are 1-hop in full mesh
    for (auto* node : nodes) {
        for (auto* other : nodes) {
            if (node->address != other->address) {
                EXPECT_TRUE(HasRouteTo(*node, other->address))
                    << node->name << " should have route to " << other->name;
                EXPECT_EQ(GetHopCount(*node, other->address), 1)
                    << node->name << " to " << other->name
                    << " should be 1 hop (direct)";
                // In full mesh, next hop should be the destination itself
                EXPECT_EQ(GetNextHop(*node, other->address), other->address)
                    << node->name << " should route directly to "
                    << other->name;
            }
        }
    }

    // Send message between non-adjacent logical positions (still 1 hop)
    std::vector<uint8_t> payload = {0x11, 0x22, 0x33};
    ASSERT_TRUE(SendMessage(*nodes[0], *nodes[3], payload));

    auto superframe_duration = GetSuperframeDuration(*nodes[0]);
    uint32_t step_ms = 15u;
    // Wait for message to be routed
    bool received =
        AdvanceTime(5000, superframe_duration * 5, step_ms, 0, [&]() {
            return HasReceivedMessageFrom(*nodes[3], nodes[0]->address,
                                          MessageType::DATA);
        });

    EXPECT_TRUE(received) << "N4 did not receive message from N1";
}

/**
 * @brief Test star topology with 5 nodes
 *
 * Topology: N1 (center) connected to N2, N3, N4, N5
 *     N2  N3
 *      \ /
 *       N1 (center)
 *      / \
 *     N4  N5
 *
 * Verifies:
 * - Center node (N1) has 1-hop routes to all
 * - Peripheral nodes route through center (2 hops to each other)
 */
TEST_F(TopologyRoutingTests, StarTopologyFiveNodes) {
    // Create star topology with center at index 0 (also network manager)
    auto nodes = GenerateStarTopology(5, 0, 0x1000, "Node", 0);

    ASSERT_EQ(nodes.size(), 5) << "Expected 5 nodes";

    // Start all nodes
    for (auto* node : nodes) {
        ASSERT_TRUE(StartNode(*node)) << "Failed to start " << node->name;
    }

    // Wait for network formation (4 normal nodes + 1 network manager)
    ASSERT_TRUE(WaitForNetworkFormation(nodes, 4))
        << "Network formation failed";

    // Wait for star topology routing to converge
    // Center has 1 hop to all; peripherals have 2 hops to each other
    ASSERT_TRUE(WaitForStarConvergence(nodes, 0))
        << "Star topology routing did not converge";

    // Print routing tables for debugging
    for (auto* node : nodes) {
        PrintRoutingTable(*node);
    }

    // Center node (N1 at index 0) should have 1-hop to all peripherals
    for (size_t i = 1; i < nodes.size(); i++) {
        EXPECT_TRUE(HasRouteTo(*nodes[0], nodes[i]->address))
            << "Center should have route to peripheral " << i;
        EXPECT_EQ(GetHopCount(*nodes[0], nodes[i]->address), 1)
            << "Center to peripheral " << i << " should be 1 hop";
    }

    // Peripheral nodes should have 1-hop to center
    for (size_t i = 1; i < nodes.size(); i++) {
        EXPECT_TRUE(HasRouteTo(*nodes[i], nodes[0]->address))
            << "Peripheral " << i << " should have route to center";
        EXPECT_EQ(GetHopCount(*nodes[i], nodes[0]->address), 1)
            << "Peripheral " << i << " to center should be 1 hop";
    }

    // Peripheral nodes should have 2-hop routes to other peripherals
    for (size_t i = 1; i < nodes.size(); i++) {
        for (size_t j = 1; j < nodes.size(); j++) {
            if (i != j) {
                EXPECT_TRUE(HasRouteTo(*nodes[i], nodes[j]->address))
                    << "Peripheral " << i << " should have route to peripheral "
                    << j;
                EXPECT_EQ(GetHopCount(*nodes[i], nodes[j]->address), 2)
                    << "Peripheral " << i << " to peripheral " << j
                    << " should be 2 hops (via center)";
                // Next hop should be the center
                EXPECT_EQ(GetNextHop(*nodes[i], nodes[j]->address),
                          nodes[0]->address)
                    << "Peripheral " << i << " should route to peripheral " << j
                    << " via center";
            }
        }
    }

    // Send message from N2 to N5 (2 hops via center N1)
    std::vector<uint8_t> payload = {0xDE, 0xAD, 0xBE, 0xEF};
    ASSERT_TRUE(SendMessage(*nodes[1], *nodes[4], payload));

    auto superframe_duration = GetSuperframeDuration(*nodes[0]);
    uint32_t step_ms = 15u;
    // Wait for message to be routed
    bool received =
        AdvanceTime(5000, superframe_duration * 5, step_ms, 0, [&]() {
            return HasReceivedMessageFrom(*nodes[4], nodes[1]->address,
                                          MessageType::DATA);
        });

    EXPECT_TRUE(received) << "N5 did not receive message from N2";
}

/**
 * @brief Test ring topology with 5 nodes
 *
 * Topology: N1 <-> N2 <-> N3 <-> N4 <-> N5 <-> N1
 *
 *     N1 <-> N2
 *     ^       |
 *     |       v
 *    N5      N3
 *      \    /
 *       N4
 *
 * Verifies:
 * - Routing chooses shorter path around ring
 * - N1 to N3: 2 hops (via N2), not 3 hops (via N5, N4)
 * - N1 to N4: 2 hops (via N5), not 3 hops (via N2, N3)
 */
TEST_F(TopologyRoutingTests, RingTopologyFiveNodes) {
    // Create ring topology with 5 nodes (first node is network manager)
    auto nodes = GenerateRingTopology(5, 0x1000, "Node", 0);

    ASSERT_EQ(nodes.size(), 5) << "Expected 5 nodes";

    // Start all nodes
    for (auto* node : nodes) {
        ASSERT_TRUE(StartNode(*node)) << "Failed to start " << node->name;
    }

    // Wait for network formation (3 normal nodes + 1 network manager)
    ASSERT_TRUE(WaitForNetworkFormation(nodes, 3))
        << "Network formation failed";

    // Wait for routing tables to stabilize
    ASSERT_TRUE(WaitForRoutingStabilization(nodes))
        << "Routing stabilization failed";

    // Distance-vector needs additional rounds to converge to shorter paths.
    // With min_sleep_fraction each superframe is longer, so allow 2 extra.
    auto superframe_ms = GetSuperframeDuration(*nodes.front());
    uint32_t step_ms = 15u;
    AdvanceTime(superframe_ms * 2, superframe_ms * 2, step_ms, 0,
                [&]() { return false; });

    // Print routing tables for debugging
    for (auto* node : nodes) {
        PrintRoutingTable(*node);
    }

    // In a 5-node ring:
    // - Adjacent nodes: 1 hop
    // - 2 positions away: 2 hops
    // - Opposite side (2 or 3 positions): route chooses shorter path

    // N1 (index 0) neighbors are N2 (index 1) and N5 (index 4)
    EXPECT_EQ(GetHopCount(*nodes[0], nodes[1]->address), 1)
        << "N1->N2 should be 1 hop";
    EXPECT_EQ(GetHopCount(*nodes[0], nodes[4]->address), 1)
        << "N1->N5 should be 1 hop";

    // N1 to N3: via N2 (2 hops) is shorter than via N5,N4 (3 hops)
    EXPECT_LE(GetHopCount(*nodes[0], nodes[2]->address), 2)
        << "N1->N3 should be at most 2 hops";

    // N1 to N4: via N5 (2 hops) is shorter than via N2,N3 (3 hops)
    EXPECT_LE(GetHopCount(*nodes[0], nodes[3]->address), 2)
        << "N1->N4 should be at most 2 hops";

    // Send message from N1 to N3 (should use shorter path)
    std::vector<uint8_t> payload = {0xCA, 0xFE};
    ASSERT_TRUE(SendMessage(*nodes[0], *nodes[2], payload));

    auto superframe_duration = GetSuperframeDuration(*nodes[0]);
    // Wait for message to be routed
    bool received =
        AdvanceTime(5000, superframe_duration * 5, step_ms, 0, [&]() {
            return HasReceivedMessageFrom(*nodes[2], nodes[0]->address,
                                          MessageType::DATA);
        });

    EXPECT_TRUE(received) << "N3 did not receive message from N1";

    if (received) {
        auto messages = GetReceivedMessages(*nodes[2], nodes[0]->address,
                                            MessageType::DATA);
        ASSERT_EQ(messages.size(), 1);
        EXPECT_TRUE(std::ranges::equal(messages[0].GetPayload(), payload));
    }
}

}  // namespace test
}  // namespace loramesher
