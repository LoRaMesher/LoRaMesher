/**
 * @file join_contention_test.cpp
 * @brief Network formation with many simultaneous joiners
 *
 * At SF10-SF12 only two discovery subslots fit a JOIN_REQUEST, so joiners that
 * heard the same beacon must spread their requests over the discovery slot
 * pairs and over superframes. These tests bound formation time in network
 * manager superframes, and check that a lone joiner still joins in the
 * superframe in which it heard the beacon.
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "routing_test_fixture.hpp"
#include "types/configurations/radio_configuration.hpp"

namespace loramesher {
namespace test {

namespace {
constexpr int kSimultaneousJoiners = 7;

/// Upper bound on network manager superframes, counted from start-up, until
/// every joiner is in normal operation
constexpr int kFormationSuperframeBound = 20;

using ProtocolState = protocols::lora_mesh::INetworkService::ProtocolState;
}  // namespace

class JoinContentionTests : public RoutingTestFixture,
                            public ::testing::WithParamInterface<uint8_t> {
   protected:
    /// Network manager plus @p joiners nodes, all in range of each other
    std::vector<TestNode*> BuildFullMesh(uint8_t sf, int joiners) {
        RadioConfig sf_config;
        sf_config.setSpreadingFactor(sf);

        std::vector<TestNode*> result;
        for (int i = 0; i <= joiners; i++) {
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

    static bool AllJoined(const std::vector<TestNode*>& nodes) {
        for (size_t i = 1; i < nodes.size(); i++) {
            if (nodes[i]->protocol->GetState() !=
                ProtocolState::NORMAL_OPERATION) {
                return false;
            }
        }
        return true;
    }

    /// Advance one network manager superframe at a time until every joiner
    /// has joined; returns the number of superframes, or @p limit + 1
    int SuperframesUntilFormed(const std::vector<TestNode*>& nodes, int limit) {
        for (int superframes = 1; superframes <= limit; superframes++) {
            uint32_t superframe_ms = GetSuperframeDuration(*nodes.front());
            if (AdvanceTime(superframe_ms, superframe_ms, 50u, 0,
                            [&]() { return AllJoined(nodes); })) {
                return superframes;
            }
        }
        return limit + 1;
    }
};

TEST_P(JoinContentionTests, SimultaneousJoinersFormWithinBound) {
    const uint8_t sf = GetParam();
    std::vector<TestNode*> nodes = BuildFullMesh(sf, kSimultaneousJoiners);
    for (auto* node : nodes) {
        ASSERT_TRUE(StartNode(*node));
    }

    int superframes =
        SuperframesUntilFormed(nodes, 2 * kFormationSuperframeBound);
    RecordProperty("formation_superframes", superframes);

    EXPECT_LE(superframes, kFormationSuperframeBound)
        << kSimultaneousJoiners << " joiners at SF" << static_cast<int>(sf)
        << " needed " << superframes << " superframes";
}

TEST_P(JoinContentionTests, LoneJoinerJoinsInItsFirstSuperframe) {
    const uint8_t sf = GetParam();
    std::vector<TestNode*> nodes = BuildFullMesh(sf, 1);
    for (auto* node : nodes) {
        ASSERT_TRUE(StartNode(*node));
    }
    TestNode& joiner = *nodes.back();

    uint32_t limit_ms = GetDiscoveryTimeout(joiner) * 4;
    ASSERT_TRUE(AdvanceTime(limit_ms, limit_ms, 50u, 0, [&]() {
        return joiner.protocol->GetState() == ProtocolState::JOINING;
    }));
    const uint32_t joining_at = GetRTOS().getTickCount();
    const uint32_t superframe_ms = GetSuperframeDuration(*nodes.front());

    ASSERT_TRUE(AdvanceTime(limit_ms, limit_ms, 50u, 0, [&]() {
        return joiner.protocol->GetState() == ProtocolState::NORMAL_OPERATION;
    }));
    EXPECT_LE(GetRTOS().getTickCount() - joining_at, superframe_ms)
        << "Lone joiner at SF" << static_cast<int>(sf)
        << " did not join in the superframe it heard the beacon in";
}

INSTANTIATE_TEST_SUITE_P(HighSpreadingFactors, JoinContentionTests,
                         ::testing::Values<uint8_t>(10, 11, 12),
                         [](const ::testing::TestParamInfo<uint8_t>& info) {
                             return "SF" + std::to_string(
                                               static_cast<int>(info.param));
                         });

}  // namespace test
}  // namespace loramesher
