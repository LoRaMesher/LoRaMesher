/**
 * @file routing_table_rotation_integration_test.cpp
 * @brief Integration tests for routing-table broadcast slicing
 *
 * Verifies that at high spreading factors (SF10-SF12, BW125) the
 * routing-table broadcast is correctly fragmented across superframes
 * so it fits within the PHY frame budget, and that the scaled aging
 * timeouts in NetworkService do not prune live routes whose refresh
 * cycle now spans multiple superframes.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "routing_test_fixture.hpp"
#include "types/configurations/radio_configuration.hpp"
#include "types/messages/message_type.hpp"

namespace loramesher {
namespace test {

class RoutingTableRotationIntegrationTest : public RoutingTestFixture {
   protected:
    static constexpr size_t kMessageTypeOffset = 4;

    bool IsRoutingTable(const std::vector<uint8_t>& packet) const {
        if (packet.size() <= kMessageTypeOffset)
            return false;
        return packet[kMessageTypeOffset] ==
               static_cast<uint8_t>(MessageType::ROUTE_TABLE);
    }

    size_t MaxRoutingPacketSize(uint16_t node_address) {
        auto packets = virtual_network_.GetSentMessages(node_address);
        size_t max_size = 0;
        for (const auto& p : packets) {
            if (IsRoutingTable(p)) {
                max_size = std::max(max_size, p.size());
            }
        }
        return max_size;
    }

    size_t CountRoutingPackets(uint16_t node_address) {
        auto packets = virtual_network_.GetSentMessages(node_address);
        return std::count_if(packets.begin(), packets.end(),
                             [this](const std::vector<uint8_t>& p) {
                                 return IsRoutingTable(p);
                             });
    }
};

// Validates the fix: every ROUTE_TABLE broadcast at SF12/BW125 must fit
// within the 51-byte PHY cap. Pre-fix, with 4 nodes (3 entries per peer)
// the packet would be 6+6+3*10 = 42 B for the first slice, but full-table
// broadcasts at larger fanouts would exceed the cap.
TEST_F(RoutingTableRotationIntegrationTest, SF12_BroadcastFitsRadioCap) {
    RadioConfig sf_config;
    sf_config.setSpreadingFactor(12);

    auto& node1 = CreateNode("Node1", 0x1000, NodeRole::NETWORK_MANAGER,
                             PinConfig(), sf_config);
    auto& node2 = CreateNode("Node2", 0x1001, NodeRole::NODE_ONLY, PinConfig(),
                             sf_config);
    auto& node3 = CreateNode("Node3", 0x1002, NodeRole::NODE_ONLY, PinConfig(),
                             sf_config);
    auto& node4 = CreateNode("Node4", 0x1003, NodeRole::NODE_ONLY, PinConfig(),
                             sf_config);

    // Line topology forces each end to know about 3 peers via the chain
    SetLinkStatus(node1, node2, true);
    SetLinkStatus(node2, node3, true);
    SetLinkStatus(node3, node4, true);

    std::vector<TestNode*> nodes = {&node1, &node2, &node3, &node4};
    for (auto* node : nodes) {
        ASSERT_TRUE(StartNode(*node)) << "Failed to start " << node->name;
    }

    ASSERT_TRUE(WaitForNetworkFormation(nodes, 3))
        << "Network formation failed at SF12";

    auto superframe_ms = GetSuperframeDuration(*nodes.front());
    AdvanceTime(superframe_ms * 6, superframe_ms * 6, 15u, 0,
                [&]() { return false; });

    const uint8_t sf12_cap = RadioConfig::GetMaxPacketSizeForSf(12, 125.0f);
    EXPECT_EQ(sf12_cap, 51u);

    for (auto* node : nodes) {
        const size_t max_size = MaxRoutingPacketSize(node->address);
        const size_t count = CountRoutingPackets(node->address);
        EXPECT_GT(count, 0u)
            << node->name << " should have broadcast routing tables";
        EXPECT_LE(max_size, sf12_cap)
            << node->name << " sent a routing packet (" << max_size
            << " B) larger than the SF12 PHY cap ("
            << static_cast<int>(sf12_cap) << " B)";
    }
}

// Validates that rotation completes: after enough superframes to cover
// the full rotation cycle, every node learns every other peer.
TEST_F(RoutingTableRotationIntegrationTest, SF12_AllRoutesEventuallyKnown) {
    RadioConfig sf_config;
    sf_config.setSpreadingFactor(12);

    auto& node1 = CreateNode("Node1", 0x1000, NodeRole::NETWORK_MANAGER,
                             PinConfig(), sf_config);
    auto& node2 = CreateNode("Node2", 0x1001, NodeRole::NODE_ONLY, PinConfig(),
                             sf_config);
    auto& node3 = CreateNode("Node3", 0x1002, NodeRole::NODE_ONLY, PinConfig(),
                             sf_config);
    auto& node4 = CreateNode("Node4", 0x1003, NodeRole::NODE_ONLY, PinConfig(),
                             sf_config);

    SetLinkStatus(node1, node2, true);
    SetLinkStatus(node2, node3, true);
    SetLinkStatus(node3, node4, true);

    std::vector<TestNode*> nodes = {&node1, &node2, &node3, &node4};
    for (auto* node : nodes) {
        ASSERT_TRUE(StartNode(*node)) << "Failed to start " << node->name;
    }

    ASSERT_TRUE(WaitForRoutingStabilization(nodes))
        << "Routing did not stabilize at SF12";

    auto superframe_ms = GetSuperframeDuration(*nodes.front());
    // Allow enough rotation cycles for the line topology (3 entries
    // max, slice capacity = 3 at SF12 -> 1 rotation step, but each
    // hop needs a separate rotation to propagate).
    AdvanceTime(superframe_ms * 6, superframe_ms * 6, 15u, 0,
                [&]() { return false; });

    for (auto* node : nodes) {
        auto network_nodes = node->protocol->GetNetworkNodesCopy();
        std::set<AddressType> known;
        for (const auto& nn : network_nodes) {
            if (nn.is_active)
                known.insert(nn.routing_entry.destination);
        }
        EXPECT_EQ(known.size(), 3u)
            << node->name << " should know all 3 peers at SF12 after rotation; "
            << "knows " << known.size();
    }
}

// Regression: at SF7 the full routing table fits in one slice. The
// rotation cursor stays at 0, no observable behavior change.
TEST_F(RoutingTableRotationIntegrationTest, SF7_BehaviourUnchanged) {
    RadioConfig sf_config;
    sf_config.setSpreadingFactor(7);

    auto& node1 = CreateNode("Node1", 0x1000, NodeRole::NETWORK_MANAGER,
                             PinConfig(), sf_config);
    auto& node2 = CreateNode("Node2", 0x1001, NodeRole::NODE_ONLY, PinConfig(),
                             sf_config);
    auto& node3 = CreateNode("Node3", 0x1002, NodeRole::NODE_ONLY, PinConfig(),
                             sf_config);
    auto& node4 = CreateNode("Node4", 0x1003, NodeRole::NODE_ONLY, PinConfig(),
                             sf_config);

    SetLinkStatus(node1, node2, true);
    SetLinkStatus(node2, node3, true);
    SetLinkStatus(node3, node4, true);

    std::vector<TestNode*> nodes = {&node1, &node2, &node3, &node4};
    for (auto* node : nodes) {
        ASSERT_TRUE(StartNode(*node)) << "Failed to start " << node->name;
    }

    ASSERT_TRUE(WaitForRoutingStabilization(nodes))
        << "Routing did not stabilize at SF7";

    auto superframe_ms = GetSuperframeDuration(*nodes.front());
    AdvanceTime(superframe_ms * 3, superframe_ms * 3, 15u, 0,
                [&]() { return false; });

    for (auto* node : nodes) {
        auto network_nodes = node->protocol->GetNetworkNodesCopy();
        std::set<AddressType> known;
        for (const auto& nn : network_nodes) {
            if (nn.is_active)
                known.insert(nn.routing_entry.destination);
        }
        EXPECT_EQ(known.size(), 3u)
            << node->name << " should know all 3 peers at SF7";
    }

    const uint8_t sf7_cap = RadioConfig::GetMaxPacketSizeForSf(7, 125.0f);
    EXPECT_EQ(sf7_cap, 242u);
    for (auto* node : nodes) {
        const size_t max_size = MaxRoutingPacketSize(node->address);
        EXPECT_LE(max_size, sf7_cap)
            << node->name << " sent an oversize routing packet at SF7";
    }
}

}  // namespace test
}  // namespace loramesher
