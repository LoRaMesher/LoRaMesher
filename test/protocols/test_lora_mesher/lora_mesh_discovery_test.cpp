/**
 * @file lora_mesh_discovery_tests.cpp
 * @brief Test suite for LoRaMesh protocol discovery and network formation
 */

#include <gtest/gtest.h>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include "lora_mesh_test_fixture.hpp"

namespace loramesher {
namespace test {

/**
  * @brief Test suite for LoRaMesh protocol discovery and network formation
  */
class LoRaMeshDiscoveryTests : public LoRaMeshTestFixture {
   protected:
    void SetUp() override { LoRaMeshTestFixture::SetUp(); }

    void TearDown() override { LoRaMeshTestFixture::TearDown(); }

    /**
     * @brief Wait for tasks to execute
     * 
     * This helper function waits a short time to allow tasks to run and
     * process any events before continuing. It helps ensure proper test
     * sequencing, especially when virtual time is used.
     */
    void WaitForTasksToExecute() {
#ifdef LORAMESHER_BUILD_ARDUINO
        GetRTOS().delay(20);
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
#endif
    }
};

/**
  * @brief Test single node discovery
  * 
  * This test verifies that a single node properly transitions to network manager state
  * after the discovery timeout when no other nodes are present.
  */
TEST_F(LoRaMeshDiscoveryTests, SingleNodeDiscovery) {
    // Create a single node
    auto& node = CreateNode("Node1", 0x1001);

    // Start the node
    ASSERT_TRUE(StartNode(node));

    // Initially, the node should be in DISCOVERY state
    EXPECT_EQ(node.protocol->GetState(),
              protocols::LoRaMeshProtocol::ProtocolState::DISCOVERY);

    WaitForTasksToExecute();

    // Advance time past the discovery timeout
    bool advanced = AdvanceTime(
        GetDiscoveryTimeout() + 100, GetDiscoveryTimeout() + 500,
        GetDiscoveryTimeout() / 3, 10, [&]() {
            return node.protocol->GetState() ==
                   protocols::LoRaMeshProtocol::ProtocolState::NETWORK_MANAGER;
        });
    EXPECT_TRUE(advanced) << "Node did not become network manager in time";

    EXPECT_TRUE(node.protocol->IsSynchronized());
    EXPECT_EQ(node.protocol->GetNetworkManager(), node.address);
}

// /**
//   * @brief Test two node network formation with sequential start
//   *
//   * This test verifies that when two nodes are within range and started sequentially,
//   * the first node becomes network manager and the second node joins the network.
//   */
// TEST_F(LoRaMeshDiscoveryTests, TwoNodeSequentialStart) {
//     // Create two nodes
//     auto& node1 = CreateNode("Node1", 0x1001);
//     auto& node2 = CreateNode("Node2", 0x1002);

//     // Ensure nodes can communicate
//     SetLinkStatus(node1, node2, true);

//     // Start the first node
//     ASSERT_TRUE(StartNode(node1));

//     // Advance time to let node1 become network manager
//     AdvanceTime(GetDiscoveryTimeout() + 100);

//     // Verify node1 is now a network manager
//     EXPECT_EQ(node1.protocol->GetState(),
//               protocols::LoRaMeshProtocol::ProtocolState::NETWORK_MANAGER);
//     EXPECT_TRUE(node1.protocol->IsSynchronized());

//     // Start the second node
//     ASSERT_TRUE(StartNode(node2));

//     // Advance time to let node2 discover node1's network
//     AdvanceTime(GetDiscoveryTimeout() / 2);

//     // Verify node2 joined node1's network
//     EXPECT_EQ(node2.protocol->GetState(),
//               protocols::LoRaMeshProtocol::ProtocolState::NORMAL_OPERATION);
//     EXPECT_TRUE(node2.protocol->IsSynchronized());
//     EXPECT_EQ(node2.protocol->GetNetworkManager(), node1.address);

//     // Verify node1 is still network manager
//     EXPECT_EQ(node1.protocol->GetState(),
//               protocols::LoRaMeshProtocol::ProtocolState::NETWORK_MANAGER);
// }

// /**
//   * @brief Test two node network formation with simultaneous start
//   *
//   * This test verifies that when two nodes are within range and started simultaneously,
//   * only one becomes the network manager.
//   */
// TEST_F(LoRaMeshDiscoveryTests, TwoNodeSimultaneousStart) {
//     // Create two nodes
//     auto& node1 = CreateNode("Node1", 0x1001);
//     auto& node2 = CreateNode("Node2", 0x1002);

//     // Ensure nodes can communicate
//     SetLinkStatus(node1, node2, true);

//     // Start both nodes simultaneously
//     ASSERT_TRUE(StartNode(node1));
//     ASSERT_TRUE(StartNode(node2));

//     // Advance time past discovery timeout
//     AdvanceTime(GetDiscoveryTimeout() + 100);

//     // Verify only one node became network manager
//     int network_manager_count = 0;
//     int normal_operation_count = 0;

//     if (node1.protocol->GetState() ==
//         protocols::LoRaMeshProtocol::ProtocolState::NETWORK_MANAGER) {
//         network_manager_count++;
//         EXPECT_EQ(node2.protocol->GetState(),
//                   protocols::LoRaMeshProtocol::ProtocolState::NORMAL_OPERATION);
//         EXPECT_EQ(node2.protocol->GetNetworkManager(), node1.address);
//         normal_operation_count++;
//     } else if (node2.protocol->GetState() ==
//                protocols::LoRaMeshProtocol::ProtocolState::NETWORK_MANAGER) {
//         network_manager_count++;
//         EXPECT_EQ(node1.protocol->GetState(),
//                   protocols::LoRaMeshProtocol::ProtocolState::NORMAL_OPERATION);
//         EXPECT_EQ(node1.protocol->GetNetworkManager(), node2.address);
//         normal_operation_count++;
//     }

//     EXPECT_EQ(network_manager_count, 1)
//         << "Expected exactly one network manager";
//     EXPECT_EQ(normal_operation_count, 1)
//         << "Expected exactly one normal operation node";

//     // Both nodes should be synchronized
//     EXPECT_TRUE(node1.protocol->IsSynchronized());
//     EXPECT_TRUE(node2.protocol->IsSynchronized());
// }

// /**
//   * @brief Test multi-node network formation with fully connected topology
//   *
//   * This test verifies that a network can form with multiple nodes in a fully connected topology.
//   */
// TEST_F(LoRaMeshDiscoveryTests, MultiNodeFullyConnected) {
//     // Create a fully connected network of 5 nodes
//     auto nodes = GenerateFullMeshTopology(5);

//     // Start all nodes simultaneously
//     for (auto* node : nodes) {
//         ASSERT_TRUE(StartNode(*node));
//     }

//     // Advance time past discovery timeout
//     AdvanceTime(GetDiscoveryTimeout() + 100);

//     // Verify only one node became network manager
//     TestNode* manager = nullptr;
//     int network_manager_count = 0;
//     int normal_operation_count = 0;

//     for (auto* node : nodes) {
//         if (node->protocol->GetState() ==
//             protocols::LoRaMeshProtocol::ProtocolState::NETWORK_MANAGER) {
//             network_manager_count++;
//             manager = node;
//         } else if (node->protocol->GetState() ==
//                    protocols::LoRaMeshProtocol::ProtocolState::
//                        NORMAL_OPERATION) {
//             normal_operation_count++;
//         }
//     }

//     EXPECT_EQ(network_manager_count, 1)
//         << "Expected exactly one network manager";
//     EXPECT_EQ(normal_operation_count, nodes.size() - 1)
//         << "Expected all other nodes to be in normal operation";

//     // All nodes should be synchronized
//     for (auto* node : nodes) {
//         EXPECT_TRUE(node->protocol->IsSynchronized());
//         if (node != manager) {
//             EXPECT_EQ(node->protocol->GetNetworkManager(), manager->address);
//         }
//     }
// }

// /**
//   * @brief Test multi-node network formation with line topology
//   *
//   * This test verifies that a network can form with multiple nodes in a line topology
//   * where each node can only communicate with its neighbors.
//   */
// TEST_F(LoRaMeshDiscoveryTests, MultiNodeLineTopology) {
//     // Create a line topology of 5 nodes (A - B - C - D - E)
//     auto nodes = GenerateLineTopology(5);

//     // Start all nodes simultaneously
//     for (auto* node : nodes) {
//         ASSERT_TRUE(StartNode(*node));
//     }

//     // Advance time - line topology needs more time for multi-hop discovery
//     AdvanceTime(GetDiscoveryTimeout() * 3);

//     // Verify only one node became network manager
//     TestNode* manager = nullptr;
//     int network_manager_count = 0;

//     for (auto* node : nodes) {
//         if (node->protocol->GetState() ==
//             protocols::LoRaMeshProtocol::ProtocolState::NETWORK_MANAGER) {
//             network_manager_count++;
//             manager = node;
//         }
//     }

//     EXPECT_EQ(network_manager_count, 1)
//         << "Expected exactly one network manager";
//     ASSERT_NE(manager, nullptr) << "Failed to find network manager";

//     // All nodes should eventually be synchronized
//     for (auto* node : nodes) {
//         EXPECT_TRUE(node->protocol->IsSynchronized())
//             << "Node " << node->name << " not synchronized";

//         if (node != manager) {
//             EXPECT_EQ(
//                 node->protocol->GetState(),
//                 protocols::LoRaMeshProtocol::ProtocolState::NORMAL_OPERATION)
//                 << "Node " << node->name << " not in NORMAL_OPERATION state";
//             EXPECT_EQ(node->protocol->GetNetworkManager(), manager->address)
//                 << "Node " << node->name << " has incorrect network manager";
//         }
//     }
// }

// /**
//   * @brief Test handling of isolated nodes
//   *
//   * This test verifies that isolated nodes (with no connection to others)
//   * become network managers of their own isolated networks.
//   */
// TEST_F(LoRaMeshDiscoveryTests, IsolatedNodes) {
//     // Create three nodes with no connections between them
//     auto& node1 = CreateNode("Node1", 0x1001);
//     auto& node2 = CreateNode("Node2", 0x1002);
//     auto& node3 = CreateNode("Node3", 0x1003);

//     // Explicitly disable all connections
//     SetLinkStatus(node1, node2, false);
//     SetLinkStatus(node1, node3, false);
//     SetLinkStatus(node2, node3, false);

//     // Start all nodes
//     ASSERT_TRUE(StartNode(node1));
//     ASSERT_TRUE(StartNode(node2));
//     ASSERT_TRUE(StartNode(node3));

//     // Advance time past discovery timeout
//     AdvanceTime(GetDiscoveryTimeout() + 100);

//     // All three nodes should become network managers
//     EXPECT_EQ(node1.protocol->GetState(),
//               protocols::LoRaMeshProtocol::ProtocolState::NETWORK_MANAGER);
//     EXPECT_EQ(node2.protocol->GetState(),
//               protocols::LoRaMeshProtocol::ProtocolState::NETWORK_MANAGER);
//     EXPECT_EQ(node3.protocol->GetState(),
//               protocols::LoRaMeshProtocol::ProtocolState::NETWORK_MANAGER);

//     // All nodes should be synchronized (to their own networks)
//     EXPECT_TRUE(node1.protocol->IsSynchronized());
//     EXPECT_TRUE(node2.protocol->IsSynchronized());
//     EXPECT_TRUE(node3.protocol->IsSynchronized());

//     // Each node should be its own network manager
//     EXPECT_EQ(node1.protocol->GetNetworkManager(), node1.address);
//     EXPECT_EQ(node2.protocol->GetNetworkManager(), node2.address);
//     EXPECT_EQ(node3.protocol->GetNetworkManager(), node3.address);
// }

// /**
//   * @brief Test network partitioning and merging
//   *
//   * This test verifies that separate networks can form when partitioned,
//   * and then merge when a connection becomes available.
//   */
// TEST_F(LoRaMeshDiscoveryTests, NetworkPartitionAndMerge) {
//     // Create two separate network partitions
//     auto [group1, group2] = CreatePartitionedNetwork(3, 3);

//     // Start all nodes
//     for (auto* node : group1) {
//         ASSERT_TRUE(StartNode(*node));
//     }
//     for (auto* node : group2) {
//         ASSERT_TRUE(StartNode(*node));
//     }

//     // Advance time to allow separate networks to form
//     AdvanceTime(GetDiscoveryTimeout() + 100);

//     // Verify two network managers (one in each partition)
//     TestNode* manager1 = FindNetworkManager(group1);
//     TestNode* manager2 = FindNetworkManager(group2);

//     ASSERT_NE(manager1, nullptr) << "Failed to find network manager in group 1";
//     ASSERT_NE(manager2, nullptr) << "Failed to find network manager in group 2";
//     EXPECT_NE(manager1, manager2)
//         << "Expected different managers for each group";

//     // Verify all nodes in each group are synchronized to their respective managers
//     for (auto* node : group1) {
//         EXPECT_TRUE(node->protocol->IsSynchronized());
//         EXPECT_EQ(node->protocol->GetNetworkManager(), manager1->address);
//     }

//     for (auto* node : group2) {
//         EXPECT_TRUE(node->protocol->IsSynchronized());
//         EXPECT_EQ(node->protocol->GetNetworkManager(), manager2->address);
//     }

//     // Now create a bridge between the two networks
//     CreateBridgeBetweenGroups(group1, group2);

//     // Advance time to allow networks to merge
//     AdvanceTime(GetDiscoveryTimeout() * 3);

//     // After merging, there should be only one network manager
//     std::vector<TestNode*> all_nodes;
//     all_nodes.insert(all_nodes.end(), group1.begin(), group1.end());
//     all_nodes.insert(all_nodes.end(), group2.begin(), group2.end());

//     int manager_count = 0;
//     TestNode* final_manager = nullptr;

//     for (auto* node : all_nodes) {
//         if (node->protocol->GetState() ==
//             protocols::LoRaMeshProtocol::ProtocolState::NETWORK_MANAGER) {
//             manager_count++;
//             final_manager = node;
//         }
//     }

//     EXPECT_EQ(manager_count, 1)
//         << "Expected exactly one network manager after merge";
//     ASSERT_NE(final_manager, nullptr) << "Failed to find final network manager";

//     // All nodes should be synchronized to the same manager
//     for (auto* node : all_nodes) {
//         EXPECT_TRUE(node->protocol->IsSynchronized())
//             << "Node " << node->name << " not synchronized";
//         EXPECT_EQ(node->protocol->GetNetworkManager(), final_manager->address)
//             << "Node " << node->name << " has incorrect network manager";
//     }
// }

// /**
//   * @brief Test network manager failure and recovery
//   *
//   * This test verifies that the network can recover when the network manager fails
//   * by electing a new network manager.
//   */
// TEST_F(LoRaMeshDiscoveryTests, NetworkManagerFailure) {
//     // Create a fully connected network of 5 nodes
//     auto nodes = GenerateFullMeshTopology(5);

//     // Start all nodes
//     for (auto* node : nodes) {
//         ASSERT_TRUE(StartNode(*node));
//     }

//     // Advance time to allow network to form
//     AdvanceTime(GetDiscoveryTimeout() + 100);

//     // Find the network manager
//     TestNode* original_manager = FindNetworkManager(nodes);
//     ASSERT_NE(original_manager, nullptr) << "Failed to find network manager";

//     // Verify all nodes are synchronized to the manager
//     for (auto* node : nodes) {
//         EXPECT_TRUE(node->protocol->IsSynchronized());
//         EXPECT_EQ(node->protocol->GetNetworkManager(),
//                   original_manager->address);
//     }

//     // Simulate failure of the network manager
//     SimulateNodeFailure(*original_manager);

//     // Advance time to allow network to recover
//     AdvanceTime(GetDiscoveryTimeout() * 3);

//     // Verify a new network manager was elected
//     TestNode* new_manager = nullptr;
//     int manager_count = 0;

//     for (auto* node : nodes) {
//         if (node != original_manager &&
//             node->protocol->GetState() ==
//                 protocols::LoRaMeshProtocol::ProtocolState::NETWORK_MANAGER) {
//             manager_count++;
//             new_manager = node;
//         }
//     }

//     EXPECT_EQ(manager_count, 1) << "Expected exactly one new network manager";
//     ASSERT_NE(new_manager, nullptr) << "Failed to find new network manager";
//     EXPECT_NE(new_manager, original_manager)
//         << "New manager should be different from original";

//     // All remaining nodes should be synchronized to the new manager
//     for (auto* node : nodes) {
//         if (node != original_manager) {
//             EXPECT_TRUE(node->protocol->IsSynchronized())
//                 << "Node " << node->name << " not synchronized";
//             EXPECT_EQ(node->protocol->GetNetworkManager(), new_manager->address)
//                 << "Node " << node->name << " has incorrect network manager";
//         }
//     }
// }

// /**
//   * @brief Test node rejoining after temporary disconnection
//   *
//   * This test verifies that a node can rejoin the network after being temporarily
//   * disconnected.
//   */
// TEST_F(LoRaMeshDiscoveryTests, NodeRejoin) {
//     // Create a fully connected network of 5 nodes
//     auto nodes = GenerateFullMeshTopology(5);

//     // Start all nodes
//     for (auto* node : nodes) {
//         ASSERT_TRUE(StartNode(*node));
//     }

//     // Advance time to allow network to form
//     AdvanceTime(GetDiscoveryTimeout() + 100);

//     // Find the network manager
//     TestNode* manager = FindNetworkManager(nodes);
//     ASSERT_NE(manager, nullptr) << "Failed to find network manager";

//     // Choose a node that is not the manager to disconnect
//     TestNode* disconnect_node = nullptr;
//     for (auto* node : nodes) {
//         if (node != manager) {
//             disconnect_node = node;
//             break;
//         }
//     }
//     ASSERT_NE(disconnect_node, nullptr) << "Failed to find node to disconnect";

//     // Verify initial state
//     EXPECT_EQ(disconnect_node->protocol->GetState(),
//               protocols::LoRaMeshProtocol::ProtocolState::NORMAL_OPERATION);
//     EXPECT_TRUE(disconnect_node->protocol->IsSynchronized());
//     EXPECT_EQ(disconnect_node->protocol->GetNetworkManager(), manager->address);

//     // Disconnect the node
//     SimulateNodeFailure(*disconnect_node);

//     // Advance time to allow network to recognize the disconnection
//     AdvanceTime(GetDiscoveryTimeout() * 2);

//     // Now reconnect the node
//     SimulateNodeRecovery(*disconnect_node);

//     // Advance time to allow node to rejoin
//     AdvanceTime(GetDiscoveryTimeout() * 2);

//     // Verify the node has rejoined
//     EXPECT_EQ(disconnect_node->protocol->GetState(),
//               protocols::LoRaMeshProtocol::ProtocolState::NORMAL_OPERATION);
//     EXPECT_TRUE(disconnect_node->protocol->IsSynchronized());
//     EXPECT_EQ(disconnect_node->protocol->GetNetworkManager(), manager->address);
// }

// /**
//   * @brief Test staggered node startup
//   *
//   * This test verifies that nodes can join an existing network when started
//   * at different times.
//   */
// TEST_F(LoRaMeshDiscoveryTests, StaggeredStartup) {
//     // Create 5 nodes but don't start them yet
//     // auto& node1 = CreateNode("Node1", 0x1001);
//     // auto& node2 = CreateNode("Node2", 0x1002);
//     // auto& node3 = CreateNode("Node3", 0x1003);
//     // auto& node4 = CreateNode("Node4", 0x1004);
//     // auto& node5 = CreateNode("Node5", 0x1005);

//     // // Make all nodes fully connected
//     // for (size_t i = 0; i < nodes_.size(); i++) {
//     //     for (size_t j = i + 1; j < nodes_.size(); j++) {
//     //         SetLinkStatus(nodes_[i], nodes_[j], true);
//     //     }
//     // }

//     // // Start first node and let it become network manager
//     // ASSERT_TRUE(StartNode(node1));
//     // AdvanceTime(GetDiscoveryTimeout() + 100);

//     // // Verify first node is network manager
//     // EXPECT_EQ(node1.protocol->GetState(),
//     //           protocols::LoRaMeshProtocol::ProtocolState::NETWORK_MANAGER);
//     // EXPECT_TRUE(node1.protocol->IsSynchronized());

//     // // Start second node
//     // ASSERT_TRUE(StartNode(node2));
//     // AdvanceTime(GetDiscoveryTimeout() / 2);

//     // // Verify second node joined
//     // EXPECT_EQ(node2.protocol->GetState(),
//     //           protocols::LoRaMeshProtocol::ProtocolState::NORMAL_OPERATION);
//     // EXPECT_TRUE(node2.protocol->IsSynchronized());
//     // EXPECT_EQ(node2.protocol->GetNetworkManager(), node1.address);

//     // // Start third node
//     // ASSERT_TRUE(StartNode(node3));
//     // AdvanceTime(GetDiscoveryTimeout() / 2);

//     // // Verify third node joined
//     // EXPECT_EQ(node3.protocol->GetState(),
//     //           protocols::LoRaMeshProtocol::ProtocolState::NORMAL_OPERATION);
//     // EXPECT_TRUE(node3.protocol->IsSynchronized());
//     // EXPECT_EQ(node3.protocol->GetNetworkManager(), node1.address);

//     // // Start fourth and fifth nodes simultaneously
//     // ASSERT_TRUE(StartNode(node4));
//     // ASSERT_TRUE(StartNode(node5));
//     // AdvanceTime(GetDiscoveryTimeout() / 2);

//     // // Verify fourth and fifth nodes joined
//     // EXPECT_EQ(node4.protocol->GetState(),
//     //           protocols::LoRaMeshProtocol::ProtocolState::NORMAL_OPERATION);
//     // EXPECT_TRUE(node4.protocol->IsSynchronized());
//     // EXPECT_EQ(node4.protocol->GetNetworkManager(), node1.address);

//     // EXPECT_EQ(node5.protocol->GetState(),
//     //           protocols::LoRaMeshProtocol::ProtocolState::NORMAL_OPERATION);
//     // EXPECT_TRUE(node5.protocol->IsSynchronized());
//     // EXPECT_EQ(node5.protocol->GetNetworkManager(), node1.address);

//     // // Verify first node is still network manager
//     // EXPECT_EQ(node1.protocol->GetState(),
//     //           protocols::LoRaMeshProtocol::ProtocolState::NETWORK_MANAGER);
// }

// /**
//   * @brief Test message propagation with different topologies
//   *
//   * This test verifies that discovery messages can propagate through different
//   * network topologies.
//   */
// TEST_F(LoRaMeshDiscoveryTests, MessagePropagationTopologies) {
//     // Test with a star topology (one central node connected to all others)
//     auto nodes = GenerateStarTopology(5);

//     // Start the central node first
//     ASSERT_TRUE(StartNode(*nodes[0]));
//     AdvanceTime(GetDiscoveryTimeout() + 100);

//     // Verify central node is network manager
//     EXPECT_EQ(nodes[0]->protocol->GetState(),
//               protocols::LoRaMeshProtocol::ProtocolState::NETWORK_MANAGER);

//     // Start all other nodes
//     for (size_t i = 1; i < nodes.size(); i++) {
//         ASSERT_TRUE(StartNode(*nodes[i]));
//     }

//     // Advance time to allow joining
//     AdvanceTime(GetDiscoveryTimeout() / 2);

//     // Verify all nodes joined
//     for (size_t i = 1; i < nodes.size(); i++) {
//         EXPECT_EQ(nodes[i]->protocol->GetState(),
//                   protocols::LoRaMeshProtocol::ProtocolState::NORMAL_OPERATION)
//             << "Node " << i << " not in NORMAL_OPERATION state";
//         EXPECT_TRUE(nodes[i]->protocol->IsSynchronized())
//             << "Node " << i << " not synchronized";
//         EXPECT_EQ(nodes[i]->protocol->GetNetworkManager(), nodes[0]->address)
//             << "Node " << i << " has incorrect network manager";
//     }
// }

// /**
//   * @brief Test with simulated packet loss
//   *
//   * This test verifies that the network can still form when there is packet loss.
//   */
// TEST_F(LoRaMeshDiscoveryTests, PacketLoss) {
//     // Create a fully connected network of 5 nodes
//     auto nodes = GenerateFullMeshTopology(5);

//     // Set moderate packet loss rate (30%)
//     SetPacketLossRate(0.3f);

//     // Start all nodes
//     for (auto* node : nodes) {
//         ASSERT_TRUE(StartNode(*node));
//     }

//     // Need more time with packet loss
//     AdvanceTime(GetDiscoveryTimeout() * 3);

//     // Verify network formation succeeded despite packet loss
//     int manager_count = 0;
//     int normal_count = 0;
//     TestNode* manager = nullptr;

//     for (auto* node : nodes) {
//         if (node->protocol->GetState() ==
//             protocols::LoRaMeshProtocol::ProtocolState::NETWORK_MANAGER) {
//             manager_count++;
//             manager = node;
//         } else if (node->protocol->GetState() ==
//                    protocols::LoRaMeshProtocol::ProtocolState::
//                        NORMAL_OPERATION) {
//             normal_count++;
//         }
//     }

//     EXPECT_EQ(manager_count, 1) << "Expected exactly one network manager";
//     ASSERT_NE(manager, nullptr) << "Failed to find network manager";

//     // Some nodes may not have joined due to packet loss, but at least some should
//     EXPECT_GT(normal_count, 0) << "Expected at least one node to join";

//     // Verify joined nodes are synchronized
//     for (auto* node : nodes) {
//         if (node->protocol->GetState() ==
//             protocols::LoRaMeshProtocol::ProtocolState::NORMAL_OPERATION) {
//             EXPECT_TRUE(node->protocol->IsSynchronized());
//             EXPECT_EQ(node->protocol->GetNetworkManager(), manager->address);
//         }
//     }
// }

}  // namespace test
}  // namespace loramesher