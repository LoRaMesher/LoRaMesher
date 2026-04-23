/**
 * @file role_change_test.cpp
 * @brief Runtime NodeRole change integration tests.
 *
 * Tests LoRaMeshProtocol::RequestNodeRoleChange() — the thread-safe runtime
 * hook exposed by LoraMesher::SetNodeRole(). Covers:
 *
 *  1. NODE_ONLY -> NETWORK_MANAGER before any network exists: the promoted
 *     node creates a network and a peer NODE_ONLY joins.
 *  2. NETWORK_MANAGER -> NODE_ONLY while actively managing: the ex-NM
 *     surrenders; the remaining AUTO-role peer wins election and takes
 *     over; the demoted node joins the new network.
 *  3. Same-role request: no-op, state unchanged.
 *  4. NODE_ONLY -> NETWORK_MANAGER while already in NORMAL_OPERATION
 *     (forced takeover via NM_CLAIM).
 *
 * Mechanism under test:
 *  - LoRaMeshProtocol::RequestNodeRoleChange() stores pending_role_ and
 *    posts ProtocolNotificationType::ROLE_CHANGE_REQUEST.
 *  - Protocol task drains the notification and calls
 *    NetworkService::ApplyRoleChange(), which either CreateNetwork()s
 *    (promotion) or triggers the surrender path (demotion), reusing the
 *    existing NM-merge machinery.
 *
 * Run with:
 *   pio test -e test_native --filter "protocols/lora_mesh/services/test_routing_role_change"
 */

#include <gtest/gtest.h>

#include "../test_routing/routing_test_fixture.hpp"
#include "protocols/lora_mesh/services/network_service.hpp"

namespace loramesher {
namespace test {

using ProtocolState = protocols::lora_mesh::INetworkService::ProtocolState;

class RoleChangeTests : public RoutingTestFixture {
   protected:
    /**
     * @brief Wait until @p node reaches @p state within @p budget_ms.
     */
    bool WaitForState(TestNode& node, ProtocolState state, uint32_t budget_ms) {
        uint32_t step_ms = 15u;
        return AdvanceTime(budget_ms, budget_ms, step_ms, 0, [&]() {
            return node.protocol->GetState() == state;
        });
    }

    /**
     * @brief Wait until exactly one @p nodes member is NETWORK_MANAGER and
     *        all others are NORMAL_OPERATION.
     */
    bool WaitForSingleNM(const std::vector<TestNode*>& nodes,
                         AddressType expected_nm, uint32_t budget_ms) {
        uint32_t step_ms = 15u;
        uint32_t elapsed = 0;
        uint32_t next_print = GetSuperframeDuration(*nodes.front()) * 2;
        return AdvanceTime(budget_ms, budget_ms, step_ms, 0, [&]() {
            elapsed += step_ms;
            int nm_count = 0;
            int normal_count = 0;
            AddressType actual_nm = 0;
            for (auto* node : nodes) {
                auto s = node->protocol->GetState();
                if (s == ProtocolState::NETWORK_MANAGER) {
                    nm_count++;
                    actual_nm = node->address;
                } else if (s == ProtocolState::NORMAL_OPERATION) {
                    normal_count++;
                }
            }
            if (elapsed >= next_print) {
                next_print += GetSuperframeDuration(*nodes.front()) * 2;
                std::cout << "  [t=" << elapsed << "ms]";
                for (auto* node : nodes) {
                    std::cout << " " << node->name << "="
                              << static_cast<int>(node->protocol->GetState())
                              << "(nm=0x" << std::hex
                              << node->protocol->GetNetworkManager() << std::dec
                              << ")";
                }
                std::cout << std::endl;
            }
            if (nm_count != 1)
                return false;
            if (actual_nm != expected_nm)
                return false;
            if (normal_count != static_cast<int>(nodes.size()) - 1)
                return false;
            for (auto* node : nodes) {
                if (node->protocol->GetNetworkManager() != expected_nm) {
                    return false;
                }
            }
            return true;
        });
    }
};

// ---------------------------------------------------------------------------
// Test 1: PromoteNodeOnlyBeforeNetworkExists
//
// Both nodes boot as NODE_ONLY. Neither creates a network (NODE_ONLY nodes
// wait forever in DISCOVERY). After confirming no network formed, promote
// Node1 to NETWORK_MANAGER via RequestNodeRoleChange(). Node1 must create
// the network and Node2 must join as NORMAL_OPERATION.
//
// This is the primary use case ("boot everything as NODE_ONLY, promote one
// when Wi-Fi connects").
// ---------------------------------------------------------------------------

TEST_F(RoleChangeTests, PromoteNodeOnlyBeforeNetworkExists) {
    auto& node1 = CreateNode("Node1", 0x0001, NodeRole::NODE_ONLY);
    auto& node2 = CreateNode("Node2", 0x0002, NodeRole::NODE_ONLY);

    auto* rtos = dynamic_cast<os::RTOSMock*>(&GetRTOS());
    ASSERT_NE(rtos, nullptr);
    rtos->SeedRandom(42);

    SetLinkStatus(node1, node2, true);

    ASSERT_TRUE(StartNode(node1)) << "Failed to start Node1";
    ASSERT_TRUE(StartNode(node2)) << "Failed to start Node2";

    // Give both nodes time to complete discovery. Neither should form a
    // network (NODE_ONLY never creates).
    uint32_t discovery_ms = GetDiscoveryTimeout(node1);
    AdvanceTime(discovery_ms * 2);

    EXPECT_EQ(node1.protocol->GetState(), ProtocolState::DISCOVERY)
        << "NODE_ONLY Node1 should remain in DISCOVERY";
    EXPECT_EQ(node2.protocol->GetState(), ProtocolState::DISCOVERY)
        << "NODE_ONLY Node2 should remain in DISCOVERY";
    EXPECT_EQ(node1.protocol->GetNodeRole(), NodeRole::NODE_ONLY);
    EXPECT_EQ(node2.protocol->GetNodeRole(), NodeRole::NODE_ONLY);

    std::cout << "=== Promoting Node1 to NETWORK_MANAGER ===" << std::endl;
    Result r = node1.protocol->RequestNodeRoleChange(NodeRole::NETWORK_MANAGER);
    ASSERT_TRUE(r.IsSuccess())
        << "RequestNodeRoleChange failed: " << r.GetErrorMessage();

    // The promotion is asynchronous — give the protocol task a tick to drain.
    ASSERT_TRUE(WaitForState(node1, ProtocolState::NETWORK_MANAGER,
                             GetSuperframeDuration(node1) * 3))
        << "Node1 did not become NETWORK_MANAGER";

    EXPECT_EQ(node1.protocol->GetNodeRole(), NodeRole::NETWORK_MANAGER);
    EXPECT_EQ(node1.protocol->GetNetworkManager(), node1.address);

    // Node2 (still NODE_ONLY) should now detect Node1's beacon and join.
    std::vector<TestNode*> all = {&node1, &node2};
    uint32_t budget_ms =
        GetDiscoveryTimeout(node1) * 4 + GetSuperframeDuration(node1) * 6;
    ASSERT_TRUE(WaitForSingleNM(all, node1.address, budget_ms))
        << "Node2 did not join promoted Node1";

    EXPECT_EQ(node2.protocol->GetState(), ProtocolState::NORMAL_OPERATION);
    EXPECT_EQ(node2.protocol->GetNetworkManager(), node1.address);
    EXPECT_EQ(node2.protocol->GetNodeRole(), NodeRole::NODE_ONLY);
}

// ---------------------------------------------------------------------------
// Test 2: DemoteNetworkManagerSurrendersNetwork
//
// NM (NETWORK_MANAGER role) and Peer (AUTO role) form a network. Demote NM
// to NODE_ONLY: the ex-NM enters DISCOVERY with surrendered_in_election_=true
// and stops beaconing. Peer (AUTO) misses beacons, enters FAULT_RECOVERY,
// runs election, wins by default, and creates a new network. The demoted
// ex-NM (now NODE_ONLY) joins the new network.
// ---------------------------------------------------------------------------

TEST_F(RoleChangeTests, DemoteNetworkManagerSurrendersNetwork) {
    auto& nm = CreateNode("NM", 0x0080, NodeRole::NETWORK_MANAGER);
    auto& peer = CreateNode("Peer", 0x0010, NodeRole::AUTO);

    auto* rtos = dynamic_cast<os::RTOSMock*>(&GetRTOS());
    ASSERT_NE(rtos, nullptr);
    rtos->SeedRandom(42);

    SetLinkStatus(nm, peer, true);

    ASSERT_TRUE(StartNode(nm)) << "Failed to start NM";
    ASSERT_TRUE(StartNode(peer)) << "Failed to start Peer";

    std::vector<TestNode*> all = {&nm, &peer};
    ASSERT_TRUE(WaitForNetworkFormation(all, 1))
        << "Initial network did not form";
    ASSERT_EQ(nm.protocol->GetState(), ProtocolState::NETWORK_MANAGER);
    ASSERT_EQ(peer.protocol->GetState(), ProtocolState::NORMAL_OPERATION);

    std::cout << "=== Demoting NM to NODE_ONLY ===" << std::endl;
    Result r = nm.protocol->RequestNodeRoleChange(NodeRole::NODE_ONLY);
    ASSERT_TRUE(r.IsSuccess());

    ASSERT_TRUE(WaitForState(nm, ProtocolState::DISCOVERY,
                             GetSuperframeDuration(nm) * 3))
        << "Demoted NM should enter DISCOVERY";
    EXPECT_EQ(nm.protocol->GetNodeRole(), NodeRole::NODE_ONLY);

    // Peer (AUTO) should detect missed beacons, run election, and become NM.
    uint32_t superframe_ms = GetSuperframeDuration(peer);
    uint32_t discovery_ms = GetDiscoveryTimeout(peer);
    uint32_t budget_ms =
        (protocols::lora_mesh::kMaxNoReceivedSyncBeacons + 6) * superframe_ms +
        discovery_ms * 3;

    ASSERT_TRUE(WaitForSingleNM(all, peer.address, budget_ms))
        << "Peer did not take over as NM after demotion";

    EXPECT_EQ(peer.protocol->GetState(), ProtocolState::NETWORK_MANAGER);
    EXPECT_EQ(nm.protocol->GetState(), ProtocolState::NORMAL_OPERATION)
        << "Demoted ex-NM should have rejoined Peer's new network";
    EXPECT_EQ(nm.protocol->GetNetworkManager(), peer.address);
}

// ---------------------------------------------------------------------------
// Test 3: SameRoleRequestIsNoOp
//
// A NODE_ONLY node requesting NODE_ONLY again must not disturb state.
// A running NM requesting NETWORK_MANAGER must stay NM.
// ---------------------------------------------------------------------------

TEST_F(RoleChangeTests, SameRoleRequestIsNoOp) {
    auto& nm = CreateNode("NM", 0x0001, NodeRole::NETWORK_MANAGER);
    auto& peer = CreateNode("Peer", 0x0002, NodeRole::NODE_ONLY);

    auto* rtos = dynamic_cast<os::RTOSMock*>(&GetRTOS());
    ASSERT_NE(rtos, nullptr);
    rtos->SeedRandom(42);
    SetLinkStatus(nm, peer, true);

    ASSERT_TRUE(StartNode(nm));
    ASSERT_TRUE(StartNode(peer));

    std::vector<TestNode*> all = {&nm, &peer};
    ASSERT_TRUE(WaitForNetworkFormation(all, 1));
    ASSERT_EQ(nm.protocol->GetState(), ProtocolState::NETWORK_MANAGER);

    AddressType nm_addr_before = nm.protocol->GetNetworkManager();

    // Request the same role on both nodes.
    ASSERT_TRUE(nm.protocol->RequestNodeRoleChange(NodeRole::NETWORK_MANAGER)
                    .IsSuccess());
    ASSERT_TRUE(
        peer.protocol->RequestNodeRoleChange(NodeRole::NODE_ONLY).IsSuccess());

    AdvanceTime(GetSuperframeDuration(nm) * 3);

    EXPECT_EQ(nm.protocol->GetState(), ProtocolState::NETWORK_MANAGER);
    EXPECT_EQ(peer.protocol->GetState(), ProtocolState::NORMAL_OPERATION);
    EXPECT_EQ(nm.protocol->GetNetworkManager(), nm_addr_before);
    EXPECT_EQ(peer.protocol->GetNetworkManager(), nm_addr_before);
    EXPECT_EQ(nm.protocol->GetNodeRole(), NodeRole::NETWORK_MANAGER);
    EXPECT_EQ(peer.protocol->GetNodeRole(), NodeRole::NODE_ONLY);
}

// ---------------------------------------------------------------------------
// Test 4: ForcedPromotionInNormalOperationUnseatsIncumbent
//
// A NODE_ONLY node already joined to an incumbent NM requests promotion to
// NETWORK_MANAGER. The node sends an NM_CLAIM from its DISCOVERY_TX slot;
// the incumbent compares priorities (our new role gives a lower priority
// value because NETWORK_MANAGER role has base 0-63) and yields via the
// standard NM-merge path. The former NODE_ONLY ends up NETWORK_MANAGER.
//
// Address design:
//   incumbent=0x0080 (NETWORK_MANAGER role) -> priority = 0 + (0x80>>1) = 64
//   challenger=0x0010 (NODE_ONLY at boot)   -> priority after promotion =
//       0 + (0x10>>1) = 8  (beats 64)
// ---------------------------------------------------------------------------

TEST_F(RoleChangeTests, ForcedPromotionInNormalOperationUnseatsIncumbent) {
    auto& incumbent =
        CreateNode("Incumbent", 0x0080, NodeRole::NETWORK_MANAGER);
    auto& challenger = CreateNode("Challenger", 0x0010, NodeRole::NODE_ONLY);

    auto* rtos = dynamic_cast<os::RTOSMock*>(&GetRTOS());
    ASSERT_NE(rtos, nullptr);
    rtos->SeedRandom(42);
    SetLinkStatus(incumbent, challenger, true);

    ASSERT_TRUE(StartNode(incumbent));
    ASSERT_TRUE(StartNode(challenger));

    std::vector<TestNode*> all = {&incumbent, &challenger};
    ASSERT_TRUE(WaitForNetworkFormation(all, 1))
        << "Initial network failed to form with incumbent NM";
    ASSERT_EQ(incumbent.protocol->GetState(), ProtocolState::NETWORK_MANAGER);
    ASSERT_EQ(challenger.protocol->GetState(), ProtocolState::NORMAL_OPERATION);

    std::cout << "=== Challenger requests promotion while in NORMAL_OPERATION "
                 "==="
              << std::endl;
    Result r =
        challenger.protocol->RequestNodeRoleChange(NodeRole::NETWORK_MANAGER);
    ASSERT_TRUE(r.IsSuccess());

    // After the claim exchange, challenger wins and incumbent yields +
    // rejoins the challenger's network.
    uint32_t superframe_ms = GetSuperframeDuration(incumbent);
    uint32_t discovery_ms = GetDiscoveryTimeout(incumbent);
    uint32_t budget_ms =
        12 * superframe_ms + discovery_ms * 3 +
        (protocols::lora_mesh::kMaxNoReceivedSyncBeacons + 4) * superframe_ms;

    ASSERT_TRUE(WaitForSingleNM(all, challenger.address, budget_ms))
        << "Challenger did not unseat incumbent";

    EXPECT_EQ(challenger.protocol->GetState(), ProtocolState::NETWORK_MANAGER);
    EXPECT_EQ(incumbent.protocol->GetState(), ProtocolState::NORMAL_OPERATION);
    EXPECT_EQ(challenger.protocol->GetNodeRole(), NodeRole::NETWORK_MANAGER);
}

}  // namespace test
}  // namespace loramesher
