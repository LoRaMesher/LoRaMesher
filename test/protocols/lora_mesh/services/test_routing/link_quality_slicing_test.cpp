/**
 * @file link_quality_slicing_test.cpp
 * @brief Link-quality stability when routing-table broadcasts are sliced
 *
 * At high spreading factors the per-frame MTU only carries a few routing
 * entries, so a node's table is broadcast as a rotating slice across several
 * superframes. A receiver whose own entry is not in the current slice must NOT
 * conclude the link is unidirectional: the peer simply hasn't broadcast that
 * portion of its table yet. These tests exercise a dense, fully bidirectional
 * mesh across SF7-SF12 and assert that the direct link quality stays stable and
 * the routing tables fully converge, while genuine unidirectional links are
 * still detected.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "routing_test_fixture.hpp"
#include "types/configurations/radio_configuration.hpp"

namespace loramesher {
namespace test {

namespace {
/// Nodes in the dense mesh. With 7 peers per node the routing table exceeds
/// the SF10-SF12 per-frame entry budget (3 entries) and must be sliced across
/// ~3 superframes, reproducing the field scenario while staying fast to run.
constexpr int kDenseMeshNodes = 8;

using LinkStats =
    types::protocols::lora_mesh::NetworkNodeRoute::LinkQualityStats;
}  // namespace

/**
 * @brief Parameterized over the full LoRa spreading-factor range (7-12).
 */
class LinkQualitySlicingTests : public RoutingTestFixture,
                                public ::testing::WithParamInterface<uint8_t> {
   protected:
    /// Build a fully connected mesh of kDenseMeshNodes nodes at the given SF.
    std::vector<TestNode*> BuildDenseMesh(uint8_t sf) {
        RadioConfig sf_config;
        sf_config.setSpreadingFactor(sf);

        std::vector<TestNode*> result;
        for (int i = 0; i < kDenseMeshNodes; i++) {
            NodeRole role =
                (i == 0) ? NodeRole::NETWORK_MANAGER : NodeRole::NODE_ONLY;
            auto& node = CreateNode("Node" + std::to_string(i + 1),
                                    static_cast<AddressType>(0x1000 + i), role,
                                    PinConfig(), sf_config);
            result.push_back(&node);
        }

        for (size_t i = 0; i < result.size(); i++) {
            for (size_t j = i + 1; j < result.size(); j++) {
                SetLinkStatus(*result[i], *result[j], true);
            }
        }
        return result;
    }
};

/**
 * @brief Dense bidirectional mesh: quality stable, tables converge.
 *
 * Reproduces the SF12 field symptom where the direct link quality oscillates
 * between 1 and >200 (EWMA stays flat) and the routing table fragments down to
 * roughly the slice capacity. In a fully bidirectional mesh, after a few
 * rotation cycles:
 *  - no direct neighbor with >=3 receptions is ever flagged unidirectional,
 *  - the direct link quality never collapses to the unidirectional floor (1),
 *  - every node knows all peers as 1-hop routes.
 */
TEST_P(LinkQualitySlicingTests, DenseMeshLinkQualityStable) {
    const uint8_t sf = GetParam();

    std::vector<TestNode*> nodes = BuildDenseMesh(sf);
    for (auto* node : nodes) {
        ASSERT_TRUE(StartNode(*node)) << "Failed to start " << node->name
                                      << " at SF" << static_cast<int>(sf);
    }

    ASSERT_TRUE(WaitForNetworkFormation(nodes, kDenseMeshNodes - 1))
        << "Network formation failed at SF" << static_cast<int>(sf);

    // Let routing tables exchange for a bounded window spanning several full
    // rotation cycles. Bounded (not a wait-for-convergence) so the buggy
    // baseline fails fast instead of burning a large timeout.
    auto superframe_ms = GetSuperframeDuration(*nodes.front());
    AdvanceTime(superframe_ms * 12, superframe_ms * 12, 15u, 0,
                [&]() { return false; });

    // Sample direct link quality across several superframes. The slice cursor
    // advances each superframe, so this spans more than a full rotation cycle
    // and catches any rotation-induced collapse to the unidirectional floor.
    constexpr int kSamples = 6;
    for (int s = 0; s < kSamples; s++) {
        AdvanceTime(superframe_ms, superframe_ms, 15u, 0,
                    [&]() { return false; });

        for (auto* node : nodes) {
            auto network_nodes = node->protocol->GetNetworkNodesCopy();
            for (const auto& route : network_nodes) {
                if (!route.is_active || route.routing_entry.hop_count != 1) {
                    continue;  // only established direct neighbors
                }
                const auto& ls = route.link_stats;
                if (ls.messages_received < LinkStats::kMinSamplesForQuality) {
                    continue;  // still provisional
                }

                // The bidirectional link must never be flagged unidirectional.
                bool flagged_unidirectional =
                    ls.remote_link_quality == 0 && ls.messages_expected >= 3;
                EXPECT_FALSE(flagged_unidirectional)
                    << node->name << " falsely flagged 0x" << std::hex
                    << route.routing_entry.destination << std::dec
                    << " unidirectional at SF" << static_cast<int>(sf)
                    << " (sample " << s << ")";

                // Direct quality must stay healthy, not flap to the floor (1).
                uint8_t quality = ls.CalculateQuality();
                EXPECT_GT(quality, 1)
                    << node->name << " direct quality to 0x" << std::hex
                    << route.routing_entry.destination << std::dec
                    << " collapsed to " << static_cast<int>(quality) << " at SF"
                    << static_cast<int>(sf) << " (sample " << s << ")";
            }
        }
    }

    // Convergence: every node must know all peers as 1-hop routes. The flapping
    // collapses each table toward the slice capacity, so this one-shot check
    // fails on the buggy baseline without waiting on a long timeout.
    for (auto* node : nodes) {
        size_t one_hop_peers = 0;
        auto network_nodes = node->protocol->GetNetworkNodesCopy();
        for (const auto& route : network_nodes) {
            if (route.is_active && route.routing_entry.hop_count == 1) {
                one_hop_peers++;
            }
        }
        EXPECT_EQ(one_hop_peers, static_cast<size_t>(kDenseMeshNodes - 1))
            << node->name << " converged to " << one_hop_peers
            << " 1-hop peers (expected " << (kDenseMeshNodes - 1) << ") at SF"
            << static_cast<int>(sf);
    }
}

INSTANTIATE_TEST_SUITE_P(AllSpreadingFactors, LinkQualitySlicingTests,
                         ::testing::Values<uint8_t>(7, 8, 9, 10, 11, 12),
                         [](const ::testing::TestParamInfo<uint8_t>& info) {
                             return "SF" + std::to_string(
                                               static_cast<int>(info.param));
                         });

/**
 * @brief Genuine unidirectional links must still be detected.
 *
 * Edge hears Relay, but Relay does not hear Edge. After enough rotation cycles
 * for Relay's table to fully cycle without ever listing Edge as a reception,
 * Edge must mark the direct link to Relay unidirectional (remote_link_quality
 * stays 0). Guards the sticky-quality fix against suppressing real detection.
 */
TEST_F(LinkQualitySlicingTests, GenuineUnidirectionalStillDetected) {
    RadioConfig sf_config;
    sf_config.setSpreadingFactor(7);

    auto& nm = CreateNode("NM", 0x2000, NodeRole::NETWORK_MANAGER, PinConfig(),
                          sf_config);
    auto& relay = CreateNode("Relay", 0x2001, NodeRole::NODE_ONLY, PinConfig(),
                             sf_config);
    auto& edge =
        CreateNode("Edge", 0x2002, NodeRole::NODE_ONLY, PinConfig(), sf_config);

    // NM <-> Relay and NM <-> Edge bidirectional. Relay->Edge one-way only:
    // Edge hears Relay, but Relay never hears Edge.
    SetLinkStatus(nm, relay, true);
    SetLinkStatus(nm, edge, true);
    SetLinkStatus(relay, edge, false);
    SetDirectionalLink(relay, edge, true);  // relay -> edge only

    std::vector<TestNode*> nodes = {&nm, &relay, &edge};
    for (auto* node : nodes) {
        ASSERT_TRUE(StartNode(*node)) << "Failed to start " << node->name;
    }

    ASSERT_TRUE(WaitForNetworkFormation(nodes, 2));
    ASSERT_TRUE(WaitForRoutingStabilization(nodes));

    // Give Relay's table several full rotations: if Edge were a reception it
    // would have appeared in a slice by now.
    auto superframe_ms = GetSuperframeDuration(*nodes.front());
    bool detected =
        AdvanceTime(superframe_ms * 20, superframe_ms * 20, 15u, 0, [&]() {
            auto network_nodes = edge.protocol->GetNetworkNodesCopy();
            for (const auto& route : network_nodes) {
                if (route.routing_entry.destination != relay.address) {
                    continue;
                }
                const auto& ls = route.link_stats;
                return ls.messages_received >=
                           LinkStats::kMinSamplesForQuality &&
                       ls.remote_link_quality == 0 && ls.messages_expected >= 3;
            }
            return false;
        });

    EXPECT_TRUE(detected)
        << "Edge should detect the Relay->Edge link as unidirectional";
}

}  // namespace test
}  // namespace loramesher
