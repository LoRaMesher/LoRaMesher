/**
 * @file tdma_routing_mismatch_test.cpp
 * @brief Tests for TDMA-aware routing with unidirectional link detection
 *
 * Verifies that confirmed unidirectional links (quality=0) cause the
 * routing table to converge to indirect routes via relay nodes, and that
 * phantom non-TDMA nodes are rejected by FindNextHop.
 */

#include "../test_routing/routing_test_fixture.hpp"

namespace loramesher {
namespace test {

class TDMARoutingMismatchTests : public RoutingTestFixture {};

// ─────────────────────────────────────────────────────────────────────────────
// Key scenario:
//
//   GW (0x2000) ── NM (0x1000) ── Core2 (0x1001)
//        |              |
//     (cut later)    (bidirectional)
//        |              |
//      Edge1 (0x3000) ──┘
//        /          \.
//   Edge2 (0x3001)  Edge3 (0x3002)
//
// 1. Network forms with GW↔Edge1 BIDIRECTIONAL.
// 2. CUT Edge1→GW. Edge1 still hears GW but GW cannot hear Edge1.
// 3. After 3+ routing tables with remote_quality=0, Edge1 detects the
//    link as unidirectional (quality=0, cost=65535).
// 4. The entries loop finds an indirect route via NM (hop=2).
// 5. DATA from Edge1 to GW is delivered via NM.
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief After a link becomes unidirectional, routing converges to an
 *        indirect route and data is delivered via relay.
 *
 * After cutting Edge1→GW, Edge1 detects the link as unidirectional
 * (quality=0). The ETX cost becomes infinite (65535), so the entries loop
 * overwrites the direct route with a 2-hop path via NM. Data is then
 * delivered normally through the indirect route.
 */
TEST_F(TDMARoutingMismatchTests, UnidirectionalLinkConvergesToIndirectRoute) {
    // ── Create nodes ─────────────────────────────────────────────────────
    auto& nm = CreateNode("NM", 0x1000, NodeRole::NETWORK_MANAGER);
    auto& gw = CreateNode("GW", 0x2000, NodeRole::NODE_ONLY);
    auto& core2 = CreateNode("Core2", 0x1001, NodeRole::NODE_ONLY);
    auto& edge1 = CreateNode("Edge1", 0x3000, NodeRole::NODE_ONLY);
    auto& edge2 = CreateNode("Edge2", 0x3001, NodeRole::NODE_ONLY);
    auto& edge3 = CreateNode("Edge3", 0x3002, NodeRole::NODE_ONLY);

    // ── All links bidirectional (including Edge1↔GW) ─────────────────────
    SetLinkStatus(nm, gw, true);
    SetLinkStatus(nm, core2, true);
    SetLinkStatus(nm, edge1, true);
    SetLinkStatus(gw, edge1, true);
    SetLinkStatus(edge1, edge2, true);
    SetLinkStatus(edge1, edge3, true);

    // ── Form and stabilize ───────────────────────────────────────────────
    std::vector<TestNode*> all_nodes = {&nm,    &gw,    &core2,
                                        &edge1, &edge2, &edge3};
    for (auto* node : all_nodes) {
        ASSERT_TRUE(StartNode(*node)) << "Failed to start " << node->name;
    }
    ASSERT_TRUE(WaitForNetworkFormation(all_nodes, 5))
        << "Network formation failed";
    ASSERT_TRUE(WaitForRoutingStabilization(all_nodes))
        << "Routing stabilization failed";

    auto superframe_ms = GetSuperframeDuration(nm);
    auto step_ms = 15u;

    // Converge distance-vector so both GW↔Edge1 are confirmed hop=1
    AdvanceTime(superframe_ms * 3, superframe_ms * 3, step_ms, 0,
                [&]() { return false; });

    // ── Verify Edge1 has GW as hop=1 ─────────────────────────────────────
    ASSERT_EQ(GetHopCount(edge1, gw.address), 1)
        << "Edge1 should see GW as direct neighbor (hop=1)";

    std::cout << "=== Before link cut ===" << std::endl;
    PrintRoutingTable(edge1);

    // ── CUT Edge1→GW link (unidirectional: GW→Edge1 only) ───────────────
    SetDirectionalLink(edge1, gw, false);

    // ── Wait for unidirectional detection and route convergence ──────────
    // Edge1 needs 3+ expected messages with remote_quality=0 to confirm
    // unidirectional (quality=0, cost=65535). Then the entries loop from
    // NM's routing table provides a 2-hop route to GW via NM.
    bool route_converged =
        AdvanceTime(superframe_ms * 12, superframe_ms * 12, step_ms, 0,
                    [&]() { return GetHopCount(edge1, gw.address) > 1; });

    std::cout << "=== After unidirectional detection ===" << std::endl;
    PrintRoutingTable(edge1);

    ASSERT_TRUE(route_converged)
        << "Edge1 should converge to indirect route (hop>1) after "
           "unidirectional detection";
    EXPECT_GE(GetHopCount(edge1, gw.address), 2)
        << "Edge1 should route to GW via relay (hop>=2)";

    // ── Send DATA from Edge1 → GW via indirect route ────────────────────
    constexpr int kNumMessages = 10;
    std::vector<uint8_t> payload = {0xCA, 0xFE, 0xBA, 0xBE};

    for (int i = 0; i < kNumMessages; i++) {
        payload[0] = static_cast<uint8_t>(i);
        SendMessage(edge1, gw, payload);
    }

    bool any_received =
        AdvanceTime(superframe_ms * 10, superframe_ms * 10, step_ms, 0, [&]() {
            return CountReceivedMessages(gw, edge1.address,
                                         MessageType::DATA) >= 1;
        });

    size_t delivered =
        CountReceivedMessages(gw, edge1.address, MessageType::DATA);
    std::cout << "Delivered " << delivered << "/" << kNumMessages
              << " from Edge1 to GW (via indirect route)" << std::endl;

    PrintRoutingTable(edge1);

    EXPECT_TRUE(any_received)
        << "Indirect route via NM should deliver DATA to GW";
    EXPECT_GE(delivered, 1u)
        << "At least 1 of " << kNumMessages << " should reach GW via NM";
}

/**
 * @brief Routes THROUGH a unidirectional neighbor retain stale quality
 *        and must be degraded so alternative routes can win.
 *
 * Topology (mirrors real experiment 0x3428 ↔ 0x77A4):
 *
 *   NM (0x1000) ─── Target (0x2000)
 *        |
 *   Edge1 (0x3000) ··· Broadcaster (0x4000) ─── Target (0x2000)
 *         (broadcast-only: Edge1 hears Broadcaster but can't send)
 *
 * 1. Network forms with Edge1↔Broadcaster bidirectional.
 *    Edge1 learns route to Target via Broadcaster (hop=2, quality ~200).
 * 2. CUT Edge1→Broadcaster (unidirectional: Broadcaster→Edge1 only).
 *    Edge1 still receives Broadcaster's routing tables (broadcasts).
 * 3. After 3+ messages: unidirectional detected (quality=1).
 *    BUT the route to Target via Broadcaster retains stale quality ~200.
 *    NM offers Target at quality ~150 (via marginal NM link), cost > stale.
 * 4. The cascade fix should degrade Target's quality through Broadcaster,
 *    letting the NM route win.
 */
TEST_F(TDMARoutingMismatchTests, StaleRouteThroughUnidirectionalNeighbor) {
    auto& nm = CreateNode("NM", 0x1000, NodeRole::NETWORK_MANAGER);
    auto& target = CreateNode("Target", 0x2000, NodeRole::NODE_ONLY);
    auto& edge1 = CreateNode("Edge1", 0x3000, NodeRole::NODE_ONLY);
    auto& broadcaster = CreateNode("Broadcaster", 0x4000, NodeRole::NODE_ONLY);
    auto& edge2 = CreateNode("Edge2", 0x5000, NodeRole::NODE_ONLY);

    // Full connectivity initially
    SetLinkStatus(nm, target, true);
    SetLinkStatus(nm, edge1, true);
    SetLinkStatus(edge1, broadcaster, true);
    SetLinkStatus(broadcaster, target, true);
    SetLinkStatus(edge1, edge2, true);

    std::vector<TestNode*> all_nodes = {&nm, &target, &edge1, &broadcaster,
                                        &edge2};
    for (auto* node : all_nodes) {
        ASSERT_TRUE(StartNode(*node)) << "Failed to start " << node->name;
    }
    ASSERT_TRUE(WaitForNetworkFormation(all_nodes, 4))
        << "Network formation failed";
    ASSERT_TRUE(WaitForRoutingStabilization(all_nodes))
        << "Routing stabilization failed";

    auto superframe_ms = GetSuperframeDuration(nm);
    auto step_ms = 15u;

    // Let routing converge — Edge1 should have route to Target via
    // Broadcaster (hop=2) since Broadcaster↔Target is a direct link
    AdvanceTime(superframe_ms * 5, superframe_ms * 5, step_ms, 0,
                [&]() { return false; });

    // Verify Edge1 can deliver to Target before degradation
    std::vector<uint8_t> payload = {0xAA, 0xBB};
    SendMessage(edge1, target, payload);
    AdvanceTime(superframe_ms * 5, superframe_ms * 5, step_ms, 0, [&]() {
        return CountReceivedMessages(target, edge1.address,
                                     MessageType::DATA) >= 1;
    });
    ASSERT_GE(CountReceivedMessages(target, edge1.address, MessageType::DATA),
              1u)
        << "Edge1 should deliver to Target before link cut";

    // ── Degrade NM↔Edge1 to ~50% PDR (mimics marginal link) ───────────
    // This makes the NM route marginal but still functional for data.
    // The stale Broadcaster route (quality ~200) would normally beat
    // the NM route (quality ~128) without proactive degradation.
    SetLinkLoss(nm, edge1, 0.5f);

    // Let quality stabilize with the loss
    AdvanceTime(superframe_ms * 5, superframe_ms * 5, step_ms, 0,
                [&]() { return false; });

    std::cout << "=== Before link cut (NM link degraded) ===" << std::endl;
    std::cout << "Edge1 next_hop to Target: 0x" << std::hex
              << GetNextHop(edge1, target.address) << std::dec << std::endl;
    PrintRoutingTable(edge1);

    // ── CUT Edge1→Broadcaster (unidirectional) ──────────────────────────
    // Edge1 still hears Broadcaster's broadcasts (routing tables) but
    // cannot send unicast data to Broadcaster.
    SetDirectionalLink(edge1, broadcaster, false);

    // With proactive degradation in FindNextHop: the first DATA attempt
    // to Broadcaster fails TDMA check → route quality degraded to 1 →
    // entries loop from NM overwrites on next superframe. Fast convergence.
    bool route_switched =
        AdvanceTime(superframe_ms * 15, superframe_ms * 15, step_ms, 0, [&]() {
            AddressType nh = GetNextHop(edge1, target.address);
            return nh != broadcaster.address && nh != 0;
        });

    std::cout << "=== After unidirectional detection ===" << std::endl;
    AddressType nh = GetNextHop(edge1, target.address);
    std::cout << "Edge1 next_hop to Target: 0x" << std::hex << nh << std::dec
              << std::endl;
    PrintRoutingTable(edge1);

    // Routing table check: route to Target should no longer go through
    // Broadcaster. The cascade fix should degrade the stale quality,
    // allowing the NM route to win.
    EXPECT_TRUE(route_switched)
        << "Route to Target should switch away from unidirectional "
           "Broadcaster";
    EXPECT_NE(nh, broadcaster.address)
        << "Edge1 should NOT route to Target via Broadcaster";

    // Data delivery: verify end-to-end through the corrected route.
    // With 50% PDR on NM↔Edge1, not all messages will arrive, but
    // at least some should get through the 2-hop path via NM.
    size_t before =
        CountReceivedMessages(target, edge1.address, MessageType::DATA);

    constexpr int kNumMessages = 15;
    for (int i = 0; i < kNumMessages; i++) {
        payload[0] = static_cast<uint8_t>(i + 0x20);
        SendMessage(edge1, target, payload);
    }

    size_t expected = before + kNumMessages / 4;

    bool any_received =
        AdvanceTime(superframe_ms * 15, superframe_ms * 15, step_ms, 0, [&]() {
            return CountReceivedMessages(target, edge1.address,
                                         MessageType::DATA) >= expected;
        });

    size_t delivered =
        CountReceivedMessages(target, edge1.address, MessageType::DATA) -
        before;
    std::cout << "Delivered " << delivered << "/" << kNumMessages
              << " from Edge1 to Target (via NM, 50% PDR link)" << std::endl;

    EXPECT_TRUE(any_received)
        << "Edge1 should deliver to Target via NM after route correction";
    EXPECT_GE(delivered, 1u) << "At least 1 of " << kNumMessages
                             << " should reach Target via NM despite 50% PDR";
}

/**
 * @brief Link outage causes FAULT_RECOVERY, then recovery restores routing
 *        and data delivery.
 *
 * Topology:  NM ── Edge1
 *            |
 *            GW
 *
 * 1. Network forms, Edge1 routes to GW via NM (hop=2).
 * 2. CUT NM↔Edge1 link completely. Edge1 misses sync beacons →
 *    enters FAULT_RECOVERY after kMaxNoReceivedSyncBeacons (5).
 * 3. RESTORE the link. Edge1 should rejoin (DISCOVERY → JOINING →
 *    NORMAL_OPERATION), re-establish routes, and deliver data again.
 */
TEST_F(TDMARoutingMismatchTests, LinkOutageRecoveryAfterFaultRecovery) {
    using ProtocolState = protocols::lora_mesh::INetworkService::ProtocolState;

    auto& nm = CreateNode("NM", 0x1000, NodeRole::NETWORK_MANAGER);
    auto& gw = CreateNode("GW", 0x2000, NodeRole::NODE_ONLY);
    auto& edge1 = CreateNode("Edge1", 0x3000, NodeRole::NODE_ONLY);

    SetLinkStatus(nm, gw, true);
    SetLinkStatus(nm, edge1, true);

    std::vector<TestNode*> all_nodes = {&nm, &gw, &edge1};
    for (auto* node : all_nodes) {
        ASSERT_TRUE(StartNode(*node)) << "Failed to start " << node->name;
    }
    ASSERT_TRUE(WaitForNetworkFormation(all_nodes, 2))
        << "Network formation failed";
    ASSERT_TRUE(WaitForRoutingStabilization(all_nodes))
        << "Routing stabilization failed";

    auto superframe_ms = GetSuperframeDuration(nm);
    auto step_ms = 15u;

    // Let routing converge
    AdvanceTime(superframe_ms * 4, superframe_ms * 4, step_ms, 0,
                [&]() { return false; });

    // Verify Edge1 can deliver data to GW before the outage
    std::vector<uint8_t> payload = {0xBE, 0xEF};
    SendMessage(edge1, gw, payload);
    AdvanceTime(superframe_ms * 5, superframe_ms * 5, step_ms, 0, [&]() {
        return CountReceivedMessages(gw, edge1.address, MessageType::DATA) >= 1;
    });
    size_t pre_outage =
        CountReceivedMessages(gw, edge1.address, MessageType::DATA);
    ASSERT_GE(pre_outage, 1u) << "Edge1 should deliver to GW before outage";

    std::cout << "=== Before link outage ===" << std::endl;
    PrintRoutingTable(edge1);

    // ── CUT NM↔Edge1 completely ─────────────────────────────────────────
    SetLinkStatus(nm, edge1, false);

    // Wait until Edge1 enters FAULT_RECOVERY (5 missed beacons + margin)
    bool entered_fault =
        AdvanceTime(superframe_ms * 10, superframe_ms * 10, step_ms, 0, [&]() {
            auto state = edge1.protocol->GetState();
            return state == ProtocolState::FAULT_RECOVERY ||
                   state == ProtocolState::NM_ELECTION ||
                   state == ProtocolState::DISCOVERY;
        });

    auto fault_state = edge1.protocol->GetState();
    std::cout << "Edge1 state after outage: " << static_cast<int>(fault_state)
              << std::endl;
    ASSERT_TRUE(entered_fault)
        << "Edge1 should enter FAULT_RECOVERY/DISCOVERY after link outage";

    // Let it sit in fault state for a few superframes
    AdvanceTime(superframe_ms * 3, superframe_ms * 3, step_ms, 0,
                [&]() { return false; });

    // ── RESTORE NM↔Edge1 link ───────────────────────────────────────────
    SetLinkStatus(nm, edge1, true);

    std::cout << "=== Link restored, waiting for rejoin ===" << std::endl;

    // Wait for Edge1 to rejoin and reach NORMAL_OPERATION
    bool rejoined =
        AdvanceTime(superframe_ms * 20, superframe_ms * 20, step_ms, 0, [&]() {
            return edge1.protocol->GetState() ==
                   ProtocolState::NORMAL_OPERATION;
        });

    std::cout << "Edge1 state after restore: "
              << static_cast<int>(edge1.protocol->GetState()) << std::endl;
    ASSERT_TRUE(rejoined)
        << "Edge1 should rejoin and reach NORMAL_OPERATION after link restore";

    // Let routing converge after rejoin
    AdvanceTime(superframe_ms * 5, superframe_ms * 5, step_ms, 0,
                [&]() { return false; });

    std::cout << "=== After recovery ===" << std::endl;
    PrintRoutingTable(edge1);

    // ── Verify data delivery after recovery ─────────────────────────────
    size_t before_recovery =
        CountReceivedMessages(gw, edge1.address, MessageType::DATA);

    constexpr int kNumMessages = 10;
    for (int i = 0; i < kNumMessages; i++) {
        payload[0] = static_cast<uint8_t>(i + 0x10);
        SendMessage(edge1, gw, payload);
    }

    bool any_received =
        AdvanceTime(superframe_ms * 10, superframe_ms * 10, step_ms, 0, [&]() {
            return CountReceivedMessages(gw, edge1.address, MessageType::DATA) >
                   before_recovery;
        });

    size_t post_recovery =
        CountReceivedMessages(gw, edge1.address, MessageType::DATA) -
        before_recovery;
    std::cout << "Delivered " << post_recovery << "/" << kNumMessages
              << " from Edge1 to GW (after recovery)" << std::endl;

    EXPECT_TRUE(any_received)
        << "Edge1 should deliver DATA to GW after link recovery";
    EXPECT_GE(post_recovery, 1u) << "At least 1 of " << kNumMessages
                                 << " should reach GW after recovery";
}

/**
 * @brief FindNextHop rejects a phantom non-TDMA next_hop.
 *
 * A node outside the network's TDMA schedule is never returned as a
 * next_hop even if it appears in the routing table from overheard
 * broadcasts.
 */
TEST_F(TDMARoutingMismatchTests, PhantomNodeRejectedByTDMACheck) {
    auto& nm = CreateNode("NM", 0x1000, NodeRole::NETWORK_MANAGER);
    auto& gw = CreateNode("GW", 0x2000, NodeRole::NODE_ONLY);
    auto& edge1 = CreateNode("Edge1", 0x3000, NodeRole::NODE_ONLY);
    auto& phantom = CreateNode("Phantom", 0x5000, NodeRole::NETWORK_MANAGER);

    SetLinkStatus(nm, gw, true);
    SetLinkStatus(nm, edge1, true);

    ASSERT_TRUE(StartNode(nm));
    ASSERT_TRUE(StartNode(gw));
    ASSERT_TRUE(StartNode(edge1));

    std::vector<TestNode*> main_nodes = {&nm, &gw, &edge1};
    ASSERT_TRUE(WaitForNetworkFormation(main_nodes, 2));
    ASSERT_TRUE(WaitForRoutingStabilization(main_nodes));

    auto superframe_ms = GetSuperframeDuration(nm);
    auto step_ms = 15u;

    AdvanceTime(superframe_ms * 3, superframe_ms * 3, step_ms, 0,
                [&]() { return false; });

    ASSERT_TRUE(StartNode(phantom));
    SetDirectionalLink(phantom, edge1, true);
    SetLinkStatus(phantom, gw, true);

    AdvanceTime(superframe_ms * 6, superframe_ms * 6, step_ms, 0,
                [&]() { return false; });

    AddressType nh = GetNextHop(edge1, gw.address);
    EXPECT_NE(nh, phantom.address)
        << "Edge1 must NOT route via Phantom (not in TDMA schedule)";

    std::cout << "Edge1 next_hop to GW: 0x" << std::hex << nh << std::dec
              << std::endl;
    PrintRoutingTable(edge1);
}

}  // namespace test
}  // namespace loramesher
