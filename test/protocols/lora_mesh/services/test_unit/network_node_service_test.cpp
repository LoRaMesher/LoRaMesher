/**
 * @file network_node_service_test.cpp
 * @brief Unit tests for NetworkNodeService implementation
 * 
 * Location: test/protocols/lora_mesh/services/unit/network_node_service_test.cpp
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <vector>

#include "../test/protocols/lora_mesh/mocks/mock_time_provider.hpp"
#include "protocols/lora_mesh/services/network_node_service.hpp"
#include "types/protocols/lora_mesh/network_node.hpp"

namespace {
using namespace loramesher::types::protocols::lora_mesh;
}

namespace loramesher {
namespace protocols {
namespace lora_mesh {
namespace test {

/**
 * @brief Test fixture for NetworkNodeService tests
 */
class NetworkNodeServiceTest : public ::testing::Test {
   protected:
    void SetUp() override {
        time_provider_mock_ = std::make_shared<MockTimeProvider>();

        // Set up default behavior for all time provider methods
        // Use WillRepeatedly to avoid uninteresting call warnings
        EXPECT_CALL(*time_provider_mock_, GetCurrentTime())
            .WillRepeatedly(::testing::Return(initial_time_));

        EXPECT_CALL(*time_provider_mock_, Sleep(::testing::_))
            .WillRepeatedly(::testing::Return());

        EXPECT_CALL(*time_provider_mock_, GetElapsedTime(::testing::_))
            .WillRepeatedly(::testing::Invoke([this](uint32_t ref) {
                return (current_time_ >= ref) ? (current_time_ - ref) : 0;
            }));

        // Create service with unlimited nodes
        service_ = std::make_unique<NetworkNodeService>(time_provider_mock_, 0);

        // Advance time for each test
        current_time_ = initial_time_;
    }

    void TearDown() override { service_.reset(); }

    // Helper method to advance time
    void AdvanceTime(uint32_t ms) {
        current_time_ += ms;
        // Clear old expectations and set new ones
        ::testing::Mock::VerifyAndClearExpectations(time_provider_mock_.get());

        EXPECT_CALL(*time_provider_mock_, GetCurrentTime())
            .WillRepeatedly(::testing::Return(current_time_));

        EXPECT_CALL(*time_provider_mock_, Sleep(::testing::_))
            .WillRepeatedly(::testing::Return());

        EXPECT_CALL(*time_provider_mock_, GetElapsedTime(::testing::_))
            .WillRepeatedly(::testing::Invoke([this](uint32_t ref) {
                return (current_time_ >= ref) ? (current_time_ - ref) : 0;
            }));
    }

    // Helper method to create test nodes
    void AddTestNode(AddressType address, uint8_t battery = 100,
                     bool is_manager = false, uint8_t capabilities = 0,
                     uint8_t slots = 1) {
        service_->UpdateNetworkNode(address, battery, is_manager, capabilities,
                                    slots);
    }

    std::shared_ptr<MockTimeProvider> time_provider_mock_;
    std::unique_ptr<NetworkNodeService> service_;

    static constexpr uint32_t initial_time_ = 10000;
    uint32_t current_time_;

    // Test node addresses
    static constexpr AddressType NODE_1 = 0x1111;
    static constexpr AddressType NODE_2 = 0x2222;
    static constexpr AddressType NODE_3 = 0x3333;
    static constexpr AddressType MANAGER_NODE = 0xAAAA;

    // Node capability constants
    static constexpr uint8_t ROUTER = 0x01;
    static constexpr uint8_t GATEWAY = 0x02;
    static constexpr uint8_t BATTERY_POWERED = 0x04;
    static constexpr uint8_t SENSOR_NODE = 0x20;
};

/**
     * @brief Test adding a new node to the network
     */
TEST_F(NetworkNodeServiceTest, AddNewNode) {
    // Initially empty network
    EXPECT_EQ(service_->GetNetworkSize(), 0);
    EXPECT_FALSE(service_->IsNodeInNetwork(NODE_1));

    // Add a new node
    bool is_new = service_->UpdateNetworkNode(NODE_1, 75, false);

    // Verify node was added
    EXPECT_TRUE(is_new);
    EXPECT_EQ(service_->GetNetworkSize(), 1);
    EXPECT_TRUE(service_->IsNodeInNetwork(NODE_1));

    // Check node properties
    const NetworkNode* node = service_->GetNode(NODE_1);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->address, NODE_1);
    EXPECT_EQ(node->battery_level, 75);
    EXPECT_FALSE(node->is_network_manager);
    EXPECT_EQ(node->last_seen, current_time_);
}

/**
     * @brief Test updating an existing node
     */
TEST_F(NetworkNodeServiceTest, UpdateExistingNode) {
    // Add initial node
    service_->UpdateNetworkNode(NODE_1, 100, false);
    EXPECT_EQ(service_->GetNetworkSize(), 1);

    // Advance time and update node
    AdvanceTime(5000);
    bool is_updated = service_->UpdateNetworkNode(NODE_1, 80, true, ROUTER, 3);

    // Should not be counted as new
    EXPECT_TRUE(is_updated);
    EXPECT_EQ(service_->GetNetworkSize(), 1);

    // Check updated properties
    const NetworkNode* node = service_->GetNode(NODE_1);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->battery_level, 80);
    EXPECT_TRUE(node->is_network_manager);
    EXPECT_EQ(node->capabilities, ROUTER);
    EXPECT_EQ(node->allocated_slots, 3);
    EXPECT_EQ(node->last_seen, current_time_);
}

/**
     * @brief Test battery level validation
     */
TEST_F(NetworkNodeServiceTest, BatteryLevelValidation) {
    // Add node with invalid battery level
    service_->UpdateNetworkNode(NODE_1, 150, false);  // > 100%

    const NetworkNode* node = service_->GetNode(NODE_1);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->battery_level, 100);  // Should be clamped to 100

    // Update with valid battery level
    service_->UpdateNetworkNode(NODE_1, 0, false);  // Minimum
    node = service_->GetNode(NODE_1);
    EXPECT_EQ(node->battery_level, 0);
}

/**
     * @brief Test removing inactive nodes
     */
TEST_F(NetworkNodeServiceTest, RemoveInactiveNodes) {
    // Add nodes at different times
    AddTestNode(NODE_1, 100);
    AdvanceTime(5000);
    AddTestNode(NODE_2, 90);
    AdvanceTime(5000);
    AddTestNode(NODE_3, 80);

    EXPECT_EQ(service_->GetNetworkSize(), 3);

    // Remove nodes inactive for more than 8 seconds
    // NODE_1 was added 10s ago, NODE_2 5s ago, NODE_3 0s ago
    AdvanceTime(1000);  // Total: NODE_1=11s, NODE_2=6s, NODE_3=1s

    size_t removed = service_->RemoveInactiveNodes(8000);  // 8 second timeout

    EXPECT_EQ(removed, 1);  // Only NODE_1 should be removed
    EXPECT_EQ(service_->GetNetworkSize(), 2);
    EXPECT_FALSE(service_->IsNodeInNetwork(NODE_1));
    EXPECT_TRUE(service_->IsNodeInNetwork(NODE_2));
    EXPECT_TRUE(service_->IsNodeInNetwork(NODE_3));
}

/**
     * @brief Test network manager operations
     */
TEST_F(NetworkNodeServiceTest, NetworkManagerOperations) {
    // Add regular nodes and a manager
    AddTestNode(NODE_1, 100, false);
    AddTestNode(NODE_2, 90, false);
    AddTestNode(MANAGER_NODE, 100, true, GATEWAY | ROUTER, 5);

    // Get all network managers
    auto managers = service_->GetNetworkManagers();
    EXPECT_EQ(managers.size(), 1);
    EXPECT_EQ(managers[0]->address, MANAGER_NODE);
    EXPECT_TRUE(managers[0]->is_network_manager);

    // Add another manager
    AddTestNode(0xBBBB, 100, true);
    managers = service_->GetNetworkManagers();
    EXPECT_EQ(managers.size(), 2);
}

/**
     * @brief Test node capabilities management
     */
TEST_F(NetworkNodeServiceTest, NodeCapabilities) {
    // Add nodes with different capabilities
    AddTestNode(NODE_1, 100, false, ROUTER);
    AddTestNode(NODE_2, 100, false, GATEWAY | ROUTER);
    AddTestNode(NODE_3, 100, false, SENSOR_NODE | BATTERY_POWERED);

    // Test capability queries
    auto routers = service_->GetNodesWithCapability(ROUTER);
    EXPECT_EQ(routers.size(), 2);  // NODE_1 and NODE_2

    auto gateways = service_->GetNodesWithCapability(GATEWAY);
    EXPECT_EQ(gateways.size(), 1);  // Only NODE_2

    auto sensors = service_->GetNodesWithCapability(SENSOR_NODE);
    EXPECT_EQ(sensors.size(), 1);  // Only NODE_3

    // Update capabilities
    bool updated =
        service_->UpdateNodeCapabilities(NODE_1, GATEWAY | SENSOR_NODE);
    EXPECT_TRUE(updated);

    const NetworkNode* node = service_->GetNode(NODE_1);
    EXPECT_EQ(node->capabilities, GATEWAY | SENSOR_NODE);

    // Verify updated capability queries
    gateways = service_->GetNodesWithCapability(GATEWAY);
    EXPECT_EQ(gateways.size(), 2);  // NODE_1 and NODE_2
}

/**
     * @brief Test node sorting functionality
     */
TEST_F(NetworkNodeServiceTest, NodeSorting) {
    // Add nodes with different properties
    AddTestNode(0x3333, 60, false, 0, 1);  // address=3333, battery=60, slots=1
    AddTestNode(0x1111, 90, false, 0, 3);  // address=1111, battery=90, slots=3
    AddTestNode(0x2222, 80, false, 0, 2);  // address=2222, battery=80, slots=2

    const auto& nodes = service_->GetNetworkNodes();

    // Sort by address
    service_->SortNodes(NetworkNodeService::SortCriteria::ADDRESS);
    EXPECT_EQ(nodes[0].address, 0x1111);
    EXPECT_EQ(nodes[1].address, 0x2222);
    EXPECT_EQ(nodes[2].address, 0x3333);

    // Sort by battery level (descending)
    service_->SortNodes(NetworkNodeService::SortCriteria::BATTERY_LEVEL);
    EXPECT_EQ(nodes[0].battery_level, 90);  // 0x1111
    EXPECT_EQ(nodes[1].battery_level, 80);  // 0x2222
    EXPECT_EQ(nodes[2].battery_level, 60);  // 0x3333

    // Sort by allocated slots (descending)
    service_->SortNodes(NetworkNodeService::SortCriteria::ALLOCATED_SLOTS);
    EXPECT_EQ(nodes[0].allocated_slots, 3);  // 0x1111
    EXPECT_EQ(nodes[1].allocated_slots, 2);  // 0x2222
    EXPECT_EQ(nodes[2].allocated_slots, 1);  // 0x3333
}

/**
     * @brief Test network statistics
     */
TEST_F(NetworkNodeServiceTest, NetworkStatistics) {
    // Add various nodes
    AddTestNode(NODE_1, 100, false, ROUTER, 2);
    AddTestNode(NODE_2, 80, true, GATEWAY, 3);  // Manager
    AddTestNode(NODE_3, 60, false, SENSOR_NODE, 1);

    auto stats = service_->GetNetworkStats();

    EXPECT_EQ(stats.total_nodes, 3);
    EXPECT_EQ(stats.network_managers, 1);
    EXPECT_EQ(stats.active_nodes, 3);           // All recently added
    EXPECT_EQ(stats.avg_battery_level, 80);     // (100+80+60)/3
    EXPECT_EQ(stats.total_allocated_slots, 6);  // 2+3+1
    EXPECT_EQ(stats.oldest_node_age_ms, 0);     // All just added

    // Advance time and check age
    AdvanceTime(30001);
    stats = service_->GetNetworkStats();
    EXPECT_EQ(stats.oldest_node_age_ms, 30001);
    EXPECT_EQ(stats.active_nodes, 0);  // None active within 30s default
}

/**
     * @brief Test node limit enforcement
     */
TEST_F(NetworkNodeServiceTest, NodeLimitEnforcement) {
    // Create service with limited capacity
    auto limited_service =
        std::make_unique<NetworkNodeService>(time_provider_mock_, 3);

    // Add nodes up to limit
    limited_service->UpdateNetworkNode(NODE_1, 100, false);
    limited_service->UpdateNetworkNode(NODE_2, 100, false);
    limited_service->UpdateNetworkNode(NODE_3, 100, false);
    EXPECT_EQ(limited_service->GetNetworkSize(), 3);

    // Add nodes at different times to control which gets removed
    AdvanceTime(1000);
    limited_service->UpdateNetworkNode(NODE_2, 100, false);  // Update NODE_2

    AdvanceTime(1000);
    limited_service->UpdateNetworkNode(NODE_3, 100, false);  // Update NODE_3

    // Try to add one more node (should remove oldest - NODE_1)
    AdvanceTime(1000);
    bool added = limited_service->UpdateNetworkNode(0x4444, 100, false);

    EXPECT_TRUE(added);
    EXPECT_EQ(limited_service->GetNetworkSize(), 3);
    EXPECT_FALSE(
        limited_service->IsNodeInNetwork(NODE_1));  // Oldest, should be removed
    EXPECT_TRUE(limited_service->IsNodeInNetwork(NODE_2));
    EXPECT_TRUE(limited_service->IsNodeInNetwork(NODE_3));
    EXPECT_TRUE(limited_service->IsNodeInNetwork(0x4444));  // New node added
}

/**
     * @brief Test removing specific nodes
     */
TEST_F(NetworkNodeServiceTest, RemoveSpecificNode) {
    // Add test nodes
    AddTestNode(NODE_1, 100, false);
    AddTestNode(NODE_2, 100, true);  // Manager
    AddTestNode(NODE_3, 100, false);
    EXPECT_EQ(service_->GetNetworkSize(), 3);

    // Remove existing node
    bool removed = service_->RemoveNode(NODE_2);
    EXPECT_TRUE(removed);
    EXPECT_EQ(service_->GetNetworkSize(), 2);
    EXPECT_FALSE(service_->IsNodeInNetwork(NODE_2));

    // Try to remove non-existent node
    removed = service_->RemoveNode(0x9999);
    EXPECT_FALSE(removed);
    EXPECT_EQ(service_->GetNetworkSize(), 2);
}

/**
     * @brief Test updating specific node properties
     */
TEST_F(NetworkNodeServiceTest, UpdateSpecificProperties) {
    // Add test node
    AddTestNode(NODE_1, 100, false, ROUTER, 2);

    // Update capabilities
    AdvanceTime(1000);
    bool updated =
        service_->UpdateNodeCapabilities(NODE_1, GATEWAY | SENSOR_NODE);
    EXPECT_TRUE(updated);

    const NetworkNode* node = service_->GetNode(NODE_1);
    EXPECT_EQ(node->capabilities, GATEWAY | SENSOR_NODE);
    EXPECT_EQ(node->last_seen, current_time_);  // Should update timestamp

    // Update allocated slots
    AdvanceTime(1000);
    updated = service_->UpdateNodeAllocatedSlots(NODE_1, 5);
    EXPECT_TRUE(updated);

    node = service_->GetNode(NODE_1);
    EXPECT_EQ(node->allocated_slots, 5);
    EXPECT_EQ(node->last_seen, current_time_);  // Should update timestamp

    // Try to update non-existent node
    updated = service_->UpdateNodeCapabilities(0x9999, ROUTER);
    EXPECT_FALSE(updated);
}

/**
     * @brief Test edge cases and error conditions
     */
TEST_F(NetworkNodeServiceTest, EdgeCases) {
    // Test with zero battery level
    service_->UpdateNetworkNode(NODE_1, 0, false);
    const NetworkNode* node = service_->GetNode(NODE_1);
    EXPECT_EQ(node->battery_level, 0);

    // Test with all capabilities
    service_->UpdateNetworkNode(NODE_2, 100, false, 0xFF, 255);
    node = service_->GetNode(NODE_2);
    EXPECT_EQ(node->capabilities, 0xFF);
    EXPECT_EQ(node->allocated_slots, 255);

    // Test removing from empty network
    auto empty_service =
        std::make_unique<NetworkNodeService>(time_provider_mock_);
    size_t removed = empty_service->RemoveInactiveNodes(1000);
    EXPECT_EQ(removed, 0);

    // Test getting node that doesn't exist
    const NetworkNode* missing = service_->GetNode(0x9999);
    EXPECT_EQ(missing, nullptr);
}

/**
     * @brief Test time provider integration
     */
TEST_F(NetworkNodeServiceTest, TimeProviderIntegration) {
    // Reset expectations since SetUp already sets WillRepeatedly
    ::testing::Mock::VerifyAndClearExpectations(time_provider_mock_.get());

    // Verify time provider is called correctly
    EXPECT_CALL(*time_provider_mock_, GetCurrentTime())
        .Times(::testing::AtLeast(1))
        .WillRepeatedly(::testing::Return(current_time_));

    // Add a node (should call GetCurrentTime)
    service_->UpdateNetworkNode(NODE_1, 100, false);

    // Verify timestamp was set correctly
    const NetworkNode* node = service_->GetNode(NODE_1);
    EXPECT_EQ(node->last_seen, current_time_);

    // Update node and verify time is called again
    AdvanceTime(5000);

    service_->UpdateNetworkNode(NODE_1, 90, false);
    node = service_->GetNode(NODE_1);
    EXPECT_EQ(node->last_seen, current_time_);
}

/**
     * @brief Test concurrent operations simulation
     */
TEST_F(NetworkNodeServiceTest, ConcurrentOperationsSimulation) {
    // Simulate rapid updates to multiple nodes
    for (int i = 0; i < 10; ++i) {
        service_->UpdateNetworkNode(0x1000 + i, 100 - i * 5, i % 2 == 0);
        AdvanceTime(100);
    }

    EXPECT_EQ(service_->GetNetworkSize(), 10);

    // Simulate mixed operations
    for (int i = 0; i < 5; ++i) {
        service_->UpdateNodeCapabilities(0x1000 + i, ROUTER);
        service_->UpdateNodeAllocatedSlots(0x1005 + i, i + 1);
        AdvanceTime(50);
    }

    // Verify state remains consistent
    auto stats = service_->GetNetworkStats();
    EXPECT_EQ(stats.total_nodes, 10);
}

}  // namespace test
}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher