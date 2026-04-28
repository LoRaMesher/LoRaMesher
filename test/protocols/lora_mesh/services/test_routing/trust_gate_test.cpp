/**
 * @file trust_gate_test.cpp
 * @brief Integration tests for the link-quality / route-install trust gate
 *
 * Exercises the "trust requires multiple samples" property end-to-end:
 *  - one transient direct packet from a fresh source must not displace an
 *    established multi-hop route (Bug A — quality inflation from one packet)
 *  - sync beacons relayed via a multi-hop path do not install a phantom
 *    route to the NM (Bug B — phantom 2-hop route from sync beacon)
 *  - sync beacons still keep an existing NM route alive between routing-
 *    table exchanges (regression guard for the refresh-only path)
 */
#include <gtest/gtest.h>

#include "routing_test_fixture.hpp"

namespace loramesher {
namespace test {

class RoutingTrustGateTests : public RoutingTestFixture {};

/**
 * @brief Bug A replay — one packet on a flapping link must not promote
 * a destination to a direct neighbour over an established 2-hop route.
 *
 * Topology: N1(NM) ── N2 ── N3, with the N1↔N3 link disabled.
 * After convergence N1 has via=N2 hops=2 for N3. Briefly opening the
 * direct link is enough for at most one routing-table exchange to leak
 * through. Without Fix 1, that single packet would set the direct route
 * quality to 200 and beat the indirect route on ETX cost.
 */
TEST_F(RoutingTrustGateTests, OnePacketDoesNotDisplaceIndirectRoute) {
    auto nodes = GenerateLineTopology(3, 0x1000, "Node", 0);
    ASSERT_EQ(nodes.size(), 3u);

    for (auto* node : nodes) {
        ASSERT_TRUE(StartNode(*node)) << "Failed to start " << node->name;
    }
    ASSERT_TRUE(WaitForNetworkFormation(nodes, 2));
    ASSERT_TRUE(WaitForRoutingStabilization(nodes));

    auto superframe_ms = GetSuperframeDuration(*nodes[0]);
    uint32_t step_ms = 15u;
    AdvanceTime(superframe_ms * 2, superframe_ms * 2, step_ms, 0,
                [&]() { return false; });

    ASSERT_EQ(GetNextHop(*nodes[0], nodes[2]->address), nodes[1]->address)
        << "Pre-condition: N1 must reach N3 via N2";
    ASSERT_EQ(GetHopCount(*nodes[0], nodes[2]->address), 2);

    SetLinkStatus(*nodes[0], *nodes[2], true);
    AdvanceTime(superframe_ms, superframe_ms, step_ms, 0,
                [&]() { return false; });
    SetLinkStatus(*nodes[0], *nodes[2], false);
    AdvanceTime(superframe_ms * 2, superframe_ms * 2, step_ms, 0,
                [&]() { return false; });

    EXPECT_EQ(GetNextHop(*nodes[0], nodes[2]->address), nodes[1]->address)
        << "One direct packet must not displace the established indirect "
           "route";
    EXPECT_EQ(GetHopCount(*nodes[0], nodes[2]->address), 2);
}

/**
 * @brief Bug B regression — bootstrap must still install the NM route
 * via routing-table exchanges, now that sync beacons no longer install
 * multi-hop routes.
 *
 * Topology: N1(NM) ── N2 ── N3. Convergence proves the leaf still gets
 * its NM route via N2 (driven by routing tables, not by a single
 * forwarded beacon). Continued operation must not flap that route.
 */
TEST_F(RoutingTrustGateTests, MultiHopNmRouteInstalledViaRoutingTable) {
    auto nodes = GenerateLineTopology(3, 0x1000, "Node", 0);
    ASSERT_EQ(nodes.size(), 3u);

    for (auto* node : nodes) {
        ASSERT_TRUE(StartNode(*node)) << "Failed to start " << node->name;
    }
    ASSERT_TRUE(WaitForNetworkFormation(nodes, 2));
    ASSERT_TRUE(WaitForRoutingStabilization(nodes));

    auto superframe_ms = GetSuperframeDuration(*nodes[0]);
    uint32_t step_ms = 15u;
    AdvanceTime(superframe_ms * 2, superframe_ms * 2, step_ms, 0,
                [&]() { return false; });

    EXPECT_TRUE(HasRouteTo(*nodes[2], nodes[0]->address))
        << "Leaf must have an NM route after convergence";
    EXPECT_EQ(GetHopCount(*nodes[2], nodes[0]->address), 2);
    EXPECT_EQ(GetNextHop(*nodes[2], nodes[0]->address), nodes[1]->address);

    auto check_stable = [&]() {
        EXPECT_EQ(GetHopCount(*nodes[2], nodes[0]->address), 2);
        EXPECT_EQ(GetNextHop(*nodes[2], nodes[0]->address), nodes[1]->address);
    };
    for (int i = 0; i < 4; ++i) {
        AdvanceTime(superframe_ms, superframe_ms, step_ms, 0,
                    [&]() { return false; });
        check_stable();
    }
}

}  // namespace test
}  // namespace loramesher
