/**
 * @file spreading_factor_test.cpp
 * @brief Tests mesh networking with non-default spreading factors
 *
 * Verifies that network formation, routing, and message delivery
 * work correctly when using spreading factors other than the default SF7.
 */

#include <gtest/gtest.h>

#include <algorithm>

#include "routing_test_fixture.hpp"

namespace loramesher {
namespace test {

/**
 * @brief Test suite for non-default spreading factor configurations
 */
class SpreadingFactorTests : public RoutingTestFixture {};

/**
 * @brief Test three-node line topology with SF9
 *
 * Topology: N1(NM) <-> N2 <-> N3
 *
 * Verifies:
 * - Network formation succeeds with SF9 (longer time-on-air)
 * - Routing tables have correct hop counts
 * - End-to-end message delivery works across 2 hops
 */
TEST_F(SpreadingFactorTests, ThreeNodeLineSF9) {
    RadioConfig sf9_config;
    sf9_config.setSpreadingFactor(9);

    auto& node1 = CreateNode("Node1", 0x1000, NodeRole::NETWORK_MANAGER,
                             PinConfig(), sf9_config);
    auto& node2 = CreateNode("Node2", 0x1001, NodeRole::NODE_ONLY, PinConfig(),
                             sf9_config);
    auto& node3 = CreateNode("Node3", 0x1002, NodeRole::NODE_ONLY, PinConfig(),
                             sf9_config);

    // Line topology: N1 <-> N2 <-> N3
    SetLinkStatus(node1, node2, true);
    SetLinkStatus(node2, node3, true);

    std::vector<TestNode*> nodes = {&node1, &node2, &node3};
    for (auto* node : nodes) {
        ASSERT_TRUE(StartNode(*node)) << "Failed to start " << node->name;
    }

    // Wait for network formation (2 normal nodes + 1 NM)
    ASSERT_TRUE(WaitForNetworkFormation(nodes, 2))
        << "Network formation failed with SF9";

    // Wait for routing tables to stabilize
    ASSERT_TRUE(WaitForRoutingStabilization(nodes))
        << "Routing stabilization failed with SF9";

    // Allow distance-vector convergence
    auto superframe_ms = GetSuperframeDuration(*nodes.front());
    AdvanceTime(superframe_ms * 2, superframe_ms * 2, 15u, 0,
                [&]() { return false; });

    // Print routing tables for debugging
    for (auto* node : nodes) {
        PrintRoutingTable(*node);
    }

    // Verify hop counts
    EXPECT_EQ(GetHopCount(node1, node2.address), 1) << "N1->N2 should be 1 hop";
    EXPECT_EQ(GetHopCount(node1, node3.address), 2)
        << "N1->N3 should be 2 hops";
    EXPECT_EQ(GetHopCount(node3, node2.address), 1) << "N3->N2 should be 1 hop";
    EXPECT_EQ(GetHopCount(node3, node1.address), 2)
        << "N3->N1 should be 2 hops";

    // Verify next hop from N1 to N3 goes through N2
    EXPECT_EQ(GetNextHop(node1, node3.address), node2.address)
        << "N1 should route to N3 via N2";
    EXPECT_EQ(GetNextHop(node3, node1.address), node2.address)
        << "N3 should route to N1 via N2";

    // Send message from N1 to N3 (2 hops)
    std::vector<uint8_t> payload = {0xDE, 0xAD, 0xBE, 0xEF};
    ASSERT_TRUE(SendMessage(node1, node3, payload))
        << "Failed to send message from N1 to N3";

    auto superframe_duration = GetSuperframeDuration(*nodes[0]);
    uint32_t step_ms = 15u;

    bool received =
        AdvanceTime(5000, superframe_duration * 4, step_ms, 0, [&]() {
            return HasReceivedMessageFrom(node3, node1.address,
                                          MessageType::DATA);
        });

    EXPECT_TRUE(received) << "N3 did not receive message from N1 with SF9";

    if (received) {
        auto messages =
            GetReceivedMessages(node3, node1.address, MessageType::DATA);
        ASSERT_EQ(messages.size(), 1) << "Expected exactly 1 message";
        EXPECT_TRUE(std::ranges::equal(messages[0].GetPayload(), payload))
            << "Message payload mismatch";
    }
}

}  // namespace test
}  // namespace loramesher
