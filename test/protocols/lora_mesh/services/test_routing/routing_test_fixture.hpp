/**
 * @file routing_test_fixture.hpp
 * @brief Test fixture for LoRaMesh routing tests with routing-specific helpers
 */
#pragma once

#include "../test_integration/lora_mesh_test_fixture.hpp"

namespace loramesher {
namespace test {

/**
 * @brief Test fixture for routing tests
 *
 * Extends LoRaMeshTestFixture with routing-specific helper methods
 * for verifying routing table entries and message delivery.
 */
class RoutingTestFixture : public LoRaMeshTestFixture {
   protected:
    void SetUp() override { LoRaMeshTestFixture::SetUp(); }

    void TearDown() override { LoRaMeshTestFixture::TearDown(); }

    /**
     * @brief Wait for routing tables to stabilize across all nodes
     *
     * Waits until all nodes have discovered all other nodes in the network.
     * Note: This only checks that nodes know about each other, NOT that
     * routes have optimal hop counts. Use topology-specific wait functions
     * (e.g., WaitForFullMeshConvergence) when hop counts matter.
     *
     * @param nodes Nodes to check
     * @return bool True if routing stabilized, false on timeout
     */
    bool WaitForRoutingStabilization(const std::vector<TestNode*>& nodes) {
        // Increase multiplier to allow more time for routing table propagation
        uint32_t timeout_ms =
            GetDiscoveryTimeout(*nodes.front()) * (nodes.size() + 5);

        uint32_t step_ms = 15u;
        uint32_t end_superframe_time = 0;
        return AdvanceTime(timeout_ms, timeout_ms, step_ms, 0, [&]() {
            // Check if all nodes have discovered all other nodes
            for (auto* node : nodes) {
                auto network_nodes = node->protocol->GetNetworkNodesCopy();
                // Each node should know about all other nodes
                if (network_nodes.size() < nodes.size() - 1) {
                    return false;
                }
            }

            if (end_superframe_time == 0) {
                end_superframe_time = GetRTOS().getTickCount() +
                                      GetSuperframeDuration(*nodes.front());
            }

            // Wait for at least one full superframe after all nodes have discovered each other
            if (GetRTOS().getTickCount() < end_superframe_time) {
                return false;
            }

            return true;
        });
    }

    /**
     * @brief Wait for full mesh routing to converge (all routes should be 1
     * hop)
     *
     * In a full mesh topology where all nodes can communicate directly,
     * all routes should converge to 1 hop. This function waits for the
     * distance-vector algorithm to fully converge.
     *
     * @param nodes Nodes in the full mesh topology
     * @return bool True if all routes converged to 1 hop, false on timeout
     */
    bool WaitForFullMeshConvergence(const std::vector<TestNode*>& nodes) {
        // Need extra time for all nodes to broadcast their routing tables
        // Each node broadcasts in CONTROL_TX slot once per superframe
        uint32_t timeout_ms =
            GetDiscoveryTimeout(*nodes.front()) * (nodes.size() + 5);

        uint32_t step_ms = 15u;
        uint32_t end_superframe_time = 0;
        return AdvanceTime(timeout_ms, timeout_ms, step_ms, 0, [&]() {
            for (auto* node : nodes) {
                auto network_nodes = node->protocol->GetNetworkNodesCopy();
                // Each node should know about all other nodes
                if (network_nodes.size() < nodes.size() - 1) {
                    return false;
                }
                // All routes should be 1 hop in full mesh
                for (const auto& route : network_nodes) {
                    if (route.is_active && route.routing_entry.hop_count != 1) {
                        return false;  // Routes not yet converged
                    }
                }
            }

            if (end_superframe_time == 0) {
                end_superframe_time = GetRTOS().getTickCount() +
                                      GetSuperframeDuration(*nodes.front()) * 2;
            }

            // Wait for at least one full superframe after all nodes have discovered each other
            if (GetRTOS().getTickCount() < end_superframe_time) {
                return false;
            }

            return true;
        });
    }

    /**
     * @brief Wait for star topology routing to converge
     *
     * In a star topology:
     * - Center node has 1 hop to all peripheral nodes
     * - Peripheral nodes have 1 hop to center
     * - Peripheral nodes have 2 hops to other peripherals (via center)
     *
     * @param nodes All nodes in star topology
     * @param center_index Index of the center node in the nodes vector
     * @return bool True if routes converged to expected hop counts
     */
    bool WaitForStarConvergence(const std::vector<TestNode*>& nodes,
                                size_t center_index) {
        uint32_t timeout_ms =
            GetDiscoveryTimeout(*nodes.front()) * (nodes.size() + 5);
        AddressType center_addr = nodes[center_index]->address;

        uint32_t step_ms = 15u;
        uint32_t end_superframe_time = 0;
        return AdvanceTime(timeout_ms, timeout_ms, step_ms, 0, [&]() {
            for (size_t i = 0; i < nodes.size(); i++) {
                auto network_nodes = nodes[i]->protocol->GetNetworkNodesCopy();
                if (network_nodes.size() < nodes.size() - 1) {
                    return false;
                }

                for (const auto& route : network_nodes) {
                    if (!route.is_active) {
                        continue;
                    }

                    uint8_t expected_hops;
                    if (i == center_index) {
                        expected_hops = 1;  // Center to anyone = 1 hop
                    } else if (route.routing_entry.destination == center_addr) {
                        expected_hops = 1;  // Peripheral to center = 1 hop
                    } else {
                        expected_hops = 2;  // Peripheral to peripheral = 2 hops
                    }

                    if (route.routing_entry.hop_count != expected_hops) {
                        return false;
                    }
                }
            }

            if (end_superframe_time == 0) {
                end_superframe_time = GetRTOS().getTickCount() +
                                      GetSuperframeDuration(*nodes.front());
            }

            // Wait for at least one full superframe after all nodes have discovered each other
            if (GetRTOS().getTickCount() < end_superframe_time) {
                return false;
            }
            return true;
        });
    }

    /**
     * @brief Check if node has an active route to destination
     *
     * @param node Node to check
     * @param destination Destination address
     * @return bool True if active route exists
     */
    bool HasRouteTo(TestNode& node, AddressType destination) {
        auto network_nodes = node.protocol->GetNetworkNodesCopy();
        for (const auto& route : network_nodes) {
            if (route.routing_entry.destination == destination &&
                route.is_active) {
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Get next hop for a destination
     *
     * @param node Node to query
     * @param destination Destination address
     * @return AddressType Next hop address (0 if no route)
     */
    AddressType GetNextHop(TestNode& node, AddressType destination) {
        auto network_nodes = node.protocol->GetNetworkNodesCopy();
        for (const auto& route : network_nodes) {
            if (route.routing_entry.destination == destination &&
                route.is_active) {
                return route.next_hop;
            }
        }
        return 0;
    }

    /**
     * @brief Get hop count to destination
     *
     * @param node Node to query
     * @param destination Destination address
     * @return uint8_t Hop count (UINT8_MAX if no route)
     */
    uint8_t GetHopCount(TestNode& node, AddressType destination) {
        auto network_nodes = node.protocol->GetNetworkNodesCopy();
        for (const auto& route : network_nodes) {
            if (route.routing_entry.destination == destination &&
                route.is_active) {
                return route.routing_entry.hop_count;
            }
        }
        return UINT8_MAX;
    }

    /**
     * @brief Verify route exists with expected hop count
     *
     * @param node Node to check
     * @param destination Destination address
     * @param expected_hops Expected hop count
     * @return bool True if route exists with expected hop count
     */
    bool VerifyRoute(TestNode& node, AddressType destination,
                     uint8_t expected_hops) {
        return GetHopCount(node, destination) == expected_hops;
    }

    /**
     * @brief Generate a ring topology
     *
     * Creates nodes connected in a ring: N1↔N2↔N3↔...↔Nn↔N1
     *
     * @param num_nodes Number of nodes to create
     * @param base_address Base address for nodes (default: 0x1000)
     * @param name_prefix Prefix for node names (default: "Node")
     * @param manager_index Index of the node to designate as NETWORK_MANAGER
     *                      (-1 = all nodes use AUTO role, default)
     * @return std::vector<TestNode*> Pointers to the created nodes
     */
    std::vector<TestNode*> GenerateRingTopology(
        int num_nodes, AddressType base_address = 0x1000,
        const std::string& name_prefix = "Node", int manager_index = -1) {
        std::vector<TestNode*> result;

        // Create nodes
        for (int i = 0; i < num_nodes; i++) {
            std::string name = name_prefix + std::to_string(i + 1);
            AddressType address = base_address + i;

            // Determine node role based on manager_index
            NodeRole role = NodeRole::AUTO;
            if (manager_index >= 0) {
                role = (i == manager_index) ? NodeRole::NETWORK_MANAGER
                                            : NodeRole::NODE_ONLY;
            }

            CreateNode(name, address, role);

            for (auto& node_ptr : nodes_) {
                if (node_ptr->address == address) {
                    result.push_back(node_ptr.get());
                    break;
                }
            }
        }

        // First disable all connections
        for (size_t i = 0; i < result.size(); i++) {
            for (size_t j = 0; j < result.size(); j++) {
                if (i != j) {
                    SetLinkStatus(*result[i], *result[j], false);
                }
            }
        }

        // Connect nodes in a ring
        for (size_t i = 0; i < result.size(); i++) {
            size_t next = (i + 1) % result.size();
            SetLinkStatus(*result[i], *result[next], true);
        }

        return result;
    }

    /**
     * @brief Print routing table for debugging
     *
     * @param node Node whose routing table to print
     */
    void PrintRoutingTable(TestNode& node) {
        std::cout << "Routing table for " << node.name << " (0x" << std::hex
                  << node.address << std::dec << "):" << std::endl;

        auto network_nodes = node.protocol->GetNetworkNodesCopy();
        for (const auto& route : network_nodes) {
            std::cout << "  -> 0x" << std::hex
                      << route.routing_entry.destination << std::dec
                      << " via 0x" << std::hex << route.next_hop << std::dec
                      << " (hops: "
                      << static_cast<int>(route.routing_entry.hop_count)
                      << ", active: " << (route.is_active ? "yes" : "no") << ")"
                      << std::endl;
        }
    }

    /**
     * @brief Count received messages from a specific source
     *
     * @param node Node to check
     * @param from_address Source address (0 for any)
     * @param type Message type (ANY for any)
     * @return size_t Number of matching messages
     */
    size_t CountReceivedMessages(const TestNode& node,
                                 AddressType from_address = 0,
                                 MessageType type = MessageType::ANY) {
        size_t count = 0;
        for (const auto& msg : node.received_messages) {
            if ((from_address == 0 || msg.GetSource() == from_address) &&
                (type == MessageType::ANY || msg.GetType() == type)) {
                count++;
            }
        }
        return count;
    }

    /**
     * @brief Clear received messages for all nodes
     */
    void ClearAllReceivedMessages() {
        for (auto& node : nodes_) {
            node->received_messages.clear();
        }
    }
};

}  // namespace test
}  // namespace loramesher
