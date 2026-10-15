/**
 * @file light_sleep_clock_test.cpp
 * @brief Members light-sleeping on an inaccurate sleep clock, against a
 *        network manager that never sleeps
 *
 * The clock that times a light sleep runs on the RC oscillator, so a member's
 * schedule drifts while it sleeps. The tests check that members keep the
 * network healthy and calibrate their sleep clock, that a joining node and a
 * newcomer joining through a sleeping member are not thrown off by the
 * drift, and that data flows.
 */

#include <gtest/gtest.h>

#include <algorithm>

#include "node_reboot_fixture.hpp"

namespace loramesher {
namespace test {
namespace {

struct LightSleepScenario {
    const char* label;
    NodeRebootFixture::Topology topology;
    int node_count;
    int32_t clock_ppm;
};

std::ostream& operator<<(std::ostream& out,
                         const LightSleepScenario& scenario) {
    return out << scenario.label;
}

class LightSleepClockTest
    : public NodeRebootFixture,
      public ::testing::WithParamInterface<LightSleepScenario> {
   protected:
    std::vector<TestNode*> FormNetwork() {
        NetworkSpec spec;
        spec.topology = GetParam().topology;
        spec.node_count = GetParam().node_count;
        spec.member_role = NodeRole::NODE_ONLY;
        spec.member_light_sleep = true;
        spec.member_light_sleep_clock_ppm = GetParam().clock_ppm;
        auto nodes = BuildNetwork(spec);
        StartAndFormNetwork(nodes);
        return nodes;
    }

    /// Hops from the manager to member @p index
    int32_t HopsOf(size_t index) const {
        return GetParam().topology == Topology::kLine
                   ? static_cast<int32_t>(index)
                   : 1;
    }
};

TEST_P(LightSleepClockTest, MembersCalibrateTheirSleepClock) {
    auto nodes = FormNetwork();
    ASSERT_FALSE(HasFatalFailure());
    TestNode& manager = *nodes.front();

    EXPECT_TRUE(StaysHealthy(nodes, manager.address, superframe_ms_ * 12,
                             formed_network_id_))
        << DescribeNetwork(nodes);
    for (size_t i = 1; i < nodes.size(); ++i) {
        const auto calibration =
            nodes[i]
                ->protocol->GetNetworkServiceForTest()
                ->GetSleepClockCalibration(power::SleepKind::LIGHT);
        EXPECT_TRUE(calibration.IsCalibrated()) << nodes[i]->name;
        // Each relay re-times the beacon with up to guard/2 of residual
        // offset, which is large against the short simulated sleeps
        EXPECT_NEAR(calibration.GetPpm(), GetParam().clock_ppm,
                    2500 * HopsOf(i))
            << nodes[i]->name;
    }
    EXPECT_TRUE(unexpected_managers_.empty());
}

TEST_P(LightSleepClockTest, DataFlowsBothWays) {
    auto nodes = FormNetwork();
    ASSERT_FALSE(HasFatalFailure());
    TestNode& manager = *nodes.front();
    TestNode& farthest = *nodes.back();

    EXPECT_EQ(ExpectDataFlows(manager, farthest, 4), 4u);
    EXPECT_EQ(ExpectDataFlows(farthest, manager, 4), 4u);
    EXPECT_TRUE(StaysHealthy(nodes, manager.address, superframe_ms_ * 4,
                             formed_network_id_))
        << DescribeNetwork(nodes);
}

TEST_P(LightSleepClockTest, NewcomerJoinsThroughASleepingMember) {
    auto nodes = FormNetwork();
    ASSERT_FALSE(HasFatalFailure());
    TestNode& manager = *nodes.front();
    TestNode& sponsor = *nodes.back();
    EXPECT_TRUE(StaysHealthy(nodes, manager.address, superframe_ms_ * 4,
                             formed_network_id_))
        << DescribeNetwork(nodes);

    // The newcomer light-sleeps on an inaccurate clock too, and only hears a
    // member that light-sleeps every superframe
    const AddressType address =
        static_cast<AddressType>(kBaseAddress + nodes.size());
    TestNode& newcomer =
        CreateNode("Newcomer", address, NodeRole::NODE_ONLY, PinConfig(),
                   RadioConfig(), MakeCustomizer(address));
    SetLightSleepClockError(newcomer, GetParam().clock_ppm);
    for (auto* node : nodes) {
        SetLinkStatus(newcomer, *node, node == &sponsor);
    }
    nodes.push_back(&newcomer);
    ASSERT_TRUE(StartNode(newcomer));

    ASSERT_TRUE(WaitForHealthyNetwork(
        nodes, manager.address, RecoveryBudgetMs(nodes), formed_network_id_))
        << DescribeNetwork(nodes);
    EXPECT_EQ(ExpectDataFlows(newcomer, manager, 2), 2u);
}

INSTANTIATE_TEST_SUITE_P(
    Scenarios, LightSleepClockTest,
    ::testing::Values(
        LightSleepScenario{"Star3_Fast", NodeRebootFixture::Topology::kStar, 3,
                           20000},
        LightSleepScenario{"Line3_Fast", NodeRebootFixture::Topology::kLine, 3,
                           20000},
        LightSleepScenario{"Line3_Slow", NodeRebootFixture::Topology::kLine, 3,
                           -15000}),
    [](const ::testing::TestParamInfo<LightSleepScenario>& info) {
        return std::string(info.param.label);
    });

}  // namespace
}  // namespace test
}  // namespace loramesher
