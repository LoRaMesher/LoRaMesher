// /**
//  * @file lora_mesh_routing_tests.cpp
//  * @brief Test suite for LoRaMesh protocol routing functionality
//  */

// #include <gtest/gtest.h>
// #include <chrono>
// #include <memory>
// #include <thread>
// #include <vector>

// #include "lora_mesh_test_fixture.hpp"

// namespace loramesher {
// namespace test {

// /**
//  * @brief Test suite for LoRaMesh protocol routing functionality
//  */
// class LoRaMeshRoutingTests : public LoRaMeshTestFixture {
//    protected:
//     void SetUp() override { LoRaMeshTestFixture::SetUp(); }

//     void TearDown() override { LoRaMeshTestFixture::TearDown(); }

//     /**
//      * @brief Wait for routing tables to stabilize
//      */
//     bool WaitForRoutingStabilization(const std::vector<TestNode*>& nodes,
//                                      uint32_t timeout_ms = 10000) {
//         return AdvanceTime(timeout_ms, timeout_ms, 100, 20, [&]() {
//             Check if all nodes have discovered all other nodes for (auto* node :
//                                                                     nodes) {
//                 auto& network_nodes = node->protocol->GetNetworkNodes();
//                 Each node should know about all other
//                     nodes if (network_nodes.size() < nodes.size() - 1) {
//                     return false;
//                 }
//             }
//             return true;
//         });
//     }
// };

// /**
//  * @brief Test direct routing between adjacent nodes
//  */
// TEST_F(LoRaMeshRoutingTests, DirectRouting) {
//     Create two nodes auto& node1 = CreateNode("Node1", 0x1001);
//     auto& node2 = CreateNode("Node2", 0x1002);

//     SetLinkStatus(node1, node2, true);

//     Start nodes ASSERT_TRUE(StartNode(node1));
//     ASSERT_TRUE(StartNode(node2));

//     Wait for network formation
//     std::vector<TestNode*> nodes = {&node1, &node2};
//     ASSERT_TRUE(WaitForNetworkFormation(nodes, 1));

//     Wait for routing tables to stabilize
//     ASSERT_TRUE(WaitForRoutingStabilization(nodes));

//     Send a message from node1 to node2 std::vector<uint8_t> payload = {
//         0x01, 0x02, 0x03};
//     ASSERT_TRUE(SendMessage(node1, node2, MessageType::DATA_MSG, payload));

//     Wait for message to be received
//     bool received = AdvanceTime(1000, 2000, 100, 10, [&]() {
//         return HasReceivedMessageFrom(node2, node1.address,
//                                       MessageType::DATA_MSG);
//     });

//     EXPECT_TRUE(received) << "Node2 did not receive message from Node1";

//     Verify the message content auto messages =
//         GetReceivedMessages(node2, node1.address, MessageType::DATA_MSG);
//     ASSERT_EQ(messages.size(), 1);

//     auto msg_payload = messages[0].GetPayload();
//     EXPECT_EQ(msg_payload, payload);
// }

// /**
//  * @brief Test multi-hop routing in line topology
//  */
// TEST_F(LoRaMeshRoutingTests, MultiHopLineTopology) {
//     // Create line topology: Node1 - Node2 - Node3 - Node4
//     auto nodes = GenerateLineTopology(4);

//     // Start all nodes
//     for (auto* node : nodes) {
//         ASSERT_TRUE(StartNode(*node));
//     }

//     // Wait for network formation
//     ASSERT_TRUE(WaitForNetworkFormation(nodes, 3));

//     // Wait longer for routing tables to propagate through the line
//     ASSERT_TRUE(WaitForRoutingStabilization(nodes, 20000));

//     // Send message from Node1 to Node4 (requires 3 hops)
//     std::vector<uint8_t> payload = {0xAA, 0xBB, 0xCC};
//     ASSERT_TRUE(
//         SendMessage(*nodes[0], *nodes[3], MessageType::DATA_MSG, payload));

//     // Wait for message to be routed
//     bool received = AdvanceTime(5000, 10000, 200, 20, [&]() {
//         return HasReceivedMessageFrom(*nodes[3], nodes[0]->address,
//                                       MessageType::DATA_MSG);
//     });

//     EXPECT_TRUE(received)
//         << "Node4 did not receive message from Node1 through multi-hop";

//     // Verify intermediate nodes participated in routing
//     // Node2 and Node3 should have seen the message
//     // Note: This depends on protocol implementation details
// }

// /**
//  * @brief Test routing table updates after topology change
//  */
// TEST_F(LoRaMeshRoutingTests, RoutingTableUpdateAfterTopologyChange) {
//     // Create a full mesh of 4 nodes
//     auto nodes = GenerateFullMeshTopology(4);

//     // Start all nodes
//     for (auto* node : nodes) {
//         ASSERT_TRUE(StartNode(*node));
//     }

//     // Wait for network formation
//     ASSERT_TRUE(WaitForNetworkFormation(nodes, 3));
//     ASSERT_TRUE(WaitForRoutingStabilization(nodes));

//     // Verify initial routing - all nodes should have direct routes
//     for (auto* node : nodes) {
//         auto& network_nodes = node->protocol->GetNetworkNodes();
//         for (const auto& route : network_nodes) {
//             if (route.routing_entry.destination != node->address) {
//                 EXPECT_EQ(route.routing_entry.hop_count, 1)
//                     << "Expected direct route (1 hop) in full mesh";
//             }
//         }
//     }

//     // Now break the link between Node1 and Node3
//     SetLinkStatus(*nodes[0], *nodes[2], false);

//     // Wait for routing tables to update
//     bool updated = AdvanceTime(10000, 15000, 500, 20, [&]() {
//         // Check if Node1 has found alternative route to Node3
//         auto& node1_routes = nodes[0]->protocol->GetNetworkNodes();
//         for (const auto& route : node1_routes) {
//             if (route.routing_entry.destination == nodes[2]->address) {
//                 // Should now route through Node2 or Node4 (2 hops)
//                 return route.routing_entry.hop_count > 1;
//             }
//         }
//         return false;
//     });

//     EXPECT_TRUE(updated)
//         << "Routing table did not update after topology change";

//     // Send message from Node1 to Node3 using new route
//     std::vector<uint8_t> payload = {0x11, 0x22, 0x33};
//     ASSERT_TRUE(
//         SendMessage(*nodes[0], *nodes[2], MessageType::DATA_MSG, payload));

//     // Message should still arrive despite broken direct link
//     bool received = AdvanceTime(3000, 5000, 200, 20, [&]() {
//         return HasReceivedMessageFrom(*nodes[2], nodes[0]->address,
//                                       MessageType::DATA_MSG);
//     });

//     EXPECT_TRUE(received) << "Message not received after route change";
// }

// /**
//  * @brief Test routing loop prevention
//  */
// TEST_F(LoRaMeshRoutingTests, RoutingLoopPrevention) {
//     // Create a ring topology that could cause loops
//     auto nodes = GenerateFullMeshTopology(4);

//     // Start all nodes
//     for (auto* node : nodes) {
//         ASSERT_TRUE(StartNode(*node));
//     }

//     // Wait for network formation
//     ASSERT_TRUE(WaitForNetworkFormation(nodes, 3));
//     ASSERT_TRUE(WaitForRoutingStabilization(nodes));

//     // Create a complex topology by removing some links
//     // This creates potential for routing loops
//     SetLinkStatus(*nodes[0], *nodes[2], false);
//     SetLinkStatus(*nodes[1], *nodes[3], false);

//     // Wait for routing to stabilize
//     AdvanceTime(10000);

//     // Send multiple messages and verify no loops occur
//     for (int i = 0; i < 5; i++) {
//         std::vector<uint8_t> payload = {static_cast<uint8_t>(i)};
//         ASSERT_TRUE(
//             SendMessage(*nodes[0], *nodes[2], MessageType::DATA_MSG, payload));
//     }

//     // Wait and check that we don't receive duplicate messages
//     AdvanceTime(5000);

//     auto messages = GetReceivedMessages(*nodes[2], nodes[0]->address,
//                                         MessageType::DATA_MSG);
//     EXPECT_EQ(messages.size(), 5)
//         << "Expected exactly 5 messages, possible routing loop detected";
// }

// /**
//  * @brief Test broadcast message routing
//  */
// TEST_F(LoRaMeshRoutingTests, BroadcastRouting) {
//     // Create a network of 5 nodes
//     auto nodes = GenerateFullMeshTopology(5);

//     // Start all nodes
//     for (auto* node : nodes) {
//         ASSERT_TRUE(StartNode(*node));
//     }

//     // Wait for network formation
//     ASSERT_TRUE(WaitForNetworkFormation(nodes, 4));
//     ASSERT_TRUE(WaitForRoutingStabilization(nodes));

//     // Send broadcast message from Node1
//     auto message_opt = BaseMessage::Create(
//         0xFFFF,  // Broadcast address
//         nodes[0]->address, MessageType::DATA_MSG, {0xBC, 0xBC, 0xBC});
//     ASSERT_TRUE(message_opt.has_value());
//     ASSERT_TRUE(nodes[0]->protocol->SendMessage(message_opt.value()));

//     // All other nodes should receive the broadcast
//     bool all_received = AdvanceTime(5000, 10000, 200, 20, [&]() {
//         for (size_t i = 1; i < nodes.size(); i++) {
//             if (!HasReceivedMessageFrom(*nodes[i], nodes[0]->address,
//                                         MessageType::DATA_MSG)) {
//                 return false;
//             }
//         }
//         return true;
//     });

//     EXPECT_TRUE(all_received) << "Not all nodes received broadcast message";

//     // Verify each node received exactly one copy
//     for (size_t i = 1; i < nodes.size(); i++) {
//         auto messages = GetReceivedMessages(*nodes[i], nodes[0]->address,
//                                             MessageType::DATA_MSG);
//         EXPECT_EQ(messages.size(), 1)
//             << "Node " << i << " received wrong number of broadcasts";
//     }
// }

// /**
//  * @brief Test route quality metrics and selection
//  */
// TEST_F(LoRaMeshRoutingTests, LinkQualityBasedRouting) {
//     // Create a network where Node1 has two paths to Node4
//     // Path 1: Node1 -> Node2 -> Node4 (good quality)
//     // Path 2: Node1 -> Node3 -> Node4 (poor quality)

//     auto& node1 = CreateNode("Node1", 0x1001);
//     auto& node2 = CreateNode("Node2", 0x1002);
//     auto& node3 = CreateNode("Node3", 0x1003);
//     auto& node4 = CreateNode("Node4", 0x1004);

//     // Set up topology
//     SetLinkStatus(node1, node2, true);
//     SetLinkStatus(node1, node3, true);
//     SetLinkStatus(node2, node4, true);
//     SetLinkStatus(node3, node4, true);

//     // Make Node1-Node3 link poor quality by adding delay
//     SetMessageDelay(node1, node3, 500);
//     SetMessageDelay(node3, node4, 500);

//     // Start all nodes
//     ASSERT_TRUE(StartNode(node1));
//     ASSERT_TRUE(StartNode(node2));
//     ASSERT_TRUE(StartNode(node3));
//     ASSERT_TRUE(StartNode(node4));

//     std::vector<TestNode*> nodes = {&node1, &node2, &node3, &node4};

//     // Wait for network formation
//     ASSERT_TRUE(WaitForNetworkFormation(nodes, 3));
//     ASSERT_TRUE(WaitForRoutingStabilization(nodes));

//     // Send multiple messages to establish link quality
//     for (int i = 0; i < 10; i++) {
//         std::vector<uint8_t> payload = {static_cast<uint8_t>(i)};
//         SendMessage(node1, node4, MessageType::DATA_MSG, payload);
//         AdvanceTime(1000);
//     }

//     // Check that Node1 prefers the route through Node2
//     auto& node1_routes = node1.protocol->GetNetworkNodes();
//     for (const auto& route : node1_routes) {
//         if (route.routing_entry.destination == node4.address) {
//             EXPECT_EQ(route.next_hop, node2.address)
//                 << "Node1 should prefer route through Node2 due to better "
//                    "quality";
//             break;
//         }
//     }
// }

// /**
//  * @brief Test maximum hop count limitation
//  */
// TEST_F(LoRaMeshRoutingTests, MaxHopCountLimit) {
//     // Create a long line topology that exceeds max hop count
//     auto nodes = GenerateLineTopology(7);  // 6 hops from first to last

//     // Start all nodes
//     for (auto* node : nodes) {
//         ASSERT_TRUE(StartNode(*node));
//     }

//     // Wait for network formation
//     ASSERT_TRUE(WaitForNetworkFormation(nodes, 6));

//     // Wait for routing - may not fully converge due to hop limit
//     AdvanceTime(20000);

//     // Check if nodes beyond max hop count are reachable
//     auto config = nodes[0]->protocol->GetServiceConfiguration();
//     uint8_t max_hops = config.network_config.max_hops;

//     // Try to send message from first to last node
//     if (nodes.size() - 1 > max_hops) {
//         // Message should not be deliverable
//         std::vector<uint8_t> payload = {0xFF};
//         SendMessage(*nodes[0], *nodes[nodes.size() - 1], MessageType::DATA_MSG,
//                     payload);

//         // Wait briefly
//         AdvanceTime(5000);

//         // Last node should not receive the message
//         EXPECT_FALSE(
//             HasReceivedMessageFrom(*nodes[nodes.size() - 1], nodes[0]->address))
//             << "Message delivered beyond max hop count limit";
//     }
// }

// /**
//  * @brief Test routing table size limits
//  */
// TEST_F(LoRaMeshRoutingTests, RoutingTableSizeLimit) {
//     // Create more nodes than the routing table can hold
//     int num_nodes = 10;  // Adjust based on protocol limits
//     auto nodes = GenerateFullMeshTopology(num_nodes);

//     // Start all nodes
//     for (auto* node : nodes) {
//         ASSERT_TRUE(StartNode(*node));
//     }

//     // Wait for network formation
//     AdvanceTime(30000);  // Longer time for large network

//     // Check routing table sizes
//     for (auto* node : nodes) {
//         auto& network_nodes = node->protocol->GetNetworkNodes();
//         auto config = node->protocol->GetServiceConfiguration();

//         EXPECT_LE(network_nodes.size(), config.network_config.max_network_nodes)
//             << "Routing table exceeded maximum size limit";
//     }
// }

// /**
//  * @brief Test route timeout and expiration
//  */
// TEST_F(LoRaMeshRoutingTests, RouteTimeout) {
//     // Create a simple 3-node line topology
//     auto nodes = GenerateLineTopology(3);

//     // Start all nodes
//     for (auto* node : nodes) {
//         ASSERT_TRUE(StartNode(*node));
//     }

//     // Wait for network formation
//     ASSERT_TRUE(WaitForNetworkFormation(nodes, 2));
//     ASSERT_TRUE(WaitForRoutingStabilization(nodes));

//     // Verify Node1 has route to Node3
//     auto& node1_routes = nodes[0]->protocol->GetNetworkNodes();
//     bool has_route = false;
//     for (const auto& route : node1_routes) {
//         if (route.routing_entry.destination == nodes[2]->address) {
//             has_route = true;
//             EXPECT_TRUE(route.is_active) << "Route should be active";
//             break;
//         }
//     }
//     EXPECT_TRUE(has_route) << "Node1 should have route to Node3";

//     // Disconnect Node2 (breaking the path)
//     SimulateNodeFailure(*nodes[1]);

//     // Wait for route timeout
//     auto config = nodes[0]->protocol->GetServiceConfiguration();
//     uint32_t route_timeout = config.network_config.route_timeout_ms;

//     bool route_expired = AdvanceTime(
//         route_timeout + 5000, route_timeout + 10000, 1000, 20, [&]() {
//             auto& routes = nodes[0]->protocol->GetNetworkNodes();
//             for (const auto& route : routes) {
//                 if (route.routing_entry.destination == nodes[2]->address) {
//                     return !route.is_active;  // Route should be inactive
//                 }
//             }
//             return true;  // Route removed entirely
//         });

//     EXPECT_TRUE(route_expired) << "Route did not expire after timeout";
// }

// }  // namespace test
// }  // namespace loramesher