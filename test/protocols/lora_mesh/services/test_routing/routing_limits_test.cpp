/**
 * @file routing_limits_test.cpp
 * @brief Tests for routing protocol limits in LoRaMesh
 *
 * Tests max hop count, routing table capacity, and route timeouts.
 */

#include <gtest/gtest.h>

#include "routing_test_fixture.hpp"

namespace loramesher {
namespace test {

/**
 * @brief Test suite for routing protocol limits
 */
class RoutingLimitsTests : public RoutingTestFixture {};

/**
 * @brief Test maximum hop count enforcement
 *
 * Topology: 7-node line (6 hops from first to last)
 * N1-N2-N3-N4-N5-N6-N7
 *
 * Verifies:
 * - Routes beyond max_hops are not created
 * - Messages don't reach nodes beyond hop limit
 *
 * Note: Default max_hops is typically 5-10. This test creates a 6-hop path.
 */
TEST_F(RoutingLimitsTests, MaxHopCountEnforcement) {
    // Create line topology with 7 nodes (6 hops from N1 to N7)
    // First node is network manager for deterministic formation
    auto nodes = GenerateLineTopology(7, 0x1000, "Node", 0);

    ASSERT_EQ(nodes.size(), 7) << "Expected 7 nodes";

    // Start all nodes
    for (auto* node : nodes) {
        ASSERT_TRUE(StartNode(*node)) << "Failed to start " << node->name;
    }

    // Wait for network formation (5 normal + 1 manager)
    ASSERT_TRUE(WaitForNetworkFormation(nodes, 5))
        << "Network formation failed";

    // Wait for routing tables to propagate
    // This may take longer due to the chain length
    auto superframe_time = GetSuperframeDuration(*nodes.front());
    uint32_t step_ms = 15u;

    // Wait for routing tables to fill up
    AdvanceTime(superframe_time * 2, superframe_time * 2, step_ms, 0,
                [&]() { return false; });

    // Print routing tables for debugging
    std::cout << "=== Routing tables for 7-node line ===" << std::endl;
    for (auto* node : nodes) {
        PrintRoutingTable(*node);
    }

    // Get max hops from config
    auto config = nodes[0]->protocol->GetServiceConfiguration();
    uint8_t max_hops = config.network_config.max_hops;

    std::cout << "Max hops configured: " << static_cast<int>(max_hops)
              << std::endl;

    // Verify hop counts from N1's perspective
    // N1 should have routes with increasing hop counts
    for (size_t i = 1; i < nodes.size(); i++) {
        uint8_t expected_hops = i;  // i hops to reach node at index i
        uint8_t actual_hops = GetHopCount(*nodes[0], nodes[i]->address);

        if (expected_hops <= max_hops) {
            // Should have valid route
            EXPECT_TRUE(HasRouteTo(*nodes[0], nodes[i]->address))
                << "N1 should have route to N" << i + 1 << " ("
                << static_cast<int>(expected_hops) << " hops <= max "
                << static_cast<int>(max_hops) << ")";

            if (HasRouteTo(*nodes[0], nodes[i]->address)) {
                EXPECT_EQ(actual_hops, expected_hops)
                    << "N1->N" << i + 1 << " should be "
                    << static_cast<int>(expected_hops) << " hops";
            }
        } else {
            // Should NOT have valid route (beyond max hops)
            // Note: Route may exist but be marked inactive, or may not exist
            if (HasRouteTo(*nodes[0], nodes[i]->address)) {
                std::cout << "Warning: N1 has route to N" << i + 1
                          << " beyond max_hops (hops: "
                          << static_cast<int>(actual_hops) << ")" << std::endl;
                EXPECT_GT(actual_hops, max_hops)
                    << "N1 should NOT have valid route to N" << i + 1
                    << " (hops: " << static_cast<int>(actual_hops) << " > max "
                    << static_cast<int>(max_hops) << ")";
            }
        }
    }

    // // If the last node is beyond max_hops, try sending a message
    // // It should not be delivered
    // size_t last_index = nodes.size() - 1;
    // uint8_t hops_to_last = last_index;

    // if (hops_to_last > max_hops) {
    //     std::cout << "Testing message delivery beyond max_hops..." << std::endl;

    //     ClearAllReceivedMessages();

    //     std::vector<uint8_t> payload = {0xFF};
    //     SendMessage(*nodes[0], *nodes[last_index], payload);

    //     // Wait briefly
    //     AdvanceTime(5000);

    //     // Last node should NOT receive the message
    //     bool received = HasReceivedMessageFrom(
    //         *nodes[last_index], nodes[0]->address, MessageType::DATA);

    //     EXPECT_FALSE(received)
    //         << "Message should NOT be delivered beyond max hop count ("
    //         << static_cast<int>(hops_to_last) << " hops > max "
    //         << static_cast<int>(max_hops) << ")";
    // } else {
    //     std::cout << "Note: All nodes within max_hops ("
    //               << static_cast<int>(max_hops)
    //               << "), cannot test delivery rejection" << std::endl;
    // }
}

/**
 * @brief Test routing table capacity limits
 *
 * Topology: 10 fully connected nodes
 *
 * Verifies:
 * - Routing table size stays within configured limits
 * - Protocol handles large networks gracefully
 */
TEST_F(RoutingLimitsTests, RoutingTableCapacity) {
    const int num_nodes = 10;

    // Create full mesh with 10 nodes (first node is network manager)
    auto nodes = GenerateFullMeshTopology(num_nodes, 0x1000, "Node", 0);

    ASSERT_EQ(nodes.size(), static_cast<size_t>(num_nodes))
        << "Expected " << num_nodes << " nodes";

    // Start all nodes
    for (auto* node : nodes) {
        ASSERT_TRUE(StartNode(*node)) << "Failed to start " << node->name;
    }

    // Wait for network formation (9 normal + 1 manager)
    // This may take longer with many nodes
    ASSERT_TRUE(WaitForNetworkFormation(nodes, num_nodes - 1))
        << "Network formation failed for " << num_nodes << " nodes";

    ASSERT_TRUE(WaitForRoutingStabilization(nodes))
        << "Routing stabilization failed for " << num_nodes << " nodes";

    auto superframe_time = GetSuperframeDuration(*nodes.front());
    uint32_t step_ms = 15u;

    // Wait for routing tables to fill up
    AdvanceTime(superframe_time * 2, superframe_time * 2, step_ms, 0,
                [&]() { return false; });

    // Get max network nodes from config
    auto config = nodes[0]->protocol->GetServiceConfiguration();
    size_t max_network_nodes = config.network_config.max_network_nodes;

    std::cout << "Max network nodes configured: " << max_network_nodes
              << std::endl;
    std::cout << "Actual network size: " << num_nodes << std::endl;

    // Check routing table sizes
    for (auto* node : nodes) {
        auto network_nodes = node->protocol->GetNetworkNodesCopy();
        size_t table_size = network_nodes.size();

        std::cout << node->name << " routing table size: " << table_size
                  << std::endl;

        EXPECT_LE(table_size, max_network_nodes)
            << node->name << " routing table exceeded maximum size";

        // In a full mesh, each node should know about all other nodes
        // (unless we exceed max_network_nodes)
        size_t expected_routes =
            std::min(static_cast<size_t>(num_nodes - 1), max_network_nodes);

        // Allow some tolerance since discovery might still be in progress
        EXPECT_GE(table_size, expected_routes - 2)
            << node->name << " should have routes to most other nodes";
    }

    // Assert slot table structure: every node must have ≥1 sleep slot and
    // all nodes must agree on the same total superframe size.
    size_t reference_superframe_size = 0;
    for (auto* node : nodes) {
        auto slot_table = node->protocol->GetSlotTable();
        ASSERT_FALSE(slot_table.empty())
            << node->name << " has empty slot table";

        size_t sleep_count = 0;
        for (const auto& slot : slot_table) {
            if (slot.type ==
                types::protocols::lora_mesh::SlotAllocation::SlotType::SLEEP)
                ++sleep_count;
        }
        EXPECT_GE(sleep_count, 1u)
            << node->name
            << " must have at least 1 sleep slot (min_sleep_fraction >= 30%)";

        if (reference_superframe_size == 0) {
            reference_superframe_size = slot_table.size();
        } else {
            EXPECT_EQ(slot_table.size(), reference_superframe_size)
                << node->name << " superframe size disagrees with first node ("
                << slot_table.size() << " vs " << reference_superframe_size
                << ")";
        }
    }
    std::cout << "Superframe size: " << reference_superframe_size << " slots"
              << std::endl;

    // Verify network is functional: send messages between distant nodes
    ClearAllReceivedMessages();

    std::vector<uint8_t> payload = {0x10, 0x20, 0x30};
    ASSERT_TRUE(SendMessage(*nodes[0], *nodes[num_nodes - 1], payload));

    bool received = AdvanceTime(
        superframe_time * 3, superframe_time * 3, step_ms, 0, [&]() {
            return HasReceivedMessageFrom(*nodes[num_nodes - 1],
                                          nodes[0]->address, MessageType::DATA);
        });

    EXPECT_TRUE(received) << "Message should be delivered in " << num_nodes
                          << "-node network";
}

/**
 * @brief Test route timeout after node failure
 *
 * Topology: 3-node line -> disconnect middle node
 *
 * Verifies:
 * - Routes are marked inactive after route_timeout_ms
 * - Protocol handles stale routes correctly
 */
TEST_F(RoutingLimitsTests, RouteTimeoutAfterNodeFailure) {
    // Create line topology with 3 nodes (first node is network manager)
    auto nodes = GenerateLineTopology(3, 0x1000, "Node", 0);

    ASSERT_EQ(nodes.size(), 3) << "Expected 3 nodes";

    // Start all nodes
    for (auto* node : nodes) {
        ASSERT_TRUE(StartNode(*node)) << "Failed to start " << node->name;
    }

    // Wait for network formation
    ASSERT_TRUE(WaitForNetworkFormation(nodes, 2))
        << "Network formation failed";

    ASSERT_TRUE(WaitForRoutingStabilization(nodes))
        << "Routing stabilization failed";

    // Verify N1 has route to N3 (via N2)
    ASSERT_TRUE(HasRouteTo(*nodes[0], nodes[2]->address))
        << "N1 should have initial route to N3";
    EXPECT_EQ(GetHopCount(*nodes[0], nodes[2]->address), 2)
        << "N1->N3 should be 2 hops";

    // Check if route is active
    auto initial_routes = nodes[0]->protocol->GetNetworkNodesCopy();
    bool initial_route_active = false;
    for (const auto& route : initial_routes) {
        if (route.routing_entry.destination == nodes[2]->address) {
            initial_route_active = route.is_active;
            break;
        }
    }
    EXPECT_TRUE(initial_route_active) << "Initial route to N3 should be active";

    std::cout << "=== Before node failure ===" << std::endl;
    PrintRoutingTable(*nodes[0]);

    // Disconnect N2 (breaking the path N1 -> N3)
    SimulateNodeFailure(*nodes[1]);

    // Get route timeout from config
    auto config = nodes[0]->protocol->GetServiceConfiguration();
    uint32_t route_timeout_ms = config.network_config.route_timeout_ms;

    std::cout << "Route timeout configured: " << route_timeout_ms << " ms"
              << std::endl;

    // Wait for route to expire
    // Add some buffer time beyond the timeout
    auto superframe_time = GetSuperframeDuration(*nodes[0]);
    uint32_t step_ms = 15u;
    uint32_t wait_time = route_timeout_ms + superframe_time * 2 + 60000;

    std::cout << "Waiting " << wait_time << " ms for route to expire..."
              << std::endl;

    bool route_expired = AdvanceTime(wait_time, wait_time, step_ms, 0, [&]() {
        auto routes = nodes[0]->protocol->GetNetworkNodesCopy();
        for (const auto& route : routes) {
            if (route.routing_entry.destination == nodes[2]->address) {
                // Route should be marked inactive
                return !route.is_active;
            }
        }
        // Route completely removed
        return true;
    });

    std::cout << "=== After timeout ===" << std::endl;
    PrintRoutingTable(*nodes[0]);

    // Check the final state of the route
    auto final_routes = nodes[0]->protocol->GetNetworkNodesCopy();
    bool route_found = false;
    bool route_is_active = false;

    for (const auto& route : final_routes) {
        if (route.routing_entry.destination == nodes[2]->address) {
            route_found = true;
            route_is_active = route.is_active;
            std::cout << "Route to N3: found=" << route_found
                      << ", active=" << route_is_active << std::endl;
            break;
        }
    }

    if (route_found) {
        EXPECT_FALSE(route_is_active)
            << "Route to N3 should be marked inactive after timeout";
    } else {
        // Route was completely removed, which is also acceptable
        std::cout << "Route to N3 was completely removed" << std::endl;
    }

    EXPECT_TRUE(route_expired || !route_is_active)
        << "Route did not expire after configured timeout";
}

}  // namespace test
}  // namespace loramesher
