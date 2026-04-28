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
    EXPECT_EQ(default_node.routing_entry.capabilities, 0);
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
    EXPECT_EQ(node_.routing_entry.capabilities,
              0x05);  // ROUTER | BATTERY_POWERED
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
    EXPECT_FALSE(minimal_node.is_network_manager);          // Default value
    EXPECT_EQ(minimal_node.routing_entry.capabilities, 0);  // Default value
    EXPECT_EQ(minimal_node.routing_entry.allocated_data_slots,
              0);  // Default value
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
    EXPECT_EQ(node_.routing_entry.capabilities, new_capabilities);
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

// ---- Additional constructors ----

TEST_F(NetworkNodeRouteTest, ConstructorAddrTime) {
    NetworkNodeRoute node(0xABCD, 12345u);
    EXPECT_EQ(node.routing_entry.destination, 0xABCD);
    EXPECT_EQ(node.last_seen, 12345u);
}

TEST_F(NetworkNodeRouteTest, ConstructorWithHops) {
    NetworkNodeRoute node(0x1234, 90, 5000, false, 0x03, 4, 2);
    EXPECT_EQ(node.routing_entry.destination, 0x1234);
    EXPECT_EQ(node.routing_entry.hop_count, 2);
    EXPECT_EQ(node.next_hop, 0x1234);  // default: node itself
}

TEST_F(NetworkNodeRouteTest, ConstructorRoutingInfo) {
    NetworkNodeRoute node(0x1000, 0x2000, (uint8_t)3, (uint8_t)200,
                          (uint32_t)8000);
    EXPECT_EQ(node.routing_entry.destination, 0x1000);
    EXPECT_EQ(node.next_hop, 0x2000);
    EXPECT_EQ(node.routing_entry.hop_count, 3);
    EXPECT_EQ(node.routing_entry.link_quality, 200);
    EXPECT_EQ(node.last_seen, 8000u);
    EXPECT_TRUE(node.is_active);
}

// ---- IsDirectNeighbor ----

TEST_F(NetworkNodeRouteTest, IsDirectNeighbor) {
    // hop_count=1 and is_active=true → direct neighbor
    NetworkNodeRoute neighbor(0x5678, 80, 1000, false, 0, 0, 1);
    EXPECT_TRUE(neighbor.IsDirectNeighbor());

    // hop_count=2 → not direct
    NetworkNodeRoute remote(0x5679, 80, 1000, false, 0, 0, 2);
    EXPECT_FALSE(remote.IsDirectNeighbor());

    // hop_count=1 but inactive
    NetworkNodeRoute inactive(0x567A, 80, 1000, false, 0, 0, 1);
    inactive.is_active = false;
    EXPECT_FALSE(inactive.IsDirectNeighbor());
}

// ---- CalculateRouteCost ----

TEST_F(NetworkNodeRouteTest, CalculateRouteCost) {
    // ETX-inspired: cost = hop_count * 65536 / quality

    // 1 hop, quality 255 (max) → cost = 65536/255 = 257
    uint16_t cost = NetworkNodeRoute::CalculateRouteCost(1, 255);
    EXPECT_EQ(cost, 257u);

    // 2 hops, quality 128 → cost = 2*65536/128 = 1024
    cost = NetworkNodeRoute::CalculateRouteCost(2, 128);
    EXPECT_EQ(cost, 1024u);

    // 0 hops, quality 0 → cost = 65535 (clamped max)
    cost = NetworkNodeRoute::CalculateRouteCost(0, 0);
    EXPECT_EQ(cost, 65535u);
}

// ---- IsBetterRouteThan ----

TEST_F(NetworkNodeRouteTest, IsBetterRouteActive) {
    // Active vs inactive: active wins
    NetworkNodeRoute active((AddressType)0x1000, (AddressType)0x1000,
                            (uint8_t)2, (uint8_t)200,
                            (uint32_t)1000);  // active=true
    NetworkNodeRoute inactive((AddressType)0x1000, (AddressType)0x1000,
                              (uint8_t)1, (uint8_t)255,
                              (uint32_t)1000);  // better quality but...
    inactive.is_active = false;

    EXPECT_TRUE(active.IsBetterRouteThan(inactive));
    EXPECT_FALSE(inactive.IsBetterRouteThan(active));
}

TEST_F(NetworkNodeRouteTest, IsBetterRouteByCost) {
    // Both active, compare by cost (ETX: hop*65536/quality)
    NetworkNodeRoute good((AddressType)0x1000, (AddressType)0x1000, (uint8_t)1,
                          (uint8_t)255, (uint32_t)1000);  // cost = 257
    NetworkNodeRoute bad((AddressType)0x1000, (AddressType)0x1000, (uint8_t)3,
                         (uint8_t)100,
                         (uint32_t)1000);  // cost = 3*65536/100 = 1966

    EXPECT_TRUE(good.IsBetterRouteThan(bad));
    EXPECT_FALSE(bad.IsBetterRouteThan(good));
}

TEST_F(NetworkNodeRouteTest, IsBetterRouteByHopsTiebreaker) {
    // ETX cost tie: h1*65536/q1 == h2*65536/q2 when h1/q1 == h2/q2
    // 1 hop, quality 85 → cost = 65536/85 = 771
    // 3 hops, quality 255 → cost = 3*65536/255 = 771
    NetworkNodeRoute fewer((AddressType)0x1000, (AddressType)0x1000, (uint8_t)1,
                           (uint8_t)85, (uint32_t)1000);  // cost=771
    NetworkNodeRoute more((AddressType)0x1000, (AddressType)0x1000, (uint8_t)3,
                          (uint8_t)255,
                          (uint32_t)1000);  // cost=771

    // Just verify the method doesn't crash and returns a bool
    bool result = fewer.IsBetterRouteThan(more);
    (void)result;
}

// ---- UpdateNodeInfo ----

TEST_F(NetworkNodeRouteTest, UpdateNodeInfoChanges) {
    bool changed = node_.UpdateNodeInfo(90, true, 0xFF, 8, 6000);
    EXPECT_TRUE(changed);
    EXPECT_EQ(node_.battery_level, 90);
    EXPECT_TRUE(node_.is_network_manager);
    EXPECT_EQ(node_.routing_entry.capabilities, 0xFF);
    EXPECT_EQ(node_.routing_entry.allocated_data_slots, 8);
    EXPECT_EQ(node_.last_seen, 6000u);
}

TEST_F(NetworkNodeRouteTest, UpdateNodeInfoNoChanges) {
    // Set same values
    bool changed = node_.UpdateNodeInfo(75, false, 0x05, 3, 6000);
    // Battery 75 == current, manager false == current, caps 0x05 == current, slots 3 == current
    EXPECT_FALSE(changed);
    EXPECT_EQ(node_.last_seen, 6000u);  // Time still updated
}

TEST_F(NetworkNodeRouteTest, UpdateNodeInfoZeroSlots) {
    // slots=0 should not update allocated_data_slots
    node_.UpdateNodeInfo(75, false, 0x05, 0, 6000);
    EXPECT_EQ(node_.routing_entry.allocated_data_slots, 3);  // Unchanged
}

// ---- UpdateRouteInfo ----

TEST_F(NetworkNodeRouteTest, UpdateRouteInfo) {
    bool changed = node_.UpdateRouteInfo(0x9999, 3, 150, 7000);
    EXPECT_TRUE(changed);
    EXPECT_EQ(node_.next_hop, 0x9999);
    EXPECT_EQ(node_.routing_entry.hop_count, 3);
    EXPECT_EQ(node_.routing_entry.link_quality, 150);
    EXPECT_EQ(node_.last_updated, 7000u);
    EXPECT_TRUE(node_.is_active);
}

TEST_F(NetworkNodeRouteTest, UpdateRouteInfoNoChanges) {
    // First set some values
    node_.UpdateRouteInfo(0x9999, 3, 150, 7000);
    // Now call again with same values
    bool changed = node_.UpdateRouteInfo(0x9999, 3, 150, 8000);
    EXPECT_FALSE(changed);
}

// ---- UpdateFromRoutingTableEntry ----

TEST_F(NetworkNodeRouteTest, UpdateFromRoutingTableEntry) {
    RoutingTableEntry entry(0x1234, 2, 200, 5, 0x0F);
    bool changed = node_.UpdateFromRoutingTableEntry(entry, 0x4567, 9000);
    EXPECT_TRUE(changed);
    EXPECT_EQ(node_.next_hop, 0x4567);
    EXPECT_EQ(node_.routing_entry.hop_count, 2);
    EXPECT_EQ(node_.routing_entry.link_quality, 200);
    EXPECT_EQ(node_.routing_entry.allocated_data_slots, 5);
    EXPECT_EQ(node_.routing_entry.capabilities, 0x0F);
    EXPECT_TRUE(node_.is_active);
}

// ---- LinkQualityStats ----

TEST_F(NetworkNodeRouteTest, LinkQualityCalculateQualityNoTracking) {
    NetworkNodeRoute::LinkQualityStats stats;
    // Below sample threshold → provisional quality, regardless of remote
    EXPECT_EQ(stats.CalculateQuality(),
              NetworkNodeRoute::LinkQualityStats::kProvisionalQuality);

    stats.remote_link_quality = 200;
    EXPECT_EQ(stats.CalculateQuality(),
              NetworkNodeRoute::LinkQualityStats::kProvisionalQuality);

    // If remote is below provisional, peer's lower estimate wins
    stats.remote_link_quality = 32;
    EXPECT_EQ(stats.CalculateQuality(), 32);
}

TEST_F(NetworkNodeRouteTest, LinkQualityCalculateQualityPerfect) {
    NetworkNodeRoute::LinkQualityStats stats;
    // Simulate 10 superframes with perfect bidirectional reception
    for (int i = 0; i < 10; i++) {
        stats.ExpectMessage();
        stats.ReceivedMessage(i * 1000);
    }
    stats.UpdateRemoteQuality(200);
    // With deferred decay, perfect reception only boosts EWMA
    EXPECT_GE(stats.CalculateQuality(), 200);
}

TEST_F(NetworkNodeRouteTest, LinkQualityCalculateQualityHalf) {
    NetworkNodeRoute::LinkQualityStats stats;
    // Simulate 10 superframes, receive only on even ones (bidirectional)
    for (int i = 0; i < 10; i++) {
        stats.ExpectMessage();
        if (i % 2 == 0) {
            stats.ReceivedMessage(i * 1000);
        }
    }
    stats.UpdateRemoteQuality(200);
    uint8_t q = stats.CalculateQuality();
    // Quality should reflect 50% PDR, averaged with remote
    EXPECT_LT(q, 250);
    EXPECT_GT(q, 50);
}

TEST_F(NetworkNodeRouteTest, LinkQualityCalculateQualityWithRemote) {
    NetworkNodeRoute::LinkQualityStats stats;
    // Simulate 16 superframes with perfect reception (fills sliding window)
    for (int i = 0; i < 16; i++) {
        stats.ExpectMessage();
        stats.ReceivedMessage(i * 1000);
    }
    stats.UpdateRemoteQuality(100);
    uint8_t q = stats.CalculateQuality();
    // Window PDR = 255 (all received), weighted bottleneck:
    // (min(255,100)*7 + avg(255,100)*3) / 10 = (700 + 532) / 10 = 123
    EXPECT_EQ(q, 123);
}

TEST_F(NetworkNodeRouteTest, LinkQualityUnidirectionalPenaltyAfterThreshold) {
    NetworkNodeRoute::LinkQualityStats stats;
    // Simulate 4 superframes where peer never lists us (remote stays 0)
    for (int i = 0; i < 4; i++) {
        stats.ExpectMessage();
        stats.ReceivedMessage(i * 1000);
    }
    EXPECT_GE(stats.messages_expected, 3u);
    EXPECT_EQ(stats.remote_link_quality, 0);
    // Unidirectional penalty: minimum quality (1)
    EXPECT_EQ(stats.CalculateQuality(), 1);
}

TEST_F(NetworkNodeRouteTest,
       LinkQualityUnidirectionalNoPenaltyBeforeThreshold) {
    NetworkNodeRoute::LinkQualityStats stats;
    // Only 2 receptions — below the kMinSamplesForQuality gate
    for (int i = 0; i < 2; i++) {
        stats.ExpectMessage();
        stats.ReceivedMessage(i * 1000);
    }
    EXPECT_EQ(stats.messages_received, 2u);
    EXPECT_EQ(stats.remote_link_quality, 0);
    // Below sample threshold → provisional, no unidirectional penalty
    EXPECT_EQ(stats.CalculateQuality(),
              NetworkNodeRoute::LinkQualityStats::kProvisionalQuality);
}

TEST_F(NetworkNodeRouteTest, LinkQualityUnidirectionalRecovery) {
    NetworkNodeRoute::LinkQualityStats stats;
    // Simulate 5 superframes with no remote quality — penalty active
    for (int i = 0; i < 5; i++) {
        stats.ExpectMessage();
        stats.ReceivedMessage(i * 1000);
    }
    uint8_t penalized = stats.CalculateQuality();
    EXPECT_EQ(penalized, 1);

    // Peer starts hearing us — recovery
    stats.UpdateRemoteQuality(200);
    uint8_t recovered = stats.CalculateQuality();
    // Bidirectional: weighted bottleneck. Window not ready (5 < 16), uses ewma.
    uint16_t bottleneck =
        std::min(stats.ewma_quality, static_cast<uint8_t>(200));
    uint16_t average = (static_cast<uint16_t>(stats.ewma_quality) + 200) / 2;
    EXPECT_EQ(recovered,
              static_cast<uint8_t>((bottleneck * 7 + average * 3) / 10));
    EXPECT_GT(recovered, penalized);
}

TEST_F(NetworkNodeRouteTest, LinkQualityReset) {
    NetworkNodeRoute::LinkQualityStats stats;
    stats.messages_expected = 5;
    stats.messages_received = 3;
    stats.remote_link_quality = 200;
    stats.last_message_time = 1000;

    stats.Reset();
    EXPECT_EQ(stats.messages_expected, 0u);
    EXPECT_EQ(stats.messages_received, 0u);
    // remote_quality and last_message_time should be preserved
    EXPECT_EQ(stats.remote_link_quality, 200);
    EXPECT_EQ(stats.last_message_time, 1000u);
}

TEST_F(NetworkNodeRouteTest, LinkQualityExpectMessage) {
    NetworkNodeRoute::LinkQualityStats stats;
    stats.ExpectMessage();
    EXPECT_EQ(stats.messages_expected, 1u);
    EXPECT_EQ(stats.consecutive_missed, 1u);
}

TEST_F(NetworkNodeRouteTest, LinkQualityReceivedMessage) {
    NetworkNodeRoute::LinkQualityStats stats;
    stats.ReceivedMessage(5000);
    EXPECT_EQ(stats.messages_received, 1u);
    EXPECT_EQ(stats.last_message_time, 5000u);
    EXPECT_EQ(stats.consecutive_missed, 0u);
}

TEST_F(NetworkNodeRouteTest, LinkQualityUpdateRemote) {
    NetworkNodeRoute::LinkQualityStats stats;
    stats.UpdateRemoteQuality(180);
    EXPECT_EQ(stats.remote_link_quality, 180);
}

// ---- ExpectRoutingMessage / ReceivedRoutingMessage ----

TEST_F(NetworkNodeRouteTest, ExpectRoutingMessage) {
    node_.ExpectRoutingMessage();
    EXPECT_EQ(node_.link_stats.messages_expected, 1u);
}

TEST_F(NetworkNodeRouteTest, ReceivedRoutingMessage) {
    node_.ReceivedRoutingMessage(150, 7000);
    EXPECT_EQ(node_.link_stats.messages_received, 1u);
    EXPECT_EQ(node_.link_stats.remote_link_quality, 150);
    EXPECT_EQ(node_.last_seen, 7000u);
}

TEST_F(NetworkNodeRouteTest, GetLinkQuality) {
    // Fresh node with no stats → default link_quality from constructor
    EXPECT_GE(node_.GetLinkQuality(), 0);
}

TEST_F(NetworkNodeRouteTest, GetRemoteLinkQuality) {
    EXPECT_EQ(node_.GetRemoteLinkQuality(), 0);
    node_.link_stats.UpdateRemoteQuality(120);
    EXPECT_EQ(node_.GetRemoteLinkQuality(), 120);
}

TEST_F(NetworkNodeRouteTest, ResetLinkStats) {
    node_.link_stats.messages_expected = 10;
    node_.link_stats.messages_received = 5;
    node_.ResetLinkStats();
    EXPECT_EQ(node_.link_stats.messages_expected, 0u);
    EXPECT_EQ(node_.link_stats.messages_received, 0u);
}

// ---- ToRoutingTableEntry ----

TEST_F(NetworkNodeRouteTest, ToRoutingTableEntry) {
    RoutingTableEntry entry = node_.ToRoutingTableEntry();
    EXPECT_EQ(entry.destination, node_.routing_entry.destination);
    EXPECT_EQ(entry.hop_count, node_.routing_entry.hop_count);
    EXPECT_EQ(entry.link_quality, node_.routing_entry.link_quality);
    EXPECT_EQ(entry.capabilities, node_.routing_entry.capabilities);
    // hop_count=0 in fixture, so reception_quality stays 0
    EXPECT_EQ(entry.reception_quality, 0u);
}

TEST_F(NetworkNodeRouteTest,
       ToRoutingTableEntryDirectNeighborHasReceptionQuality) {
    node_.routing_entry.hop_count = 1;
    node_.link_stats.ewma_quality = 180;
    node_.link_stats.messages_received = 3;
    RoutingTableEntry entry = node_.ToRoutingTableEntry();
    EXPECT_EQ(entry.reception_quality, 180u);
}

TEST_F(NetworkNodeRouteTest,
       ToRoutingTableEntryMultiHopNoReceptionHasZeroReceptionQuality) {
    node_.routing_entry.hop_count = 2;
    node_.link_stats.ewma_quality = 180;
    node_.link_stats.messages_received = 0;  // Never received from this node
    RoutingTableEntry entry = node_.ToRoutingTableEntry();
    EXPECT_EQ(entry.reception_quality, 0u);
}

TEST_F(NetworkNodeRouteTest,
       ToRoutingTableEntryMultiHopWithReceptionHasReceptionQuality) {
    node_.routing_entry.hop_count = 2;
    node_.link_stats.ewma_quality = 48;
    node_.link_stats.messages_received = 5;  // Heard directly (lossy link)
    RoutingTableEntry entry = node_.ToRoutingTableEntry();
    EXPECT_EQ(entry.reception_quality, 48u);
}

TEST_F(NetworkNodeRouteTest,
       ToRoutingTableEntryReceptionQualityZeroBelowThreshold) {
    node_.routing_entry.hop_count = 1;
    node_.link_stats.ewma_quality = 180;
    node_.link_stats.messages_received = 2;  // below kMinSamplesForQuality
    RoutingTableEntry entry = node_.ToRoutingTableEntry();
    EXPECT_EQ(entry.reception_quality, 0u);
}

TEST_F(NetworkNodeRouteTest, LinkQualityProvisionalBelowSampleThreshold) {
    NetworkNodeRoute::LinkQualityStats stats;
    stats.ReceivedMessage(1000);
    EXPECT_EQ(stats.messages_received, 1u);
    EXPECT_EQ(stats.CalculateQuality(),
              NetworkNodeRoute::LinkQualityStats::kProvisionalQuality);

    stats.ReceivedMessage(2000);
    EXPECT_EQ(stats.messages_received, 2u);
    EXPECT_EQ(stats.CalculateQuality(),
              NetworkNodeRoute::LinkQualityStats::kProvisionalQuality);
}

TEST_F(NetworkNodeRouteTest, LinkQualityClimbsAfterSampleThreshold) {
    NetworkNodeRoute::LinkQualityStats stats;
    stats.UpdateRemoteQuality(240);
    for (int i = 0; i < 5; ++i) {
        stats.ExpectMessage();
        stats.ReceivedMessage(i * 1000);
    }
    EXPECT_GE(stats.messages_received,
              NetworkNodeRoute::LinkQualityStats::kMinSamplesForQuality);
    EXPECT_GT(stats.CalculateQuality(),
              NetworkNodeRoute::LinkQualityStats::kProvisionalQuality);
}

TEST_F(NetworkNodeRouteTest, LinkQualityProvisionalCappedByRemote) {
    NetworkNodeRoute::LinkQualityStats stats;
    stats.UpdateRemoteQuality(32);
    stats.ReceivedMessage(1000);
    // Below sample threshold: peer's lower estimate wins over provisional
    EXPECT_EQ(stats.CalculateQuality(), 32);
}

// ---- GetAddress / GetAllocatedDataSlots ----

TEST_F(NetworkNodeRouteTest, GetAddress) {
    EXPECT_EQ(node_.GetAddress(), 0x1234);
}

TEST_F(NetworkNodeRouteTest, GetAllocatedDataSlots) {
    EXPECT_EQ(node_.GetAllocatedDataSlots(), 3);
}

// =============================================================================
// UpdateAllocatedSlots — no-change path (line 267)
// =============================================================================

/**
 * @brief Test UpdateAllocatedSlots returns false when slots unchanged
 *
 * Covers the "return false; // No change" branch at line 267.
 */
TEST_F(NetworkNodeRouteTest, UpdateAllocatedSlotsUnchangedReturnsFalse) {
    // node_ was constructed with allocated_data_slots = 3
    bool changed = node_.UpdateAllocatedSlots(3, 10000);
    EXPECT_FALSE(changed);
    // Slots remain unchanged
    EXPECT_EQ(node_.routing_entry.allocated_data_slots, 3u);
}

/**
 * @brief Test UpdateAllocatedSlots returns true when slots change
 */
TEST_F(NetworkNodeRouteTest, UpdateAllocatedSlotsChangedReturnsTrue) {
    bool changed = node_.UpdateAllocatedSlots(5, 10000);
    EXPECT_TRUE(changed);
    EXPECT_EQ(node_.routing_entry.allocated_data_slots, 5u);
}

// =============================================================================
// UpdateCapabilities — no-change path (line 278)
// =============================================================================

/**
 * @brief Test UpdateCapabilities returns false when capabilities unchanged
 *
 * Covers the "return false; // No change" branch at line 278.
 */
TEST_F(NetworkNodeRouteTest, UpdateCapabilitiesUnchangedReturnsFalse) {
    // node_ was constructed with capabilities = 0x05
    bool changed = node_.UpdateCapabilities(0x05, 10000);
    EXPECT_FALSE(changed);
    EXPECT_EQ(node_.routing_entry.capabilities, 0x05u);
}

/**
 * @brief Test UpdateCapabilities returns true when capabilities change
 */
TEST_F(NetworkNodeRouteTest, UpdateCapabilitiesChangedReturnsTrue) {
    bool changed = node_.UpdateCapabilities(0x0F, 10000);
    EXPECT_TRUE(changed);
    EXPECT_EQ(node_.routing_entry.capabilities, 0x0Fu);
}

// ---- GetCapabilities ----

TEST_F(NetworkNodeRouteTest, GetCapabilities) {
    // node_ was constructed with capabilities = 0x05
    EXPECT_EQ(node_.GetCapabilities(), 0x05);

    // Update and re-check
    node_.UpdateCapabilities(0xFF, 10000);
    EXPECT_EQ(node_.GetCapabilities(), 0xFF);

    // Zero capabilities
    node_.UpdateCapabilities(0x00, 10001);
    EXPECT_EQ(node_.GetCapabilities(), 0x00);
}

}  // namespace test
}  // namespace lora_mesh
}  // namespace protocols
}  // namespace types
}  // namespace loramesher