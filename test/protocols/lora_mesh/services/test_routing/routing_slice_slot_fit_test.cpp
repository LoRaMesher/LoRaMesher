/**
 * @file routing_slice_slot_fit_test.cpp
 * @brief Routing broadcasts that do not fit the control slot as built
 *
 * A routing slice whose transmission is skipped because it does not fit the
 * remaining slot time must not be lost, and a node must keep listening to a
 * neighbour it hears whatever its route to it. Otherwise neighbours missing
 * from the sender's transmitted slices mark the link unidirectional, reroute
 * the sender through relays and stop listening in its data slots, so its
 * broadcasts are lost.
 *
 * The slot is sized by the network manager for its own maximum packet size;
 * the members, configured for larger packets, build slices too long for it.
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "routing_test_fixture.hpp"
#include "types/configurations/radio_configuration.hpp"

namespace loramesher {
namespace test {

namespace {
constexpr int kMeshNodes = 6;
/// Manager's packet size: its slots fit a 2-entry routing slice, while a
/// member's whole table (5 entries) would be one slice
constexpr uint8_t kManagerPacketSize = 40;
constexpr uint8_t kSpreadingFactor = 9;
}  // namespace

class RoutingSliceSlotFitTest : public RoutingTestFixture {
   protected:
    std::vector<TestNode*> BuildMesh() {
        RadioConfig radio;
        radio.setSpreadingFactor(kSpreadingFactor);
        std::vector<TestNode*> nodes;
        for (int i = 0; i < kMeshNodes; i++) {
            const bool manager = i == 0;
            auto& node = CreateNode(
                "Node" + std::to_string(i + 1),
                static_cast<AddressType>(0x1000 + i),
                manager ? NodeRole::NETWORK_MANAGER : NodeRole::NODE_ONLY,
                PinConfig(), radio, [manager](LoRaMeshProtocolConfig& config) {
                    if (manager) {
                        config.setMaxPacketSize(kManagerPacketSize);
                    }
                });
            nodes.push_back(&node);
        }
        for (size_t i = 0; i < nodes.size(); i++) {
            for (size_t j = i + 1; j < nodes.size(); j++) {
                SetLinkStatus(*nodes[i], *nodes[j], true);
            }
        }
        return nodes;
    }
};

TEST_F(RoutingSliceSlotFitTest, NeighboursKeepHearingAMemberWithLongSlices) {
    std::vector<TestNode*> nodes = BuildMesh();
    for (auto* node : nodes) {
        ASSERT_TRUE(StartNode(*node)) << node->name;
    }
    ASSERT_TRUE(WaitForNetworkFormation(nodes, kMeshNodes - 1));

    const auto superframe_ms = GetSuperframeDuration(*nodes.front());
    AdvanceTime(superframe_ms * 12, superframe_ms * 12, 15u, 0,
                [&]() { return false; });

    // Every node keeps every peer as a direct neighbour
    for (auto* node : nodes) {
        size_t one_hop_peers = 0;
        for (const auto& route : node->protocol->GetNetworkNodes()) {
            if (route.is_active && route.routing_entry.hop_count == 1) {
                one_hop_peers++;
            }
        }
        EXPECT_EQ(one_hop_peers, static_cast<size_t>(kMeshNodes - 1))
            << node->name;
    }

    // A member's broadcast reaches every other node
    TestNode& sender = *nodes.back();
    ClearAllReceivedMessages();
    std::vector<uint8_t> payload = {0xBC, 0x01};
    ASSERT_TRUE(sender.protocol->SendBroadcast(payload));
    const bool all_received =
        AdvanceTime(superframe_ms * 4, superframe_ms * 4, 50u, 0, [&]() {
            for (auto* node : nodes) {
                if (node != &sender &&
                    !HasReceivedMessageFrom(*node, sender.address,
                                            MessageType::DATA)) {
                    return false;
                }
            }
            return true;
        });
    EXPECT_TRUE(all_received);
}

}  // namespace test
}  // namespace loramesher
