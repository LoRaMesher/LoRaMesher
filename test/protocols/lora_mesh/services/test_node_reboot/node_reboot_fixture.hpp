/**
 * @file node_reboot_fixture.hpp
 * @brief Fixture that reboots nodes of a simulated mesh and checks recovery
 *
 * A reboot powers a node off (its protocol and hardware are destroyed, packets
 * addressed to it are lost), lets virtual time pass and builds a new stack for
 * the same device. Each node owns a MemoryStateStore that outlives its
 * reboots, emulating the RTC memory or flash a real device keeps its snapshot
 * in, so the same harness covers OTA reboots and deep sleep.
 */
#pragma once

#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../test_routing/routing_test_fixture.hpp"
#include "protocols/lora_mesh/services/network_service.hpp"
#include "types/storage/memory_state_store.hpp"

namespace loramesher {
namespace test {

class NodeRebootFixture : public RoutingTestFixture {
   public:
    enum class Topology {
        kLine,      ///< Node i linked to node i+1; the manager is at one end
        kStar,      ///< Every node linked to the manager only
        kFullMesh,  ///< Every node linked to every other node
    };

    /**
     * @brief Shape of the network under test; node 0 is the manager
     */
    struct NetworkSpec {
        Topology topology = Topology::kLine;
        int node_count = 4;
        /// Role of the nodes other than the manager
        NodeRole member_role = NodeRole::AUTO;
        /// Give every node a state store so reboots are warm restarts
        bool with_store = true;
        /// Target TX duty cycle; lower values give longer superframes
        float target_duty_cycle = 1.0f;
    };

   protected:
    using ProtocolState = protocols::lora_mesh::INetworkService::ProtocolState;

    /// Address of node i is kBaseAddress + i
    static constexpr AddressType kBaseAddress = 0x1000;
    /// Health checks with this manager accept any single network manager
    static constexpr AddressType kAnyManager = 0;
    /// Payload marker of the data messages sent by ExpectDataFlows()
    static constexpr uint8_t kDataMarker = 0xD7;

    /**
     * @brief Create and link the nodes of @p spec, without starting them
     */
    std::vector<TestNode*> BuildNetwork(const NetworkSpec& spec) {
        spec_ = spec;
        std::vector<TestNode*> nodes;
        for (int i = 0; i < spec.node_count; ++i) {
            const AddressType address =
                static_cast<AddressType>(kBaseAddress + i);
            const NodeRole role =
                (i == 0) ? NodeRole::NETWORK_MANAGER : spec.member_role;
            if (spec.with_store) {
                stores_[address] =
                    std::make_shared<storage::MemoryStateStore>();
            }
            TestNode& node =
                CreateNode("Node" + std::to_string(i), address, role,
                           PinConfig(), RadioConfig(), MakeCustomizer(address));
            nodes.push_back(&node);
        }
        for (size_t i = 0; i < nodes.size(); ++i) {
            for (size_t j = i + 1; j < nodes.size(); ++j) {
                SetLinkStatus(*nodes[i], *nodes[j], IsLinked(spec, i, j));
            }
        }
        return nodes;
    }

    /**
     * @brief Start every node and wait until the network is healthy
     *
     * Records the network id, the superframe duration and every member's
     * control slot so later checks can compare against them.
     */
    void StartAndFormNetwork(const std::vector<TestNode*>& nodes) {
        for (auto* node : nodes) {
            ASSERT_TRUE(StartNode(*node));
        }
        ASSERT_TRUE(WaitForHealthyNetwork(nodes, nodes.front()->address,
                                          FormationBudgetMs(nodes)))
            << DescribeNetwork(nodes);
        superframe_ms_ = nodes.front()->protocol->GetSuperframeDuration();
        discovery_timeout_ms_ = GetDiscoveryTimeout(*nodes.front());
        formed_network_id_ = NetworkIdOf(*nodes.front());
        formed_control_slots_ = ControlSlotsOf(nodes);
        unexpected_managers_.clear();
    }

    /// The protocol configuration customizer of the node at @p address
    std::function<void(LoRaMeshProtocolConfig&)> MakeCustomizer(
        AddressType address) {
        return [this, address](LoRaMeshProtocolConfig& config) {
            config.setTargetDutyCycle(spec_.target_duty_cycle);
            auto it = stores_.find(address);
            if (it != stores_.end()) {
                config.setStateStore(it->second);
            }
        };
    }

    static bool IsLinked(const NetworkSpec& spec, size_t i, size_t j) {
        switch (spec.topology) {
            case Topology::kLine:
                return j == i + 1;
            case Topology::kStar:
                return i == 0;
            case Topology::kFullMesh:
                return true;
        }
        return false;
    }

    // ------------------------------------------------------------------
    // Reboots
    // ------------------------------------------------------------------

    /**
     * @brief Save a node's state into its store, as before a planned reset
     */
    Result SaveNodeState(TestNode& node) {
        BindTestThread(node);
        Result result = node.protocol->SaveState();
        GetRTOS().SetCurrentTaskNodeAddress("0xFFFF");
        return result;
    }

    /**
     * @brief Power-cycle the given nodes
     *
     * Every node optionally saves its state, then all go down together, stay
     * off for @p downtime_ms and boot in the given order, @p boot_stagger_ms
     * apart.
     */
    void RebootNodes(const std::vector<TestNode*>& nodes, uint32_t downtime_ms,
                     bool save_state, uint32_t boot_stagger_ms = 0) {
        if (save_state) {
            for (auto* node : nodes) {
                ASSERT_TRUE(SaveNodeState(*node)) << node->name;
            }
        }
        for (auto* node : nodes) {
            ShutdownNode(*node);
        }
        AdvanceTime(downtime_ms);
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (i > 0 && boot_stagger_ms > 0) {
                AdvanceTime(boot_stagger_ms);
            }
            ASSERT_TRUE(BootNode(*nodes[i])) << nodes[i]->name;
        }
    }

    void RebootNode(TestNode& node, uint32_t downtime_ms, bool save_state) {
        RebootNodes({&node}, downtime_ms, save_state);
    }

    // ------------------------------------------------------------------
    // Network health
    // ------------------------------------------------------------------

    static uint16_t NetworkIdOf(TestNode& node) {
        return node.protocol->GetNetworkServiceForTest()->GetNetworkId();
    }

    static uint8_t ControlSlotOf(TestNode& node) {
        return node.protocol->GetNetworkServiceForTest()
            ->GetMyControlSlotIndex();
    }

    static std::map<AddressType, uint8_t> ControlSlotsOf(
        const std::vector<TestNode*>& nodes) {
        std::map<AddressType, uint8_t> slots;
        for (auto* node : nodes) {
            slots[node->address] = ControlSlotOf(*node);
        }
        return slots;
    }

    /**
     * @brief Whether the running @p nodes form one healthy network
     *
     * Healthy means: @p manager (any node with kAnyManager) is the only
     * network manager and every other
     * node is in normal operation; all nodes agree on the manager and on one
     * non-zero network id (@p network_id when non-zero); every member received
     * the manager's sync beacon of the previous superframe; control slots are
     * unique and inside the manager's control band; every node has an active
     * route to every other node.
     */
    bool IsHealthy(const std::vector<TestNode*>& nodes, AddressType manager,
                   uint16_t network_id = 0) {
        last_unhealthy_reason_ = FindHealthProblem(nodes, manager, network_id);
        return last_unhealthy_reason_.empty();
    }

    /**
     * @brief First reason the network is not healthy (empty when healthy)
     */
    std::string FindHealthProblem(const std::vector<TestNode*>& nodes,
                                  AddressType manager, uint16_t network_id) {
        TestNode* manager_node = nullptr;
        int manager_count = 0;
        for (auto* node : nodes) {
            if (!node->protocol) {
                return node->name + " is powered off";
            }
            const bool is_manager = manager == kAnyManager
                                        ? node->protocol->GetState() ==
                                              ProtocolState::NETWORK_MANAGER
                                        : node->address == manager;
            if (is_manager) {
                manager_node = node;
                ++manager_count;
            }
        }
        if (manager_node == nullptr) {
            return "no manager in the network";
        }
        if (manager_count > 1) {
            return std::to_string(manager_count) + " network managers";
        }
        manager = manager_node->address;
        healthy_manager_ = manager_node;

        const uint16_t expected_id =
            network_id != 0 ? network_id : NetworkIdOf(*manager_node);
        if (expected_id == 0) {
            return "manager has no network id";
        }
        const uint8_t band = manager_node->protocol->GetNetworkServiceForTest()
                                 ->GetAllocatedControlSlots();
        std::set<uint8_t> used_slots;
        for (auto* node : nodes) {
            const ProtocolState expected_state =
                node == manager_node ? ProtocolState::NETWORK_MANAGER
                                     : ProtocolState::NORMAL_OPERATION;
            if (node->protocol->GetState() != expected_state) {
                return node->name + " is in state " +
                       std::to_string(
                           static_cast<int>(node->protocol->GetState()));
            }
            if (node->protocol->GetNetworkManager() != manager) {
                return node->name + " follows another manager";
            }
            if (NetworkIdOf(*node) != expected_id) {
                return node->name + " is in another network";
            }
            // Members must follow the manager's current superframe: the
            // counter is 1 from each superframe start until its beacon arrives
            const uint8_t missed = node->protocol->GetNetworkServiceForTest()
                                       ->GetMissedSyncBeaconCount();
            if (node != manager_node && missed > 1) {
                return node->name + " missed " + std::to_string(missed) +
                       " sync beacons";
            }
            const uint8_t slot = ControlSlotOf(*node);
            if (slot >= band || !used_slots.insert(slot).second) {
                return node->name + " has control slot " +
                       std::to_string(slot) + " (band " + std::to_string(band) +
                       ")";
            }
            for (auto* other : nodes) {
                if (other != node && !HasRouteTo(*node, other->address)) {
                    return node->name + " has no active route to " +
                           other->name;
                }
            }
        }
        return "";
    }

    /**
     * @brief Advance time until IsHealthy() holds for kSettleSuperframes
     *
     * While waiting, any node other than @p manager that becomes network
     * manager is recorded in unexpected_managers_.
     */
    bool WaitForHealthyNetwork(const std::vector<TestNode*>& nodes,
                               AddressType manager, uint32_t budget_ms,
                               uint16_t network_id = 0) {
        // Converged means healthy for kSettleSuperframes in a row: a joining
        // node can change the network depth and with it the slot layout,
        // which costs the deepest nodes a beacon or two
        uint32_t healthy_since = 0;
        bool healthy = false;
        return AdvanceTime(budget_ms, budget_ms, kStepMs, 0, [&]() {
            RecordUnexpectedManagers(nodes, manager);
            const uint32_t now = GetRTOS().getTickCount();
            if (!IsHealthy(nodes, manager, network_id)) {
                healthy = false;
                return false;
            }
            if (!healthy) {
                healthy = true;
                healthy_since = now;
            }
            return now - healthy_since >= SettleTimeMs(nodes, manager);
        });
    }

    /// Time a network must stay healthy to count as converged
    uint32_t SettleTimeMs(const std::vector<TestNode*>& nodes,
                          AddressType manager) {
        uint32_t superframe_ms = superframe_ms_;
        for (auto* node : nodes) {
            if (node->address == manager && node->protocol) {
                superframe_ms = node->protocol->GetSuperframeDuration();
            }
        }
        return superframe_ms * kSettleSuperframes;
    }

    /**
     * @brief Require IsHealthy() to hold at every step for @p duration_ms
     *
     * @return true if the network stayed healthy the whole time
     */
    bool StaysHealthy(const std::vector<TestNode*>& nodes, AddressType manager,
                      uint32_t duration_ms, uint16_t network_id = 0) {
        uint32_t elapsed = 0;
        while (elapsed < duration_ms) {
            AdvanceTime(kStepMs);
            elapsed += kStepMs;
            RecordUnexpectedManagers(nodes, manager);
            if (!IsHealthy(nodes, manager, network_id)) {
                return false;
            }
        }
        return true;
    }

    void RecordUnexpectedManagers(const std::vector<TestNode*>& nodes,
                                  AddressType manager) {
        if (manager == kAnyManager) {
            return;
        }
        for (auto* node : nodes) {
            if (node->protocol && node->address != manager &&
                node->protocol->GetState() == ProtocolState::NETWORK_MANAGER) {
                unexpected_managers_.insert(node->address);
            }
        }
    }

    /**
     * @brief Send @p count unicast messages and count how many arrive once
     *
     * @return Number of distinct messages delivered to @p to
     */
    size_t ExpectDataFlows(TestNode& from, TestNode& to, uint8_t count) {
        const uint8_t tag = next_data_tag_++;
        const auto delivered = [&]() {
            std::set<uint8_t> seen;
            size_t total = 0;
            for (const auto& msg : to.received_messages) {
                const auto payload = msg.GetPayload();
                if (msg.GetSource() == from.address && payload.size() == 3 &&
                    payload[0] == kDataMarker && payload[1] == tag) {
                    seen.insert(payload[2]);
                    ++total;
                }
            }
            EXPECT_EQ(total, seen.size())
                << "Duplicate delivery " << from.name << " -> " << to.name;
            return seen.size();
        };

        for (uint8_t i = 0; i < count; ++i) {
            EXPECT_TRUE(SendMessage(from, to, {kDataMarker, tag, i}))
                << from.name << " -> " << to.name;
            // One message per superframe stays within a node's data slots
            AdvanceTime(superframe_ms_);
        }
        AdvanceTime(DataBudgetMs(), DataBudgetMs(), kStepMs, 0,
                    [&]() { return delivered() == count; });
        return delivered();
    }

    // ------------------------------------------------------------------
    // Budgets
    // ------------------------------------------------------------------

    uint32_t FormationBudgetMs(const std::vector<TestNode*>& nodes) {
        const uint32_t discovery_ms = discovery_timeout_ms_ != 0
                                          ? discovery_timeout_ms_
                                          : GetDiscoveryTimeout(*nodes.front());
        return discovery_ms * static_cast<uint32_t>(nodes.size() + 4);
    }

    /// Time a network may take to become healthy again after reboots
    uint32_t RecoveryBudgetMs(const std::vector<TestNode*>& nodes) {
        return std::max<uint32_t>(FormationBudgetMs(nodes),
                                  superframe_ms_ * 60);
    }

    uint32_t DataBudgetMs() const { return superframe_ms_ * 20; }

    // ------------------------------------------------------------------
    // Diagnostics
    // ------------------------------------------------------------------

    std::string DescribeNetwork(const std::vector<TestNode*>& nodes) {
        std::ostringstream out;
        out << "Last health problem: " << last_unhealthy_reason_
            << "\nNetwork state:";
        for (auto* node : nodes) {
            out << "\n  " << node->name << " 0x" << std::hex << node->address;
            if (!node->protocol) {
                out << " powered off" << std::dec;
                continue;
            }
            out << " state=" << std::dec
                << static_cast<int>(node->protocol->GetState()) << " nm=0x"
                << std::hex << node->protocol->GetNetworkManager() << " id=0x"
                << NetworkIdOf(*node) << std::dec
                << " ctrl=" << static_cast<int>(ControlSlotOf(*node))
                << " band="
                << static_cast<int>(node->protocol->GetNetworkServiceForTest()
                                        ->GetAllocatedControlSlots())
                << " routes=" << node->protocol->GetNetworkNodes().size();
        }
        return out.str();
    }

    static constexpr uint32_t kStepMs = 15;
    static constexpr uint32_t kSettleSuperframes = 4;

    NetworkSpec spec_;
    std::map<AddressType, std::shared_ptr<storage::MemoryStateStore>> stores_;
    uint32_t superframe_ms_ = 0;
    uint32_t discovery_timeout_ms_ = 0;
    uint16_t formed_network_id_ = 0;
    std::map<AddressType, uint8_t> formed_control_slots_;
    std::set<AddressType> unexpected_managers_;
    /// Manager of the network the last successful IsHealthy() call checked
    TestNode* healthy_manager_ = nullptr;
    /// Why the last IsHealthy() call failed (empty when it passed)
    std::string last_unhealthy_reason_;
    uint8_t next_data_tag_ = 1;

   private:
    static void BindTestThread(const TestNode& node) {
        char addr_str[8];
        snprintf(addr_str, sizeof(addr_str), "0x%04X", node.address);
        GetRTOS().SetCurrentTaskNodeAddress(addr_str);
    }
};

}  // namespace test
}  // namespace loramesher
