/**
 * @file lora_mesh_test_fixture.hpp
 * @brief Complete test fixture for LoRaMesh protocol using real HardwareManager
 */
#pragma once

#include <gtest/gtest.h>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include <thread>
#include "../test/utils/network_testing_impl.hpp"
#include "hardware/hardware_manager.hpp"
#include "hardware/radiolib/radiolib_radio.hpp"
#include "mocks/mock_radio_test_helpers.hpp"
#include "os/os_port.hpp"
#include "protocols/lora_mesh_protocol.hpp"
#include "types/configurations/protocol_configuration.hpp"
#include "types/radio/radio_state.hpp"
#include "utils/file_log_handler.hpp"
#include "utils/logger.hpp"

namespace loramesher {
namespace test {

/**
 * @brief Test fixture for LoRaMesh protocol tests
 *
 * This fixture sets up a test environment with simulated radio communication
 * between LoRaMesh protocol instances, using the real HardwareManager but
 * with mocked radios and a virtual network.
 */
class LoRaMeshTestFixture : public ::testing::Test {
   protected:
    struct TestNode {
        std::string name;
        AddressType address;
        PinConfig pin_config;
        RadioConfig radio_config;
        std::shared_ptr<hardware::HardwareManager> hardware_manager;
        std::unique_ptr<protocols::LoRaMeshProtocol> protocol;
        std::vector<BaseMessage> received_messages;
        radio::test::MockRadio* mock_radio;
    };

    VirtualNetwork virtual_network_;
    VirtualTimeController time_controller_;
    std::vector<std::shared_ptr<TestNode>> nodes_;
    std::map<AddressType, std::vector<BaseMessage>> message_log_;
    std::vector<std::unique_ptr<RadioToNetworkAdapter>> network_adapters_;

    // File logging support
    std::unique_ptr<FileLogHandler> file_log_handler_;
    std::unique_ptr<LogHandler> original_log_handler_;
    std::string log_directory_;

    LoRaMeshTestFixture()
        : time_controller_(virtual_network_), log_directory_("test_logs") {}

    void SetUp() override {
        // Set up file logging for this test
        SetupFileLogging();
    }

    void TearDown() override {
        // Clean up network adapters FIRST to prevent race conditions
        network_adapters_.clear();

        // Stop and clean up all nodes
        for (auto& node : nodes_) {
            if (node->protocol) {
                // Stop the protocol before destroying it to avoid race conditions
                node->protocol->Stop();
                node->protocol.reset();
            }
            node->hardware_manager.reset();
        }
        nodes_.clear();
        message_log_.clear();

        // Clean up file logging
        CleanupFileLogging();
    }

    /**
     * @brief Create a test node with the given configuration
     *
     * @param name Node name for debugging
     * @param address Node address
     * @param pin_config Pin configuration (default: unique pins based on index)
     * @param radio_config Radio configuration (default: mock radio)
     * @return TestNode& Reference to the created node
     */
    TestNode& CreateNode(const std::string& name, AddressType address,
                         const PinConfig& pin_config = PinConfig(),
                         const RadioConfig& radio_config = RadioConfig()) {
        // Create a node with unique address and pin configuration
        auto node = std::make_shared<TestNode>();
        node->name = name;
        node->address = address;

        // Use provided pin config or create a unique one
        if (pin_config.getNss() == 0) {
            // Create a unique pin configuration
            PinConfig unique_pins;
            int index = nodes_.size();
            unique_pins.setNss(10 + index * 10);    // 10, 20, 30, ...
            unique_pins.setDio0(11 + index * 10);   // 11, 21, 31, ...
            unique_pins.setReset(12 + index * 10);  // 12, 22, 32, ...
            unique_pins.setDio1(13 + index * 10);   // 13, 23, 33, ...
            node->pin_config = unique_pins;
        } else {
            node->pin_config = pin_config;
        }

        // Use provided radio config or create one with MockRadio type
        // TODO: This is needed?
        // if (radio_config.getRadioType() == RadioType::kUnknown) {
        //     RadioConfig mock_config;
        //     mock_config.setRadioType(RadioType::kMockRadio);
        //     mock_config.setFrequency(868.0f);
        //     mock_config.setSpreadingFactor(7);
        //     mock_config.setBandwidth(125.0f);
        //     mock_config.setCodingRate(5);  // 4/5
        //     mock_config.setPower(17);      // 17 dBm
        //     mock_config.setSyncWord(0x12);
        //     mock_config.setCRC(true);
        //     mock_config.setPreambleLength(8);
        //     node->radio_config = mock_config;
        // } else {
        node->radio_config = radio_config;
        // }

        // Create and initialize the hardware manager with our pin and radio config
        node->hardware_manager = std::make_shared<hardware::HardwareManager>(
            node->pin_config, node->radio_config);

        Result result = node->hardware_manager->Initialize();
        if (!result) {
            std::cerr << "Failed to initialize hardware manager for " << name
                      << ": " << result.GetErrorMessage() << std::endl;
            // Create an empty node as failure
            auto empty_node = std::make_shared<TestNode>();
            nodes_.push_back(empty_node);
            return *empty_node;
        }

        // Get the mock radio and connect it to our virtual network
        auto* radio_ptr = dynamic_cast<radio::RadioLibRadio*>(
            node->hardware_manager->getRadio());
        if (!radio_ptr) {
            std::cerr << "Failed to get RadioLibRadio instance for " << name
                      << std::endl;
            auto empty_node = std::make_shared<TestNode>();
            nodes_.push_back(empty_node);
            return *empty_node;
        }

        // Get the mock radio from RadioLibRadio
        node->mock_radio = &radio::GetRadioLibMockForTesting(*radio_ptr);

        // Connect the mock radio to our virtual network
        ConnectRadioToNetwork(node->mock_radio, node->address);

        // Create the protocol instance
        node->protocol = std::make_unique<protocols::LoRaMeshProtocol>();

        // Initialize the protocol with our hardware manager
        result = node->protocol->Init(node->hardware_manager, address);
        if (!result) {
            std::cerr << "Failed to initialize protocol for " << name << ": "
                      << result.GetErrorMessage() << std::endl;
            // Return an empty node as failure
            auto empty_node = std::make_shared<TestNode>();
            nodes_.push_back(empty_node);
            return *empty_node;
        }

        // Configure the protocol with default configuration
        LoRaMeshProtocolConfig config(address);
        result = node->protocol->Configure(config);
        if (!result) {
            std::cerr << "Failed to configure protocol for " << name << ": "
                      << result.GetErrorMessage() << std::endl;
        }

        // Set up message reception tracking
        SetupMessageTracking(*node);

        // Add the node to our collection and return a reference to it
        nodes_.push_back(node);
        return *node;
    }

    /**
     * @brief Connect a mock radio to the virtual network
     *
     * @param mock_radio Mock radio to connect
     * @param address Node address to use
     */
    void ConnectRadioToNetwork(radio::test::MockRadio* mock_radio,
                               AddressType address) {
        // Create and track the adapter for proper cleanup
        auto adapter = std::make_unique<RadioToNetworkAdapter>(
            mock_radio, virtual_network_, address);

        // Register the node with the virtual network
        virtual_network_.RegisterNode(address, adapter.get());

        // Store the adapter for cleanup
        network_adapters_.push_back(std::move(adapter));
    }

    /**
     * @brief Start a node
     *
     * @param node Node to start
     * @return Result Success if started successfully, error details otherwise
     */
    Result StartNode(TestNode& node) {
        if (!node.protocol) {
            return Result(LoraMesherErrorCode::kInvalidState,
                          "Protocol not initialized");
        }
        return node.protocol->Start();
    }

    /**
     * @brief Stop a node
     *
     * @param node Node to stop
     * @return Result Success if stopped successfully, error details otherwise
     */
    Result StopNode(TestNode& node) {
        if (!node.protocol) {
            return Result(LoraMesherErrorCode::kInvalidState,
                          "Protocol not initialized");
        }
        return node.protocol->Stop();
    }

    /**
     * @brief Waits for a condition to be met while advancing simulated time
     *
     * This function periodically advances the simulation time and checks if a
     * specified condition is met. It continues checking until either the condition
     * is satisfied or a timeout occurs.
     *
     * @param time_ms Total time to advance in milliseconds
     * @param timeout_ms Maximum time to wait for the condition in milliseconds (0 = no timeout)
     * @param check_interval_ms Interval between condition checks in milliseconds (constant)
     * @param real_sleep_ms Real sleep time between iterations in milliseconds
     * @param condition Function returning true when the wait condition is met (nullptr = no condition)
     * @return bool True if the condition was met, false if timeout occurred
     */
    bool AdvanceTime(uint32_t time_ms, uint32_t timeout_ms = 0,
                     uint32_t check_interval_ms = 10,
                     uint32_t real_sleep_ms = 2,
                     std::function<bool()> condition = nullptr) {

        // If no condition was provided, advance time directly
        if (!condition) {
            time_controller_.AdvanceTime(time_ms);

            if (real_sleep_ms > 0) {
                // Minimal sleep to allow tasks to execute
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(real_sleep_ms));
            }

            return true;
        }

        // Check condition immediately before starting the loop
        if (condition()) {
            return true;
        }

        // Calculate optimal time stepping parameters
        const uint32_t kEffectiveTimeoutMs =
            (timeout_ms > 0) ? timeout_ms : time_ms;
        const uint32_t kOptimalTimeStepMs = CalculateOptimalTimeStep(
            time_ms, kEffectiveTimeoutMs, check_interval_ms);

        uint32_t elapsed_ms = 0;
        uint32_t total_sim_time_advanced = 0;

        // Continue checking until timeout or condition is met
        while (elapsed_ms < kEffectiveTimeoutMs) {
            // Calculate time to advance in this iteration
            uint32_t time_to_advance = kOptimalTimeStepMs;

            // Advance simulation time
            time_controller_.AdvanceTime(time_to_advance);
            elapsed_ms += time_to_advance;
            total_sim_time_advanced += time_to_advance;

            // Minimal real sleep to allow tasks to execute
            if (real_sleep_ms > 0) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(real_sleep_ms));
            }

            // Check if condition is met
            if (condition()) {
                return true;
            }
        }

        return false;  // Timeout occurred
    }

    /**
     * @brief Calculate optimal time step for AdvanceTime function
     *
     * @param total_time_ms Total time to advance
     * @param timeout_ms Timeout for condition checking
     * @param check_interval_ms Desired check interval
     * @return Optimal time step in milliseconds
     */
    uint32_t CalculateOptimalTimeStep(uint32_t total_time_ms,
                                      uint32_t timeout_ms,
                                      uint32_t check_interval_ms) {
        // Always use check_interval_ms as the base, but optimize for performance
        uint32_t base_step = std::max(check_interval_ms, 1u);

        // For timeout mode, we need to balance responsiveness with performance
        if (timeout_ms > 0) {
            // Use larger steps for better performance, but not too large
            // that we miss the condition check window
            uint32_t efficient_step = std::min(base_step * 2, 50u);
            return std::min(efficient_step,
                            timeout_ms / 10);  // At least 10 checks
        }

        // For non-timeout mode, use check_interval_ms directly
        return base_step;
    }

    /**
     * @brief Get the discovery timeout period used by the protocol
     *
     * @return Discovery timeout in milliseconds
     */
    uint32_t GetDiscoveryTimeout(TestNode& node) {
        return node.protocol->GetDiscoveryTimeout();
    }

    /**
     * @brief Get the superframe duration
     * 
     * @return Superframe duration in milliseconds
     */
    uint32_t GetSuperframeDuration(TestNode& node) {
        auto slot_duration = GetSlotDuration(node);
        auto slot_table = node.protocol->GetSlotTable();
        return slot_table.size() * slot_duration;
    }

    /**
     * @brief Get the slot duration used by the protocol
     *
     * @return Slot duration in milliseconds
     */
    uint32_t GetSlotDuration(TestNode& node) {
        // Return the Slot duration
        return node.protocol->GetSlotDuration();
    }

    /**
     * @brief Set link status between two nodes
     *
     * @param node1 First node
     * @param node2 Second node
     * @param active Whether the link should be active
     */
    void SetLinkStatus(const TestNode& node1, const TestNode& node2,
                       bool active) {
        virtual_network_.SetLinkStatus(node1.address, node2.address, active);
    }

    /**
     * @brief Set message delay between two nodes
     *
     * @param node1 First node
     * @param node2 Second node
     * @param delay_ms Delay in milliseconds
     */
    void SetMessageDelay(const TestNode& node1, const TestNode& node2,
                         uint32_t delay_ms) {
        virtual_network_.SetMessageDelay(node1.address, node2.address,
                                         delay_ms);
    }

    /**
     * @brief Set packet loss rate for the entire network
     *
     * @param rate Loss rate (0.0 = no loss, 1.0 = all packets lost)
     */
    void SetPacketLossRate(float rate) {
        virtual_network_.SetPacketLossRate(rate);
    }

    /**
     * @brief Set up message tracking for a node
     *
     * This sets up a callback to track messages received by the node.
     *
     * @param node Node to set up message tracking for
     */
    void SetupMessageTracking(TestNode& node) {
        // Set up message callback to track received messages
        node.protocol->SetMessageReceivedCallback(
            [this, address = node.address,
             name = node.name](const BaseMessage& message) {
                // Find the node in our collection
                for (auto& node_ptr : nodes_) {
                    if (node_ptr->address == address) {
                        // Store the message in the node's received messages
                        node_ptr->received_messages.push_back(message);
                        break;
                    }
                }

                // Also store in the global message log
                message_log_[address].push_back(message);

                // Log for debugging
                std::cout << name << " received message from " << std::hex
                          << message.GetSource() << " to "
                          << message.GetDestination()
                          << " type: " << static_cast<int>(message.GetType())
                          << std::dec << std::endl;
            });
    }

    /**
     * @brief Send a message from one node to another
     *
     * @param from Source node
     * @param to Destination node
     * @param type Message type
     * @param payload Message payload
     * @return Result Success if message was sent successfully, error details otherwise
     */
    Result SendMessage(TestNode& from, TestNode& to, MessageType type,
                       const std::vector<uint8_t>& payload) {
        auto message_opt =
            BaseMessage::Create(to.address, from.address, type, payload);
        if (!message_opt.has_value()) {
            return Result(LoraMesherErrorCode::kSerializationError,
                          "Failed to create message");
        }

        return from.protocol->SendMessage(message_opt.value());
    }

    /**
     * @brief Check if a node has received a message from another node
     *
     * @param node Node to check
     * @param from Source node address
     * @param type Message type (default: any type)
     * @return bool True if message was received, false otherwise
     */
    bool HasReceivedMessageFrom(const TestNode& node, AddressType from,
                                MessageType type = MessageType::ANY) {
        for (const auto& msg : node.received_messages) {
            if (msg.GetSource() == from &&
                (type == MessageType::ANY || msg.GetType() == type)) {
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Get messages received by a node
     *
     * @param node Node to get messages for
     * @param from Source node address (default: any source)
     * @param type Message type (default: any type)
     * @return std::vector<BaseMessage> Matching messages
     */
    std::vector<BaseMessage> GetReceivedMessages(
        const TestNode& node, AddressType from = 0,
        MessageType type = MessageType::ANY) {
        std::vector<BaseMessage> result;

        for (const auto& msg : node.received_messages) {
            if ((from == 0 || msg.GetSource() == from) &&
                (type == MessageType::ANY || msg.GetType() == type)) {
                result.push_back(msg);
            }
        }

        return result;
    }

    /**
     * @brief Generate a fully connected mesh topology
     *
     * @param num_nodes Number of nodes to create
     * @param base_address Base address for nodes (default: 0x1000)
     * @param name_prefix Prefix for node names (default: "Node")
     * @return std::vector<TestNode*> Pointers to the created nodes
     */
    std::vector<TestNode*> GenerateFullMeshTopology(
        int num_nodes, AddressType base_address = 0x1000,
        const std::string& name_prefix = "Node") {
        std::vector<TestNode*> result;

        // Create nodes with unique addresses and pin configurations
        for (int i = 0; i < num_nodes; i++) {
            std::string name = name_prefix + std::to_string(i + 1);
            AddressType address = base_address + i;

            // Create the node with default unique pin configuration
            TestNode& node_ref = CreateNode(name, address);

            // Find the pointer to the created node
            for (auto& node_ptr : nodes_) {
                if (node_ptr->address == address) {
                    result.push_back(node_ptr.get());
                    break;
                }
            }
        }

        // Connect all nodes to each other (fully connected mesh)
        for (size_t i = 0; i < result.size(); i++) {
            for (size_t j = i + 1; j < result.size(); j++) {
                SetLinkStatus(*result[i], *result[j], true);
            }
        }

        return result;
    }

    /**
     * @brief Generate a line topology where each node only connects to its neighbors
     *
     * @param num_nodes Number of nodes to create
     * @param base_address Base address for nodes (default: 0x1000)
     * @param name_prefix Prefix for node names (default: "Node")
     * @return std::vector<TestNode*> Pointers to the created nodes
     */
    std::vector<TestNode*> GenerateLineTopology(
        int num_nodes, AddressType base_address = 0x1000,
        const std::string& name_prefix = "Node") {
        std::vector<TestNode*> result;

        // Create nodes with unique addresses and pin configurations
        for (int i = 0; i < num_nodes; i++) {
            std::string name = name_prefix + std::to_string(i + 1);
            AddressType address = base_address + i;

            // Create the node with default unique pin configuration
            TestNode& node_ref = CreateNode(name, address);

            // Find the pointer to the created node
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

        // Connect nodes in a line (each node connected only to neighbors)
        for (size_t i = 0; i < result.size() - 1; i++) {
            SetLinkStatus(*result[i], *result[i + 1], true);
        }

        return result;
    }

    /**
     * @brief Generate a star topology with one central node connected to all others
     *
     * @param num_nodes Total number of nodes including the central node
     * @param central_node_index Index of the central node (default: 0)
     * @param base_address Base address for nodes (default: 0x1000)
     * @param name_prefix Prefix for node names (default: "Node")
     * @return std::vector<TestNode*> Pointers to the created nodes
     */
    std::vector<TestNode*> GenerateStarTopology(
        int num_nodes, int central_node_index = 0,
        AddressType base_address = 0x1000,
        const std::string& name_prefix = "Node") {
        std::vector<TestNode*> result;

        // Create nodes with unique addresses and pin configurations
        for (int i = 0; i < num_nodes; i++) {
            std::string name = name_prefix + std::to_string(i + 1);
            AddressType address = base_address + i;

            // Create the node with default unique pin configuration
            TestNode& node_ref = CreateNode(name, address);

            // Find the pointer to the created node
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

        // Connect central node to all others
        for (size_t i = 0; i < result.size(); i++) {
            if (i != central_node_index) {
                SetLinkStatus(*result[central_node_index], *result[i], true);
            }
        }

        return result;
    }

    /**
     * @brief Create a partitioned network with two separated groups
     *
     * @param group1_size Number of nodes in first group
     * @param group2_size Number of nodes in second group
     * @param group1_base_address Base address for group 1 (default: 0x1000)
     * @param group2_base_address Base address for group 2 (default: 0x2000)
     * @return std::pair<std::vector<TestNode*>, std::vector<TestNode*>> Pointers to nodes in each group
     */
    std::pair<std::vector<TestNode*>, std::vector<TestNode*>>
    CreatePartitionedNetwork(int group1_size, int group2_size,
                             AddressType group1_base_address = 0x1000,
                             AddressType group2_base_address = 0x2000) {
        std::vector<TestNode*> group1;
        std::vector<TestNode*> group2;

        // Create first group
        for (int i = 0; i < group1_size; i++) {
            std::string name = "Group1_Node" + std::to_string(i + 1);
            AddressType address = group1_base_address + i;

            // Create the node with default unique pin configuration
            TestNode& node_ref = CreateNode(name, address);

            // Find the pointer to the created node
            for (auto& node_ptr : nodes_) {
                if (node_ptr->address == address) {
                    group1.push_back(node_ptr.get());
                    break;
                }
            }
        }

        // Create second group
        for (int i = 0; i < group2_size; i++) {
            std::string name = "Group2_Node" + std::to_string(i + 1);
            AddressType address = group2_base_address + i;

            // Create the node with default unique pin configuration
            TestNode& node_ref = CreateNode(name, address);

            // Find the pointer to the created node
            for (auto& node_ptr : nodes_) {
                if (node_ptr->address == address) {
                    group2.push_back(node_ptr.get());
                    break;
                }
            }
        }

        // Connect nodes within each group
        for (size_t i = 0; i < group1.size(); i++) {
            for (size_t j = i + 1; j < group1.size(); j++) {
                SetLinkStatus(*group1[i], *group1[j], true);
            }
        }

        for (size_t i = 0; i < group2.size(); i++) {
            for (size_t j = i + 1; j < group2.size(); j++) {
                SetLinkStatus(*group2[i], *group2[j], true);
            }
        }

        // Ensure no connections between groups
        for (auto* node1 : group1) {
            for (auto* node2 : group2) {
                SetLinkStatus(*node1, *node2, false);
            }
        }

        return {group1, group2};
    }

    /**
     * @brief Create a bridge connection between two partitioned groups
     *
     * @param group1 First group of nodes
     * @param group2 Second group of nodes
     * @param bridge_node1_index Index of node in first group to connect
     * @param bridge_node2_index Index of node in second group to connect
     */
    void CreateBridgeBetweenGroups(const std::vector<TestNode*>& group1,
                                   const std::vector<TestNode*>& group2,
                                   size_t bridge_node1_index = 0,
                                   size_t bridge_node2_index = 0) {
        if (bridge_node1_index >= group1.size() ||
            bridge_node2_index >= group2.size()) {
            std::cerr << "Invalid bridge node indices" << std::endl;
            return;
        }

        // Create a link between the bridge nodes
        SetLinkStatus(*group1[bridge_node1_index], *group2[bridge_node2_index],
                      true);
    }

    /**
     * @brief Find the network manager node in a collection of nodes
     *
     * @param nodes Collection of nodes to search
     * @return TestNode* Pointer to the network manager node, or nullptr if none found
     */
    TestNode* FindNetworkManager(const std::vector<TestNode*>& nodes) {
        for (auto* node : nodes) {
            if (node->protocol->GetState() ==
                protocols::lora_mesh::INetworkService::ProtocolState::
                    NETWORK_MANAGER) {
                return node;
            }
        }
        return nullptr;
    }

    /**
     * @brief Wait for network formation to complete
     *
     * This advances time until the network has formed with the expected
     * number of nodes in NORMAL_OPERATION state.
     *
     * @param nodes Nodes to check
     * @param expected_normal_nodes Number of nodes expected to be in NORMAL_OPERATION state
     * @param timeout_ms Maximum time to wait (default: 2x discovery timeout)
     * @param check_interval_ms Interval between checks (default: 100ms)
     * @return bool True if network formed as expected, false otherwise
     */
    bool WaitForNetworkFormation(const std::vector<TestNode*>& nodes,
                                 int expected_normal_nodes,
                                 uint32_t timeout_ms = 0,
                                 uint32_t check_interval_ms = 100) {
        if (timeout_ms == 0) {
            auto* first_node = nodes.front();
            timeout_ms = GetDiscoveryTimeout(*first_node) * 2;
            LOG_DEBUG("Using default timeout of %u ms for network formation",
                      timeout_ms);
        }

        uint32_t elapsed = 0;

        while (elapsed < timeout_ms) {
            // Count nodes in each state
            int network_manager_count = 0;
            int normal_operation_count = 0;

            for (auto* node : nodes) {
                auto state = node->protocol->GetState();
                if (state == protocols::lora_mesh::INetworkService::
                                 ProtocolState::NETWORK_MANAGER) {
                    network_manager_count++;
                } else if (state == protocols::lora_mesh::INetworkService::
                                        ProtocolState::NORMAL_OPERATION) {
                    normal_operation_count++;
                }
            }

            // Check if network has formed as expected
            if (network_manager_count == 1 &&
                normal_operation_count == expected_normal_nodes) {
                return true;
            }

            // Advance time and try again
            AdvanceTime(check_interval_ms);
            elapsed += check_interval_ms;
        }

        return false;
    }

    /**
     * @brief Simulate node failure by disconnecting it from the network
     *
     * @param node Node to disconnect
     */
    void SimulateNodeFailure(TestNode& node) {
        // Disconnect node from all others
        for (auto& other_node : nodes_) {
            if (other_node->address != node.address) {
                SetLinkStatus(node, *(other_node.get()), false);
            }
        }
    }

    /**
     * @brief Simulate node recovery by reconnecting it to the network
     *
     * @param node Node to reconnect
     * @param connect_to_all Whether to connect to all nodes or just neighbors
     */
    void SimulateNodeRecovery(TestNode& node, bool connect_to_all = true) {
        if (connect_to_all) {
            // Connect node to all others
            for (auto& other_node : nodes_) {
                if (other_node->address != node.address) {
                    SetLinkStatus(node, *(other_node.get()), true);
                }
            }
        } else {
            // Find the node index in our nodes vector
            int node_index = -1;
            for (size_t i = 0; i < nodes_.size(); i++) {
                if (nodes_[i]->address == node.address) {
                    node_index = static_cast<int>(i);
                    break;
                }
            }

            if (node_index == -1) {
                return;
            }

            // Connect only to adjacent nodes if they exist
            if (node_index > 0) {
                SetLinkStatus(node, *(nodes_[node_index - 1].get()), true);
            }
            if (node_index < static_cast<int>(nodes_.size()) - 1) {
                SetLinkStatus(node, *(nodes_[node_index + 1].get()), true);
            }
        }
    }

   private:
    /**
     * @brief Set up file logging for the current test
     */
    void SetupFileLogging() {
        // Create log directory if it doesn't exist
        CreateLogDirectory();

        // Get current test info from GoogleTest
        const ::testing::TestInfo* test_info =
            ::testing::UnitTest::GetInstance()->current_test_info();
        std::string test_name = std::string(test_info->test_case_name()) + "_" +
                                std::string(test_info->name());

        // Create unique log filename
        std::string log_filename = log_directory_ + "/" + test_name + ".log";

        try {
            // Create file log handler
            file_log_handler_ =
                std::make_unique<FileLogHandler>(log_filename, false, true);

            // Set the file handler as the active logger
            LOG.SetHandler(std::move(file_log_handler_));

            // Log test start
            LOG_INFO("=== Test Started: %s ===", test_name.c_str());

        } catch (const std::exception& e) {
            // If file logging fails, continue with console logging
            std::cerr << "Warning: Could not set up file logging: " << e.what()
                      << std::endl;
        }
    }

    /**
     * @brief Clean up file logging after test completion
     */
    void CleanupFileLogging() {
        if (file_log_handler_ && file_log_handler_->IsOpen()) {
            // Log test end
            LOG_INFO("=== Test Completed ===");
            LOG_FLUSH();

            // Get the log filename before cleanup
            std::string log_filename = file_log_handler_->GetFilename();

            // Reset to default console handler
            LOG.SetHandler(std::make_unique<ConsoleLogHandler>());

            // Clean up file handler
            file_log_handler_.reset();

            // Print log file location for user
            std::cout << "Test log saved to: " << log_filename << std::endl;
        }
    }

    /**
     * @brief Create log directory if it doesn't exist
     */
    void CreateLogDirectory() {
        // Use system command to create directory (cross-platform)
        std::string command = "mkdir -p " + log_directory_;
        system(command.c_str());
    }

   public:
    /**
     * @brief Set custom log directory
     * @param directory Directory path for log files
     */
    void SetLogDirectory(const std::string& directory) {
        log_directory_ = directory;
    }

    /**
     * @brief Get current log file path
     * @return Path to current log file, or empty string if not logging to file
     */
    std::string GetLogFilePath() const {
        if (file_log_handler_ && file_log_handler_->IsOpen()) {
            return file_log_handler_->GetFilename();
        }
        return "";
    }
};

}  // namespace test
}  // namespace loramesher