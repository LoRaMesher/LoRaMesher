/**
 * @file node_reboot_test.cpp
 * @brief Node reboot (OTA reset, deep sleep) integration tests
 *
 * Every test forms a network, powers some nodes off and on again and checks
 * that the network recovers: the same manager and network id, the members'
 * control slots, no second manager at any time, and data flowing in both
 * directions. Nodes save their state into a per-node store before going down
 * (warm restart); the tests without a store document the cold-start behavior
 * the store avoids.
 */

#include <gtest/gtest.h>

#include "node_reboot_fixture.hpp"
#include "types/storage/snapshot_codec.hpp"

namespace loramesher {
namespace test {
namespace {

/// Which nodes a scenario reboots
enum class RebootTarget {
    kManager,               ///< The network manager only
    kFarthestMember,        ///< The member farthest from the manager
    kRelayMember,           ///< A member that relays for others (line only)
    kAllNodes,              ///< Every node at once
    kAllNodesManagerLast,   ///< Every node; the manager boots last
};

struct RebootScenario {
    const char* label;
    NodeRebootFixture::Topology topology;
    int node_count;
    NodeRole member_role;
    RebootTarget target;
    uint32_t downtime_superframes;
    uint32_t boot_stagger_superframes;
    /// Target duty cycle; lower values give longer superframes
    float target_duty_cycle = 1.0f;
    /// The outage outlasts the members' election backoff, so any single
    /// manager may run the network afterwards
    bool successor_allowed = false;
};

std::ostream& operator<<(std::ostream& out, const RebootScenario& scenario) {
    return out << scenario.label;
}

class WarmRebootTest : public NodeRebootFixture,
                       public ::testing::WithParamInterface<RebootScenario> {
   protected:
    std::vector<TestNode*> Targets(const std::vector<TestNode*>& nodes,
                                   RebootTarget target) {
        switch (target) {
            case RebootTarget::kManager:
                return {nodes.front()};
            case RebootTarget::kFarthestMember:
                return {nodes.back()};
            case RebootTarget::kRelayMember:
                return {nodes[1]};
            case RebootTarget::kAllNodes:
                return nodes;
            case RebootTarget::kAllNodesManagerLast: {
                std::vector<TestNode*> order(nodes.begin() + 1, nodes.end());
                order.push_back(nodes.front());
                return order;
            }
        }
        return {};
    }
};

TEST_P(WarmRebootTest, NetworkResumesWithSameIdentityAndSlots) {
    const RebootScenario& scenario = GetParam();
    NetworkSpec spec;
    spec.topology = scenario.topology;
    spec.node_count = scenario.node_count;
    spec.member_role = scenario.member_role;
    spec.target_duty_cycle = scenario.target_duty_cycle;
    auto nodes = BuildNetwork(spec);
    ASSERT_NO_FATAL_FAILURE(StartAndFormNetwork(nodes));
    TestNode& manager = *nodes.front();
    const auto targets = Targets(nodes, scenario.target);

    // Traffic before the reboot fills the duplicate caches with the rebooted
    // nodes' sequence numbers
    for (auto* node : targets) {
        if (node != &manager) {
            EXPECT_EQ(ExpectDataFlows(*node, manager, 3), 3u) << node->name;
        }
    }

    ASSERT_NO_FATAL_FAILURE(RebootNodes(
        targets, scenario.downtime_superframes * superframe_ms_, true,
        scenario.boot_stagger_superframes * superframe_ms_));

    for (auto* node : targets) {
        EXPECT_FALSE(stores_.at(node->address)->Load().has_value())
            << node->name << " snapshot must be consumed by the boot";
    }

    const AddressType expected_manager =
        scenario.successor_allowed ? kAnyManager : manager.address;
    ASSERT_TRUE(WaitForHealthyNetwork(nodes, expected_manager,
                                      RecoveryBudgetMs(nodes),
                                      formed_network_id_))
        << DescribeNetwork(nodes);
    TestNode& recovered_manager = *healthy_manager_;
    if (!scenario.successor_allowed) {
        EXPECT_TRUE(unexpected_managers_.empty())
            << "A member became network manager during recovery";
        EXPECT_EQ(ControlSlotsOf(nodes), formed_control_slots_);
    }
    EXPECT_TRUE(StaysHealthy(nodes, recovered_manager.address,
                             superframe_ms_ * 10, formed_network_id_))
        << DescribeNetwork(nodes);

    // Data flows in both directions with every rebooted node and across the
    // whole network
    for (auto* node : targets) {
        if (node != &recovered_manager) {
            EXPECT_EQ(ExpectDataFlows(*node, recovered_manager, 3), 3u)
                << node->name;
            EXPECT_EQ(ExpectDataFlows(recovered_manager, *node, 3), 3u)
                << node->name;
        }
    }
    TestNode& farthest =
        &recovered_manager == nodes.back() ? *nodes.front() : *nodes.back();
    EXPECT_EQ(ExpectDataFlows(farthest, recovered_manager, 3), 3u);
    EXPECT_EQ(ExpectDataFlows(recovered_manager, farthest, 3), 3u);
}

using Topology = NodeRebootFixture::Topology;

INSTANTIATE_TEST_SUITE_P(
    Scenarios, WarmRebootTest,
    ::testing::Values(
        RebootScenario{"Line4_Manager", Topology::kLine, 4, NodeRole::AUTO,
                       RebootTarget::kManager, 2, 0},
        RebootScenario{"Line4_Manager_NodeOnly", Topology::kLine, 4,
                       NodeRole::NODE_ONLY, RebootTarget::kManager, 2, 0},
        RebootScenario{"Star5_Manager", Topology::kStar, 5, NodeRole::AUTO,
                       RebootTarget::kManager, 2, 0},
        RebootScenario{"Mesh4_Manager", Topology::kFullMesh, 4,
                       NodeRole::AUTO, RebootTarget::kManager, 2, 0},
        RebootScenario{"Line4_FarthestMember", Topology::kLine, 4,
                       NodeRole::AUTO, RebootTarget::kFarthestMember, 2, 0},
        RebootScenario{"Line4_RelayMember", Topology::kLine, 4,
                       NodeRole::AUTO, RebootTarget::kRelayMember, 2, 0},
        RebootScenario{"Star5_Member", Topology::kStar, 5, NodeRole::AUTO,
                       RebootTarget::kFarthestMember, 2, 0},
        RebootScenario{"Line4_AllNodes", Topology::kLine, 4, NodeRole::AUTO,
                       RebootTarget::kAllNodes, 2, 0},
        RebootScenario{"Line4_AllNodes_ManagerLast", Topology::kLine, 4,
                       NodeRole::AUTO, RebootTarget::kAllNodesManagerLast, 2,
                       1},
        RebootScenario{"Star5_AllNodes", Topology::kStar, 5, NodeRole::AUTO,
                       RebootTarget::kAllNodes, 2, 0},
        RebootScenario{"Mesh4_AllNodes", Topology::kFullMesh, 4,
                       NodeRole::AUTO, RebootTarget::kAllNodes, 2, 0},
        RebootScenario{"Line4_AllNodes_NodeOnly", Topology::kLine, 4,
                       NodeRole::NODE_ONLY, RebootTarget::kAllNodes, 2, 0},
        // Downtimes long enough for the members to lose the manager and for
        // the manager to mark a silent member's link inactive
        RebootScenario{"Star5_Manager_LongDowntime_NodeOnly", Topology::kStar,
                       5, NodeRole::NODE_ONLY, RebootTarget::kManager, 8, 0},
        RebootScenario{"Line4_Manager_LongDowntime", Topology::kLine, 4,
                       NodeRole::AUTO, RebootTarget::kManager, 8, 0, 1.0f,
                       true},
        RebootScenario{"Mesh4_Manager_LongDowntime", Topology::kFullMesh, 4,
                       NodeRole::AUTO, RebootTarget::kManager, 8, 0, 1.0f,
                       true},
        RebootScenario{"Line4_Manager_LongDowntime_NodeOnly", Topology::kLine,
                       4, NodeRole::NODE_ONLY, RebootTarget::kManager, 8, 0},
        RebootScenario{"Line4_FarthestMember_LongDowntime", Topology::kLine, 4,
                       NodeRole::AUTO, RebootTarget::kFarthestMember, 15, 0},
        RebootScenario{"Star5_Member_LongDowntime", Topology::kStar, 5,
                       NodeRole::AUTO, RebootTarget::kFarthestMember, 15, 0},
        RebootScenario{"Line4_RelayMember_LongDowntime_NodeOnly",
                       Topology::kLine, 4, NodeRole::NODE_ONLY,
                       RebootTarget::kRelayMember, 15, 0},
        RebootScenario{"Line4_AllNodes_LongDowntime", Topology::kLine, 4,
                       NodeRole::AUTO, RebootTarget::kAllNodes, 15, 0},
        // Low duty cycle: superframes several times longer
        RebootScenario{"Line4_Manager_LowDutyCycle", Topology::kLine, 4,
                       NodeRole::AUTO, RebootTarget::kManager, 2, 0, 0.2f},
        RebootScenario{"Line4_AllNodes_LowDutyCycle", Topology::kLine, 4,
                       NodeRole::AUTO, RebootTarget::kAllNodes, 2, 0, 0.2f},
        RebootScenario{"Star5_Manager_LowDutyCycle", Topology::kStar, 5,
                       NodeRole::AUTO, RebootTarget::kManager, 3, 0, 0.2f}),
    [](const ::testing::TestParamInfo<RebootScenario>& info) {
        return std::string(info.param.label);
    });

class NodeRebootTest : public NodeRebootFixture {};

TEST_F(NodeRebootTest, ControlLine4StaysHealthyWithoutReboot) {
    NetworkSpec spec;
    auto nodes = BuildNetwork(spec);
    ASSERT_NO_FATAL_FAILURE(StartAndFormNetwork(nodes));
    EXPECT_TRUE(StaysHealthy(nodes, nodes.front()->address, superframe_ms_ * 20,
                             formed_network_id_))
        << DescribeNetwork(nodes);
    EXPECT_EQ(ExpectDataFlows(*nodes.back(), *nodes.front(), 3), 3u);
}

TEST_F(NodeRebootTest, ManagerColdRebootFormsNetworkWithNewId) {
    UseTestSeed(7);
    NetworkSpec spec;
    spec.member_role = NodeRole::NODE_ONLY;
    spec.with_store = false;
    auto nodes = BuildNetwork(spec);
    ASSERT_NO_FATAL_FAILURE(StartAndFormNetwork(nodes));
    TestNode& manager = *nodes.front();

    ASSERT_NO_FATAL_FAILURE(RebootNode(manager, superframe_ms_ * 2, false));

    ASSERT_TRUE(WaitForHealthyNetwork(nodes, manager.address,
                                      RecoveryBudgetMs(nodes)))
        << DescribeNetwork(nodes);
    EXPECT_NE(NetworkIdOf(manager), formed_network_id_);
}

TEST_F(NodeRebootTest, MemberRebootKeepsSequenceNumbersWithStore) {
    NetworkSpec spec;
    spec.topology = Topology::kStar;
    auto nodes = BuildNetwork(spec);
    ASSERT_NO_FATAL_FAILURE(StartAndFormNetwork(nodes));
    TestNode& manager = *nodes.front();
    TestNode& member = *nodes.back();

    EXPECT_EQ(ExpectDataFlows(member, manager, 3), 3u);
    ASSERT_NO_FATAL_FAILURE(RebootNode(member, superframe_ms_ * 2, true));
    ASSERT_TRUE(WaitForHealthyNetwork(nodes, manager.address,
                                      RecoveryBudgetMs(nodes),
                                      formed_network_id_))
        << DescribeNetwork(nodes);

    EXPECT_EQ(ExpectDataFlows(member, manager, 3), 3u);
}

TEST_F(NodeRebootTest, MemberColdRebootLosesMessagesToDuplicateCache) {
    NetworkSpec spec;
    spec.topology = Topology::kStar;
    spec.with_store = false;
    auto nodes = BuildNetwork(spec);
    ASSERT_NO_FATAL_FAILURE(StartAndFormNetwork(nodes));
    TestNode& manager = *nodes.front();
    TestNode& member = *nodes.back();

    EXPECT_EQ(ExpectDataFlows(member, manager, 3), 3u);
    ASSERT_NO_FATAL_FAILURE(RebootNode(member, superframe_ms_ * 2, false));
    ASSERT_TRUE(WaitForHealthyNetwork(nodes, manager.address,
                                      RecoveryBudgetMs(nodes),
                                      formed_network_id_))
        << DescribeNetwork(nodes);

    // The restarted sequence numbers collide with the manager's cache entries
    // of the messages sent before the reboot
    EXPECT_LT(ExpectDataFlows(member, manager, 3), 3u);
}

TEST_F(NodeRebootTest, SnapshotIsConsumedByTheBoot) {
    NetworkSpec spec;
    spec.member_role = NodeRole::NODE_ONLY;
    auto nodes = BuildNetwork(spec);
    ASSERT_NO_FATAL_FAILURE(StartAndFormNetwork(nodes));
    TestNode& manager = *nodes.front();

    ASSERT_NO_FATAL_FAILURE(RebootNode(manager, superframe_ms_ * 2, true));
    ASSERT_TRUE(WaitForHealthyNetwork(nodes, manager.address,
                                      RecoveryBudgetMs(nodes),
                                      formed_network_id_))
        << DescribeNetwork(nodes);

    // A second reset without saving (a crash) must not reuse the snapshot
    ASSERT_NO_FATAL_FAILURE(RebootNode(manager, superframe_ms_ * 2, false));
    ASSERT_TRUE(WaitForHealthyNetwork(nodes, manager.address,
                                      RecoveryBudgetMs(nodes)))
        << DescribeNetwork(nodes);
    EXPECT_NE(NetworkIdOf(manager), formed_network_id_);
}

TEST_F(NodeRebootTest, CorruptedSnapshotFallsBackToColdStart) {
    NetworkSpec spec;
    spec.member_role = NodeRole::NODE_ONLY;
    auto nodes = BuildNetwork(spec);
    ASSERT_NO_FATAL_FAILURE(StartAndFormNetwork(nodes));
    TestNode& manager = *nodes.front();
    auto& store = *stores_.at(manager.address);

    ASSERT_TRUE(SaveNodeState(manager));
    auto blob = store.Load();
    ASSERT_TRUE(blob.has_value());
    (*blob)[blob->size() / 2] ^= 0x40;
    ASSERT_TRUE(store.Save(*blob));

    ASSERT_NO_FATAL_FAILURE(RebootNode(manager, superframe_ms_ * 2, false));
    EXPECT_FALSE(store.Load().has_value());
    ASSERT_TRUE(WaitForHealthyNetwork(nodes, manager.address,
                                      RecoveryBudgetMs(nodes)))
        << DescribeNetwork(nodes);
    EXPECT_NE(NetworkIdOf(manager), formed_network_id_);
}

TEST_F(NodeRebootTest, SnapshotOfAnotherNodeIsIgnored) {
    NetworkSpec spec;
    spec.member_role = NodeRole::NODE_ONLY;
    auto nodes = BuildNetwork(spec);
    ASSERT_NO_FATAL_FAILURE(StartAndFormNetwork(nodes));
    TestNode& manager = *nodes.front();
    TestNode& member = *nodes.back();

    ASSERT_TRUE(SaveNodeState(member));
    auto member_blob = stores_.at(member.address)->Load();
    ASSERT_TRUE(member_blob.has_value());
    ASSERT_TRUE(stores_.at(manager.address)->Save(*member_blob));

    ASSERT_NO_FATAL_FAILURE(RebootNode(manager, superframe_ms_ * 2, false));
    ASSERT_TRUE(WaitForHealthyNetwork(nodes, manager.address,
                                      RecoveryBudgetMs(nodes)))
        << DescribeNetwork(nodes);
    EXPECT_NE(NetworkIdOf(manager), formed_network_id_);
}

TEST_F(NodeRebootTest, RepeatedRebootsKeepNetworkIdentity) {
    NetworkSpec spec;
    auto nodes = BuildNetwork(spec);
    ASSERT_NO_FATAL_FAILURE(StartAndFormNetwork(nodes));
    TestNode& manager = *nodes.front();

    const std::vector<std::vector<TestNode*>> cycles = {
        {nodes[0]}, {nodes[2]}, nodes, {nodes[0]}, {nodes[1], nodes[3]}};
    for (size_t cycle = 0; cycle < cycles.size(); ++cycle) {
        SCOPED_TRACE("cycle " + std::to_string(cycle));
        ASSERT_NO_FATAL_FAILURE(
            RebootNodes(cycles[cycle], superframe_ms_ * 2, true));
        ASSERT_TRUE(WaitForHealthyNetwork(nodes, manager.address,
                                          RecoveryBudgetMs(nodes),
                                          formed_network_id_))
            << DescribeNetwork(nodes);
        EXPECT_EQ(ControlSlotsOf(nodes), formed_control_slots_);
        EXPECT_TRUE(StaysHealthy(nodes, manager.address, superframe_ms_ * 4,
                                 formed_network_id_))
            << DescribeNetwork(nodes);
    }
    EXPECT_TRUE(unexpected_managers_.empty());
    EXPECT_EQ(ExpectDataFlows(*nodes.back(), manager, 3), 3u);
    EXPECT_EQ(ExpectDataFlows(manager, *nodes.back(), 3), 3u);
}

TEST_F(NodeRebootTest, ManagerBackAfterSuccessorElectedJoinsSuccessor) {
    NetworkSpec spec;
    spec.topology = Topology::kFullMesh;
    spec.node_count = 3;
    auto nodes = BuildNetwork(spec);
    ASSERT_NO_FATAL_FAILURE(StartAndFormNetwork(nodes));
    TestNode& manager = *nodes.front();
    std::vector<TestNode*> members(nodes.begin() + 1, nodes.end());

    ASSERT_TRUE(SaveNodeState(manager));
    ShutdownNode(manager);

    // The members elect a successor that keeps the network id
    TestNode* successor = nullptr;
    ASSERT_TRUE(AdvanceTime(RecoveryBudgetMs(nodes), RecoveryBudgetMs(nodes),
                            kStepMs, 0, [&]() {
                                for (auto* node : members) {
                                    if (IsHealthy(members, node->address,
                                                  formed_network_id_)) {
                                        successor = node;
                                        return true;
                                    }
                                }
                                return false;
                            }))
        << DescribeNetwork(nodes);
    unexpected_managers_.clear();

    // The old manager comes back with its snapshot and must not split the
    // network: it joins the successor's network as a member
    ASSERT_TRUE(BootNode(manager));
    ASSERT_TRUE(WaitForHealthyNetwork(nodes, successor->address,
                                      RecoveryBudgetMs(nodes),
                                      formed_network_id_))
        << DescribeNetwork(nodes);
    EXPECT_TRUE(unexpected_managers_.empty())
        << "The returning manager took over the network";
    EXPECT_TRUE(StaysHealthy(nodes, successor->address, superframe_ms_ * 10,
                             formed_network_id_))
        << DescribeNetwork(nodes);
    EXPECT_EQ(ExpectDataFlows(manager, *successor, 3), 3u);
    EXPECT_EQ(ExpectDataFlows(*successor, manager, 3), 3u);
}

TEST_F(NodeRebootTest, VanishedMemberReservationExpires) {
    NetworkSpec spec;
    spec.node_count = 3;
    spec.member_role = NodeRole::NODE_ONLY;
    auto nodes = BuildNetwork(spec);
    ASSERT_NO_FATAL_FAILURE(StartAndFormNetwork(nodes));
    TestNode& manager = *nodes.front();
    TestNode& vanished = *nodes.back();
    const std::vector<TestNode*> remaining = {nodes[0], nodes[1]};
    auto* manager_service = manager.protocol->GetNetworkServiceForTest();
    const uint8_t band = manager_service->GetAllocatedControlSlots();
    ASSERT_EQ(band, 3u);

    // The manager saves while the farthest member is still in the network,
    // then that member disappears for good
    ASSERT_TRUE(SaveNodeState(manager));
    ShutdownNode(vanished);
    ShutdownNode(manager);
    AdvanceTime(superframe_ms_ * 2);
    ASSERT_TRUE(BootNode(manager));

    ASSERT_TRUE(WaitForHealthyNetwork(remaining, manager.address,
                                      RecoveryBudgetMs(nodes),
                                      formed_network_id_))
        << DescribeNetwork(remaining);
    manager_service = manager.protocol->GetNetworkServiceForTest();
    EXPECT_EQ(manager_service->GetAllocatedControlSlots(), band)
        << "The vanished member's slot is held while it may still return";

    // Once the member would have timed out of the routing table, its slot is
    // released and the control band shrinks
    const uint32_t release_budget =
        manager_service->GetConfig().node_timeout_ms * 2;
    EXPECT_TRUE(AdvanceTime(release_budget, release_budget, kStepMs, 0, [&]() {
        return manager_service->GetAllocatedControlSlots() < band;
    }));
    EXPECT_TRUE(IsHealthy(remaining, manager.address, formed_network_id_))
        << DescribeNetwork(remaining);
}

TEST_F(NodeRebootTest, WarmMembersWaitForSlowManager) {
    NetworkSpec spec;
    auto nodes = BuildNetwork(spec);
    ASSERT_NO_FATAL_FAILURE(StartAndFormNetwork(nodes));
    TestNode& manager = *nodes.front();
    std::vector<TestNode*> members(nodes.begin() + 1, nodes.end());

    // Members boot right away; the manager needs longer than a cold
    // discovery window (e.g. it validates a new firmware image first)
    const uint32_t discovery_ms = discovery_timeout_ms_;
    for (auto* node : nodes) {
        ASSERT_TRUE(SaveNodeState(*node));
    }
    for (auto* node : nodes) {
        ShutdownNode(*node);
    }
    AdvanceTime(superframe_ms_);
    for (auto* node : members) {
        ASSERT_TRUE(BootNode(*node));
    }
    ASSERT_TRUE(AdvanceTime(discovery_ms * 3 / 2, discovery_ms * 3 / 2,
                            kStepMs, 0, [&]() {
                                RecordUnexpectedManagers(members,
                                                         manager.address);
                                return false;
                            }) == false);
    EXPECT_TRUE(unexpected_managers_.empty())
        << "A member formed its own network before the manager was back";
    ASSERT_TRUE(BootNode(manager));

    ASSERT_TRUE(WaitForHealthyNetwork(nodes, manager.address,
                                      RecoveryBudgetMs(nodes),
                                      formed_network_id_))
        << DescribeNetwork(nodes);
    EXPECT_TRUE(unexpected_managers_.empty());
    EXPECT_EQ(ControlSlotsOf(nodes), formed_control_slots_);
}

}  // namespace
}  // namespace test
}  // namespace loramesher

namespace loramesher {
namespace test {
namespace {

/// Long outages that split AUTO members into partitions which each elect
/// a manager under the same network id. Healing them needs the network
/// merge, which is disabled (see docs/todo_network_merge.md).
class PartitionHealTest : public NodeRebootFixture,
                          public ::testing::WithParamInterface<bool> {};

TEST_P(PartitionHealTest, RelayOutageHealsToSingleManager) {
    GTEST_SKIP() << "Network merge disabled — see docs/todo_network_merge.md";
    NetworkSpec spec;
    spec.with_store = GetParam();
    auto nodes = BuildNetwork(spec);
    ASSERT_NO_FATAL_FAILURE(StartAndFormNetwork(nodes));
    ASSERT_NO_FATAL_FAILURE(
        RebootNode(*nodes[1], superframe_ms_ * 15, spec.with_store));
    EXPECT_TRUE(WaitForHealthyNetwork(nodes, kAnyManager,
                                      RecoveryBudgetMs(nodes) * 3,
                                      formed_network_id_))
        << DescribeNetwork(nodes);
}

TEST_P(PartitionHealTest, StarCenterOutageHealsToSingleManager) {
    GTEST_SKIP() << "Network merge disabled — see docs/todo_network_merge.md";
    NetworkSpec spec;
    spec.topology = Topology::kStar;
    spec.node_count = 5;
    spec.with_store = GetParam();
    auto nodes = BuildNetwork(spec);
    ASSERT_NO_FATAL_FAILURE(StartAndFormNetwork(nodes));
    ASSERT_NO_FATAL_FAILURE(
        RebootNode(*nodes[0], superframe_ms_ * 8, spec.with_store));
    EXPECT_TRUE(WaitForHealthyNetwork(nodes, kAnyManager,
                                      RecoveryBudgetMs(nodes) * 3))
        << DescribeNetwork(nodes);
}

INSTANTIATE_TEST_SUITE_P(Store, PartitionHealTest, ::testing::Bool(),
                         [](const ::testing::TestParamInfo<bool>& info) {
                             return info.param ? "Warm" : "Cold";
                         });

}  // namespace
}  // namespace test
}  // namespace loramesher
