/**
 * @file sync_beacon_subslot_test.cpp
 * @brief Experiment: SYNC_BEACON subslot-assignment strategy comparison.
 *
 * Two layers of evidence:
 *
 * 1. Unit tests on the assignment mechanism (deterministic). They prove the
 *    collision *condition*: under ADDRESS_MODULO, addresses congruent modulo the
 *    subslot count map to the same subslot every superframe, whereas the
 *    ADDRESS_HASH mix makes such addresses diverge across superframes. RANDOM
 *    has the same self-healing property by construction (fresh draw per frame).
 *
 * 2. Integration tests on the diamond topology (NM--{A,B}--C, no direct NM--C
 *    link). A and B are C's only beacon sources. These show that RANDOM and
 *    ADDRESS_HASH let C join, and that ADDRESS_MODULO is collision-free when
 *    addresses differ modulo n.
 *
 *    NOTE: the integration simulator models collisions (overlapping on-air
 *    windows at a receiver are dropped) but does NOT model the tight
 *    time-alignment of two independently-scheduled forwarders: their virtual
 *    send times never coincide closely enough to overlap, so the permanent
 *    same-subslot starvation does not reproduce at the system level here. The
 *    assignment-level collision condition is therefore established by the unit
 *    tests, not by the simulator.
 */

#include <gtest/gtest.h>

#include <functional>

#include "../test_routing/routing_test_fixture.hpp"
#include "protocols/lora_mesh/services/subslot_scheduler.hpp"

namespace loramesher {
namespace test {

namespace {
using ProtocolState = protocols::lora_mesh::INetworkService::ProtocolState;
using SubslotAssignment = protocols::lora_mesh::SubslotAssignment;
using protocols::lora_mesh::SubslotConfig;
using protocols::lora_mesh::SubslotScheduler;

constexpr uint8_t kSubslots = 5;

// Subslot a node lands in under ADDRESS_MODULO (frame-independent).
uint8_t AddressModuloSubslot(uint16_t address) {
    return address % kSubslots;
}

// Subslot a node lands in under ADDRESS_HASH for a given superframe.
uint8_t AddressHashSubslot(uint16_t address, uint32_t frame) {
    return SubslotScheduler::MixAddressFrame(address, frame) % kSubslots;
}
}  // namespace

// ===========================================================================
// Assignment mechanism (deterministic unit tests)
// ===========================================================================

// 0x0005 and 0x0041 differ by 60 = LCM(1..5), so they are congruent for every
// subslot count <= 5. Under ADDRESS_MODULO they share a subslot regardless of
// how the effective count collapses with time-on-air -- the collision
// condition is permanent.
TEST(SyncBeaconSubslotAssignment,
     AddressModuloCongruentAddressesAlwaysCollide) {
    for (uint8_t n = 1; n <= kSubslots; ++n) {
        EXPECT_EQ(0x0005u % n, 0x0041u % n)
            << "Congruent addresses must share a subslot for n=" << int(n);
    }
}

// The ADDRESS_HASH mix makes the same congruent addresses occupy different
// subslots in the large majority of superframes -- the collisions self-heal.
TEST(SyncBeaconSubslotAssignment, AddressHashCongruentAddressesDiverge) {
    constexpr uint32_t kFrames = 50;
    uint32_t address_modulo_diffs = 0;
    uint32_t address_hash_diffs = 0;
    for (uint32_t frame = 0; frame < kFrames; ++frame) {
        if (AddressModuloSubslot(0x0005) != AddressModuloSubslot(0x0041)) {
            ++address_modulo_diffs;
        }
        if (AddressHashSubslot(0x0005, frame) !=
            AddressHashSubslot(0x0041, frame)) {
            ++address_hash_diffs;
        }
    }
    // Address-modulo never separates congruent addresses.
    EXPECT_EQ(address_modulo_diffs, 0u);
    // The hash separates them in most frames (expected ~ (n-1)/n = 80%).
    EXPECT_GT(address_hash_diffs, kFrames / 2)
        << "ADDRESS_HASH should de-correlate congruent addresses across frames";
}

// A single node walks a varied sequence of subslots under ADDRESS_HASH, so a
// collision in one superframe does not recur deterministically.
TEST(SyncBeaconSubslotAssignment, AddressHashVariesAcrossFrames) {
    bool seen[kSubslots] = {false};
    uint32_t distinct = 0;
    for (uint32_t frame = 0; frame < 50; ++frame) {
        uint8_t s = AddressHashSubslot(0x0005, frame);
        if (!seen[s]) {
            seen[s] = true;
            ++distinct;
        }
    }
    EXPECT_GE(distinct, 3u)
        << "A node should rotate through multiple subslots over time";
}

// ===========================================================================
// System behavior (integration tests on the diamond topology)
// ===========================================================================

class SyncBeaconSubslotTests : public RoutingTestFixture {
   protected:
    /// Builds a config customizer that overrides the sync-beacon subslot
    /// strategy while leaving every other default in place.
    static std::function<void(LoRaMeshProtocolConfig&)> WithSyncStrategy(
        SubslotAssignment strategy) {
        return [strategy](LoRaMeshProtocolConfig& config) {
            auto sync_config = config.getSyncBeaconSubslotConfig();
            sync_config.strategy = strategy;
            config.setSyncBeaconSubslotConfig(sync_config);
        };
    }

    /// Creates the NM--{A,B}--C diamond with the given strategy and addresses.
    /// Returns the four nodes in order {NM, A, B, C}.
    ///
    /// When `start_c` is false, C is created and linked but NOT started, so the
    /// caller can first establish A and B as NORMAL_OPERATION forwarders before
    /// C tries to join. This is required to expose same-subslot collisions: if
    /// all four start together, C synchronizes from whichever forwarder reaches
    /// NORMAL_OPERATION first (only nodes in NORMAL_OPERATION forward beacons),
    /// before both A and B are forwarding simultaneously.
    std::vector<TestNode*> BuildDiamond(SubslotAssignment strategy,
                                        AddressType addr_a, AddressType addr_b,
                                        bool start_c = true) {
        auto customizer = WithSyncStrategy(strategy);
        auto& nm = CreateNode("NM", 0x0001, NodeRole::NETWORK_MANAGER,
                              PinConfig(), RadioConfig(), customizer);
        auto& a = CreateNode("A", addr_a, NodeRole::NODE_ONLY, PinConfig(),
                             RadioConfig(), customizer);
        auto& b = CreateNode("B", addr_b, NodeRole::NODE_ONLY, PinConfig(),
                             RadioConfig(), customizer);
        auto& c = CreateNode("C", 0x0002, NodeRole::NODE_ONLY, PinConfig(),
                             RadioConfig(), customizer);

        // Diamond links: NM--A, NM--B, A--C, B--C. No NM--C, no A--B.
        SetLinkStatus(nm, a, true);
        SetLinkStatus(nm, b, true);
        SetLinkStatus(a, c, true);
        SetLinkStatus(b, c, true);

        StartNode(nm);
        StartNode(a);
        StartNode(b);
        if (start_c) {
            StartNode(c);
        }

        return {&nm, &a, &b, &c};
    }

    /// Brings up NM, A and B together, waits until both forwarders are in
    /// NORMAL_OPERATION, then starts C. Returns the four nodes {NM, A, B, C}.
    std::vector<TestNode*> BuildDiamondEstablishedForwarders(
        SubslotAssignment strategy, AddressType addr_a, AddressType addr_b) {
        auto nodes = BuildDiamond(strategy, addr_a, addr_b, /*start_c=*/false);
        EXPECT_TRUE(WaitForNormalOperation(*nodes[1], 25))
            << "Forwarder A should join via the NM";
        EXPECT_TRUE(WaitForNormalOperation(*nodes[2], 25))
            << "Forwarder B should join via the NM";
        StartNode(*nodes[3]);
        return nodes;
    }

    /// Advances time until `node` reaches NORMAL_OPERATION or the budget of
    /// `superframes` elapses. Returns true if it reached NORMAL_OPERATION.
    bool WaitForNormalOperation(TestNode& node, uint32_t superframes) {
        uint32_t budget = GetSuperframeDuration(node) * superframes;
        return AdvanceTime(budget, budget, 25u, 0, [&]() {
            return node.protocol->GetState() == ProtocolState::NORMAL_OPERATION;
        });
    }
};

/**
 * Pure-random subslot lets the downstream node join.
 */
TEST_F(SyncBeaconSubslotTests, RandomCongruentAddressesDownstreamJoins) {
    auto nodes = BuildDiamond(SubslotAssignment::RANDOM, 0x0005, 0x0041);
    TestNode& c = *nodes[3];

    EXPECT_TRUE(WaitForNormalOperation(c, 40))
        << "With RANDOM subslots C should hear a clean beacon and join";
    EXPECT_EQ(c.protocol->GetState(), ProtocolState::NORMAL_OPERATION);
}

/**
 * Hybrid (nonlinear hash of address + superframe) also lets C join, without
 * relying on hardware entropy.
 */
TEST_F(SyncBeaconSubslotTests, AddressHashCongruentAddressesDownstreamJoins) {
    auto nodes = BuildDiamond(SubslotAssignment::ADDRESS_HASH, 0x0005, 0x0041);
    TestNode& c = *nodes[3];

    EXPECT_TRUE(WaitForNormalOperation(c, 40))
        << "With ADDRESS_HASH subslots C should hear a clean beacon and join";
    EXPECT_EQ(c.protocol->GetState(), ProtocolState::NORMAL_OPERATION);
}

/**
 * Control: with distinct-modulo addresses, address-modulo is collision-free at
 * C and C joins normally.
 */
TEST_F(SyncBeaconSubslotTests, AddressModuloDistinctAddressesNoCollision) {
    // 0x0005 % 5 == 0, 0x0006 % 5 == 1 -> A and B occupy different subslots.
    auto nodes =
        BuildDiamond(SubslotAssignment::ADDRESS_MODULO, 0x0005, 0x0006);
    TestNode& c = *nodes[3];

    EXPECT_TRUE(WaitForNormalOperation(c, 40))
        << "Distinct-modulo addresses must not collide; C should join";
    EXPECT_EQ(c.protocol->GetState(), ProtocolState::NORMAL_OPERATION);
    EXPECT_EQ(virtual_network_.GetCollisionCount(c.address), 0u)
        << "Address-modulo must be collision-free when addresses differ mod n";
}

// ---------------------------------------------------------------------------
// Reproduction: both forwarders established before C joins.
//
// With A and B both in NORMAL_OPERATION and phase-aligned to the NM, two
// congruent-address forwarders transmit in the same subslot at the same time
// every superframe. Their on-air windows overlap at C and the simulator drops
// both, so C never obtains a clean beacon and cannot join.
// ---------------------------------------------------------------------------

/**
 * The reproduction of the starvation defect under ADDRESS_MODULO.
 */
TEST_F(SyncBeaconSubslotTests, AddressModuloCongruentStarvesLateJoiner) {
    auto nodes = BuildDiamondEstablishedForwarders(
        SubslotAssignment::ADDRESS_MODULO, 0x0005, 0x0041);
    TestNode& c = *nodes[3];

    EXPECT_FALSE(WaitForNormalOperation(c, 30))
        << "C must NOT join: A and B collide in the same subslot every frame";
    EXPECT_NE(c.protocol->GetState(), ProtocolState::NORMAL_OPERATION);
    EXPECT_GT(virtual_network_.GetCollisionCount(c.address), 0u)
        << "C should be observing beacon collisions";
}

/**
 * RANDOM resolves the starvation: the colliding forwarders reshuffle and C
 * eventually hears a clean beacon.
 */
TEST_F(SyncBeaconSubslotTests, RandomCongruentLetsLateJoinerJoin) {
    auto nodes = BuildDiamondEstablishedForwarders(SubslotAssignment::RANDOM,
                                                   0x0005, 0x0041);
    TestNode& c = *nodes[3];

    EXPECT_TRUE(WaitForNormalOperation(c, 40))
        << "With RANDOM subslots the late joiner should still join";
    EXPECT_EQ(c.protocol->GetState(), ProtocolState::NORMAL_OPERATION);
}

/**
 * ADDRESS_HASH resolves the starvation deterministically.
 */
TEST_F(SyncBeaconSubslotTests, AddressHashCongruentLetsLateJoinerJoin) {
    auto nodes = BuildDiamondEstablishedForwarders(
        SubslotAssignment::ADDRESS_HASH, 0x0005, 0x0041);
    TestNode& c = *nodes[3];

    EXPECT_TRUE(WaitForNormalOperation(c, 40))
        << "With ADDRESS_HASH subslots the late joiner should still join";
    EXPECT_EQ(c.protocol->GetState(), ProtocolState::NORMAL_OPERATION);
}

}  // namespace test
}  // namespace loramesher
