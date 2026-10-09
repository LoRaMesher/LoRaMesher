/**
 * @file deep_sleep_test.cpp
 * @brief Members deep-sleeping through SLEEP runs and resuming their
 *        membership without rejoining
 *
 * A member that deep-sleeps is powered off for the rest of its SLEEP run and
 * boots again with its protocol clock restarted at 0, exactly when its timer
 * fires. The tests check that it resumes in normal operation (no discovery,
 * no join), keeps its control slot and its links, and that data reaches it
 * and leaves it exactly once, while the network stays healthy throughout.
 */

#include <gtest/gtest.h>

#include <algorithm>

#include "node_reboot_fixture.hpp"

namespace loramesher {
namespace test {
namespace {

/// Deep sleep for any SLEEP run of a second or more
power::DeepSleepPolicy TestDeepSleepPolicy() {
    power::DeepSleepPolicy policy;
    policy.enabled = true;
    policy.min_sleep_ms = 1000;
    policy.boot_time_ms = 100;
    policy.clock_drift_ppm = 2000;
    return policy;
}

struct DeepSleepScenario {
    const char* label;
    NodeRebootFixture::Topology topology;
    int node_count;
    float target_duty_cycle;
};

std::ostream& operator<<(std::ostream& out, const DeepSleepScenario& scenario) {
    return out << scenario.label;
}

class DeepSleepTest : public NodeRebootFixture,
                      public ::testing::WithParamInterface<DeepSleepScenario> {
   protected:
    void TearDown() override {
        for (const auto& [address, count] : sleeps_without_beacon_) {
            ADD_FAILURE() << "Node 0x" << std::hex << address << std::dec
                          << " deep-slept " << count
                          << " times without a beacon in its superframe";
        }
        NodeRebootFixture::TearDown();
    }

    /// Build and form the network of @p scenario with deep-sleeping members
    std::vector<TestNode*> FormNetwork(const DeepSleepScenario& scenario) {
        NetworkSpec spec;
        spec.topology = scenario.topology;
        spec.node_count = scenario.node_count;
        spec.member_role = NodeRole::NODE_ONLY;
        spec.target_duty_cycle = scenario.target_duty_cycle;
        spec.member_deep_sleep = TestDeepSleepPolicy();
        auto nodes = BuildNetwork(spec);
        StartAndFormNetwork(nodes);
        return nodes;
    }

    /// Run until every member deep-slept and resumed @p times more
    bool WaitForResumes(const std::vector<TestNode*>& nodes, size_t times) {
        std::map<AddressType, size_t> before;
        for (auto* node : nodes) {
            before[node->address] = CountOf(resumed_boots_, node->address);
        }
        const uint32_t budget =
            superframe_ms_ * (static_cast<uint32_t>(times) + 4);
        return AdvanceTime(budget, budget, kStepMs, 0, [&]() {
            for (size_t i = 1; i < nodes.size(); ++i) {
                const AddressType address = nodes[i]->address;
                if (CountOf(resumed_boots_, address) - before[address] <
                    times) {
                    return false;
                }
            }
            return true;
        });
    }

    /// Every neighbour of @p node still reports a working link to it
    ::testing::AssertionResult LinksIntact(const std::vector<TestNode*>& nodes,
                                           const TestNode& node) {
        for (auto* other : nodes) {
            if (other == &node || !other->protocol) {
                continue;
            }
            auto route = other->protocol->GetNetworkServiceForTest()
                             ->GetRoutingTable()
                             ->FindNode(node.address);
            if (!route || !route->is_active) {
                return ::testing::AssertionFailure()
                       << other->name << " lost its route to " << node.name;
            }
            if (route->IsDirectNeighbor() &&
                (route->link_stats.remote_link_quality == 0 ||
                 route->link_stats.IsUnidirectional())) {
                return ::testing::AssertionFailure()
                       << other->name << " sees a one-way link to "
                       << node.name;
            }
        }
        return ::testing::AssertionSuccess();
    }
};

TEST_P(DeepSleepTest, MembersResumeEverySuperframeWithoutRejoining) {
    auto nodes = FormNetwork(GetParam());
    ASSERT_FALSE(HasFatalFailure());
    TestNode& manager = *nodes.front();

    ASSERT_TRUE(WaitForResumes(nodes, 3)) << DescribeNetwork(nodes);
    EXPECT_TRUE(StaysHealthy(nodes, manager.address, superframe_ms_ * 20,
                             formed_network_id_))
        << DescribeNetwork(nodes);

    for (size_t i = 1; i < nodes.size(); ++i) {
        const TestNode& member = *nodes[i];
        EXPECT_GE(CountOf(deep_sleeps_, member.address), 10u) << member.name;
        EXPECT_EQ(CountOf(fallback_boots_, member.address), 0u)
            << member.name << " rejoined instead of resuming";
        EXPECT_TRUE(LinksIntact(nodes, member));
    }
    EXPECT_EQ(deep_sleeps_.count(manager.address), 0u)
        << "The network manager never deep-sleeps";
    EXPECT_TRUE(unexpected_managers_.empty());

    ASSERT_TRUE(WaitForHealthyNetwork(nodes, manager.address,
                                      superframe_ms_ * 10, formed_network_id_))
        << DescribeNetwork(nodes);
    // Members are all awake in the active part of the superframe
    ASSERT_TRUE(
        AdvanceTime(superframe_ms_ * 2, superframe_ms_ * 2, kStepMs, 0, [&]() {
            return std::all_of(nodes.begin(), nodes.end(), [](TestNode* node) {
                return node->protocol != nullptr;
            });
        }));
    EXPECT_EQ(ControlSlotsOf(nodes), formed_control_slots_);
}

TEST_P(DeepSleepTest, DataReachesSleepingMembersExactlyOnce) {
    auto nodes = FormNetwork(GetParam());
    ASSERT_FALSE(HasFatalFailure());
    TestNode& manager = *nodes.front();
    TestNode& farthest = *nodes.back();
    ASSERT_TRUE(WaitForResumes(nodes, 1)) << DescribeNetwork(nodes);

    EXPECT_EQ(ExpectDataFlows(manager, farthest, 4), 4u);
    EXPECT_EQ(ExpectDataFlows(farthest, manager, 4), 4u);
    if (nodes.size() > 2) {
        EXPECT_EQ(ExpectDataFlows(*nodes[1], farthest, 3), 3u);
    }

    // Traffic did not stop the members from sleeping
    EXPECT_GE(CountOf(resumed_boots_, farthest.address), 4u);
    EXPECT_EQ(CountOf(fallback_boots_, farthest.address), 0u);
    EXPECT_TRUE(unexpected_managers_.empty());
}

TEST_P(DeepSleepTest, ReliableMessagesAreAcknowledgedAcrossSleeps) {
    auto nodes = FormNetwork(GetParam());
    ASSERT_FALSE(HasFatalFailure());
    TestNode& manager = *nodes.front();
    TestNode& farthest = *nodes.back();
    ASSERT_TRUE(WaitForResumes(nodes, 1)) << DescribeNetwork(nodes);

    constexpr int kMessages = 3;
    for (int i = 0; i < kMessages; ++i) {
        const auto id =
            manager.protocol->GetNetworkServiceForTest()->SendReliable(
                farthest.address, {kDataMarker, 0xEE, static_cast<uint8_t>(i)},
                3);
        ASSERT_NE(id.source, 0u);
        AdvanceTime(superframe_ms_);
    }
    const auto delivered = [&]() {
        size_t count = 0;
        for (const auto& outcome : manager.delivery_outcomes) {
            count +=
                outcome.outcome == protocols::reliability::Outcome::Delivered;
        }
        return count;
    };
    EXPECT_TRUE(AdvanceTime(DataBudgetMs(), DataBudgetMs(), kStepMs, 0,
                            [&]() { return delivered() == kMessages; }));

    size_t received = 0;
    for (const auto& msg : farthest.received_messages) {
        const auto payload = msg.GetPayload();
        received += payload.size() == 3 && payload[0] == kDataMarker &&
                    payload[1] == 0xEE;
    }
    EXPECT_EQ(received, static_cast<size_t>(kMessages))
        << "Reliable messages delivered more or less than once";
    EXPECT_EQ(CountOf(fallback_boots_, farthest.address), 0u);
}

TEST_P(DeepSleepTest, SleepClockErrorIsAbsorbed) {
    auto nodes = FormNetwork(GetParam());
    ASSERT_FALSE(HasFatalFailure());
    TestNode& manager = *nodes.front();
    TestNode& farthest = *nodes.back();

    // The default 5000 ppm over a few seconds of sleep
    sleep_clock_error_ms_ = 20;
    ASSERT_TRUE(WaitForResumes(nodes, 4)) << DescribeNetwork(nodes);
    EXPECT_TRUE(StaysHealthy(nodes, manager.address, superframe_ms_ * 10,
                             formed_network_id_))
        << DescribeNetwork(nodes);
    EXPECT_EQ(ExpectDataFlows(manager, farthest, 3), 3u);
    EXPECT_EQ(ExpectDataFlows(farthest, manager, 3), 3u);
    for (size_t i = 1; i < nodes.size(); ++i) {
        EXPECT_EQ(CountOf(fallback_boots_, nodes[i]->address), 0u)
            << nodes[i]->name;
        EXPECT_TRUE(LinksIntact(nodes, *nodes[i]));
    }
}

TEST_P(DeepSleepTest, LargeSleepClockErrorIsCaughtByTheBeacon) {
    auto nodes = FormNetwork(GetParam());
    ASSERT_FALSE(HasFatalFailure());
    TestNode& manager = *nodes.front();
    TestNode& farthest = *nodes.back();

    // More than a slot per sleep: a resumed member is outside its own
    // beacon slot until it hears the beacon
    sleep_clock_error_ms_ = 700;
    ASSERT_TRUE(WaitForResumes(nodes, 4)) << DescribeNetwork(nodes);
    EXPECT_TRUE(StaysHealthy(nodes, manager.address, superframe_ms_ * 10,
                             formed_network_id_))
        << DescribeNetwork(nodes);
    EXPECT_EQ(ExpectDataFlows(manager, farthest, 3), 3u);
    EXPECT_EQ(ExpectDataFlows(farthest, manager, 3), 3u);
    for (size_t i = 1; i < nodes.size(); ++i) {
        EXPECT_EQ(CountOf(fallback_boots_, nodes[i]->address), 0u)
            << nodes[i]->name;
        EXPECT_TRUE(LinksIntact(nodes, *nodes[i]));
    }
}

TEST_P(DeepSleepTest, ResumeAfterTheBeaconWaitsForTheNextBeacon) {
    auto nodes = FormNetwork(GetParam());
    ASSERT_FALSE(HasFatalFailure());
    TestNode& manager = *nodes.front();
    TestNode& farthest = *nodes.back();
    ASSERT_TRUE(WaitForResumes(nodes, 1)) << DescribeNetwork(nodes);

    // The clock gains on every sleep, and one wake-up comes after the beacon
    // slot of its superframe: the node must hear a beacon before sleeping
    sleep_clock_error_ms_ = 200;
    next_wake_delay_ms_ = superframe_ms_ / 3;
    ASSERT_TRUE(WaitForResumes(nodes, 4)) << DescribeNetwork(nodes);
    ASSERT_FALSE(next_wake_delay_ms_.has_value());
    EXPECT_TRUE(StaysHealthy(nodes, manager.address, superframe_ms_ * 10,
                             formed_network_id_))
        << DescribeNetwork(nodes);
    EXPECT_EQ(ExpectDataFlows(farthest, manager, 3), 3u);
    EXPECT_EQ(ExpectDataFlows(manager, farthest, 3), 3u);
    EXPECT_TRUE(LinksIntact(nodes, farthest));
}

TEST_P(DeepSleepTest, NewNodeJoinsThroughADeepSleepingSponsor) {
    auto nodes = FormNetwork(GetParam());
    ASSERT_FALSE(HasFatalFailure());
    TestNode& manager = *nodes.front();
    TestNode& sponsor = *nodes.back();
    ASSERT_TRUE(WaitForResumes(nodes, 1)) << DescribeNetwork(nodes);

    // The newcomer hears only a member that deep-sleeps every superframe;
    // the member is awake in the discovery band at the end of the superframe
    const AddressType address =
        static_cast<AddressType>(kBaseAddress + nodes.size());
    TestNode& newcomer =
        CreateNode("Newcomer", address, NodeRole::NODE_ONLY, PinConfig(),
                   RadioConfig(), MakeCustomizer(address));
    for (auto* node : nodes) {
        SetLinkStatus(newcomer, *node, node == &sponsor);
    }
    nodes.push_back(&newcomer);
    ASSERT_TRUE(StartNode(newcomer));
    const size_t sponsor_sleeps = CountOf(deep_sleeps_, sponsor.address);

    ASSERT_TRUE(WaitForHealthyNetwork(
        nodes, manager.address, RecoveryBudgetMs(nodes), formed_network_id_))
        << DescribeNetwork(nodes);
    EXPECT_GT(CountOf(deep_sleeps_, sponsor.address), sponsor_sleeps)
        << "The sponsor kept deep-sleeping while the newcomer joined";
    EXPECT_EQ(CountOf(fallback_boots_, sponsor.address), 0u);
    EXPECT_EQ(ExpectDataFlows(newcomer, manager, 2), 2u);
}

TEST_P(DeepSleepTest, LateWakeFallsBackToRejoining) {
    auto nodes = FormNetwork(GetParam());
    ASSERT_FALSE(HasFatalFailure());
    TestNode& manager = *nodes.front();
    ASSERT_TRUE(WaitForResumes(nodes, 1)) << DescribeNetwork(nodes);

    // The next wake-ups come more than a superframe too late
    extra_wake_delay_ms_ = superframe_ms_ * 2;
    TestNode& farthest = *nodes.back();
    const size_t sleeps = CountOf(deep_sleeps_, farthest.address);
    ASSERT_TRUE(AdvanceTime(
        superframe_ms_ * 4, superframe_ms_ * 4, kStepMs, 0,
        [&]() { return CountOf(fallback_boots_, farthest.address) > 0; }))
        << DescribeNetwork(nodes);
    extra_wake_delay_ms_ = 0;
    EXPECT_GT(CountOf(deep_sleeps_, farthest.address), sleeps);

    // The late node rejoins the same network and keeps its slot
    ASSERT_TRUE(WaitForHealthyNetwork(
        nodes, manager.address, RecoveryBudgetMs(nodes), formed_network_id_))
        << DescribeNetwork(nodes);
    EXPECT_TRUE(unexpected_managers_.empty());
    EXPECT_EQ(ExpectDataFlows(farthest, manager, 2), 2u);
}

INSTANTIATE_TEST_SUITE_P(
    Scenarios, DeepSleepTest,
    ::testing::Values(
        DeepSleepScenario{"Star3", NodeRebootFixture::Topology::kStar, 3, 1.0f},
        DeepSleepScenario{"Line3", NodeRebootFixture::Topology::kLine, 3, 1.0f},
        DeepSleepScenario{"Line4_LowDuty", NodeRebootFixture::Topology::kLine,
                          4, 0.2f}),
    [](const ::testing::TestParamInfo<DeepSleepScenario>& info) {
        return std::string(info.param.label);
    });

}  // namespace
}  // namespace test
}  // namespace loramesher
