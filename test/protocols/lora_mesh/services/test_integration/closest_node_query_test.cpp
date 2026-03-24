/**
 * @file closest_node_query_test.cpp
 * @brief Test suite for GetClosestNodeByCapability, GetClosestGateway,
 *        and RouteEntry capability/is_network_manager fields
 */

#include <gtest/gtest.h>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include "lora_mesh_test_fixture.hpp"
#include "loramesher.hpp"
#include "types/node_capabilities.hpp"
#include "types/protocols/lora_mesh/network_node_route.hpp"

namespace loramesher {
namespace test {

/**
 * @brief Test suite for closest-node queries and RouteEntry field exposure
 */
class ClosestNodeQueryTests : public LoRaMeshTestFixture {
   protected:
    void SetUp() override { LoRaMeshTestFixture::SetUp(); }

    void TearDown() override { LoRaMeshTestFixture::TearDown(); }

    void WaitForTasksToExecute() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    /**
     * @brief Build a LoraMesher wrapping an already-started protocol node
     *
     * Since LoraMesher is the public API that owns GetClosestGateway etc.,
     * but the test fixture creates protocols directly, we test the underlying
     * data through the protocol and verify RouteEntry conversion.
     */
    std::vector<RouteEntry> GetRouteEntriesFromNode(TestNode& node) {
        std::vector<RouteEntry> routes;
        auto network_nodes = node.protocol->GetNetworkNodesCopy();
        routes.reserve(network_nodes.size());
        for (const auto& n : network_nodes) {
            RouteEntry entry;
            entry.destination = n.routing_entry.destination;
            entry.next_hop = n.next_hop;
            entry.hop_count = n.routing_entry.hop_count;
            entry.link_quality = n.GetLinkQuality();
            entry.last_seen_ms = n.last_seen;
            entry.is_valid = n.is_active;
            entry.capabilities = n.routing_entry.capabilities;
            entry.is_network_manager = n.is_network_manager;
            routes.push_back(entry);
        }
        return routes;
    }

    /**
     * @brief Find closest node by capability from a node's routing table
     *
     * Replicates LoraMesher::GetClosestNodeByCapability logic for testing
     * at the protocol level.
     */
    std::optional<RouteEntry> FindClosestNodeByCapability(TestNode& node,
                                                          uint8_t capability) {
        auto network_nodes = node.protocol->GetNetworkNodesCopy();

        std::optional<RouteEntry> best;
        uint16_t best_cost = UINT16_MAX;

        for (const auto& n : network_nodes) {
            if (!n.is_active) {
                continue;
            }
            if ((n.routing_entry.capabilities & capability) == 0) {
                continue;
            }

            uint16_t cost = types::protocols::lora_mesh::NetworkNodeRoute::
                CalculateRouteCost(n.routing_entry.hop_count,
                                   n.GetLinkQuality());

            if (cost < best_cost) {
                best_cost = cost;
                RouteEntry entry;
                entry.destination = n.routing_entry.destination;
                entry.next_hop = n.next_hop;
                entry.hop_count = n.routing_entry.hop_count;
                entry.link_quality = n.GetLinkQuality();
                entry.last_seen_ms = n.last_seen;
                entry.is_valid = n.is_active;
                entry.capabilities = n.routing_entry.capabilities;
                entry.is_network_manager = n.is_network_manager;
                best = entry;
            }
        }

        return best;
    }
};

/**
 * @brief Test that RouteEntry includes capabilities and is_network_manager
 *        after network formation
 */
TEST_F(ClosestNodeQueryTests, RouteEntryIncludesCapabilitiesAndManagerFlag) {
    auto& manager = CreateNode("Manager", 0x1001);
    auto& node2 = CreateNode("Node2", 0x1002);

    SetLinkStatus(manager, node2, true);

    ASSERT_TRUE(StartNode(manager));
    manager.protocol->SetNodeCapabilities(GATEWAY);

    WaitForTasksToExecute();

    auto discovery_timeout = GetDiscoveryTimeout(manager);
    auto slot_duration = GetSlotDuration(manager);

    bool became_manager = AdvanceTime(
        discovery_timeout + 500, discovery_timeout + 500, 15, 0, [&]() {
            return manager.protocol->GetState() ==
                   protocols::lora_mesh::INetworkService::ProtocolState::
                       NETWORK_MANAGER;
        });
    ASSERT_TRUE(became_manager) << "Node did not become network manager";

    ASSERT_TRUE(StartNode(node2));
    WaitForTasksToExecute();

    bool joined = AdvanceTime(discovery_timeout + 5000,
                              discovery_timeout + 5000, 15, 0, [&]() {
                                  return node2.protocol->GetState() ==
                                         protocols::lora_mesh::INetworkService::
                                             ProtocolState::NORMAL_OPERATION;
                              });
    ASSERT_TRUE(joined) << "Node2 did not join network";

    // Wait for routing info to propagate
    auto superframe = GetSuperframeDuration(manager);
    AdvanceTime(superframe * 3);
    WaitForTasksToExecute();

    // Get route entries from node2's perspective
    auto routes = GetRouteEntriesFromNode(node2);

    // Find the manager's entry
    bool found_manager = false;
    for (const auto& r : routes) {
        if (r.destination == 0x1001) {
            found_manager = true;
            EXPECT_EQ(r.capabilities, GATEWAY)
                << "Manager capabilities should be GATEWAY";
            EXPECT_TRUE(r.is_network_manager)
                << "Manager should have is_network_manager=true";
        }
    }
    EXPECT_TRUE(found_manager) << "Manager not found in node2's routing table";
}

/**
 * @brief Test GetClosestGateway returns nullopt when no gateways exist
 */
TEST_F(ClosestNodeQueryTests, GetClosestGatewayReturnsNulloptWhenNoGateways) {
    auto& manager = CreateNode("Manager", 0x1001);
    auto& node2 = CreateNode("Node2", 0x1002);

    SetLinkStatus(manager, node2, true);

    ASSERT_TRUE(StartNode(manager));
    // No gateway capability set on any node

    WaitForTasksToExecute();

    auto discovery_timeout = GetDiscoveryTimeout(manager);
    auto slot_duration = GetSlotDuration(manager);

    bool became_manager = AdvanceTime(
        discovery_timeout + 500, discovery_timeout + 500, 15, 0, [&]() {
            return manager.protocol->GetState() ==
                   protocols::lora_mesh::INetworkService::ProtocolState::
                       NETWORK_MANAGER;
        });
    ASSERT_TRUE(became_manager);

    ASSERT_TRUE(StartNode(node2));
    WaitForTasksToExecute();

    bool joined = AdvanceTime(discovery_timeout + 5000,
                              discovery_timeout + 5000, 15, 0, [&]() {
                                  return node2.protocol->GetState() ==
                                         protocols::lora_mesh::INetworkService::
                                             ProtocolState::NORMAL_OPERATION;
                              });
    ASSERT_TRUE(joined);

    auto superframe = GetSuperframeDuration(manager);
    AdvanceTime(superframe * 3);
    WaitForTasksToExecute();

    // Query for gateway from node2 - should return nullopt
    auto result = FindClosestNodeByCapability(node2, GATEWAY);
    EXPECT_FALSE(result.has_value())
        << "Should return nullopt when no gateways exist";
}

/**
 * @brief Test GetClosestGateway returns the closest gateway
 *        when multiple gateways exist
 */
TEST_F(ClosestNodeQueryTests,
       GetClosestGatewayReturnsClosestWhenMultipleExist) {
    // Create a line topology: node1 -- node2 -- node3
    // node1 and node3 are gateways; node2 queries for closest
    auto& node1 = CreateNode("GW1", 0x1001);
    auto& node2 = CreateNode("Querier", 0x1002);
    auto& node3 = CreateNode("GW2", 0x1003);

    // Line: node1 <-> node2 <-> node3
    SetLinkStatus(node1, node2, true);
    SetLinkStatus(node2, node3, true);
    SetLinkStatus(node1, node3, false);

    // Start node1 as manager
    ASSERT_TRUE(StartNode(node1));
    node1.protocol->SetNodeCapabilities(GATEWAY);

    WaitForTasksToExecute();

    auto discovery_timeout = GetDiscoveryTimeout(node1);
    auto slot_duration = GetSlotDuration(node1);

    bool became_manager = AdvanceTime(
        discovery_timeout + 500, discovery_timeout + 500, 15, 0, [&]() {
            return node1.protocol->GetState() ==
                   protocols::lora_mesh::INetworkService::ProtocolState::
                       NETWORK_MANAGER;
        });
    ASSERT_TRUE(became_manager);

    // Start node2
    ASSERT_TRUE(StartNode(node2));
    WaitForTasksToExecute();

    bool node2_joined = AdvanceTime(
        discovery_timeout + 5000, discovery_timeout + 5000, 15, 0, [&]() {
            return node2.protocol->GetState() ==
                   protocols::lora_mesh::INetworkService::ProtocolState::
                       NORMAL_OPERATION;
        });
    ASSERT_TRUE(node2_joined);

    // Start node3 with GATEWAY
    ASSERT_TRUE(StartNode(node3));
    node3.protocol->SetNodeCapabilities(GATEWAY);
    WaitForTasksToExecute();

    auto superframe = GetSuperframeDuration(node1);

    bool node3_joined =
        AdvanceTime(superframe * 3 + 5000, superframe * 3 + 5000, 15, 0, [&]() {
            return node3.protocol->GetState() ==
                   protocols::lora_mesh::INetworkService::ProtocolState::
                       NORMAL_OPERATION;
        });
    ASSERT_TRUE(node3_joined);

    // Wait for capabilities and routing to propagate
    superframe = GetSuperframeDuration(node1);
    AdvanceTime(superframe * 4);
    WaitForTasksToExecute();

    // Both node1 and node3 are gateways from node2's perspective
    // Both are direct neighbors (1 hop), so either could be closest
    auto result = FindClosestNodeByCapability(node2, GATEWAY);
    ASSERT_TRUE(result.has_value()) << "Should find at least one gateway";
    EXPECT_TRUE(result->destination == 0x1001 || result->destination == 0x1003)
        << "Closest gateway should be one of the two gateways";
    EXPECT_EQ(result->capabilities & GATEWAY, GATEWAY)
        << "Result should have GATEWAY capability";
}

/**
 * @brief Test GetClosestNodeByCapability with custom capability flag
 */
TEST_F(ClosestNodeQueryTests, GetClosestNodeByCustomCapability) {
    constexpr uint8_t CUSTOM_CAP = 0x02;

    auto& manager = CreateNode("Manager", 0x1001);
    auto& node2 = CreateNode("CustomNode", 0x1002);
    auto& node3 = CreateNode("Querier", 0x1003);

    SetLinkStatus(manager, node2, true);
    SetLinkStatus(manager, node3, true);
    SetLinkStatus(node2, node3, true);

    ASSERT_TRUE(StartNode(manager));
    WaitForTasksToExecute();

    auto discovery_timeout = GetDiscoveryTimeout(manager);
    auto slot_duration = GetSlotDuration(manager);

    bool became_manager = AdvanceTime(
        discovery_timeout + 500, discovery_timeout + 500, 15, 0, [&]() {
            return manager.protocol->GetState() ==
                   protocols::lora_mesh::INetworkService::ProtocolState::
                       NETWORK_MANAGER;
        });
    ASSERT_TRUE(became_manager);

    // Start node2 with custom capability
    ASSERT_TRUE(StartNode(node2));
    node2.protocol->SetNodeCapabilities(CUSTOM_CAP);
    WaitForTasksToExecute();

    bool node2_joined = AdvanceTime(
        discovery_timeout + 5000, discovery_timeout + 5000, 15, 0, [&]() {
            return node2.protocol->GetState() ==
                   protocols::lora_mesh::INetworkService::ProtocolState::
                       NORMAL_OPERATION;
        });
    ASSERT_TRUE(node2_joined);

    // Start node3 (no custom capability)
    ASSERT_TRUE(StartNode(node3));
    WaitForTasksToExecute();

    auto superframe = GetSuperframeDuration(manager);

    bool node3_joined =
        AdvanceTime(superframe * 3 + 5000, superframe * 3 + 5000, 15, 0, [&]() {
            return node3.protocol->GetState() ==
                   protocols::lora_mesh::INetworkService::ProtocolState::
                       NORMAL_OPERATION;
        });
    ASSERT_TRUE(node3_joined);

    // Wait for capabilities to propagate
    superframe = GetSuperframeDuration(manager);
    AdvanceTime(superframe * 4);
    WaitForTasksToExecute();

    // Query for CUSTOM_CAP from node3 - should find node2
    auto result = FindClosestNodeByCapability(node3, CUSTOM_CAP);
    ASSERT_TRUE(result.has_value())
        << "Should find node with custom capability";
    EXPECT_EQ(result->destination, 0x1002)
        << "Should find node2 with custom capability";
    EXPECT_NE(result->capabilities & CUSTOM_CAP, 0)
        << "Result should have custom capability";

    // Query for GATEWAY from node3 - should return nullopt
    auto gw_result = FindClosestNodeByCapability(node3, GATEWAY);
    EXPECT_FALSE(gw_result.has_value())
        << "Should not find gateway when none have GATEWAY capability";
}

/**
 * @brief Test GetClosestNodeByCapability ignores inactive routes
 */
TEST_F(ClosestNodeQueryTests, GetClosestNodeByCapabilityIgnoresInactiveRoutes) {
    auto& manager = CreateNode("Manager", 0x1001);
    auto& gw_node = CreateNode("Gateway", 0x1002);

    SetLinkStatus(manager, gw_node, true);

    ASSERT_TRUE(StartNode(manager));
    WaitForTasksToExecute();

    auto discovery_timeout = GetDiscoveryTimeout(manager);
    auto slot_duration = GetSlotDuration(manager);

    bool became_manager = AdvanceTime(
        discovery_timeout + 500, discovery_timeout + 500, 15, 0, [&]() {
            return manager.protocol->GetState() ==
                   protocols::lora_mesh::INetworkService::ProtocolState::
                       NETWORK_MANAGER;
        });
    ASSERT_TRUE(became_manager);

    // Start gateway node
    ASSERT_TRUE(StartNode(gw_node));
    gw_node.protocol->SetNodeCapabilities(GATEWAY);
    WaitForTasksToExecute();

    bool joined = AdvanceTime(discovery_timeout + 5000,
                              discovery_timeout + 5000, 15, 0, [&]() {
                                  return gw_node.protocol->GetState() ==
                                         protocols::lora_mesh::INetworkService::
                                             ProtocolState::NORMAL_OPERATION;
                              });
    ASSERT_TRUE(joined);

    // Wait for capabilities to propagate
    auto superframe = GetSuperframeDuration(manager);
    bool found_gateway =
        AdvanceTime(superframe * 3, superframe * 3, 100, 0, [&]() {
            auto result = FindClosestNodeByCapability(manager, GATEWAY);
            return result.has_value() && result->destination == 0x1002;
        });

    ASSERT_TRUE(found_gateway) << "Should find gateway before disconnection";

    // Verify gateway is found before disconnection
    auto result_before = FindClosestNodeByCapability(manager, GATEWAY);
    ASSERT_TRUE(result_before.has_value())
        << "Should find gateway before disconnection";
    EXPECT_EQ(result_before->destination, 0x1002);

    // Simulate node failure (disconnect the gateway)
    SimulateNodeFailure(gw_node);

    // Advance enough time for the route to expire
    auto route_timeout = discovery_timeout * 15;
    AdvanceTime(route_timeout, route_timeout, 100, 0, [&]() {
        // During this time, the route may still be present but should eventually be marked inactive
        auto intermediate_result =
            FindClosestNodeByCapability(manager, GATEWAY);
        if (intermediate_result.has_value() && intermediate_result->is_valid) {
            // If still valid, keep advancing
            return false;
        }
        return true;  // Keep advancing until route is removed or marked inactive
    });

    // Now query again - inactive routes should be ignored
    auto result_after = FindClosestNodeByCapability(manager, GATEWAY);

    // The route should either be gone or marked inactive
    if (result_after.has_value()) {
        // If still present, verify it's still marked valid
        // (some implementations may keep routes active for a while)
        EXPECT_FALSE(result_after->is_valid)
            << "If returned, route should be marked as invalid";
    }
    // Otherwise nullopt is the expected result for an inactive route
}

}  // namespace test
}  // namespace loramesher
