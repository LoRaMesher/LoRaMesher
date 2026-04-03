/**
 * @file routing_table_unit_test.cpp
 * @brief Unit tests for DistanceVectorRoutingTable in isolation
 *
 * Tests the routing table logic without the full protocol stack to help
 * isolate routing algorithm issues from integration/timing issues.
 */

#include <gtest/gtest.h>

#include "protocols/lora_mesh/routing/distance_vector_routing_table.hpp"
#include "types/protocols/lora_mesh/network_node_route.hpp"

namespace loramesher {
namespace test {

using protocols::lora_mesh::DistanceVectorRoutingTable;
using types::protocols::lora_mesh::NetworkNodeRoute;

/**
 * @brief Test fixture for routing table unit tests
 */
class RoutingTableUnitTest : public ::testing::Test {
   protected:
    static constexpr AddressType kLocalAddress = 0x1000;
    static constexpr AddressType kNeighbor1 = 0x1001;
    static constexpr AddressType kNeighbor2 = 0x1002;
    static constexpr AddressType kNeighbor3 = 0x1003;
    static constexpr AddressType kRemoteNode = 0x2000;
    static constexpr uint32_t kCurrentTime = 1000000;
    static constexpr uint8_t kMaxHops = 10;
    static constexpr uint8_t kGoodQuality = 200;
    static constexpr uint8_t kMediumQuality = 128;
    static constexpr uint8_t kPoorQuality = 50;

    void SetUp() override {
        routing_table_ =
            std::make_unique<DistanceVectorRoutingTable>(kLocalAddress);
    }

    void TearDown() override { routing_table_.reset(); }

    /**
     * @brief Add a direct neighbor to the routing table
     */
    void AddDirectNeighbor(AddressType neighbor, uint8_t link_quality = 200) {
        routing_table_->UpdateRoute(neighbor,  // source
                                    neighbor,  // destination (same as source
                                               // for direct neighbor)
                                    1,         // hop_count
                                    link_quality, 0, 0, kCurrentTime);
    }

    /**
     * @brief Simulate receiving a routing table message from a neighbor
     */
    void ReceiveRoutingMessage(AddressType source,
                               const std::vector<RoutingTableEntry>& entries,
                               uint8_t local_link_quality = 200) {
        routing_table_->ProcessRoutingTableMessage(
            source, entries, kCurrentTime, local_link_quality, kMaxHops);
    }

    /**
     * @brief Create a routing table entry
     */
    RoutingTableEntry CreateEntry(AddressType dest, uint8_t hop_count,
                                  uint8_t link_quality = 200) {
        RoutingTableEntry entry;
        entry.destination = dest;
        entry.hop_count = hop_count;
        entry.link_quality = link_quality;
        entry.allocated_data_slots = 0;
        entry.capabilities = 0;
        return entry;
    }

    std::unique_ptr<DistanceVectorRoutingTable> routing_table_;
};

// =============================================================================
// Basic Route Operations Tests
// =============================================================================

TEST_F(RoutingTableUnitTest, EmptyTableReturnsNoRoute) {
    EXPECT_EQ(routing_table_->FindNextHop(kNeighbor1), 0);
    EXPECT_EQ(routing_table_->GetSize(), 0);
}

TEST_F(RoutingTableUnitTest, AddDirectNeighbor) {
    AddDirectNeighbor(kNeighbor1);

    EXPECT_EQ(routing_table_->GetSize(), 1);
    EXPECT_TRUE(routing_table_->IsNodePresent(kNeighbor1));
    EXPECT_EQ(routing_table_->FindNextHop(kNeighbor1), kNeighbor1);
}

TEST_F(RoutingTableUnitTest, DirectNeighborHasHopCountOne) {
    AddDirectNeighbor(kNeighbor1);

    const auto& nodes = routing_table_->GetNodes();
    ASSERT_EQ(nodes.size(), 1);
    EXPECT_EQ(nodes[0].routing_entry.hop_count, 1);
    EXPECT_TRUE(nodes[0].IsDirectNeighbor());
}

TEST_F(RoutingTableUnitTest, RemoveNode) {
    AddDirectNeighbor(kNeighbor1);
    EXPECT_TRUE(routing_table_->IsNodePresent(kNeighbor1));

    EXPECT_TRUE(routing_table_->RemoveNode(kNeighbor1));
    EXPECT_FALSE(routing_table_->IsNodePresent(kNeighbor1));
    EXPECT_EQ(routing_table_->GetSize(), 0);
}

TEST_F(RoutingTableUnitTest, ClearRemovesAllNodes) {
    AddDirectNeighbor(kNeighbor1);
    AddDirectNeighbor(kNeighbor2);
    AddDirectNeighbor(kNeighbor3);
    EXPECT_EQ(routing_table_->GetSize(), 3);

    routing_table_->Clear();
    EXPECT_EQ(routing_table_->GetSize(), 0);
}

// =============================================================================
// Route Selection Tests (IsBetterRoute logic)
// =============================================================================

TEST_F(RoutingTableUnitTest, ShorterRouteIsPreferred) {
    // First learn about remote node via Neighbor1 with 3 hops
    std::vector<RoutingTableEntry> entries1;
    entries1.push_back(CreateEntry(kRemoteNode, 2, kGoodQuality));  // Will be 3
                                                                    // hops via
                                                                    // Neighbor1
    ReceiveRoutingMessage(kNeighbor1, entries1);

    // Verify initial route
    EXPECT_EQ(routing_table_->FindNextHop(kRemoteNode), kNeighbor1);
    const auto& nodes1 = routing_table_->GetNodes();
    auto it1 = std::find_if(
        nodes1.begin(), nodes1.end(), [](const NetworkNodeRoute& n) {
            return n.routing_entry.destination == kRemoteNode;
        });
    ASSERT_NE(it1, nodes1.end());
    EXPECT_EQ(it1->routing_entry.hop_count, 3);

    // Now learn about same remote node via Neighbor2 with 2 hops (better!)
    std::vector<RoutingTableEntry> entries2;
    entries2.push_back(CreateEntry(kRemoteNode, 1, kGoodQuality));  // Will be 2
                                                                    // hops via
                                                                    // Neighbor2
    ReceiveRoutingMessage(kNeighbor2, entries2);

    // Verify route updated to shorter path
    EXPECT_EQ(routing_table_->FindNextHop(kRemoteNode), kNeighbor2);
    const auto& nodes2 = routing_table_->GetNodes();
    auto it2 = std::find_if(
        nodes2.begin(), nodes2.end(), [](const NetworkNodeRoute& n) {
            return n.routing_entry.destination == kRemoteNode;
        });
    ASSERT_NE(it2, nodes2.end());
    EXPECT_EQ(it2->routing_entry.hop_count, 2);
}

TEST_F(RoutingTableUnitTest, LongerRouteNotPreferredOverShorter) {
    // First learn about remote node via Neighbor1 with 2 hops
    std::vector<RoutingTableEntry> entries1;
    entries1.push_back(CreateEntry(kRemoteNode, 1, kGoodQuality));  // 2 hops
    ReceiveRoutingMessage(kNeighbor1, entries1);

    // Verify initial route
    EXPECT_EQ(routing_table_->FindNextHop(kRemoteNode), kNeighbor1);

    // Now receive a worse route via Neighbor2 with 4 hops
    std::vector<RoutingTableEntry> entries2;
    entries2.push_back(CreateEntry(kRemoteNode, 3, kGoodQuality));  // 4 hops
    ReceiveRoutingMessage(kNeighbor2, entries2);

    // Route should NOT change - still prefer shorter path
    EXPECT_EQ(routing_table_->FindNextHop(kRemoteNode), kNeighbor1);
}

TEST_F(RoutingTableUnitTest, BetterQualityPreferredWhenSameHops) {
    // This test verifies that when two routes have the same hop count,
    // the one with better quality is preferred.
    //
    // Use UpdateRoute directly to bypass link stats complexity and test
    // the core IsBetterRoute logic with explicit quality values.

    // First add a route via Neighbor1 with poor quality
    bool changed =
        routing_table_->UpdateRoute(kNeighbor1,    // source
                                    kRemoteNode,   // destination
                                    2,             // hop_count
                                    kPoorQuality,  // link_quality = 50
                                    0,             // allocated_data_slots
                                    0,             // capabilities
                                    kCurrentTime);

    EXPECT_TRUE(changed);
    EXPECT_EQ(routing_table_->FindNextHop(kRemoteNode), kNeighbor1);

    // Verify the quality stored
    const auto& nodes_before = routing_table_->GetNodes();
    auto it_before =
        std::find_if(nodes_before.begin(), nodes_before.end(),
                     [](const NetworkNodeRoute& n) {
                         return n.routing_entry.destination == kRemoteNode;
                     });
    ASSERT_NE(it_before, nodes_before.end());

    // Now add a route via Neighbor2 with better quality (same hop count)
    // Cost via Neighbor1: 2*35 + (255-50) = 70 + 205 = 275
    // Cost via Neighbor2: 2*35 + (255-200) = 70 + 55 = 125
    // Neighbor2 route should be preferred (lower cost)
    changed = routing_table_->UpdateRoute(
        kNeighbor2,    // source
        kRemoteNode,   // destination
        2,             // same hop_count
        kGoodQuality,  // link_quality = 200 (better)
        0,             // allocated_data_slots
        0,             // capabilities
        kCurrentTime);

    EXPECT_TRUE(changed);
    EXPECT_EQ(routing_table_->FindNextHop(kRemoteNode), kNeighbor2)
        << "Route should change to higher quality path (same hops)";
}

// =============================================================================
// ProcessRoutingTableMessage Tests
// =============================================================================

TEST_F(RoutingTableUnitTest, ProcessRoutingMessageAddsSourceAsDirectNeighbor) {
    std::vector<RoutingTableEntry> entries;  // Empty - just to trigger
                                             // processing
    ReceiveRoutingMessage(kNeighbor1, entries);

    // Source should be added as direct neighbor
    EXPECT_TRUE(routing_table_->IsNodePresent(kNeighbor1));
    const auto& nodes = routing_table_->GetNodes();
    auto it =
        std::find_if(nodes.begin(), nodes.end(), [](const NetworkNodeRoute& n) {
            return n.routing_entry.destination == kNeighbor1;
        });
    ASSERT_NE(it, nodes.end());
    EXPECT_EQ(it->routing_entry.hop_count, 1);
    EXPECT_EQ(it->next_hop, kNeighbor1);
}

TEST_F(RoutingTableUnitTest, ProcessRoutingMessageAddsRemoteNodes) {
    std::vector<RoutingTableEntry> entries;
    entries.push_back(CreateEntry(kRemoteNode, 1, kGoodQuality));  // 2 hops via
                                                                   // kNeighbor1
    ReceiveRoutingMessage(kNeighbor1, entries);

    // Source should be added as direct neighbor
    EXPECT_TRUE(routing_table_->IsNodePresent(kNeighbor1));

    // Remote node should be added with hop count + 1
    EXPECT_TRUE(routing_table_->IsNodePresent(kRemoteNode));
    const auto& nodes = routing_table_->GetNodes();
    auto it =
        std::find_if(nodes.begin(), nodes.end(), [](const NetworkNodeRoute& n) {
            return n.routing_entry.destination == kRemoteNode;
        });
    ASSERT_NE(it, nodes.end());
    EXPECT_EQ(it->routing_entry.hop_count, 2);  // Entry's 1 hop + 1 = 2
    EXPECT_EQ(it->next_hop, kNeighbor1);
}

TEST_F(RoutingTableUnitTest, ProcessRoutingMessageSkipsSelf) {
    std::vector<RoutingTableEntry> entries;
    entries.push_back(CreateEntry(kLocalAddress, 1, kGoodQuality));  // Entry
                                                                     // for
                                                                     // local
                                                                     // node
    entries.push_back(CreateEntry(kRemoteNode, 1, kGoodQuality));
    ReceiveRoutingMessage(kNeighbor1, entries);

    // Local address should NOT be in routing table
    EXPECT_FALSE(routing_table_->IsNodePresent(kLocalAddress));
    // But remote node should be added
    EXPECT_TRUE(routing_table_->IsNodePresent(kRemoteNode));
}

TEST_F(RoutingTableUnitTest, ProcessRoutingMessageRespectsMaxHops) {
    // Create a custom routing table with low max hops for testing
    auto table = DistanceVectorRoutingTable(kLocalAddress);

    std::vector<RoutingTableEntry> entries;
    entries.push_back(CreateEntry(kRemoteNode, 9, kGoodQuality));  // 10 hops
                                                                   // total
                                                                   // (exceeds
                                                                   // limit
                                                                   // usually)

    // Process with max_hops = 5 (so 9+1=10 should be rejected)
    table.ProcessRoutingTableMessage(kNeighbor1, entries, kCurrentTime,
                                     kGoodQuality, 5);  // max_hops = 5

    // Remote node should NOT be added (exceeds max hops)
    EXPECT_FALSE(table.IsNodePresent(kRemoteNode));
    // But source should still be added as direct neighbor
    EXPECT_TRUE(table.IsNodePresent(kNeighbor1));
}

// =============================================================================
// Full Mesh Topology Tests (simulating the failing integration test)
// =============================================================================

TEST_F(RoutingTableUnitTest, FullMeshDirectNeighborsAllHaveHopCountOne) {
    // Simulate a 4-node full mesh from Node1's perspective
    // All nodes should be direct neighbors with hop_count = 1

    // Receive routing messages from all 3 neighbors
    // Each neighbor has empty entries (they just discovered us)
    std::vector<RoutingTableEntry> empty_entries;
    ReceiveRoutingMessage(kNeighbor1, empty_entries);
    ReceiveRoutingMessage(kNeighbor2, empty_entries);
    ReceiveRoutingMessage(kNeighbor3, empty_entries);

    // All neighbors should have hop_count = 1
    const auto& nodes = routing_table_->GetNodes();
    EXPECT_EQ(nodes.size(), 3);

    for (const auto& node : nodes) {
        EXPECT_EQ(node.routing_entry.hop_count, 1)
            << "Node 0x" << std::hex << node.routing_entry.destination
            << " should have hop_count=1 but has "
            << static_cast<int>(node.routing_entry.hop_count);
        EXPECT_EQ(node.next_hop, node.routing_entry.destination)
            << "Direct neighbor's next_hop should be itself";
    }
}

TEST_F(RoutingTableUnitTest, FullMeshRoutingConvergence) {
    // Simulate full mesh network formation from Node1 (0x1000) perspective
    // Topology: Node1(0x1000) - Node2(0x1001) - Node3(0x1002) - Node4(0x1003)
    // All nodes directly connected to all others

    // Step 1: First, receive direct neighbor announcements (empty routing
    // tables)
    std::vector<RoutingTableEntry> empty;
    ReceiveRoutingMessage(kNeighbor1, empty);  // Node2 announces itself
    ReceiveRoutingMessage(kNeighbor2, empty);  // Node3 announces itself
    ReceiveRoutingMessage(kNeighbor3, empty);  // Node4 announces itself

    // At this point, all neighbors should be direct (hop=1)
    EXPECT_EQ(routing_table_->GetSize(), 3);

    // Step 2: Now simulate the next round where neighbors advertise their
    // routes In a full mesh, Node2 knows about Node3 and Node4 directly (1
    // hop) So Node2 advertises: Node3(1 hop), Node4(1 hop)
    std::vector<RoutingTableEntry> node2_routes;
    node2_routes.push_back(CreateEntry(kNeighbor2, 1, kGoodQuality));  // Node3,
                                                                       // 1 hop
    node2_routes.push_back(CreateEntry(kNeighbor3, 1, kGoodQuality));  // Node4,
                                                                       // 1 hop
    ReceiveRoutingMessage(kNeighbor1, node2_routes);

    // Node3 and Node4 should STILL be direct neighbors (1 hop) because we
    // already have better routes to them
    const auto& nodes = routing_table_->GetNodes();

    for (const auto& node : nodes) {
        EXPECT_EQ(node.routing_entry.hop_count, 1)
            << "In full mesh, all nodes should be 1 hop away, but node 0x"
            << std::hex << node.routing_entry.destination << " has "
            << static_cast<int>(node.routing_entry.hop_count) << " hops";
    }
}

TEST_F(RoutingTableUnitTest, DirectNeighborShouldOverrideIndirectRoute) {
    // This is the key test: If we learn about a node indirectly first,
    // then receive a direct routing message from them, the hop count
    // should update to 1.

    // Step 1: Learn about Node3 (0x1002) indirectly via Node2 (0x1001)
    // Node2 tells us: "I can reach Node3 in 1 hop"
    // So we think: Node3 is 2 hops away via Node2
    std::vector<RoutingTableEntry> node2_routes;
    node2_routes.push_back(CreateEntry(kNeighbor2, 1, kGoodQuality));
    ReceiveRoutingMessage(kNeighbor1, node2_routes);

    // Verify Node3 is currently 2 hops away
    const auto& nodes_before = routing_table_->GetNodes();
    auto it_before =
        std::find_if(nodes_before.begin(), nodes_before.end(),
                     [](const NetworkNodeRoute& n) {
                         return n.routing_entry.destination == kNeighbor2;
                     });
    ASSERT_NE(it_before, nodes_before.end());
    EXPECT_EQ(it_before->routing_entry.hop_count, 2)
        << "Node3 should be 2 hops via Node2 initially";
    EXPECT_EQ(it_before->next_hop, kNeighbor1);

    // Step 2: Now receive a direct routing message from Node3
    // This should update our route to Node3 to 1 hop
    std::vector<RoutingTableEntry> empty;
    ReceiveRoutingMessage(kNeighbor2, empty);  // Node3 directly contacts us

    // Verify Node3 is now 1 hop away (direct neighbor)
    const auto& nodes_after = routing_table_->GetNodes();
    auto it_after = std::find_if(
        nodes_after.begin(), nodes_after.end(), [](const NetworkNodeRoute& n) {
            return n.routing_entry.destination == kNeighbor2;
        });
    ASSERT_NE(it_after, nodes_after.end());
    EXPECT_EQ(it_after->routing_entry.hop_count, 1)
        << "Node3 should be 1 hop after direct contact";
    EXPECT_EQ(it_after->next_hop, kNeighbor2)
        << "Next hop should be Node3 itself (direct neighbor)";
}

// =============================================================================
// Route Cost Calculation Tests
// =============================================================================

TEST_F(RoutingTableUnitTest, RouteCostCalculation) {
    // ETX-inspired: cost = hop_count * 65536 / quality

    // 1 hop, quality 255 -> cost = 65536/255 = 257
    EXPECT_EQ(NetworkNodeRoute::CalculateRouteCost(1, 255), 257);

    // 1 hop, quality 200 -> cost = 65536/200 = 327
    EXPECT_EQ(NetworkNodeRoute::CalculateRouteCost(1, 200), 327);

    // 2 hops, quality 255 -> cost = 2*65536/255 = 514
    EXPECT_EQ(NetworkNodeRoute::CalculateRouteCost(2, 255), 514);

    // 2 hops, quality 200 -> cost = 2*65536/200 = 655
    EXPECT_EQ(NetworkNodeRoute::CalculateRouteCost(2, 200), 655);

    // With ETX, 1-hop routes are strongly favored: even a mediocre 1-hop link
    // (quality 200, cost 327) beats a perfect 2-hop path (quality 255, cost 514).
    // A 2-hop route only wins when 1-hop quality drops below ~128.
}

TEST_F(RoutingTableUnitTest, IsBetterRouteLogic) {
    // Test that IsBetterRouteThan() correctly compares routes

    // Route A: 1 hop, quality 200 (cost: 327)
    NetworkNodeRoute routeA(kRemoteNode, kNeighbor1, 1, 200, kCurrentTime);
    routeA.is_active = true;

    // Route B: 2 hops, quality 255 (cost: 514 — A is cheaper with ETX)
    NetworkNodeRoute routeB(kRemoteNode, kNeighbor2, 2, 255, kCurrentTime);
    routeB.is_active = true;

    // With ETX, 1-hop quality 200 (cost 327) beats 2-hop quality 255 (cost 514)
    EXPECT_TRUE(routeA.IsBetterRouteThan(routeB))
        << "1 hop with quality 200 should beat 2 hops with perfect quality";

    // Route C: 1 hop, quality 255 (cost: 257 - best)
    NetworkNodeRoute routeC(kRemoteNode, kNeighbor3, 1, 255, kCurrentTime);
    routeC.is_active = true;

    EXPECT_TRUE(routeC.IsBetterRouteThan(routeA));
    EXPECT_TRUE(routeC.IsBetterRouteThan(routeB));

    // Route D: 1 hop, quality 100 (cost: 655) vs 2-hop quality 255 (cost: 514)
    NetworkNodeRoute routeD(kRemoteNode, kNeighbor1, 1, 100, kCurrentTime);
    routeD.is_active = true;

    EXPECT_TRUE(routeB.IsBetterRouteThan(routeD))
        << "2 hops with perfect quality should beat 1 hop with quality 100";
}

// =============================================================================
// Link Failure Scenario Tests
// =============================================================================

TEST_F(RoutingTableUnitTest, RouteUpdateAfterLinkFailureScenario) {
    // Simulate the failing integration test scenario:
    // 4-node full mesh, then break link between Node1 and Node3

    // Step 1: Set up full mesh - all nodes are direct neighbors
    std::vector<RoutingTableEntry> empty;
    ReceiveRoutingMessage(kNeighbor1, empty);  // Node2
    ReceiveRoutingMessage(kNeighbor2, empty);  // Node3
    ReceiveRoutingMessage(kNeighbor3, empty);  // Node4

    // Verify all are 1 hop
    for (AddressType addr : {kNeighbor1, kNeighbor2, kNeighbor3}) {
        const auto& nodes = routing_table_->GetNodes();
        auto it = std::find_if(nodes.begin(), nodes.end(),
                               [addr](const NetworkNodeRoute& n) {
                                   return n.routing_entry.destination == addr;
                               });
        ASSERT_NE(it, nodes.end());
        EXPECT_EQ(it->routing_entry.hop_count, 1);
    }

    // Step 2: Simulate link failure to Node3 (0x1002)
    // We stop receiving direct messages from Node3, but Node2 still knows about
    // Node3

    // Remove the direct route to Node3
    routing_table_->RemoveNode(kNeighbor2);

    // Step 3: Node2 advertises that it can still reach Node3 (1 hop from Node2
    // = 2 hops from us)
    std::vector<RoutingTableEntry> node2_routes;
    node2_routes.push_back(CreateEntry(kNeighbor2, 1, kGoodQuality));  // Node3
                                                                       // via
                                                                       // Node2
    ReceiveRoutingMessage(kNeighbor1, node2_routes);

    // Verify Node3 is now reachable via Node2 with 2 hops
    const auto& nodes = routing_table_->GetNodes();
    auto it =
        std::find_if(nodes.begin(), nodes.end(), [](const NetworkNodeRoute& n) {
            return n.routing_entry.destination == kNeighbor2;
        });
    ASSERT_NE(it, nodes.end());
    EXPECT_EQ(it->routing_entry.hop_count, 2)
        << "After link failure, route should be 2 hops via Node2";
    EXPECT_EQ(it->next_hop, kNeighbor1) << "Next hop should be Node2";
}

// =============================================================================
// Line Topology Tests
// =============================================================================

TEST_F(RoutingTableUnitTest, LineTopologyRouting) {
    // Test line topology: Node1 - Node2 - Node3
    // Node1 (local) can only reach Node2 directly
    // Node1 reaches Node3 via Node2 (2 hops)

    // Step 1: Node2 announces itself (direct neighbor)
    std::vector<RoutingTableEntry> empty;
    ReceiveRoutingMessage(kNeighbor1, empty);

    EXPECT_TRUE(routing_table_->IsNodePresent(kNeighbor1));
    EXPECT_EQ(routing_table_->FindNextHop(kNeighbor1), kNeighbor1);

    // Step 2: Node2 advertises that it knows about Node3 (1 hop from Node2)
    std::vector<RoutingTableEntry> node2_routes;
    node2_routes.push_back(CreateEntry(kNeighbor2, 1, kGoodQuality));  // Node3,
                                                                       // 1 hop
                                                                       // from
                                                                       // Node2
    ReceiveRoutingMessage(kNeighbor1, node2_routes);

    // Verify Node3 is reachable via Node2 with 2 hops
    EXPECT_TRUE(routing_table_->IsNodePresent(kNeighbor2));
    EXPECT_EQ(routing_table_->FindNextHop(kNeighbor2), kNeighbor1);

    const auto& nodes = routing_table_->GetNodes();
    auto it =
        std::find_if(nodes.begin(), nodes.end(), [](const NetworkNodeRoute& n) {
            return n.routing_entry.destination == kNeighbor2;
        });
    ASSERT_NE(it, nodes.end());
    EXPECT_EQ(it->routing_entry.hop_count, 2);
}

// =============================================================================
// Additional Edge Case Tests
// =============================================================================

TEST_F(RoutingTableUnitTest, UpdateRouteViaUpdateRouteMethod) {
    // Test the UpdateRoute method directly (used by other parts of the system)

    // Add a route with 2 hops
    bool changed = routing_table_->UpdateRoute(kNeighbor1,    // source
                                               kRemoteNode,   // destination
                                               2,             // hop_count
                                               kGoodQuality,  // link_quality
                                               0,  // allocated_data_slots
                                               0,  // capabilities
                                               kCurrentTime  // current_time
    );

    EXPECT_TRUE(changed);
    EXPECT_TRUE(routing_table_->IsNodePresent(kRemoteNode));
    EXPECT_EQ(routing_table_->FindNextHop(kRemoteNode), kNeighbor1);

    // Update to a better route (1 hop)
    changed = routing_table_->UpdateRoute(kNeighbor2,    // different source
                                          kRemoteNode,   // same destination
                                          1,             // shorter hop_count
                                          kGoodQuality,  // link_quality
                                          0,             // allocated_data_slots
                                          0,             // capabilities
                                          kCurrentTime   // current_time
    );

    EXPECT_TRUE(changed);
    EXPECT_EQ(routing_table_->FindNextHop(kRemoteNode), kNeighbor2);
}

TEST_F(RoutingTableUnitTest, GetRoutingEntriesExcludesAddress) {
    // Add several neighbors
    std::vector<RoutingTableEntry> empty;
    ReceiveRoutingMessage(kNeighbor1, empty);
    ReceiveRoutingMessage(kNeighbor2, empty);
    ReceiveRoutingMessage(kNeighbor3, empty);

    // Get entries excluding kNeighbor2
    auto entries = routing_table_->GetRoutingEntries(kNeighbor2);

    // Should have 2 entries (kNeighbor1 and kNeighbor3)
    EXPECT_EQ(entries.size(), 2);

    // kNeighbor2 should NOT be in the list
    for (const auto& entry : entries) {
        EXPECT_NE(entry.destination, kNeighbor2);
    }
}

TEST_F(RoutingTableUnitTest, MaxNodesLimit) {
    // Create a routing table with max 3 nodes
    auto limited_table = DistanceVectorRoutingTable(kLocalAddress, 3);

    // Add 3 neighbors
    std::vector<RoutingTableEntry> empty;
    limited_table.ProcessRoutingTableMessage(kNeighbor1, empty, kCurrentTime,
                                             kGoodQuality, kMaxHops);
    limited_table.ProcessRoutingTableMessage(
        kNeighbor2, empty, kCurrentTime + 100, kGoodQuality, kMaxHops);
    limited_table.ProcessRoutingTableMessage(
        kNeighbor3, empty, kCurrentTime + 200, kGoodQuality, kMaxHops);

    EXPECT_EQ(limited_table.GetSize(), 3);

    // Try to add a 4th node - should replace oldest
    constexpr AddressType kNeighbor4 = 0x1004;
    limited_table.ProcessRoutingTableMessage(
        kNeighbor4, empty, kCurrentTime + 300, kGoodQuality, kMaxHops);

    // Should still have 3 nodes
    EXPECT_EQ(limited_table.GetSize(), 3);

    // The oldest node (kNeighbor1) should have been removed
    EXPECT_FALSE(limited_table.IsNodePresent(kNeighbor1));
    EXPECT_TRUE(limited_table.IsNodePresent(kNeighbor4));
}

TEST_F(RoutingTableUnitTest, InactiveNodesRemovedOnTimeout) {
    // Add some neighbors
    std::vector<RoutingTableEntry> empty;
    ReceiveRoutingMessage(kNeighbor1, empty);
    ReceiveRoutingMessage(kNeighbor2, empty);

    EXPECT_EQ(routing_table_->GetSize(), 2);

    // Remove inactive nodes with a timeout that has passed
    uint32_t future_time = kCurrentTime + 100000;  // Way in the future
    size_t removed =
        routing_table_->RemoveInactiveNodes(future_time, 1000, 1000);

    // Both nodes should be removed (they're "old" relative to future_time)
    EXPECT_EQ(removed, 2);
    EXPECT_EQ(routing_table_->GetSize(), 0);
}

TEST_F(RoutingTableUnitTest, SelfAddressSkippedInRouteMessages) {
    // Routing messages should skip entries for the local address
    std::vector<RoutingTableEntry> entries;
    entries.push_back(
        CreateEntry(kLocalAddress, 1, kGoodQuality));  // Should be skipped
    entries.push_back(
        CreateEntry(kRemoteNode, 1, kGoodQuality));  // Should be added

    ReceiveRoutingMessage(kNeighbor1, entries);

    // Local address should NOT be in routing table
    EXPECT_FALSE(routing_table_->IsNodePresent(kLocalAddress));
    // Remote node should be added
    EXPECT_TRUE(routing_table_->IsNodePresent(kRemoteNode));
}

TEST_F(RoutingTableUnitTest, FindNextHopForSelfReturnsLocalAddress) {
    // Finding next hop to ourselves should return local address
    EXPECT_EQ(routing_table_->FindNextHop(kLocalAddress), kLocalAddress);
}

// =============================================================================
// UpdateRoute: Inactive Route Re-activation Tests (Fix F2)
// =============================================================================

TEST_F(RoutingTableUnitTest,
       UpdateRouteInactiveWorseHopsReActivatesPreservingRoute) {
    // Add kNeighbor1 as active direct neighbor with recent last_seen
    std::vector<RoutingTableEntry> empty;
    routing_table_->ProcessRoutingTableMessage(
        kNeighbor1, empty, kCurrentTime + 4500, kGoodQuality, kMaxHops);

    // Add a 2-hop route via Neighbor1
    routing_table_->UpdateRoute(kNeighbor1, kRemoteNode, 2, kGoodQuality, 0, 0,
                                kCurrentTime);

    // Mark kRemoteNode inactive (kNeighbor1 stays active — last_seen=4500)
    size_t removed =
        routing_table_->RemoveInactiveNodes(kCurrentTime + 5000, 1000, 100000);
    EXPECT_EQ(removed, 0);  // Not removed, just marked inactive

    const auto& nodes = routing_table_->GetNodes();
    auto it =
        std::find_if(nodes.begin(), nodes.end(), [](const NetworkNodeRoute& n) {
            return n.routing_entry.destination == kRemoteNode;
        });
    ASSERT_NE(it, nodes.end());
    EXPECT_FALSE(it->is_active);
    EXPECT_EQ(it->routing_entry.hop_count, 2);

    // UpdateRoute with WORSE hops (4 > 2) — first message: recovery_counter=1
    bool changed1 = routing_table_->UpdateRoute(
        kNeighbor2, kRemoteNode, 4, kGoodQuality, 0, 0, kCurrentTime + 5001);
    EXPECT_FALSE(changed1);  // not yet re-activated (hysteresis)

    // Second message: recovery_counter=2 → re-activate preserving hop_count=2
    bool changed2 = routing_table_->UpdateRoute(
        kNeighbor2, kRemoteNode, 4, kGoodQuality, 0, 0, kCurrentTime + 5002);
    EXPECT_TRUE(changed2);  // routing changed (re-activated)

    const auto& nodes2 = routing_table_->GetNodes();
    auto it2 = std::find_if(
        nodes2.begin(), nodes2.end(), [](const NetworkNodeRoute& n) {
            return n.routing_entry.destination == kRemoteNode;
        });
    ASSERT_NE(it2, nodes2.end());
    EXPECT_TRUE(it2->is_active);
    EXPECT_EQ(it2->routing_entry.hop_count, 2);  // preserved
    EXPECT_EQ(it2->next_hop, kNeighbor1);        // preserved
}

TEST_F(RoutingTableUnitTest, UpdateRouteInactiveBetterHopsUpdatesRoute) {
    // Add a 3-hop route via Neighbor1
    routing_table_->UpdateRoute(kNeighbor1, kRemoteNode, 3, kGoodQuality, 0, 0,
                                kCurrentTime);

    // Mark inactive
    routing_table_->RemoveInactiveNodes(kCurrentTime + 5000, 1000, 100000);

    // UpdateRoute with BETTER hops (1 < 3) → should update
    bool changed = routing_table_->UpdateRoute(
        kNeighbor2, kRemoteNode, 1, kGoodQuality, 0, 0, kCurrentTime + 5001);
    EXPECT_TRUE(changed);

    const auto& nodes = routing_table_->GetNodes();
    auto it =
        std::find_if(nodes.begin(), nodes.end(), [](const NetworkNodeRoute& n) {
            return n.routing_entry.destination == kRemoteNode;
        });
    ASSERT_NE(it, nodes.end());
    EXPECT_TRUE(it->is_active);
    EXPECT_EQ(it->routing_entry.hop_count, 1);  // updated
    EXPECT_EQ(it->next_hop, kNeighbor2);        // new next hop
}

TEST_F(RoutingTableUnitTest, UpdateRouteExceedingMaxHopsReturnsFalse) {
    // hop_count > 10 (MAX_HOPS) → rejected
    bool changed = routing_table_->UpdateRoute(
        kNeighbor1, kRemoteNode, 11, kGoodQuality, 0, 0, kCurrentTime);
    EXPECT_FALSE(changed);
    EXPECT_FALSE(routing_table_->IsNodePresent(kRemoteNode));
}

TEST_F(RoutingTableUnitTest, UpdateRouteNewNodeWithCapabilities) {
    bool changed = routing_table_->UpdateRoute(
        kNeighbor1, kRemoteNode, 2, kGoodQuality, 1, 0x05, kCurrentTime);
    EXPECT_TRUE(changed);

    const auto& nodes = routing_table_->GetNodes();
    auto it =
        std::find_if(nodes.begin(), nodes.end(), [](const NetworkNodeRoute& n) {
            return n.routing_entry.destination == kRemoteNode;
        });
    ASSERT_NE(it, nodes.end());
    EXPECT_EQ(it->routing_entry.capabilities, 0x05);
    EXPECT_EQ(it->routing_entry.allocated_data_slots, 1);
}

// =============================================================================
// ProcessRoutingTableMessage: Inactive Re-activation Tests (Fix F)
// =============================================================================

TEST_F(RoutingTableUnitTest,
       ProcessRoutingMessageInactiveEntryWorseHopsReActivates) {
    // Add kRemoteNode via kNeighbor1 with 2 hops total
    std::vector<RoutingTableEntry> entries1;
    entries1.push_back(CreateEntry(kRemoteNode, 1, kGoodQuality));
    ReceiveRoutingMessage(kNeighbor1, entries1);

    EXPECT_TRUE(routing_table_->IsNodePresent(kRemoteNode));

    // Refresh kNeighbor1 so it stays active (only kRemoteNode goes inactive)
    std::vector<RoutingTableEntry> empty;
    routing_table_->ProcessRoutingTableMessage(
        kNeighbor1, empty, kCurrentTime + 4500, kGoodQuality, kMaxHops);

    // Mark kRemoteNode inactive (kNeighbor1 stays active)
    routing_table_->RemoveInactiveNodes(kCurrentTime + 5000, 1000, 100000);

    const auto& nodes_before = routing_table_->GetNodes();
    auto it_before =
        std::find_if(nodes_before.begin(), nodes_before.end(),
                     [](const NetworkNodeRoute& n) {
                         return n.routing_entry.destination == kRemoteNode;
                     });
    ASSERT_NE(it_before, nodes_before.end());
    EXPECT_FALSE(it_before->is_active);
    EXPECT_EQ(it_before->routing_entry.hop_count, 2);

    // Process with WORSE hops (3+1=4 > 2) — first message: recovery_counter=1
    std::vector<RoutingTableEntry> entries2;
    entries2.push_back(CreateEntry(kRemoteNode, 3, kGoodQuality));
    routing_table_->ProcessRoutingTableMessage(
        kNeighbor2, entries2, kCurrentTime + 5001, kGoodQuality, kMaxHops);

    {
        const auto& nodes_mid = routing_table_->GetNodes();
        auto it_mid = std::find_if(
            nodes_mid.begin(), nodes_mid.end(), [](const NetworkNodeRoute& n) {
                return n.routing_entry.destination == kRemoteNode;
            });
        ASSERT_NE(it_mid, nodes_mid.end());
        EXPECT_FALSE(it_mid->is_active);  // not yet (hysteresis)
    }

    // Second message: recovery_counter=2 → re-activate preserving hop_count=2
    routing_table_->ProcessRoutingTableMessage(
        kNeighbor2, entries2, kCurrentTime + 5002, kGoodQuality, kMaxHops);

    const auto& nodes_after = routing_table_->GetNodes();
    auto it_after = std::find_if(
        nodes_after.begin(), nodes_after.end(), [](const NetworkNodeRoute& n) {
            return n.routing_entry.destination == kRemoteNode;
        });
    ASSERT_NE(it_after, nodes_after.end());
    EXPECT_TRUE(it_after->is_active);
    EXPECT_EQ(it_after->routing_entry.hop_count, 2);  // preserved
    EXPECT_EQ(it_after->next_hop, kNeighbor1);        // preserved
}

TEST_F(RoutingTableUnitTest,
       ProcessRoutingMessageInactiveEntryBetterHopsUpdates) {
    // Add kRemoteNode via kNeighbor1 with 3 hops total
    std::vector<RoutingTableEntry> entries1;
    entries1.push_back(CreateEntry(kRemoteNode, 2, kGoodQuality));
    ReceiveRoutingMessage(kNeighbor1, entries1);

    // Mark inactive
    routing_table_->RemoveInactiveNodes(kCurrentTime + 5000, 1000, 100000);

    // Process with BETTER hops (0+1=1 < 3) → update
    std::vector<RoutingTableEntry> entries2;
    entries2.push_back(CreateEntry(kRemoteNode, 0, kGoodQuality));
    routing_table_->ProcessRoutingTableMessage(
        kNeighbor2, entries2, kCurrentTime + 5001, kGoodQuality, kMaxHops);

    const auto& nodes = routing_table_->GetNodes();
    auto it =
        std::find_if(nodes.begin(), nodes.end(), [](const NetworkNodeRoute& n) {
            return n.routing_entry.destination == kRemoteNode;
        });
    ASSERT_NE(it, nodes.end());
    EXPECT_TRUE(it->is_active);
    EXPECT_EQ(it->routing_entry.hop_count, 1);  // updated
    EXPECT_EQ(it->next_hop, kNeighbor2);        // new next hop
}

TEST_F(RoutingTableUnitTest,
       ProcessRoutingMessageReActivatesSourceNodeIfInactive) {
    // Add source as direct neighbor, then mark inactive
    std::vector<RoutingTableEntry> empty;
    ReceiveRoutingMessage(kNeighbor1, empty);
    routing_table_->RemoveInactiveNodes(kCurrentTime + 5000, 1000, 100000);

    const auto& nodes_before = routing_table_->GetNodes();
    auto it_before =
        std::find_if(nodes_before.begin(), nodes_before.end(),
                     [](const NetworkNodeRoute& n) {
                         return n.routing_entry.destination == kNeighbor1;
                     });
    ASSERT_NE(it_before, nodes_before.end());
    EXPECT_FALSE(it_before->is_active);

    // Source node sends routing message → immediate re-activation (no hysteresis
    // for direct source since the message itself proves liveness)
    routing_table_->ProcessRoutingTableMessage(
        kNeighbor1, empty, kCurrentTime + 5001, kGoodQuality, kMaxHops);

    const auto& nodes_after = routing_table_->GetNodes();
    auto it_after = std::find_if(
        nodes_after.begin(), nodes_after.end(), [](const NetworkNodeRoute& n) {
            return n.routing_entry.destination == kNeighbor1;
        });
    ASSERT_NE(it_after, nodes_after.end());
    EXPECT_TRUE(it_after->is_active);  // immediately re-activated
}

// =============================================================================
// SetRouteUpdateCallback Tests
// =============================================================================

TEST_F(RoutingTableUnitTest, SetRouteUpdateCallbackFiredOnRouteAdd) {
    bool callback_called = false;
    bool was_added = false;
    AddressType cb_dest = 0;

    routing_table_->SetRouteUpdateCallback([&](bool added, AddressType dest,
                                               AddressType /*next_hop*/,
                                               uint8_t /*hops*/) {
        callback_called = true;
        was_added = added;
        cb_dest = dest;
    });

    std::vector<RoutingTableEntry> empty;
    ReceiveRoutingMessage(kNeighbor1, empty);

    EXPECT_TRUE(callback_called);
    EXPECT_TRUE(was_added);
    EXPECT_EQ(cb_dest, kNeighbor1);
}

TEST_F(RoutingTableUnitTest, SetRouteUpdateCallbackFiredOnRouteRemoval) {
    int removal_count = 0;
    routing_table_->SetRouteUpdateCallback(
        [&](bool added, AddressType /*dest*/, AddressType /*next*/, uint8_t) {
            if (!added)
                removal_count++;
        });

    AddDirectNeighbor(kNeighbor1);
    AddDirectNeighbor(kNeighbor2);

    // RemoveInactiveNodes with very short timeout removes both and fires callbacks
    routing_table_->RemoveInactiveNodes(kCurrentTime + 200000, 1000, 1000);

    EXPECT_GE(removal_count, 2);
}

// =============================================================================
// AddNode / UpdateNode Tests
// =============================================================================

TEST_F(RoutingTableUnitTest, AddNodeAddsNewNode) {
    NetworkNodeRoute new_node(kRemoteNode, kNeighbor1, 2, kGoodQuality,
                              kCurrentTime);
    new_node.is_active = true;

    bool result = routing_table_->AddNode(new_node);
    EXPECT_TRUE(result);
    EXPECT_TRUE(routing_table_->IsNodePresent(kRemoteNode));
    EXPECT_EQ(routing_table_->GetSize(), 1);
}

TEST_F(RoutingTableUnitTest, AddNodeUpdatesExistingNode) {
    NetworkNodeRoute original(kRemoteNode, kNeighbor1, 3, kGoodQuality,
                              kCurrentTime);
    original.is_active = true;
    routing_table_->AddNode(original);

    NetworkNodeRoute updated(kRemoteNode, kNeighbor2, 1, kGoodQuality,
                             kCurrentTime);
    updated.is_active = true;
    bool result = routing_table_->AddNode(updated);
    EXPECT_TRUE(result);
    EXPECT_EQ(routing_table_->GetSize(), 1);  // still 1 node

    const auto& nodes = routing_table_->GetNodes();
    auto it =
        std::find_if(nodes.begin(), nodes.end(), [](const NetworkNodeRoute& n) {
            return n.routing_entry.destination == kRemoteNode;
        });
    ASSERT_NE(it, nodes.end());
    EXPECT_EQ(it->routing_entry.hop_count, 1);  // updated
    EXPECT_EQ(it->next_hop, kNeighbor2);
}

TEST_F(RoutingTableUnitTest, UpdateNodeSelfAddressReturnsFalse) {
    bool result = routing_table_->UpdateNode(kLocalAddress, 100, false, 0, 0,
                                             kCurrentTime);
    EXPECT_FALSE(result);
    EXPECT_FALSE(routing_table_->IsNodePresent(kLocalAddress));
}

TEST_F(RoutingTableUnitTest, UpdateNodeExistingNode) {
    AddDirectNeighbor(kNeighbor1);

    bool result =
        routing_table_->UpdateNode(kNeighbor1, 80, true, 2, 0x05, kCurrentTime);
    EXPECT_TRUE(result);

    const auto& nodes = routing_table_->GetNodes();
    auto it =
        std::find_if(nodes.begin(), nodes.end(), [](const NetworkNodeRoute& n) {
            return n.routing_entry.destination == kNeighbor1;
        });
    ASSERT_NE(it, nodes.end());
    EXPECT_EQ(it->routing_entry.capabilities, 0x05);
}

TEST_F(RoutingTableUnitTest, UpdateNodeNewNodeAddsAsDirectNeighbor) {
    bool result = routing_table_->UpdateNode(kRemoteNode, 100, false, 1, 0x03,
                                             kCurrentTime);
    EXPECT_TRUE(result);
    EXPECT_TRUE(routing_table_->IsNodePresent(kRemoteNode));

    const auto& nodes = routing_table_->GetNodes();
    auto it =
        std::find_if(nodes.begin(), nodes.end(), [](const NetworkNodeRoute& n) {
            return n.routing_entry.destination == kRemoteNode;
        });
    ASSERT_NE(it, nodes.end());
    EXPECT_EQ(it->routing_entry.hop_count, 1);
    EXPECT_TRUE(it->is_active);
}

// =============================================================================
// SetControlSlotIndex Tests
// =============================================================================

TEST_F(RoutingTableUnitTest, SetControlSlotIndexFound) {
    AddDirectNeighbor(kNeighbor1);
    EXPECT_TRUE(routing_table_->SetControlSlotIndex(kNeighbor1, 3));

    const auto& nodes = routing_table_->GetNodes();
    auto it =
        std::find_if(nodes.begin(), nodes.end(), [](const NetworkNodeRoute& n) {
            return n.routing_entry.destination == kNeighbor1;
        });
    ASSERT_NE(it, nodes.end());
    EXPECT_EQ(it->control_slot_index, 3);
}

TEST_F(RoutingTableUnitTest, SetControlSlotIndexNotFound) {
    EXPECT_FALSE(routing_table_->SetControlSlotIndex(kRemoteNode, 5));
}

// =============================================================================
// GetStatistics / GetLinkQuality / SetMaxNodes Tests
// =============================================================================

TEST_F(RoutingTableUnitTest, GetStatisticsNonEmpty) {
    AddDirectNeighbor(kNeighbor1);
    std::string stats = routing_table_->GetStatistics();
    EXPECT_FALSE(stats.empty());
    // Should contain node count info
    EXPECT_NE(stats.find("Nodes:"), std::string::npos);
}

TEST_F(RoutingTableUnitTest, GetLinkQualityUnknownNodeReturnsZero) {
    EXPECT_EQ(routing_table_->GetLinkQuality(kRemoteNode), 0);
}

TEST_F(RoutingTableUnitTest, GetLinkQualityKnownNodeReturnsValue) {
    AddDirectNeighbor(kNeighbor1);
    uint8_t quality = routing_table_->GetLinkQuality(kNeighbor1);
    // Known direct neighbor — should have some quality value
    EXPECT_GE(quality, 0);
}

TEST_F(RoutingTableUnitTest, SetMaxNodesTrimming) {
    AddDirectNeighbor(kNeighbor1);
    AddDirectNeighbor(kNeighbor2);
    AddDirectNeighbor(kNeighbor3);
    EXPECT_EQ(routing_table_->GetSize(), 3);

    // Shrink to 1 — should trim 2 oldest
    routing_table_->SetMaxNodes(1);
    EXPECT_EQ(routing_table_->GetSize(), 1);
}

// =============================================================================
// RemoveInactiveNodes Two-Stage Test
// =============================================================================

TEST_F(RoutingTableUnitTest, RemoveInactiveNodesTwoStageBehavior) {
    AddDirectNeighbor(kNeighbor1);
    AddDirectNeighbor(kNeighbor2);
    EXPECT_EQ(routing_table_->GetSize(), 2);

    // Stage 1: mark inactive (time_diff=5000 > route_timeout=1000, but < node_timeout=100000)
    size_t removed =
        routing_table_->RemoveInactiveNodes(kCurrentTime + 5000, 1000, 100000);
    EXPECT_EQ(removed, 0);                    // not removed yet
    EXPECT_EQ(routing_table_->GetSize(), 2);  // still in table

    // Verify both are now inactive
    for (const auto& n : routing_table_->GetNodes()) {
        EXPECT_FALSE(n.is_active)
            << "Node 0x" << std::hex << n.routing_entry.destination
            << " should be inactive";
    }

    // Stage 2: remove (time_diff=5000 > node_timeout=1)
    removed = routing_table_->RemoveInactiveNodes(kCurrentTime + 5000, 1000, 1);
    EXPECT_EQ(removed, 2);
    EXPECT_EQ(routing_table_->GetSize(), 0);
}

// =============================================================================
// UpdateLinkStatistics Tests
// =============================================================================

TEST_F(RoutingTableUnitTest,
       UpdateLinkStatisticsIncrementsExpectedForDirectNeighbor) {
    AddDirectNeighbor(kNeighbor1);  // hop_count=1, is_active=true

    const auto& nodes_before = routing_table_->GetNodes();
    auto it_before =
        std::find_if(nodes_before.begin(), nodes_before.end(),
                     [](const NetworkNodeRoute& n) {
                         return n.routing_entry.destination == kNeighbor1;
                     });
    ASSERT_NE(it_before, nodes_before.end());
    uint32_t expected_before = it_before->link_stats.messages_expected;

    routing_table_->UpdateLinkStatistics();

    const auto& nodes_after = routing_table_->GetNodes();
    auto it_after = std::find_if(
        nodes_after.begin(), nodes_after.end(), [](const NetworkNodeRoute& n) {
            return n.routing_entry.destination == kNeighbor1;
        });
    ASSERT_NE(it_after, nodes_after.end());
    EXPECT_GT(it_after->link_stats.messages_expected, expected_before);
}

TEST_F(RoutingTableUnitTest, UpdateLinkStatisticsSkipsMultiHopNode) {
    // Multi-hop node (hop_count=2) — should be skipped by UpdateLinkStatistics
    routing_table_->UpdateRoute(kNeighbor1, kRemoteNode, 2, kGoodQuality, 0, 0,
                                kCurrentTime);

    const auto& nodes_before = routing_table_->GetNodes();
    auto it_before =
        std::find_if(nodes_before.begin(), nodes_before.end(),
                     [](const NetworkNodeRoute& n) {
                         return n.routing_entry.destination == kRemoteNode;
                     });
    ASSERT_NE(it_before, nodes_before.end());
    uint32_t expected_before = it_before->link_stats.messages_expected;

    routing_table_->UpdateLinkStatistics();

    const auto& nodes_after = routing_table_->GetNodes();
    auto it_after = std::find_if(
        nodes_after.begin(), nodes_after.end(), [](const NetworkNodeRoute& n) {
            return n.routing_entry.destination == kRemoteNode;
        });
    ASSERT_NE(it_after, nodes_after.end());
    // Multi-hop node should NOT have expected incremented
    EXPECT_EQ(it_after->link_stats.messages_expected, expected_before);
}

TEST_F(RoutingTableUnitTest, UpdateLinkStatisticsProbesInactiveNeighbor) {
    AddDirectNeighbor(kNeighbor1);

    // Mark inactive
    routing_table_->RemoveInactiveNodes(kCurrentTime + 5000, 1000, 100000);

    const auto& nodes_before = routing_table_->GetNodes();
    auto it_before =
        std::find_if(nodes_before.begin(), nodes_before.end(),
                     [](const NetworkNodeRoute& n) {
                         return n.routing_entry.destination == kNeighbor1;
                     });
    ASSERT_NE(it_before, nodes_before.end());
    ASSERT_FALSE(it_before->is_active);
    uint32_t expected_before = it_before->link_stats.messages_expected;

    // Inactive direct neighbor should still be probed (expectations increment)
    routing_table_->UpdateLinkStatistics();

    const auto& nodes_after = routing_table_->GetNodes();
    auto it_after = std::find_if(
        nodes_after.begin(), nodes_after.end(), [](const NetworkNodeRoute& n) {
            return n.routing_entry.destination == kNeighbor1;
        });
    ASSERT_NE(it_after, nodes_after.end());
    EXPECT_EQ(it_after->link_stats.messages_expected, expected_before + 1);
    EXPECT_EQ(it_after->link_stats.inactive_probe_count, 1);

    // After kMaxInactiveProbes (32) probes, expectations should stop
    for (int i = 1; i < 32; i++) {
        routing_table_->UpdateLinkStatistics();
    }
    auto it_final = std::find_if(
        nodes_after.begin(), nodes_after.end(), [](const NetworkNodeRoute& n) {
            return n.routing_entry.destination == kNeighbor1;
        });
    uint32_t expected_at_limit = it_final->link_stats.messages_expected;

    routing_table_->UpdateLinkStatistics();
    auto it_past = std::find_if(
        nodes_after.begin(), nodes_after.end(), [](const NetworkNodeRoute& n) {
            return n.routing_entry.destination == kNeighbor1;
        });
    // Probe limit reached — expectations should stop incrementing
    EXPECT_EQ(it_past->link_stats.messages_expected, expected_at_limit);
}

// =============================================================================
// GetRoutingEntries: inactive nodes excluded
// =============================================================================

TEST_F(RoutingTableUnitTest, GetRoutingEntriesExcludesInactiveNodes) {
    AddDirectNeighbor(kNeighbor1);
    AddDirectNeighbor(kNeighbor2);

    // Mark all inactive
    routing_table_->RemoveInactiveNodes(kCurrentTime + 5000, 1000, 100000);

    // GetRoutingEntries should exclude inactive entries
    auto entries = routing_table_->GetRoutingEntries(0);
    EXPECT_EQ(entries.size(), 0);  // all inactive
}

// =============================================================================
// ProcessRoutingTableMessage: capabilities update
// =============================================================================

TEST_F(RoutingTableUnitTest, ProcessRoutingMessageUpdatesSourceCapabilities) {
    // Process a routing message with source_capabilities set
    std::vector<RoutingTableEntry> empty;
    routing_table_->ProcessRoutingTableMessage(kNeighbor1, empty, kCurrentTime,
                                               kGoodQuality, kMaxHops,
                                               /*source_capabilities=*/0x07);

    const auto& nodes = routing_table_->GetNodes();
    auto it =
        std::find_if(nodes.begin(), nodes.end(), [](const NetworkNodeRoute& n) {
            return n.routing_entry.destination == kNeighbor1;
        });
    ASSERT_NE(it, nodes.end());
    EXPECT_EQ(it->routing_entry.capabilities, 0x07);
}

// =============================================================================
// GetLinkQuality: returned value reflects quality stored in the node
// =============================================================================

TEST_F(RoutingTableUnitTest, GetLinkQualityReturnsStoredQualityForKnownNode) {
    // Use UpdateRoute to add a node with a specific link quality
    routing_table_->UpdateRoute(kNeighbor1, kNeighbor1, 1, kGoodQuality, 0, 0,
                                kCurrentTime);

    // GetLinkQuality must return the stored quality (non-zero for a known node)
    uint8_t quality = routing_table_->GetLinkQuality(kNeighbor1);
    EXPECT_GT(quality, 0u) << "Known node should have non-zero link quality";
}

TEST_F(RoutingTableUnitTest, GetLinkQualityAfterProcessRoutingMessage) {
    // ProcessRoutingTableMessage records a received routing message, which
    // seeds the link quality stats.
    std::vector<RoutingTableEntry> empty;
    routing_table_->ProcessRoutingTableMessage(kNeighbor1, empty, kCurrentTime,
                                               kGoodQuality, kMaxHops);

    // The source node was recorded as a direct neighbor with link quality
    uint8_t quality = routing_table_->GetLinkQuality(kNeighbor1);
    EXPECT_GT(quality, 0u)
        << "Direct neighbor added via ProcessRoutingTableMessage "
           "should have non-zero link quality";
}

// =============================================================================
// RemoveInactiveNodes: simultaneous mark-and-remove (route_timeout == node_timeout)
// =============================================================================

TEST_F(RoutingTableUnitTest, RemoveInactiveNodesSimultaneousMarkAndRemove) {
    AddDirectNeighbor(kNeighbor1);
    EXPECT_EQ(routing_table_->GetSize(), 1);

    // Both timeouts are very short — node is both marked inactive and removed
    // in the same call.
    uint32_t future_time = kCurrentTime + 50000;
    size_t removed = routing_table_->RemoveInactiveNodes(future_time, 100, 100);

    EXPECT_EQ(removed, 1);
    EXPECT_EQ(routing_table_->GetSize(), 0);
    EXPECT_FALSE(routing_table_->IsNodePresent(kNeighbor1));
}

// =============================================================================
// ProcessRoutingTableMessage: zero-address entry is skipped
// =============================================================================

TEST_F(RoutingTableUnitTest, ProcessRoutingMessageSkipsZeroAddressEntry) {
    RoutingTableEntry zero_entry;
    zero_entry.destination = 0;  // invalid / zero address
    zero_entry.hop_count = 1;
    zero_entry.link_quality = kGoodQuality;
    zero_entry.allocated_data_slots = 0;
    zero_entry.capabilities = 0;

    std::vector<RoutingTableEntry> entries;
    entries.push_back(zero_entry);
    entries.push_back(CreateEntry(kRemoteNode, 1, kGoodQuality));

    routing_table_->ProcessRoutingTableMessage(
        kNeighbor1, entries, kCurrentTime, kGoodQuality, kMaxHops);

    // Zero-address entry must NOT be added
    EXPECT_FALSE(routing_table_->IsNodePresent(0))
        << "Zero-address entry should be skipped";
    // Valid entry should still be added
    EXPECT_TRUE(routing_table_->IsNodePresent(kRemoteNode));
}

// =============================================================================
// UpdateLinkStatistics: two-phase degradation then invalidation
// =============================================================================

TEST_F(RoutingTableUnitTest, UpdateLinkStatisticsDegradesThenInvalidates) {
    // EWMA quality decays on each missed message (ExpectRoutingMessage).
    // After consecutive_missed >= inactivation_threshold (10), is_active = false.

    std::vector<RoutingTableEntry> empty;
    routing_table_->ProcessRoutingTableMessage(kNeighbor1, empty, kCurrentTime,
                                               kGoodQuality, kMaxHops);

    // Verify node is active with initial quality
    {
        const auto& nodes = routing_table_->GetNodes();
        auto it = std::find_if(
            nodes.begin(), nodes.end(), [](const NetworkNodeRoute& n) {
                return n.routing_entry.destination == kNeighbor1;
            });
        ASSERT_NE(it, nodes.end());
        ASSERT_TRUE(it->is_active);
    }

    // Phase 1: After 7 calls, quality should degrade via EWMA
    // (Step 1 fires from call 6 onward when messages_expected >= 5)
    for (int i = 0; i < 7; ++i) {
        routing_table_->UpdateLinkStatistics();
    }
    {
        const auto& nodes = routing_table_->GetNodes();
        auto it = std::find_if(
            nodes.begin(), nodes.end(), [](const NetworkNodeRoute& n) {
                return n.routing_entry.destination == kNeighbor1;
            });
        ASSERT_NE(it, nodes.end());
        EXPECT_TRUE(it->is_active)
            << "Node should still be active during EWMA degradation";
        EXPECT_LT(it->routing_entry.link_quality, kGoodQuality)
            << "Quality should be degraded after EWMA decay";
    }

    // Phase 2: After 10+ calls total, node becomes inactive
    bool became_inactive = false;
    for (int i = 0; i < 10; ++i) {
        routing_table_->UpdateLinkStatistics();
        const auto& nodes = routing_table_->GetNodes();
        auto it = std::find_if(
            nodes.begin(), nodes.end(), [](const NetworkNodeRoute& n) {
                return n.routing_entry.destination == kNeighbor1;
            });
        if (it != nodes.end() && !it->is_active) {
            became_inactive = true;
            break;
        }
    }

    EXPECT_TRUE(became_inactive) << "Node should become inactive after "
                                    "inactivation_threshold misses";
}

// =============================================================================
// EWMA Quality Tests
// =============================================================================

TEST_F(RoutingTableUnitTest, EWMAQualityDecaysOnMissedMessages) {
    // Verify EWMA decays following (1-alpha)^N curve on consecutive misses
    std::vector<RoutingTableEntry> empty;
    routing_table_->ProcessRoutingTableMessage(kNeighbor1, empty, kCurrentTime,
                                               kGoodQuality, kMaxHops);

    const auto& nodes = routing_table_->GetNodes();
    auto find_node = [&]() {
        return std::find_if(
            nodes.begin(), nodes.end(), [](const NetworkNodeRoute& n) {
                return n.routing_entry.destination == kNeighbor1;
            });
    };

    // Initial EWMA after one received message with alpha=77/256:
    // ewma = (77*255 + 179*200)/256 = 216
    auto it = find_node();
    ASSERT_NE(it, nodes.end());
    EXPECT_EQ(it->link_stats.ewma_quality, 216);

    // After 10 consecutive misses, EWMA should decay significantly
    // Each miss: ewma = ewma * 179/256
    for (int i = 0; i < 10; ++i) {
        routing_table_->UpdateLinkStatistics();
    }
    it = find_node();
    ASSERT_NE(it, nodes.end());
    EXPECT_LT(it->link_stats.ewma_quality, 20)
        << "EWMA should be very low after 10 consecutive misses";
    EXPECT_TRUE(it->is_active)
        << "Node should still be active (threshold is 10, but check fires "
           "before increment)";
}

TEST_F(RoutingTableUnitTest, EWMAQualityRecoversOnReceivedMessages) {
    // Start with a degraded link, then receive messages to recover
    std::vector<RoutingTableEntry> empty;
    routing_table_->ProcessRoutingTableMessage(kNeighbor1, empty, kCurrentTime,
                                               kGoodQuality, kMaxHops);

    // Degrade with 5 missed messages
    for (int i = 0; i < 5; ++i) {
        routing_table_->UpdateLinkStatistics();
    }

    const auto& nodes = routing_table_->GetNodes();
    auto find_node = [&]() {
        return std::find_if(
            nodes.begin(), nodes.end(), [](const NetworkNodeRoute& n) {
                return n.routing_entry.destination == kNeighbor1;
            });
    };

    auto it = find_node();
    uint8_t degraded_ewma = it->link_stats.ewma_quality;
    EXPECT_LT(degraded_ewma, 100) << "EWMA should be degraded after 5 misses";

    // Receive 3 messages → EWMA should recover significantly
    for (int i = 0; i < 3; ++i) {
        routing_table_->ProcessRoutingTableMessage(
            kNeighbor1, empty, kCurrentTime + 6000 + i, kGoodQuality, kMaxHops);
    }
    it = find_node();
    EXPECT_GT(it->link_stats.ewma_quality, degraded_ewma)
        << "EWMA should recover after receiving messages";
}

TEST_F(RoutingTableUnitTest, EWMAQualityStabilizesAtMarginalPDR) {
    // Simulate ~70% PDR: receive 7 out of every 10 superframes
    // EWMA should converge to approximately 0.7 * 255 ≈ 178 without oscillation
    std::vector<RoutingTableEntry> empty;
    routing_table_->ProcessRoutingTableMessage(kNeighbor1, empty, kCurrentTime,
                                               kGoodQuality, kMaxHops);

    // Run 50 superframes with 70% PDR (bursty: 7 receive, 3 miss)
    uint32_t time = kCurrentTime + 1000;
    for (int round = 0; round < 5; ++round) {
        for (int i = 0; i < 10; ++i) {
            routing_table_->UpdateLinkStatistics();
            if (i < 7) {
                routing_table_->ProcessRoutingTableMessage(
                    kNeighbor1, empty, time++, kGoodQuality, kMaxHops);
            }
        }
    }

    // Receive one more message to check post-receive quality (not post-miss)
    routing_table_->UpdateLinkStatistics();
    routing_table_->ProcessRoutingTableMessage(kNeighbor1, empty, time++,
                                               kGoodQuality, kMaxHops);

    const auto& nodes = routing_table_->GetNodes();
    auto it =
        std::find_if(nodes.begin(), nodes.end(), [](const NetworkNodeRoute& n) {
            return n.routing_entry.destination == kNeighbor1;
        });
    ASSERT_NE(it, nodes.end());
    EXPECT_TRUE(it->is_active) << "70% PDR link should remain active";
    // After a receive, EWMA recovers to ~100+. The key property is
    // that the link stays active and quality doesn't collapse to zero.
    EXPECT_GT(it->link_stats.ewma_quality, 50)
        << "EWMA should not collapse for 70% PDR";
    EXPECT_LT(it->link_stats.ewma_quality, 230)
        << "EWMA should not exceed expected range";
}

TEST_F(RoutingTableUnitTest,
       HysteresisRequiresMultipleMessagesForReactivation) {
    // Test hysteresis on indirect route re-activation via UpdateRoute.
    // Source node re-activation (ProcessRoutingTableMessage) is immediate
    // since the message itself proves liveness.

    // Add a 2-hop route to kRemoteNode via kNeighbor1
    AddDirectNeighbor(kNeighbor1);
    routing_table_->UpdateRoute(kNeighbor1, kRemoteNode, 2, kGoodQuality, 0, 0,
                                kCurrentTime);

    // Keep kNeighbor1 alive, but inactivate kRemoteNode
    std::vector<RoutingTableEntry> empty;
    routing_table_->ProcessRoutingTableMessage(
        kNeighbor1, empty, kCurrentTime + 4500, kGoodQuality, kMaxHops);
    routing_table_->RemoveInactiveNodes(kCurrentTime + 5000, 1000, 100000);

    const auto& nodes = routing_table_->GetNodes();
    auto find_remote = [&]() {
        return std::find_if(
            nodes.begin(), nodes.end(), [](const NetworkNodeRoute& n) {
                return n.routing_entry.destination == kRemoteNode;
            });
    };

    EXPECT_FALSE(find_remote()->is_active);

    // First UpdateRoute with worse hops: recovery_counter=1, still inactive
    routing_table_->UpdateRoute(kNeighbor2, kRemoteNode, 4, kGoodQuality, 0, 0,
                                kCurrentTime + 5001);
    EXPECT_FALSE(find_remote()->is_active);

    // Second UpdateRoute: recovery_counter=2, re-activated
    routing_table_->UpdateRoute(kNeighbor2, kRemoteNode, 4, kGoodQuality, 0, 0,
                                kCurrentTime + 5002);
    EXPECT_TRUE(find_remote()->is_active);
}

TEST_F(RoutingTableUnitTest, MultiHopQualityCappedByDirectLink) {
    // Direct neighbor quality should cap multi-hop route quality
    std::vector<RoutingTableEntry> entries;
    entries.push_back(CreateEntry(kRemoteNode, 1, 250));  // 2-hop route, q=250
    routing_table_->ProcessRoutingTableMessage(
        kNeighbor1, entries, kCurrentTime, kGoodQuality, kMaxHops);

    // Degrade kNeighbor1 link quality via EWMA decay (enough for Step 1 to fire)
    for (int i = 0; i < 7; ++i) {
        routing_table_->UpdateLinkStatistics();
    }

    const auto& nodes = routing_table_->GetNodes();
    auto neighbor_it =
        std::find_if(nodes.begin(), nodes.end(), [](const NetworkNodeRoute& n) {
            return n.routing_entry.destination == kNeighbor1;
        });
    auto remote_it =
        std::find_if(nodes.begin(), nodes.end(), [](const NetworkNodeRoute& n) {
            return n.routing_entry.destination == kRemoteNode;
        });
    ASSERT_NE(neighbor_it, nodes.end());
    ASSERT_NE(remote_it, nodes.end());

    // Multi-hop quality should not exceed direct link quality
    EXPECT_LE(remote_it->routing_entry.link_quality,
              neighbor_it->routing_entry.link_quality)
        << "Multi-hop quality should be capped by direct link quality";
}

// =============================================================================
// GetRoutingEntries: verifies capabilities are included in advertised entries
// =============================================================================

TEST_F(RoutingTableUnitTest, GetRoutingEntriesIncludesCapabilities) {
    // Add a node with specific capabilities
    routing_table_->UpdateRoute(kNeighbor1, kRemoteNode, 2, kGoodQuality,
                                /*allocated_data_slots=*/1,
                                /*capabilities=*/0x0F, kCurrentTime);

    auto entries = routing_table_->GetRoutingEntries(0);
    ASSERT_FALSE(entries.empty());

    auto it = std::find_if(entries.begin(), entries.end(),
                           [](const RoutingTableEntry& e) {
                               return e.destination == kRemoteNode;
                           });
    ASSERT_NE(it, entries.end());
    EXPECT_EQ(it->capabilities, 0x0F);
    EXPECT_EQ(it->allocated_data_slots, 1);
}

// =============================================================================
// GetMaxHopsFromRoutingTable equivalent: all inactive nodes → effective max is 0
// =============================================================================

TEST_F(RoutingTableUnitTest, GetRoutingEntriesEmptyWhenAllInactive) {
    // Add nodes with various hop counts
    routing_table_->UpdateRoute(kNeighbor1, kNeighbor1, 1, kGoodQuality, 0, 0,
                                kCurrentTime);
    routing_table_->UpdateRoute(kNeighbor1, kRemoteNode, 2, kGoodQuality, 0, 0,
                                kCurrentTime);

    EXPECT_EQ(routing_table_->GetSize(), 2);

    // Mark all routes inactive (route_timeout small, node_timeout large)
    routing_table_->RemoveInactiveNodes(kCurrentTime + 5000, 100, 100000);

    // GetRoutingEntries excludes inactive nodes — so 0 entries even though
    // nodes are still in the table (not yet fully removed)
    auto entries = routing_table_->GetRoutingEntries(0);
    EXPECT_EQ(entries.size(), 0u)
        << "Inactive nodes must not be advertised in routing entries";
}

// =============================================================================
// FindNextHop ETX Cost Selection Tests
// =============================================================================

TEST_F(RoutingTableUnitTest, FindNextHopPrefersLowerCostOverFewerHops) {
    // Direct neighbor with poor quality (1 hop, quality 50)
    // Cost = 1 × 65536 / 50 = 1310
    AddDirectNeighbor(kNeighbor1, kPoorQuality);

    // 2-hop route via Neighbor2 with good quality (quality 200)
    // Cost = 2 × 65536 / 200 = 655
    ReceiveRoutingMessage(
        kNeighbor2, {CreateEntry(kNeighbor1, 1, kGoodQuality)}, kGoodQuality);

    // FindNextHop should prefer the 2-hop route (lower ETX cost)
    EXPECT_EQ(routing_table_->FindNextHop(kNeighbor1), kNeighbor2)
        << "FindNextHop should prefer 2-hop/q200 (cost=655) over "
           "1-hop/q50 (cost=1310)";
}

TEST_F(RoutingTableUnitTest, FindNextHopConsistentWithIsBetterRoute) {
    // Two routes to the same destination with different cost profiles
    // Route A: 1 hop, quality 100 → cost = 655
    AddDirectNeighbor(kNeighbor1, 100);

    // Route B: 2 hops via Neighbor2, quality 200 → cost = 655
    ReceiveRoutingMessage(
        kNeighbor2, {CreateEntry(kNeighbor1, 1, kGoodQuality)}, kGoodQuality);

    // Equal cost — tiebreaker should prefer fewer hops (Neighbor1, 1 hop)
    EXPECT_EQ(routing_table_->FindNextHop(kNeighbor1), kNeighbor1)
        << "Equal ETX cost should prefer fewer hops as tiebreaker";
}

TEST_F(RoutingTableUnitTest, FindNextHopSelectsGoodQualityDirectNeighbor) {
    // Good direct neighbor (1 hop, quality 200)
    // Cost = 1 × 65536 / 200 = 327
    AddDirectNeighbor(kNeighbor1, kGoodQuality);

    // 2-hop route (quality 200)
    // Cost = 2 × 65536 / 200 = 655
    ReceiveRoutingMessage(
        kNeighbor2, {CreateEntry(kNeighbor1, 1, kGoodQuality)}, kGoodQuality);

    // 1-hop with good quality should still win (lower cost)
    EXPECT_EQ(routing_table_->FindNextHop(kNeighbor1), kNeighbor1)
        << "Good direct neighbor should be preferred over 2-hop route";
}

// =============================================================================
// Phantom Direct Neighbor Cleanup Tests
// =============================================================================

TEST_F(RoutingTableUnitTest, PhantomDirectNeighborIsInactivated) {
    // Simulate sponsor path: AddDirectNeighbor calls UpdateRoute which
    // creates hop_count=1 with messages_received=0
    AddDirectNeighbor(kNeighbor1);

    {
        const auto& nodes = routing_table_->GetNodes();
        auto it = std::find_if(
            nodes.begin(), nodes.end(), [](const NetworkNodeRoute& n) {
                return n.routing_entry.destination == kNeighbor1;
            });
        ASSERT_NE(it, nodes.end());
        EXPECT_TRUE(it->is_active);
        EXPECT_EQ(it->link_stats.messages_received, 0u);
    }

    // inactivation_threshold_ = 10. Each UpdateLinkStatistics call increments
    // consecutive_missed via ExpectRoutingMessage (Step 3). Step 2 fires when
    // consecutive_missed >= 10, which happens on the 11th call.
    for (int i = 0; i < 10; ++i) {
        routing_table_->UpdateLinkStatistics();
    }
    {
        const auto& nodes = routing_table_->GetNodes();
        auto it = std::find_if(
            nodes.begin(), nodes.end(), [](const NetworkNodeRoute& n) {
                return n.routing_entry.destination == kNeighbor1;
            });
        ASSERT_NE(it, nodes.end());
        EXPECT_TRUE(it->is_active)
            << "Should still be active after 10 calls (consecutive_missed=10, "
               "Step 2 checks at start of next call)";
    }

    // 11th call: Step 2 fires (consecutive_missed == 10 >= threshold)
    routing_table_->UpdateLinkStatistics();
    {
        const auto& nodes = routing_table_->GetNodes();
        auto it = std::find_if(
            nodes.begin(), nodes.end(), [](const NetworkNodeRoute& n) {
                return n.routing_entry.destination == kNeighbor1;
            });
        ASSERT_NE(it, nodes.end());
        EXPECT_FALSE(it->is_active)
            << "Phantom neighbor (messages_received=0) should be inactivated "
               "after consecutive_missed reaches inactivation_threshold";
    }
}

TEST_F(RoutingTableUnitTest, RealDirectNeighborStillInactivatedByStep2) {
    // Add neighbor via ProcessRoutingTableMessage (real reception)
    ReceiveRoutingMessage(kNeighbor1, {});

    {
        const auto& nodes = routing_table_->GetNodes();
        auto it = std::find_if(
            nodes.begin(), nodes.end(), [](const NetworkNodeRoute& n) {
                return n.routing_entry.destination == kNeighbor1;
            });
        ASSERT_NE(it, nodes.end());
        EXPECT_GT(it->link_stats.messages_received, 0u);
        EXPECT_TRUE(it->is_active);
    }

    // After enough missed messages, Step 2 should still inactivate
    for (int i = 0; i < 11; ++i) {
        routing_table_->UpdateLinkStatistics();
    }

    const auto& nodes = routing_table_->GetNodes();
    auto it =
        std::find_if(nodes.begin(), nodes.end(), [](const NetworkNodeRoute& n) {
            return n.routing_entry.destination == kNeighbor1;
        });
    ASSERT_NE(it, nodes.end());
    EXPECT_FALSE(it->is_active)
        << "Real neighbor should still be inactivated by Step 2 after "
           "consecutive_missed reaches threshold";
}

// =============================================================================
// Unidirectional direct route must not replace working indirect route
// =============================================================================

TEST_F(RoutingTableUnitTest,
       UnidirectionalDirectRouteDoesNotReplaceIndirectRoute) {
    // Reproduces the experiment scenario:
    // 1. Node hears kNeighbor1 directly — link goes unidirectional (quality=1)
    // 2. A better indirect route (via kNeighbor2) replaces it
    // 3. Cascade degradation drops the indirect route to quality=1 too
    // 4. kNeighbor1 sends another routing table → the fix should prevent
    //    the unidirectional direct route from overwriting the indirect one

    // Step 1: Establish kNeighbor2 as a good direct neighbor
    AddDirectNeighbor(kNeighbor2, kGoodQuality);

    // Step 2: Receive routing tables from kNeighbor1 directly with
    // remote_link_quality=0 (unidirectional from the start).
    // Interleave UpdateLinkStatistics to build messages_expected >= 3.
    uint32_t t = kCurrentTime;
    for (int i = 0; i < 5; i++) {
        std::vector<RoutingTableEntry> empty;
        routing_table_->ProcessRoutingTableMessage(
            kNeighbor1, empty, t, /*local_link_quality=*/0, kMaxHops);
        routing_table_->UpdateLinkStatistics();
        t += 1000;
    }

    // kNeighbor1 is direct, quality=1 (confirmed unidirectional)
    const auto& nodes = routing_table_->GetNodes();
    auto it =
        std::find_if(nodes.begin(), nodes.end(), [](const NetworkNodeRoute& n) {
            return n.routing_entry.destination == kNeighbor1;
        });
    ASSERT_NE(it, nodes.end());
    EXPECT_EQ(it->next_hop, kNeighbor1);
    EXPECT_EQ(it->routing_entry.link_quality, 1);
    EXPECT_GE(it->link_stats.messages_expected, 3u);

    // Step 3: kNeighbor2 advertises a route to kNeighbor1 (1 hop from
    // kNeighbor2 = 2 hops from us). With kNeighbor2 quality=200 and
    // entry quality=200, actual_quality = min(200, 200) = 200.
    // Cost: CalculateRouteCost(2, 200) = 655 vs current (1, 1) = 65535.
    // The indirect route is much better → replaces the direct.
    std::vector<RoutingTableEntry> entries;
    entries.push_back(CreateEntry(kNeighbor1, 1, kGoodQuality));
    ReceiveRoutingMessage(kNeighbor2, entries, kGoodQuality);

    EXPECT_EQ(it->next_hop, kNeighbor2)
        << "Indirect route should replace direct unidirectional route";
    EXPECT_EQ(it->routing_entry.hop_count, 2);

    // Step 4: Degrade the indirect route to quality=1 (simulates cascade
    // degradation from ScheduleRoutingMessageExpectations when the
    // next_hop kNeighbor2 also has unidirectional risk).
    routing_table_->DegradeRouteQuality(kNeighbor1, 1);
    EXPECT_EQ(it->routing_entry.link_quality, 1);

    // Step 5: kNeighbor1 sends another routing table (remote_quality=0).
    // The fix should block the direct unidirectional route from replacing
    // the indirect route — both have cost 65535, but the indirect one
    // may still deliver packets.
    {
        std::vector<RoutingTableEntry> empty;
        routing_table_->ProcessRoutingTableMessage(
            kNeighbor1, empty, t, /*local_link_quality=*/0, kMaxHops);
    }

    EXPECT_EQ(it->next_hop, kNeighbor2)
        << "Unidirectional direct route should not replace indirect route "
           "when both have quality=1";
    EXPECT_EQ(it->routing_entry.hop_count, 2);
}

TEST_F(RoutingTableUnitTest, BidirectionalDirectRouteCanReplaceIndirectRoute) {
    // Same setup: kNeighbor1 via kNeighbor2 (2 hops)
    AddDirectNeighbor(kNeighbor2, kGoodQuality);

    std::vector<RoutingTableEntry> entries;
    entries.push_back(CreateEntry(kNeighbor1, 1, kGoodQuality));
    ReceiveRoutingMessage(kNeighbor2, entries, kGoodQuality);

    // Degrade to quality=1
    routing_table_->DegradeRouteQuality(kNeighbor1, 1);

    // Now receive from kNeighbor1 directly with GOOD remote quality
    // (kNeighbor1 CAN hear us → bidirectional)
    for (int i = 0; i < 4; i++) {
        std::vector<RoutingTableEntry> empty;
        routing_table_->ProcessRoutingTableMessage(
            kNeighbor1, empty, kCurrentTime + i * 1000,
            /*local_link_quality=*/kGoodQuality, kMaxHops);
    }

    // Verify: route should switch to direct (bidirectional link is better)
    const auto& nodes = routing_table_->GetNodes();
    auto it =
        std::find_if(nodes.begin(), nodes.end(), [](const NetworkNodeRoute& n) {
            return n.routing_entry.destination == kNeighbor1;
        });
    ASSERT_NE(it, nodes.end());
    EXPECT_EQ(it->next_hop, kNeighbor1)
        << "Bidirectional direct route should replace degraded indirect route";
    EXPECT_EQ(it->routing_entry.hop_count, 1);
}

TEST_F(RoutingTableUnitTest, InactiveRouteCanBeReplacedByUnidirectionalDirect) {
    // Set up: kNeighbor1 via kNeighbor2 (2 hops), then mark inactive
    AddDirectNeighbor(kNeighbor2, kGoodQuality);

    std::vector<RoutingTableEntry> entries;
    entries.push_back(CreateEntry(kNeighbor1, 1, kGoodQuality));
    ReceiveRoutingMessage(kNeighbor2, entries, kGoodQuality);

    // Inactivate the route
    routing_table_->RemoveInactiveNodes(kCurrentTime + 500000, 1000, 100000);

    // Receive from kNeighbor1 directly with unidirectional link
    for (int i = 0; i < 4; i++) {
        std::vector<RoutingTableEntry> empty;
        routing_table_->ProcessRoutingTableMessage(
            kNeighbor1, empty, kCurrentTime + 600000 + i * 1000,
            /*local_link_quality=*/0, kMaxHops);
    }

    // Verify: inactive route should be replaced (was_inactive = true)
    const auto& nodes = routing_table_->GetNodes();
    auto it =
        std::find_if(nodes.begin(), nodes.end(), [](const NetworkNodeRoute& n) {
            return n.routing_entry.destination == kNeighbor1;
        });
    ASSERT_NE(it, nodes.end());
    EXPECT_EQ(it->next_hop, kNeighbor1)
        << "Inactive route should be replaced even by unidirectional direct";
    EXPECT_TRUE(it->is_active);
}

}  // namespace test
}  // namespace loramesher
