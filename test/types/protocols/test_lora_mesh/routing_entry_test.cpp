// /**
//  * @file routing_entry_test.cpp
//  * @brief Unit tests for RoutingEntry class
//  */

// #include "types/protocols/lora_mesh/routing_entry.hpp"
// #include <gtest/gtest.h>
// #include "utils/byte_operations.h"

// namespace loramesher {
// namespace types {
// namespace protocols {
// namespace lora_mesh {
// namespace test {

// /**
//  * @brief Test fixture for RoutingEntry tests
//  */
// class RoutingEntryTest : public ::testing::Test {
//    protected:
//     void SetUp() override {
//         // Create a sample routing entry for testing
//         entry_ = RoutingEntry(0x1234,  // destination
//                               0x5678,  // next_hop
//                               3,       // hop_count
//                               2,       // allocated_slots
//                               85,      // link_quality
//                               1000,    // last_updated
//                               true     // is_active
//         );
//     }

//     RoutingEntry entry_;
// };

// /**
//  * @brief Test default constructor
//  */
// TEST_F(RoutingEntryTest, DefaultConstructor) {
//     RoutingEntry default_entry;

//     // Default values should be zero/false
//     EXPECT_EQ(default_entry.destination, 0);
//     EXPECT_EQ(default_entry.next_hop, 0);
//     EXPECT_EQ(default_entry.hop_count, 0);
//     EXPECT_EQ(default_entry.allocated_slots, 0);
//     EXPECT_EQ(default_entry.link_quality, 0);
//     EXPECT_EQ(default_entry.last_updated, 0);
//     EXPECT_FALSE(default_entry.is_active);
// }

// /**
//  * @brief Test parameterized constructor
//  */
// TEST_F(RoutingEntryTest, ParameterizedConstructor) {
//     EXPECT_EQ(entry_.destination, 0x1234);
//     EXPECT_EQ(entry_.next_hop, 0x5678);
//     EXPECT_EQ(entry_.hop_count, 3);
//     EXPECT_EQ(entry_.allocated_slots, 2);
//     EXPECT_EQ(entry_.link_quality, 85);
//     EXPECT_EQ(entry_.last_updated, 1000);
//     EXPECT_TRUE(entry_.is_active);
// }

// /**
//  * @brief Test IsBetterThan logic
//  */
// TEST_F(RoutingEntryTest, IsBetterThan) {
//     // Active route is better than inactive
//     RoutingEntry inactive_entry(0x1234, 0x5678, 3, 2, 85, 1000, false);
//     EXPECT_TRUE(entry_.IsBetterThan(inactive_entry));
//     EXPECT_FALSE(inactive_entry.IsBetterThan(entry_));

//     // Lower hop count is better
//     RoutingEntry higher_hop_entry(0x1234, 0x5678, 4, 2, 85, 1000, true);
//     EXPECT_TRUE(entry_.IsBetterThan(higher_hop_entry));
//     EXPECT_FALSE(higher_hop_entry.IsBetterThan(entry_));

//     // Higher link quality is better (when hop count is equal)
//     RoutingEntry lower_quality_entry(0x1234, 0x5678, 3, 2, 75, 1000, true);
//     EXPECT_TRUE(entry_.IsBetterThan(lower_quality_entry));
//     EXPECT_FALSE(lower_quality_entry.IsBetterThan(entry_));

//     // Equal entries
//     RoutingEntry equal_entry(0x1234, 0x5678, 3, 2, 85, 1000, true);
//     EXPECT_FALSE(entry_.IsBetterThan(equal_entry));
//     EXPECT_FALSE(equal_entry.IsBetterThan(entry_));
// }

// /**
//  * @brief Test IsExpired logic
//  */
// TEST_F(RoutingEntryTest, IsExpired) {
//     uint32_t timeout_ms = 5000;

//     // Not expired
//     EXPECT_FALSE(entry_.IsExpired(1000, timeout_ms));  // Same time
//     EXPECT_FALSE(entry_.IsExpired(5999, timeout_ms));  // Just before timeout

//     // Expired
//     EXPECT_TRUE(entry_.IsExpired(6001, timeout_ms));   // After timeout
//     EXPECT_TRUE(entry_.IsExpired(10000, timeout_ms));  // Well after timeout
// }

// /**
//  * @brief Test Update method
//  */
// TEST_F(RoutingEntryTest, Update) {
//     AddressType new_next_hop = 0x9ABC;
//     uint8_t new_hop_count = 2;
//     uint8_t new_link_quality = 95;
//     uint8_t new_allocated_slots = 3;
//     uint32_t new_time = 2000;

//     entry_.Update(new_next_hop, new_hop_count, new_link_quality,
//                   new_allocated_slots, new_time);

//     EXPECT_EQ(entry_.next_hop, new_next_hop);
//     EXPECT_EQ(entry_.hop_count, new_hop_count);
//     EXPECT_EQ(entry_.link_quality, new_link_quality);
//     EXPECT_EQ(entry_.allocated_slots, new_allocated_slots);
//     EXPECT_EQ(entry_.last_updated, new_time);
//     EXPECT_TRUE(entry_.is_active);  // Should be set to active
// }

// /**
//  * @brief Test serialization and deserialization
//  */
// TEST_F(RoutingEntryTest, SerializationDeserialization) {
//     // Serialize the entry
//     std::vector<uint8_t> buffer(RoutingEntry::SerializedSize());
//     utils::ByteSerializer serializer(buffer);

//     Result result = entry_.Serialize(serializer);
//     ASSERT_TRUE(result.IsSuccess());

//     // Deserialize the entry
//     utils::ByteDeserializer deserializer(buffer);
//     auto deserialized_entry = RoutingEntry::Deserialize(deserializer);

//     ASSERT_TRUE(deserialized_entry.has_value());

//     // Compare original and deserialized entries
//     EXPECT_EQ(entry_, *deserialized_entry);
//     EXPECT_EQ(entry_.destination, deserialized_entry->destination);
//     EXPECT_EQ(entry_.next_hop, deserialized_entry->next_hop);
//     EXPECT_EQ(entry_.hop_count, deserialized_entry->hop_count);
//     EXPECT_EQ(entry_.allocated_slots, deserialized_entry->allocated_slots);
//     EXPECT_EQ(entry_.link_quality, deserialized_entry->link_quality);
//     EXPECT_EQ(entry_.last_updated, deserialized_entry->last_updated);
//     EXPECT_EQ(entry_.is_active, deserialized_entry->is_active);
// }

// /**
//  * @brief Test deserialization with insufficient data
//  */
// TEST_F(RoutingEntryTest, DeserializationWithInsufficientData) {
//     // Create buffer with insufficient data
//     std::vector<uint8_t> buffer(5);  // Much smaller than required
//     utils::ByteDeserializer deserializer(buffer);

//     auto result = RoutingEntry::Deserialize(deserializer);
//     EXPECT_FALSE(result.has_value());
// }

// /**
//  * @brief Test equality operators
//  */
// TEST_F(RoutingEntryTest, EqualityOperators) {
//     RoutingEntry equal_entry(0x1234, 0x5678, 3, 2, 85, 1000, true);
//     RoutingEntry different_entry(0x1234, 0x5678, 3, 2, 85, 1000, false);

//     EXPECT_TRUE(entry_ == equal_entry);
//     EXPECT_FALSE(entry_ != equal_entry);

//     EXPECT_FALSE(entry_ == different_entry);
//     EXPECT_TRUE(entry_ != different_entry);
// }

// /**
//  * @brief Test SerializedSize constant
//  */
// TEST_F(RoutingEntryTest, SerializedSize) {
//     // Test that the constant matches actual serialized size
//     std::vector<uint8_t> buffer(100);  // Large enough buffer
//     utils::ByteSerializer serializer(buffer);

//     entry_.Serialize(serializer);
//     size_t actual_size = serializer.getOffset();

//     EXPECT_EQ(RoutingEntry::SerializedSize(), actual_size);
// }

// /**
//  * @brief Test edge cases for hop count and link quality comparisons
//  */
// TEST_F(RoutingEntryTest, EdgeCases) {
//     // Test with minimum and maximum values
//     RoutingEntry max_entry(0x1234, 0x5678, 255, 255, 100, 1000, true);
//     RoutingEntry min_entry(0x1234, 0x5678, 0, 0, 0, 1000, true);

//     EXPECT_TRUE(min_entry.IsBetterThan(max_entry));  // Lower hop count wins

//     // Test with same hop count but different quality
//     RoutingEntry high_quality(0x1234, 0x5678, 5, 2, 95, 1000, true);
//     RoutingEntry low_quality(0x1234, 0x5678, 5, 2, 5, 1000, true);

//     EXPECT_TRUE(high_quality.IsBetterThan(low_quality));
//     EXPECT_FALSE(low_quality.IsBetterThan(high_quality));
// }

// /**
//  * @brief Test Update with various time scenarios
//  */
// TEST_F(RoutingEntryTest, UpdateTimeScenarios) {
//     uint32_t original_time = entry_.last_updated;

//     // Update with later time
//     entry_.Update(0x9999, 2, 90, 3, original_time + 1000);
//     EXPECT_EQ(entry_.last_updated, original_time + 1000);

//     // Update with earlier time (should still update)
//     entry_.Update(0x8888, 1, 80, 1, original_time - 500);
//     EXPECT_EQ(entry_.last_updated, original_time - 500);

//     // Update with same time
//     entry_.Update(0x7777, 4, 70, 5, original_time - 500);
//     EXPECT_EQ(entry_.last_updated, original_time - 500);
// }

// /**
//  * @brief Test multiple serialization/deserialization cycles
//  */
// TEST_F(RoutingEntryTest, MultipleSerializationCycles) {
//     RoutingEntry current_entry = entry_;

//     // Perform multiple serialization/deserialization cycles
//     for (int i = 0; i < 5; i++) {
//         std::vector<uint8_t> buffer(RoutingEntry::SerializedSize());
//         utils::ByteSerializer serializer(buffer);

//         Result serialize_result = current_entry.Serialize(serializer);
//         ASSERT_TRUE(serialize_result.IsSuccess());

//         utils::ByteDeserializer deserializer(buffer);
//         auto deserialized = RoutingEntry::Deserialize(deserializer);
//         ASSERT_TRUE(deserialized.has_value());

//         // Verify entry remains unchanged
//         EXPECT_EQ(current_entry, *deserialized);
//         current_entry = *deserialized;
//     }
// }

// }  // namespace test
// }  // namespace lora_mesh
// }  // namespace protocols
// }  // namespace types
// }  // namespace loramesher