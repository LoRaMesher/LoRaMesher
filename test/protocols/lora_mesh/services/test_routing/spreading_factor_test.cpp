/**
 * @file spreading_factor_test.cpp
 * @brief Tests mesh networking with non-default spreading factors
 *
 * Verifies that network formation, routing, and message delivery
 * work correctly across the full SF7-SF12 range, exercising the
 * SF-derived max_packet_size default path in LoRaMeshProtocol::Configure.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "routing_test_fixture.hpp"

namespace loramesher {
namespace test {

/**
 * @brief Parameterized test suite for spreading factor configurations
 *
 * Each parameter is a spreading factor in the valid LoRa range (7-12).
 * BW is fixed at the default 125 kHz.
 */
class SpreadingFactorTests : public RoutingTestFixture,
                             public ::testing::WithParamInterface<uint8_t> {};

/**
 * @brief Three-node line topology across SF7-SF12
 *
 * Topology: N1(NM) <-> N2 <-> N3
 *
 * Verifies for each SF:
 * - Network formation succeeds (slot_duration and max_packet_size both
 *   scale correctly with SF via ApplySfDerivedDefaults)
 * - Routing tables have correct hop counts
 * - End-to-end message delivery works across 2 hops
 */
TEST_P(SpreadingFactorTests, ThreeNodeLine) {
    const uint8_t sf = GetParam();

    RadioConfig sf_config;
    sf_config.setSpreadingFactor(sf);

    auto& node1 = CreateNode("Node1", 0x1000, NodeRole::NETWORK_MANAGER,
                             PinConfig(), sf_config);
    auto& node2 = CreateNode("Node2", 0x1001, NodeRole::NODE_ONLY, PinConfig(),
                             sf_config);
    auto& node3 = CreateNode("Node3", 0x1002, NodeRole::NODE_ONLY, PinConfig(),
                             sf_config);

    // Line topology: N1 <-> N2 <-> N3
    SetLinkStatus(node1, node2, true);
    SetLinkStatus(node2, node3, true);

    std::vector<TestNode*> nodes = {&node1, &node2, &node3};
    for (auto* node : nodes) {
        ASSERT_TRUE(StartNode(*node)) << "Failed to start " << node->name
                                      << " at SF" << static_cast<int>(sf);
    }

    ASSERT_TRUE(WaitForNetworkFormation(nodes, 2))
        << "Network formation failed at SF" << static_cast<int>(sf);

    ASSERT_TRUE(WaitForRoutingStabilization(nodes))
        << "Routing stabilization failed at SF" << static_cast<int>(sf);

    // Allow distance-vector convergence
    auto superframe_ms = GetSuperframeDuration(*nodes.front());
    AdvanceTime(superframe_ms * 2, superframe_ms * 2, 15u, 0,
                [&]() { return false; });

    for (auto* node : nodes) {
        PrintRoutingTable(*node);
    }

    EXPECT_EQ(GetHopCount(node1, node2.address), 1)
        << "N1->N2 should be 1 hop at SF" << static_cast<int>(sf);
    EXPECT_EQ(GetHopCount(node1, node3.address), 2)
        << "N1->N3 should be 2 hops at SF" << static_cast<int>(sf);
    EXPECT_EQ(GetHopCount(node3, node2.address), 1)
        << "N3->N2 should be 1 hop at SF" << static_cast<int>(sf);
    EXPECT_EQ(GetHopCount(node3, node1.address), 2)
        << "N3->N1 should be 2 hops at SF" << static_cast<int>(sf);

    EXPECT_EQ(GetNextHop(node1, node3.address), node2.address)
        << "N1 should route to N3 via N2 at SF" << static_cast<int>(sf);
    EXPECT_EQ(GetNextHop(node3, node1.address), node2.address)
        << "N3 should route to N1 via N2 at SF" << static_cast<int>(sf);

    // Send message from N1 to N3 (2 hops)
    std::vector<uint8_t> payload = {0xDE, 0xAD, 0xBE, 0xEF};
    ASSERT_TRUE(SendMessage(node1, node3, payload))
        << "Failed to send message from N1 to N3 at SF" << static_cast<int>(sf);

    auto superframe_duration = GetSuperframeDuration(*nodes[0]);
    uint32_t step_ms = 15u;

    // Scale wall-clock budget with superframe so high-SF cases don't
    // false-fail while the virtual budget is still generous.
    bool received = AdvanceTime(
        superframe_duration * 4, superframe_duration * 4, step_ms, 0, [&]() {
            return HasReceivedMessageFrom(node3, node1.address,
                                          MessageType::DATA);
        });

    EXPECT_TRUE(received) << "N3 did not receive message from N1 at SF"
                          << static_cast<int>(sf);

    if (received) {
        auto messages =
            GetReceivedMessages(node3, node1.address, MessageType::DATA);
        ASSERT_EQ(messages.size(), 1)
            << "Expected exactly 1 message at SF" << static_cast<int>(sf);
        EXPECT_TRUE(std::ranges::equal(messages[0].GetPayload(), payload))
            << "Message payload mismatch at SF" << static_cast<int>(sf);
    }
}

INSTANTIATE_TEST_SUITE_P(AllSpreadingFactors, SpreadingFactorTests,
                         ::testing::Values<uint8_t>(7, 8, 9, 10, 11, 12),
                         [](const ::testing::TestParamInfo<uint8_t>& info) {
                             return "SF" + std::to_string(
                                               static_cast<int>(info.param));
                         });

}  // namespace test
}  // namespace loramesher
