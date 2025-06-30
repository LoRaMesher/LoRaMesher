/**
 * @file network_node_test.cpp
 * @brief Unit tests for NetworkNodeRoute class
 */

#include <gtest/gtest.h>
#include <algorithm>
#include "types/protocols/lora_mesh/network_node_route.hpp"
#include "utils/byte_operations.h"

namespace loramesher {
namespace types {
namespace protocols {
namespace lora_mesh {
namespace test {

/**
 * @brief Test fixture for NetworkNodeRoute tests
 */
class NetworkNodeRouteTest : public ::testing::Test {
   protected:
    void SetUp() override {
        // Create a sample network node for testing
        node_ =
            NetworkNodeRoute(0x1234,  // address
                             75,      // battery_level
                             5000,    // last_seen
                             false,   // is_network_manager
                             0x05,    // capabilities (ROUTER | BATTERY_POWERED)
                             3        // allocated_data_slots
            );
    }

    NetworkNodeRoute node_;

    // Define capability constants for testing
    static constexpr uint8_t ROUTER = 0x01;
    static constexpr uint8_t GATEWAY = 0x02;
    static constexpr uint8_t BATTERY_POWERED = 0x04;
    static constexpr uint8_t HIGH_BANDWIDTH = 0x08;
    static constexpr uint8_t TIME_SYNC_SOURCE = 0x10;
    static constexpr uint8_t SENSOR_NODE = 0x20;
    static constexpr uint8_t RESERVED = 0x40;
    static constexpr uint8_t EXTENDED_CAPS = 0x80;
};

/**
 * @brief Test default constructor
 */
TEST_F(NetworkNodeRouteTest, DefaultConstructor) {
    NetworkNodeRoute default_node;

    // Default values should be zero/false
    EXPECT_EQ(default_node.routing_entry.destination, 0);
    EXPECT_EQ(default_node.battery_level, 100);
    EXPECT_EQ(default_node.last_seen, 0);
    EXPECT_FALSE(default_node.is_network_manager);
    EXPECT_EQ(default_node.capabilities, 0);
    EXPECT_EQ(default_node.routing_entry.allocated_data_slots, 0);
}

/**
 * @brief Test parameterized constructor
 */
TEST_F(NetworkNodeRouteTest, ParameterizedConstructor) {
    EXPECT_EQ(node_.routing_entry.destination, 0x1234);
    EXPECT_EQ(node_.battery_level, 75);
    EXPECT_EQ(node_.last_seen, 5000);
    EXPECT_FALSE(node_.is_network_manager);
    EXPECT_EQ(node_.capabilities, 0x05);  // ROUTER | BATTERY_POWERED
    EXPECT_EQ(node_.routing_entry.allocated_data_slots, 3);
}

/**
 * @brief Test constructor with minimal parameters
 */
TEST_F(NetworkNodeRouteTest, MinimalConstructor) {
    NetworkNodeRoute minimal_node(0x5678, 90, 10000);

    EXPECT_EQ(minimal_node.routing_entry.destination, 0x5678);
    EXPECT_EQ(minimal_node.battery_level, 90);
    EXPECT_EQ(minimal_node.last_seen, 10000);
    EXPECT_FALSE(minimal_node.is_network_manager);             // Default value
    EXPECT_EQ(minimal_node.capabilities, 0);                   // Default value
    EXPECT_EQ(minimal_node.routing_entry.allocated_data_slots, 0);  // Default value
}

/**
 * @brief Test IsExpired logic
 */
TEST_F(NetworkNodeRouteTest, IsExpired) {
    uint32_t timeout_ms = 10000;

    // Not expired
    EXPECT_FALSE(node_.IsExpired(5000, timeout_ms));   // Same time
    EXPECT_FALSE(node_.IsExpired(14999, timeout_ms));  // Just before timeout

    // Expired
    EXPECT_TRUE(node_.IsExpired(15001, timeout_ms));  // After timeout
    EXPECT_TRUE(node_.IsExpired(20000, timeout_ms));  // Well after timeout
}

/**
 * @brief Test UpdateLastSeen
 */
TEST_F(NetworkNodeRouteTest, UpdateLastSeen) {
    uint32_t new_time = 8000;

    node_.UpdateLastSeen(new_time);
    EXPECT_EQ(node_.last_seen, new_time);

    // Update with earlier time (should still update)
    node_.UpdateLastSeen(7000);
    EXPECT_EQ(node_.last_seen, 7000);
}

/**
 * @brief Test UpdateBatteryLevel
 */
TEST_F(NetworkNodeRouteTest, UpdateBatteryLevel) {
    uint32_t current_time = 6000;

    // Valid battery level update
    bool changed = node_.UpdateBatteryLevel(85, current_time);
    EXPECT_TRUE(changed);
    EXPECT_EQ(node_.battery_level, 85);
    EXPECT_EQ(node_.last_seen, current_time);

    // Same battery level (no change)
    changed = node_.UpdateBatteryLevel(85, current_time + 1000);
    EXPECT_FALSE(changed);
    EXPECT_EQ(node_.battery_level, 85);
    EXPECT_EQ(node_.last_seen, current_time);  // Should not update time

    // Invalid battery level (> 100)
    changed = node_.UpdateBatteryLevel(150, current_time + 2000);
    EXPECT_FALSE(changed);
    EXPECT_EQ(node_.battery_level, 85);        // Should remain unchanged
    EXPECT_EQ(node_.last_seen, current_time);  // Should not update time

    // Edge cases: 0 and 100
    changed = node_.UpdateBatteryLevel(0, current_time + 3000);
    EXPECT_TRUE(changed);
    EXPECT_EQ(node_.battery_level, 0);

    changed = node_.UpdateBatteryLevel(100, current_time + 4000);
    EXPECT_TRUE(changed);
    EXPECT_EQ(node_.battery_level, 100);
}

/**
 * @brief Test UpdateCapabilities
 */
TEST_F(NetworkNodeRouteTest, UpdateCapabilities) {
    uint32_t current_time = 6000;
    uint8_t new_capabilities = GATEWAY | HIGH_BANDWIDTH | SENSOR_NODE;

    node_.UpdateCapabilities(new_capabilities, current_time);
    EXPECT_EQ(node_.capabilities, new_capabilities);
    EXPECT_EQ(node_.last_seen, current_time);
}

/**
 * @brief Test UpdateAllocatedSlots
 */
TEST_F(NetworkNodeRouteTest, UpdateAllocatedSlots) {
    uint32_t current_time = 6000;
    uint8_t new_slots = 5;

    node_.UpdateAllocatedSlots(new_slots, current_time);
    EXPECT_EQ(node_.routing_entry.allocated_data_slots, new_slots);
    EXPECT_EQ(node_.last_seen, current_time);
}

/**
 * @brief Test HasCapability
 */
TEST_F(NetworkNodeRouteTest, HasCapability) {
    // Node has ROUTER and BATTERY_POWERED capabilities (0x05)
    EXPECT_TRUE(node_.HasCapability(ROUTER));
    EXPECT_TRUE(node_.HasCapability(BATTERY_POWERED));
    EXPECT_TRUE(node_.HasCapability(ROUTER | BATTERY_POWERED));

    EXPECT_FALSE(node_.HasCapability(GATEWAY));
    EXPECT_FALSE(node_.HasCapability(HIGH_BANDWIDTH));
    EXPECT_FALSE(node_.HasCapability(TIME_SYNC_SOURCE));
    EXPECT_FALSE(node_.HasCapability(SENSOR_NODE));

    // Test with all capabilities
    NetworkNodeRoute full_node(0x9999, 100, 1000, true, 0xFF, 5);
    for (uint8_t cap = 1; cap != 0; cap <<= 1) {
        EXPECT_TRUE(full_node.HasCapability(cap));
    }
}

/**
 * @brief Test GetCapabilitiesString
 */
TEST_F(NetworkNodeRouteTest, GetCapabilitiesString) {
    // Test with current node (ROUTER | BATTERY_POWERED)
    std::string caps_str = node_.GetCapabilitiesString();
    EXPECT_NE(caps_str.find("ROUTER"), std::string::npos);
    EXPECT_NE(caps_str.find("BATTERY_POWERED"), std::string::npos);

    // Test with no capabilities
    NetworkNodeRoute no_caps_node(0x1111, 50, 1000, false, 0, 1);
    EXPECT_EQ(no_caps_node.GetCapabilitiesString(), "NONE");

    // Test with all capabilities
    NetworkNodeRoute all_caps_node(0x2222, 50, 1000, false, 0xFF, 1);
    std::string all_caps_str = all_caps_node.GetCapabilitiesString();
    EXPECT_NE(all_caps_str.find("ROUTER"), std::string::npos);
    EXPECT_NE(all_caps_str.find("GATEWAY"), std::string::npos);
    EXPECT_NE(all_caps_str.find("BATTERY_POWERED"), std::string::npos);
    EXPECT_NE(all_caps_str.find("HIGH_BANDWIDTH"), std::string::npos);
    EXPECT_NE(all_caps_str.find("TIME_SYNC_SOURCE"), std::string::npos);
    EXPECT_NE(all_caps_str.find("SENSOR_NODE"), std::string::npos);
    EXPECT_NE(all_caps_str.find("RESERVED"), std::string::npos);
    EXPECT_NE(all_caps_str.find("EXTENDED_CAPS"), std::string::npos);
}

/**
 * @brief Test serialization and deserialization
 */
TEST_F(NetworkNodeRouteTest, SerializationDeserialization) {
    // Serialize the node
    std::vector<uint8_t> buffer(NetworkNodeRoute::SerializedSize());
    utils::ByteSerializer serializer(buffer);

    Result result = node_.Serialize(serializer);
    ASSERT_TRUE(result.IsSuccess());

    // Deserialize the node
    utils::ByteDeserializer deserializer(buffer);
    auto deserialized_node = NetworkNodeRoute::Deserialize(deserializer);

    ASSERT_TRUE(deserialized_node.has_value());

    // Compare original and deserialized nodes
    EXPECT_EQ(node_.routing_entry.destination,
              deserialized_node->routing_entry.destination);
    EXPECT_EQ(node_.battery_level, deserialized_node->battery_level);
    EXPECT_EQ(node_.last_seen, deserialized_node->last_seen);
    EXPECT_EQ(node_.is_network_manager, deserialized_node->is_network_manager);
    EXPECT_EQ(node_.capabilities, deserialized_node->capabilities);
}

/**
 * @brief Test deserialization with insufficient data
 */
TEST_F(NetworkNodeRouteTest, DeserializationWithInsufficientData) {
    // Create buffer with insufficient data
    std::vector<uint8_t> buffer(5);  // Much smaller than required
    utils::ByteDeserializer deserializer(buffer);

    auto result = NetworkNodeRoute::Deserialize(deserializer);
    EXPECT_FALSE(result.has_value());
}

/**
 * @brief Test equality operators
 */
TEST_F(NetworkNodeRouteTest, EqualityOperators) {
    NetworkNodeRoute equal_node(0x1234, 90, 8000, true, 0x10,
                                2);  // Same address
    NetworkNodeRoute different_node(0x5678, 75, 5000, false, 0x05,
                                    3);  // Different address

    // Equality is based on address only
    EXPECT_TRUE(node_ == equal_node);
    EXPECT_FALSE(node_ != equal_node);

    EXPECT_FALSE(node_ == different_node);
    EXPECT_TRUE(node_ != different_node);
}

/**
 * @brief Test less than operator for sorting
 */
TEST_F(NetworkNodeRouteTest, LessThanOperator) {
    NetworkNodeRoute smaller_node(0x1000, 50, 1000);
    NetworkNodeRoute larger_node(0x2000, 50, 1000);

    EXPECT_TRUE(smaller_node < node_);   // 0x1000 < 0x1234
    EXPECT_FALSE(node_ < smaller_node);  // 0x1234 > 0x1000
    EXPECT_TRUE(node_ < larger_node);    // 0x1234 < 0x2000
    EXPECT_FALSE(larger_node < node_);   // 0x2000 > 0x1234
}

/**
 * @brief Test SerializedSize constant
 */
TEST_F(NetworkNodeRouteTest, SerializedSize) {
    // Test that the constant matches actual serialized size
    std::vector<uint8_t> buffer(100);  // Large enough buffer
    utils::ByteSerializer serializer(buffer);

    node_.Serialize(serializer);
    size_t actual_size = serializer.getOffset();

    EXPECT_EQ(NetworkNodeRoute::SerializedSize(), actual_size);
}

/**
 * @brief Test sorting of network nodes
 */
TEST_F(NetworkNodeRouteTest, SortingNodes) {
    std::vector<NetworkNodeRoute> nodes;
    nodes.emplace_back(0x3333, 50, 1000);
    nodes.emplace_back(0x1111, 75, 2000);
    nodes.emplace_back(0x2222, 90, 3000);

    // Sort nodes by address
    std::sort(nodes.begin(), nodes.end());

    EXPECT_EQ(nodes[0].routing_entry.destination, 0x1111);
    EXPECT_EQ(nodes[1].routing_entry.destination, 0x2222);
    EXPECT_EQ(nodes[2].routing_entry.destination, 0x3333);
}

/**
 * @brief Test network manager nodes
 */
TEST_F(NetworkNodeRouteTest, NetworkManagerNodes) {
    NetworkNodeRoute manager(0x1000, 100, 1000, true,
                             GATEWAY | TIME_SYNC_SOURCE, 10);
    NetworkNodeRoute regular(0x2000, 80, 1000, false, ROUTER | SENSOR_NODE, 3);

    EXPECT_TRUE(manager.is_network_manager);
    EXPECT_FALSE(regular.is_network_manager);

    // Network manager typically has more capabilities and slots
    EXPECT_TRUE(manager.HasCapability(GATEWAY));
    EXPECT_TRUE(manager.HasCapability(TIME_SYNC_SOURCE));
    EXPECT_GT(manager.routing_entry.allocated_data_slots,
              regular.routing_entry.allocated_data_slots);
}

/**
 * @brief Test capability combinations
 */
TEST_F(NetworkNodeRouteTest, CapabilityCombinations) {
    // Test various common capability combinations

    // Gateway node
    NetworkNodeRoute gateway(0x1000, 100, 1000, true,
                             GATEWAY | ROUTER | HIGH_BANDWIDTH, 5);
    EXPECT_TRUE(gateway.HasCapability(GATEWAY));
    EXPECT_TRUE(gateway.HasCapability(ROUTER));
    EXPECT_TRUE(gateway.HasCapability(HIGH_BANDWIDTH));
    EXPECT_FALSE(gateway.HasCapability(BATTERY_POWERED));

    // Sensor node
    NetworkNodeRoute sensor(0x2000, 60, 1000, false,
                            SENSOR_NODE | BATTERY_POWERED, 1);
    EXPECT_TRUE(sensor.HasCapability(SENSOR_NODE));
    EXPECT_TRUE(sensor.HasCapability(BATTERY_POWERED));
    EXPECT_FALSE(sensor.HasCapability(ROUTER));

    // Mobile router
    NetworkNodeRoute mobile(0x3000, 45, 1000, false, ROUTER | BATTERY_POWERED,
                            2);
    EXPECT_TRUE(mobile.HasCapability(ROUTER));
    EXPECT_TRUE(mobile.HasCapability(BATTERY_POWERED));
    EXPECT_FALSE(mobile.HasCapability(GATEWAY));
}

/**
 * @brief Test time-related operations
 */
TEST_F(NetworkNodeRouteTest, TimeOperations) {
    uint32_t base_time = 10000;
    NetworkNodeRoute time_node(0x1000, 80, base_time);

    // Test various time updates and expiration checks
    time_node.UpdateLastSeen(base_time + 1000);
    EXPECT_EQ(time_node.last_seen, base_time + 1000);

    // Test expiration with different timeouts
    EXPECT_FALSE(time_node.IsExpired(base_time + 1500, 1000));  // Not expired
    EXPECT_TRUE(time_node.IsExpired(base_time + 2500, 1000));   // Expired

    // Test battery level update also updates time
    uint32_t old_time = time_node.last_seen;
    time_node.UpdateBatteryLevel(90, base_time + 3000);
    EXPECT_EQ(time_node.last_seen, base_time + 3000);
    EXPECT_GT(time_node.last_seen, old_time);
}

}  // namespace test
}  // namespace lora_mesh
}  // namespace protocols
}  // namespace types
}  // namespace loramesher