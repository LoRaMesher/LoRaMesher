/**
 * @file nm_merge_test.cpp
 * @brief Network merge tests — Path A (lite merge via NM_ELECTION extension)
 *
 * Tests the Path-A lite-merge mechanism described in docs/network_merge_design.md.
 * Two independent networks that come within radio range merge into a single network
 * governed by the NM with the lower election priority (= higher priority value is worse).
 *
 * Mechanism under test:
 *   1. ProcessSyncBeacon() in NETWORK_MANAGER state detects a foreign-network beacon
 *      (beacon_network_id != network_id_) and calls HandleForeignBeacon().
 *   2. HandleForeignBeacon() broadcasts an NM_CLAIM carrying our election_priority_.
 *   3. ProcessNMClaim() in NETWORK_MANAGER state: if the incoming claim has a lower
 *      (= higher) priority value, we yield by calling StartDiscovery(); our nodes
 *      lose their sync beacon, enter FAULT_RECOVERY, and eventually rejoin.
 *   4. NETWORK_MANAGER-role nodes (priority 0–63) always beat AUTO-role nodes (64–191).
 *      Within the same role, lower node address → lower priority value → wins.
 *
 * Priority formula (ComputeElectionPriority()):
 *   NETWORK_MANAGER role: base = 0,  addr_component = (addr & 0xFF) >> 1
 *   AUTO role:            base = 64, addr_component = (addr & 0xFF) >> 1
 *   priority = min(base + addr_component, 0xFE)
 *
 * Known limitation (Path A): interior secondary nodes that cannot hear the primary
 * network's SYNC_BEACON directly will need to rejoin via FAULT_RECOVERY + DISCOVERY.
 * These tests enable full-mesh links after the bridge so all nodes can hear the primary
 * NM directly, avoiding the interior-node limitation.
 *
 * TDMA note: Each independent network has its own superframe phase.  After the bridge
 * is enabled, the test waits up to ~10 superframe durations for the two superframe
 * phases to align enough that one NM's SYNC_BEACON_TX slot lands in the other NM's
 * CONTROL_RX or DISCOVERY_RX slot.
 *
 * Run with:
 *   pio test -e test_native --filter "protocols/lora_mesh/services/test_routing_nm_merge"
 */

#include <gtest/gtest.h>

#include "../test_routing/routing_test_fixture.hpp"
#include "protocols/lora_mesh/services/network_service.hpp"

namespace loramesher {
namespace test {

using ProtocolState = protocols::lora_mesh::INetworkService::ProtocolState;

/**
 * @brief Test suite for network-merge functionality (Path A lite merge).
 */
class NMMergeTests : public RoutingTestFixture {
   protected:
    /**
     * @brief Budget required for a complete two-network merge.
     *
     * Covers:
     *  - TDMA detection window (up to ~10 superframes for phase alignment)
     *  - NM_CLAIM exchange + yielding NM enters DISCOVERY (2 superframes)
     *  - Yielding NM rejoins primary network (3 superframes)
     *  - Interior nodes lose sync beacon → FAULT_RECOVERY → DISCOVERY → rejoin
     *    (kMaxNoReceivedSyncBeacons + GetDiscoveryTimeout + 3 extra superframes)
     *
     * @param superframe_ms Superframe duration in milliseconds
     * @param discovery_timeout_ms Discovery timeout (from GetDiscoveryTimeout)
     * @return uint32_t Generous virtual-time budget in ms
     */
    uint32_t MergeBudgetMs(uint32_t superframe_ms,
                           uint32_t discovery_timeout_ms) const {
        uint32_t detection_ms = 12 * superframe_ms;   // TDMA phase alignment
        uint32_t claim_yield_ms = 3 * superframe_ms;  // NM_CLAIM + yield
        uint32_t nm_rejoin_ms = 4 * superframe_ms;    // Yielding NM rejoins
        // Interior nodes: miss beacons → FAULT_RECOVERY → DISCOVERY → join
        uint32_t interior_ms =
            (protocols::lora_mesh::kMaxNoReceivedSyncBeacons + 2) *
                superframe_ms +
            discovery_timeout_ms + 4 * superframe_ms;
        return detection_ms + claim_yield_ms + nm_rejoin_ms + interior_ms;
    }

    /**
     * @brief Wait until all @p nodes are in a single merged network.
     *
     * Polls until exactly one node is NETWORK_MANAGER and all others
     * are NORMAL_OPERATION, within MergeBudgetMs().
     *
     * @param nodes All nodes expected to participate (both networks combined)
     * @param expected_winner_address Address of the node expected to win
     * @return true if all nodes merged within the budget
     */
    bool WaitForMerge(const std::vector<TestNode*>& nodes,
                      AddressType expected_winner_address) {
        auto& ref = *nodes.front();
        uint32_t superframe_ms = GetSuperframeDuration(ref);
        uint32_t discovery_ms = GetDiscoveryTimeout(ref);
        uint32_t budget_ms = MergeBudgetMs(superframe_ms, discovery_ms);
        uint32_t slot_ms = GetSlotDuration(ref);
        uint32_t step_ms = 15u;

        int expected_normal = static_cast<int>(nodes.size()) - 1;

        uint32_t elapsed = 0;
        uint32_t print_every = superframe_ms * 2;
        uint32_t next_print = print_every;

        return AdvanceTime(budget_ms, budget_ms, step_ms, 0, [&]() {
            elapsed += step_ms;
            int nm_count = 0;
            int normal_count = 0;
            AddressType actual_nm = 0;

            for (auto* node : nodes) {
                auto s = node->protocol->GetState();
                if (s == ProtocolState::NETWORK_MANAGER) {
                    nm_count++;
                    actual_nm = node->protocol->GetNetworkManager();
                }
                if (s == ProtocolState::NORMAL_OPERATION)
                    normal_count++;
            }

            if (elapsed >= next_print) {
                next_print += print_every;
                std::cout << "  [t=" << elapsed << "ms] states:";
                for (auto* node : nodes) {
                    std::cout << " " << node->name << "="
                              << static_cast<int>(node->protocol->GetState())
                              << "(nm=0x" << std::hex
                              << node->protocol->GetNetworkManager() << std::dec
                              << ")";
                }
                std::cout << " (nm=" << nm_count << ",norm=" << normal_count
                          << ")" << std::endl;
            }

            // All nodes must be in the merged network with the correct NM.
            // We check GetNetworkManager() for nodes already in stable states
            // (NETWORK_MANAGER or NORMAL_OPERATION) to avoid counting transient
            // JOINING/FAULT_RECOVERY nodes that don't yet have the final NM.
            bool all_nm_correct = true;
            for (auto* node : nodes) {
                auto s = node->protocol->GetState();
                if (s == ProtocolState::NETWORK_MANAGER ||
                    s == ProtocolState::NORMAL_OPERATION) {
                    if (node->protocol->GetNetworkManager() !=
                        expected_winner_address) {
                        all_nm_correct = false;
                        break;
                    }
                }
            }

            return nm_count == 1 && normal_count == expected_normal &&
                   actual_nm == expected_winner_address && all_nm_correct;
        });
    }
};

// ---------------------------------------------------------------------------
// Test 1: BasicNetworkMerge
//
// Two 2-node networks are initially isolated; after the bridge is enabled (full
// mesh) the lower-priority NM yields and all 4 nodes converge in one network.
//
// Priority design:
//   NM_A (0x0001, NETWORK_MANAGER role): priority = 0 + (0x01>>1) = 0  (wins)
//   NM_B (0x0080, NETWORK_MANAGER role): priority = 0 + (0x80>>1) = 64 (yields)
// ---------------------------------------------------------------------------

/**
 * @brief Two NETWORK_MANAGER-role networks merge; lower-address NM wins.
 *
 * Verifies:
 * - NM_A detects NM_B's foreign beacon → broadcasts NM_CLAIM (priority 0)
 * - NM_B detects NM_A's foreign beacon → broadcasts NM_CLAIM (priority 64)
 * - NM_B receives NM_A's NM_CLAIM (priority 0 < 64) → yields → StartDiscovery
 * - NM_B rejoins NM_A as NORMAL_OPERATION
 * - NodeB loses sync beacon → FAULT_RECOVERY → DISCOVERY → rejoins NM_A
 * - Final state: NM_A is NETWORK_MANAGER, others are NORMAL_OPERATION
 */
TEST_F(NMMergeTests, BasicNetworkMerge) {
    // --- Network A: NM_A (address 0x0001) + NodeA ---
    auto& nm_a = CreateNode("NM_A", 0x0001, NodeRole::NETWORK_MANAGER);
    auto& node_a = CreateNode("NodeA", 0x0002, NodeRole::NODE_ONLY);

    // --- Network B: NM_B (address 0x0080) + NodeB ---
    auto& nm_b = CreateNode("NM_B", 0x0080, NodeRole::NETWORK_MANAGER);
    auto& node_b = CreateNode("NodeB", 0x0081, NodeRole::NODE_ONLY);

    // Deterministic RNG: both NMs get 16 slots (kMinSlots), so the phase
    // offset is permanent. A fixed seed makes the offset reproducible.
    auto* rtos = dynamic_cast<os::RTOSMock*>(&GetRTOS());
    ASSERT_NE(rtos, nullptr);
    rtos->SeedRandom(42);

    // Phase 1: Isolate both networks — no cross-links
    SetLinkStatus(nm_a, node_a, true);
    SetLinkStatus(nm_b, node_b, true);
    SetLinkStatus(nm_a, nm_b, false);
    SetLinkStatus(nm_a, node_b, false);
    SetLinkStatus(node_a, nm_b, false);
    SetLinkStatus(node_a, node_b, false);

    // Phase 2: Form Network A first (NM_B and NodeB not started yet).
    ASSERT_TRUE(StartNode(nm_a)) << "Failed to start NM_A";
    ASSERT_TRUE(StartNode(node_a)) << "Failed to start NodeA";

    std::vector<TestNode*> net_a = {&nm_a, &node_a};
    ASSERT_TRUE(WaitForNetworkFormation(net_a, 1))
        << "Network A failed to form";

    // Align NM_B's start so its SYNC_BEACON TX lands in NM_A's DISCOVERY_RX
    // range (last 4 slots). Both NMs share kMinSlots=16, so the phase offset
    // is permanent — it must be correct from the start.
    uint32_t slot_ms = GetSlotDuration(nm_a);
    uint32_t sf_a = GetSuperframeDuration(nm_a);
    uint16_t current_slot = nm_a.protocol->GetCurrentSlot();
    uint16_t total_slots = sf_a / slot_ms;
    uint16_t target_slot = total_slots - 3;  // middle of DISCOVERY_RX
    uint32_t advance =
        ((target_slot - current_slot + total_slots) % total_slots) * slot_ms;
    if (advance < slot_ms * 2)
        advance += sf_a;
    AdvanceTime(advance);

    // Now start Network B — its superframe phase is aligned with Network A.
    ASSERT_TRUE(StartNode(nm_b)) << "Failed to start NM_B";
    ASSERT_TRUE(StartNode(node_b)) << "Failed to start NodeB";

    std::vector<TestNode*> net_b = {&nm_b, &node_b};
    ASSERT_TRUE(WaitForNetworkFormation(net_b, 1))
        << "Network B failed to form";

    EXPECT_EQ(nm_a.protocol->GetState(), ProtocolState::NETWORK_MANAGER)
        << "NM_A should be NETWORK_MANAGER after initial formation";
    EXPECT_EQ(nm_b.protocol->GetState(), ProtocolState::NETWORK_MANAGER)
        << "NM_B should be NETWORK_MANAGER after initial formation";

    std::cout << "=== Both networks formed. Enabling bridge (full mesh). ==="
              << std::endl;

    // NM_A: role_base=0, addr_component=(0x01>>1)=0 → priority=0
    // NM_B: role_base=0, addr_component=(0x80>>1)=64 → priority=64
    // NM_A wins (0 < 64)

    // Phase 3: Enable bridge — full mesh so all nodes can hear each other
    SetLinkStatus(nm_a, nm_b, true);
    SetLinkStatus(nm_a, node_b, true);
    SetLinkStatus(node_a, nm_b, true);
    SetLinkStatus(node_a, node_b, true);

    // Phase 4: Wait for merge — NM_A should be the sole NETWORK_MANAGER
    std::vector<TestNode*> all_nodes = {&nm_a, &node_a, &nm_b, &node_b};
    bool merged = WaitForMerge(all_nodes, nm_a.address);

    std::cout << "=== Post-merge states ===" << std::endl;
    for (auto* node : all_nodes) {
        std::cout << "  " << node->name << " (0x" << std::hex << node->address
                  << std::dec
                  << "): state=" << static_cast<int>(node->protocol->GetState())
                  << "  NM=0x" << std::hex
                  << node->protocol->GetNetworkManager() << std::dec
                  << std::endl;
    }

    ASSERT_TRUE(merged)
        << "Networks did not merge within timeout. Final states: "
        << "NM_A=" << static_cast<int>(nm_a.protocol->GetState())
        << " NodeA=" << static_cast<int>(node_a.protocol->GetState())
        << " NM_B=" << static_cast<int>(nm_b.protocol->GetState())
        << " NodeB=" << static_cast<int>(node_b.protocol->GetState());

    // Verify final state
    EXPECT_EQ(nm_a.protocol->GetState(), ProtocolState::NETWORK_MANAGER)
        << "NM_A (priority 0) must remain NETWORK_MANAGER";
    EXPECT_EQ(node_a.protocol->GetState(), ProtocolState::NORMAL_OPERATION)
        << "NodeA must be NORMAL_OPERATION in merged network";
    EXPECT_EQ(nm_b.protocol->GetState(), ProtocolState::NORMAL_OPERATION)
        << "NM_B (priority 64) must have yielded to NORMAL_OPERATION";
    EXPECT_EQ(node_b.protocol->GetState(), ProtocolState::NORMAL_OPERATION)
        << "NodeB must have rejoined merged network";

    // Verify all nodes agree on the NM
    for (auto* node : all_nodes) {
        EXPECT_EQ(node->protocol->GetNetworkManager(), nm_a.address)
            << node->name << " should recognise NM_A (0x" << std::hex
            << nm_a.address << std::dec << ") as network manager";
    }
}

// ---------------------------------------------------------------------------
// Test 2: AutoRoleNMYieldsToConfiguredNM
//
// Primary: NETWORK_MANAGER role node (priority 0–63, always beats AUTO).
// Secondary: AUTO role node that created its own network (priority 64–191).
//
// Priority design:
//   NM_A (0x0001, NETWORK_MANAGER role): priority = 0 + (0x01>>1) = 0  (wins)
//   NM_B (0x0001, AUTO role)  — wait, addresses must differ
//   NM_A (0x0002, NETWORK_MANAGER role): priority = 0 + (0x02>>1) = 1  (wins)
//   NM_B (0x0001, AUTO role):            priority = 64 + (0x01>>1) = 64 (yields)
// ---------------------------------------------------------------------------

/**
 * @brief NETWORK_MANAGER-role NM always beats an AUTO-role NM regardless of address.
 *
 * Sets up a secondary network whose NM is an AUTO-role node that created a network
 * (via DISCOVERY timeout, no other NMs present). When the bridge is enabled,
 * the NETWORK_MANAGER-role primary wins regardless of address ordering.
 *
 * Verifies:
 * - NM_A (NETWORK_MANAGER role, address 0x0002, priority 1) wins
 * - NM_B (AUTO role, address 0x0001, priority 64) yields even though its address
 *   is numerically lower (demonstrates role beats address)
 * - All nodes end up under NM_A after merge
 */
TEST_F(NMMergeTests, AutoRoleNMYieldsToConfiguredNM) {
    // NM_A: configured NETWORK_MANAGER role, higher address
    // NM_A priority = 0 + (0x02>>1) = 1
    auto& nm_a = CreateNode("NM_A", 0x0002, NodeRole::NETWORK_MANAGER);
    auto& node_a = CreateNode("NodeA", 0x0003, NodeRole::NODE_ONLY);

    // NM_B: AUTO role — will create its own network when alone during discovery
    // NM_B priority = 64 + (0x01>>1) = 64
    auto& nm_b = CreateNode("NM_B", 0x0001, NodeRole::AUTO);
    auto& node_b = CreateNode("NodeB", 0x0004, NodeRole::NODE_ONLY);

    // Deterministic RNG: both NMs get 16 slots (kMinSlots), so the phase
    // offset is permanent. A fixed seed makes the offset reproducible.
    auto* rtos = dynamic_cast<os::RTOSMock*>(&GetRTOS());
    ASSERT_NE(rtos, nullptr);
    rtos->SeedRandom(42);

    // Phase 1: Isolate both networks — no cross-links
    SetLinkStatus(nm_a, node_a, true);
    SetLinkStatus(nm_b, node_b, true);
    SetLinkStatus(nm_a, nm_b, false);
    SetLinkStatus(nm_a, node_b, false);
    SetLinkStatus(node_a, nm_b, false);
    SetLinkStatus(node_a, node_b, false);

    // Form Network A first.
    ASSERT_TRUE(StartNode(nm_a)) << "Failed to start NM_A";
    ASSERT_TRUE(StartNode(node_a)) << "Failed to start NodeA";

    std::vector<TestNode*> net_a = {&nm_a, &node_a};
    ASSERT_TRUE(WaitForNetworkFormation(net_a, 1))
        << "Network A failed to form";

    // Align NM_B's start so its SYNC_BEACON TX lands in NM_A's DISCOVERY_RX
    // range (last 4 slots). Both NMs share kMinSlots=16, so the phase offset
    // is permanent — it must be correct from the start.
    uint32_t slot_ms = GetSlotDuration(nm_a);
    uint32_t sf_a = GetSuperframeDuration(nm_a);
    uint16_t current_slot = nm_a.protocol->GetCurrentSlot();
    uint16_t total_slots = sf_a / slot_ms;
    uint16_t target_slot = total_slots - 3;
    uint32_t advance =
        ((target_slot - current_slot + total_slots) % total_slots) * slot_ms;
    if (advance < slot_ms * 2)
        advance += sf_a;
    AdvanceTime(advance);

    ASSERT_TRUE(StartNode(nm_b)) << "Failed to start NM_B";
    ASSERT_TRUE(StartNode(node_b)) << "Failed to start NodeB";

    // Phase 2: Wait for both networks to form
    std::vector<TestNode*> net_b = {&nm_b, &node_b};
    ASSERT_TRUE(WaitForNetworkFormation(net_b, 1))
        << "Network B failed to form";

    EXPECT_EQ(nm_a.protocol->GetState(), ProtocolState::NETWORK_MANAGER)
        << "NM_A (NETWORK_MANAGER role) must be NETWORK_MANAGER";
    EXPECT_EQ(nm_b.protocol->GetState(), ProtocolState::NETWORK_MANAGER)
        << "NM_B (AUTO, won election by being alone) must be NETWORK_MANAGER";

    std::cout << "=== Both networks formed. Enabling bridge (full mesh). ==="
              << std::endl;
    std::cout << "  NM_A addr=0x" << std::hex << nm_a.address
              << " (NETWORK_MANAGER role, priority≈1)" << std::dec << std::endl;
    std::cout << "  NM_B addr=0x" << std::hex << nm_b.address
              << " (AUTO role, priority≈64)" << std::dec << std::endl;

    // Phase 3: Enable bridge — full mesh
    SetLinkStatus(nm_a, nm_b, true);
    SetLinkStatus(nm_a, node_b, true);
    SetLinkStatus(node_a, nm_b, true);
    SetLinkStatus(node_a, node_b, true);

    // Phase 4: Wait for merge — NM_A (role priority 1) must win over NM_B (64)
    std::vector<TestNode*> all_nodes = {&nm_a, &node_a, &nm_b, &node_b};
    bool merged = WaitForMerge(all_nodes, nm_a.address);

    std::cout << "=== Post-merge states ===" << std::endl;
    for (auto* node : all_nodes) {
        std::cout << "  " << node->name << " (0x" << std::hex << node->address
                  << std::dec
                  << "): state=" << static_cast<int>(node->protocol->GetState())
                  << "  NM=0x" << std::hex
                  << node->protocol->GetNetworkManager() << std::dec
                  << std::endl;
    }

    ASSERT_TRUE(merged)
        << "Networks did not merge within timeout. "
        << "NM_A=" << static_cast<int>(nm_a.protocol->GetState())
        << " NodeA=" << static_cast<int>(node_a.protocol->GetState())
        << " NM_B=" << static_cast<int>(nm_b.protocol->GetState())
        << " NodeB=" << static_cast<int>(node_b.protocol->GetState());

    EXPECT_EQ(nm_a.protocol->GetState(), ProtocolState::NETWORK_MANAGER)
        << "NM_A (NETWORK_MANAGER role, priority 1) must remain NM";
    EXPECT_EQ(nm_b.protocol->GetState(), ProtocolState::NORMAL_OPERATION)
        << "NM_B (AUTO role, priority 64) must yield even with lower address";
    EXPECT_EQ(node_a.protocol->GetState(), ProtocolState::NORMAL_OPERATION)
        << "NodeA must be NORMAL_OPERATION";
    EXPECT_EQ(node_b.protocol->GetState(), ProtocolState::NORMAL_OPERATION)
        << "NodeB must have rejoined merged network";

    for (auto* node : all_nodes) {
        EXPECT_EQ(node->protocol->GetNetworkManager(), nm_a.address)
            << node->name << " should recognise NM_A as the merged NM";
    }
}

}  // namespace test
}  // namespace loramesher
