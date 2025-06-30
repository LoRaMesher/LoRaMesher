/**
 * @file superframe_test.cpp
 * @brief Unit tests for Superframe class
 */

#include "types/protocols/lora_mesh/superframe.hpp"
#include <gtest/gtest.h>
#include "utils/byte_operations.h"

namespace loramesher {
namespace types {
namespace protocols {
namespace lora_mesh {
namespace test {

/**
 * @brief Test fixture for Superframe tests
 */
class SuperframeTest : public ::testing::Test {
   protected:
    void SetUp() override {
        // Create a test superframe
        superframe_ = Superframe(100,   // total_slots
                                 60,    // data_slots
                                 20,    // discovery_slots
                                 20,    // control_slots
                                 1000,  // slot_duration_ms
                                 5000   // superframe_start_time
        );
    }

    Superframe superframe_;
};

/**
 * @brief Test default constructor
 */
TEST_F(SuperframeTest, DefaultConstructor) {
    Superframe default_superframe;

    EXPECT_EQ(default_superframe.total_slots, 100);
    EXPECT_EQ(default_superframe.data_slots, 60);
    EXPECT_EQ(default_superframe.discovery_slots, 20);
    EXPECT_EQ(default_superframe.control_slots, 20);
    EXPECT_EQ(default_superframe.slot_duration_ms, 1000);
    EXPECT_EQ(default_superframe.superframe_start_time, 0);
}

/**
 * @brief Test parameterized constructor
 */
TEST_F(SuperframeTest, ParameterizedConstructor) {
    EXPECT_EQ(superframe_.total_slots, 100);
    EXPECT_EQ(superframe_.data_slots, 60);
    EXPECT_EQ(superframe_.discovery_slots, 20);
    EXPECT_EQ(superframe_.control_slots, 20);
    EXPECT_EQ(superframe_.slot_duration_ms, 1000);
    EXPECT_EQ(superframe_.superframe_start_time, 5000);
}

/**
 * @brief Test Validate method with valid configuration
 */
TEST_F(SuperframeTest, ValidateValidConfiguration) {
    Result result = superframe_.Validate();
    EXPECT_TRUE(result.IsSuccess());
}

/**
 * @brief Test Validate method with invalid configurations
 */
TEST_F(SuperframeTest, ValidateInvalidConfigurations) {
    // Zero total slots
    Superframe zero_slots(0, 10, 10, 10, 1000);
    EXPECT_FALSE(zero_slots.Validate().IsSuccess());

    // Sum of slots exceeds total
    Superframe exceeds_total(100, 50, 30, 30, 1000);  // 50+30+30 = 110 > 100
    EXPECT_FALSE(exceeds_total.Validate().IsSuccess());

    // Invalid slot duration (too small)
    Superframe small_duration(100, 60, 20, 20, 5);
    EXPECT_FALSE(small_duration.Validate().IsSuccess());

    // Invalid slot duration (too large)
    Superframe large_duration(100, 60, 20, 20, 100000);
    EXPECT_FALSE(large_duration.Validate().IsSuccess());

    // Zero data slots
    Superframe zero_data(100, 0, 50, 50, 1000);
    EXPECT_TRUE(zero_data.Validate().IsSuccess());
}

/**
 * @brief Test GetSuperframeDuration
 */
TEST_F(SuperframeTest, GetSuperframeDuration) {
    uint32_t expected_duration = 100 * 1000;  // 100 slots * 1000ms each
    EXPECT_EQ(superframe_.GetSuperframeDuration(), expected_duration);
}

/**
 * @brief Test GetCurrentSlot
 */
TEST_F(SuperframeTest, GetCurrentSlot) {
    // Before superframe start
    EXPECT_EQ(superframe_.GetCurrentSlot(4000), 0);

    // At superframe start
    EXPECT_EQ(superframe_.GetCurrentSlot(5000), 0);

    // First slot
    EXPECT_EQ(superframe_.GetCurrentSlot(5999), 0);

    // Second slot
    EXPECT_EQ(superframe_.GetCurrentSlot(6000), 1);

    // Last slot in first cycle
    EXPECT_EQ(superframe_.GetCurrentSlot(104999), 99);

    // First slot in second cycle
    EXPECT_EQ(superframe_.GetCurrentSlot(105000), 0);
}

/**
 * @brief Test GetSlotStartTime and GetSlotEndTime
 */
TEST_F(SuperframeTest, GetSlotTimes) {
    // Slot 0
    EXPECT_EQ(superframe_.GetSlotStartTime(0), 5000);
    EXPECT_EQ(superframe_.GetSlotEndTime(0), 6000);

    // Slot 10
    EXPECT_EQ(superframe_.GetSlotStartTime(10), 15000);
    EXPECT_EQ(superframe_.GetSlotEndTime(10), 16000);

    // Test wraparound (slot number > total_slots)
    EXPECT_EQ(superframe_.GetSlotStartTime(150),
              superframe_.GetSlotStartTime(50));
}

/**
 * @brief Test IsNewSuperframe
 */
TEST_F(SuperframeTest, IsNewSuperframe) {
    // Before any superframe completion
    EXPECT_FALSE(superframe_.IsNewSuperframe(50000));

    // Just before completion
    EXPECT_FALSE(superframe_.IsNewSuperframe(104999));

    // At completion
    EXPECT_TRUE(superframe_.IsNewSuperframe(105000));

    // Well after completion
    EXPECT_TRUE(superframe_.IsNewSuperframe(200000));
}

/**
 * @brief Test AdvanceToNextSuperframe
 */
TEST_F(SuperframeTest, AdvanceToNextSuperframe) {
    uint32_t original_start = superframe_.superframe_start_time;
    uint32_t duration = superframe_.GetSuperframeDuration();

    // Advance to next superframe
    superframe_.AdvanceToNextSuperframe(120000);

    // Should advance by one complete superframe
    EXPECT_EQ(superframe_.superframe_start_time, original_start + 2 * duration);

    // If we're already past multiple superframes, should advance accordingly
    superframe_.AdvanceToNextSuperframe(350000);
    EXPECT_GT(superframe_.superframe_start_time, 300000);
}

/**
 * @brief Test GetSlotDistribution
 */
TEST_F(SuperframeTest, GetSlotDistribution) {
    auto [data_pct, discovery_pct, control_pct] =
        superframe_.GetSlotDistribution();

    EXPECT_FLOAT_EQ(data_pct, 60.0f);       // 60/100 * 100 = 60%
    EXPECT_FLOAT_EQ(discovery_pct, 20.0f);  // 20/100 * 100 = 20%
    EXPECT_FLOAT_EQ(control_pct, 20.0f);    // 20/100 * 100 = 20%

    // Test with zero total slots
    Superframe zero_total(0, 0, 0, 0, 1000);
    auto [zero_data, zero_discovery, zero_control] =
        zero_total.GetSlotDistribution();
    EXPECT_FLOAT_EQ(zero_data, 0.0f);
    EXPECT_FLOAT_EQ(zero_discovery, 0.0f);
    EXPECT_FLOAT_EQ(zero_control, 0.0f);
}

/**
 * @brief Test serialization and deserialization
 */
TEST_F(SuperframeTest, SerializationDeserialization) {
    // Serialize the superframe
    std::vector<uint8_t> buffer(Superframe::SerializedSize());
    utils::ByteSerializer serializer(buffer);

    Result result = superframe_.Serialize(serializer);
    ASSERT_TRUE(result.IsSuccess());

    // Deserialize the superframe
    utils::ByteDeserializer deserializer(buffer);
    auto deserialized_superframe = Superframe::Deserialize(deserializer);

    ASSERT_TRUE(deserialized_superframe.has_value());

    // Compare original and deserialized superframes
    EXPECT_EQ(superframe_, *deserialized_superframe);
}

/**
 * @brief Test deserialization with insufficient data
 */
TEST_F(SuperframeTest, DeserializationWithInsufficientData) {
    // Create buffer with insufficient data
    std::vector<uint8_t> buffer(10);  // Much smaller than required
    utils::ByteDeserializer deserializer(buffer);

    auto result = Superframe::Deserialize(deserializer);
    EXPECT_FALSE(result.has_value());
}

/**
 * @brief Test deserialization with invalid configuration
 */
TEST_F(SuperframeTest, DeserializationWithInvalidConfiguration) {
    // Create a superframe that would fail validation
    Superframe invalid_superframe(100, 80, 30, 30, 1000);  // Sum exceeds total

    std::vector<uint8_t> buffer(Superframe::SerializedSize());
    utils::ByteSerializer serializer(buffer);
    invalid_superframe.Serialize(serializer);

    utils::ByteDeserializer deserializer(buffer);
    auto result = Superframe::Deserialize(deserializer);

    // Should fail during validation
    EXPECT_FALSE(result.has_value());
}

/**
 * @brief Test equality operators
 */
TEST_F(SuperframeTest, EqualityOperators) {
    Superframe equal_superframe(100, 60, 20, 20, 1000, 5000);
    Superframe different_superframe(100, 60, 20, 20, 1000,
                                    6000);  // Different start time

    EXPECT_TRUE(superframe_ == equal_superframe);
    EXPECT_FALSE(superframe_ != equal_superframe);

    EXPECT_FALSE(superframe_ == different_superframe);
    EXPECT_TRUE(superframe_ != different_superframe);
}

/**
 * @brief Test utility functions
 */
TEST_F(SuperframeTest, UtilityFunctions) {
    // Test CreateDefaultSuperframe
    auto default_sf = superframe_utils::CreateDefaultSuperframe(200, 500);
    EXPECT_EQ(default_sf.total_slots, 200);
    EXPECT_EQ(default_sf.slot_duration_ms, 500);
    EXPECT_EQ(default_sf.data_slots, 120);      // 60% of 200
    EXPECT_EQ(default_sf.discovery_slots, 40);  // 20% of 200
    EXPECT_TRUE(default_sf.Validate().IsSuccess());

    // Test CreateOptimizedSuperframe for different network sizes
    auto small_network_sf = superframe_utils::CreateOptimizedSuperframe(3);
    auto medium_network_sf = superframe_utils::CreateOptimizedSuperframe(15);
    auto large_network_sf = superframe_utils::CreateOptimizedSuperframe(30);

    // Small networks should have more discovery slots proportionally
    auto [small_data, small_discovery, small_control] =
        small_network_sf.GetSlotDistribution();
    auto [large_data, large_discovery, large_control] =
        large_network_sf.GetSlotDistribution();

    EXPECT_LT(small_data,
              large_data);  // Small network has less data percentage
    EXPECT_GT(small_discovery,
              large_discovery);  // Small network has more discovery percentage

    // Test ValidateSlotDistribution
    std::string validation_msg =
        superframe_utils::ValidateSlotDistribution(superframe_);
    EXPECT_TRUE(validation_msg.empty());  // Should be valid

    // Test with unbalanced superframe
    Superframe unbalanced(100, 10, 5, 5, 1000);  // Very low percentages
    validation_msg = superframe_utils::ValidateSlotDistribution(unbalanced);
    EXPECT_FALSE(validation_msg.empty());  // Should have warnings

    // Test CalculateOptimalSlotDuration
    uint32_t optimal_duration =
        superframe_utils::CalculateOptimalSlotDuration(255,   // max_packet_size
                                                       9600,  // data_rate_bps
                                                       100    // guard_time_ms
        );

    EXPECT_GT(optimal_duration, 100);     // Should be greater than guard time
    EXPECT_EQ(optimal_duration % 10, 0);  // Should be rounded to 10ms
}

}  // namespace test
}  // namespace lora_mesh
}  // namespace protocols
}  // namespace types
}  // namespace loramesher