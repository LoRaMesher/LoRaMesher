/**
 * @file slot_allocation_test.cpp
 * @brief Unit tests for SlotAllocation class
 */

#include <gtest/gtest.h>

#include "types/protocols/lora_mesh/slot_allocation.hpp"
#include "utils/byte_operations.h"

namespace loramesher {
namespace types {
namespace protocols {
namespace lora_mesh {
namespace test {

/**
 * @brief Test fixture for SlotAllocation tests
 */
class SlotAllocationTest : public ::testing::Test {
   protected:
    AddressType target_address_ = 0x1234;  // Example target address

    void SetUp() override {
        // Create sample slot allocations for testing
        tx_slot_ =
            SlotAllocation(10, SlotAllocation::SlotType::TX, target_address_);
        rx_slot_ = SlotAllocation(11, SlotAllocation::SlotType::RX);
        sleep_slot_ = SlotAllocation(12, SlotAllocation::SlotType::SLEEP);
        discovery_tx_slot_ =
            SlotAllocation(13, SlotAllocation::SlotType::DISCOVERY_TX);
        control_rx_slot_ =
            SlotAllocation(14, SlotAllocation::SlotType::CONTROL_RX);
    }

    SlotAllocation tx_slot_;
    SlotAllocation rx_slot_;
    SlotAllocation sleep_slot_;
    SlotAllocation discovery_tx_slot_;
    SlotAllocation control_rx_slot_;
};

/**
 * @brief Test default constructor
 */
TEST_F(SlotAllocationTest, DefaultConstructor) {
    SlotAllocation default_slot;

    EXPECT_EQ(default_slot.slot_number, 0);
    EXPECT_EQ(default_slot.target_address, 0);
    EXPECT_EQ(default_slot.type, SlotAllocation::SlotType::SLEEP);
}

/**
 * @brief Test parameterized constructor
 */
TEST_F(SlotAllocationTest, ParameterizedConstructor) {
    EXPECT_EQ(tx_slot_.slot_number, 10);
    EXPECT_EQ(tx_slot_.type, SlotAllocation::SlotType::TX);
    EXPECT_EQ(tx_slot_.target_address, 0x1234);

    EXPECT_EQ(rx_slot_.slot_number, 11);
    EXPECT_EQ(rx_slot_.type, SlotAllocation::SlotType::RX);
    EXPECT_EQ(rx_slot_.target_address, 0);  // Default target
}

/**
 * @brief Test IsTxSlot method
 */
TEST_F(SlotAllocationTest, IsTxSlot) {
    EXPECT_TRUE(tx_slot_.IsTxSlot());
    EXPECT_TRUE(discovery_tx_slot_.IsTxSlot());

    EXPECT_FALSE(rx_slot_.IsTxSlot());
    EXPECT_FALSE(control_rx_slot_.IsTxSlot());
    EXPECT_FALSE(sleep_slot_.IsTxSlot());
}

/**
 * @brief Test IsRxSlot method
 */
TEST_F(SlotAllocationTest, IsRxSlot) {
    EXPECT_TRUE(rx_slot_.IsRxSlot());
    EXPECT_TRUE(control_rx_slot_.IsRxSlot());

    EXPECT_FALSE(tx_slot_.IsRxSlot());
    EXPECT_FALSE(discovery_tx_slot_.IsRxSlot());
    EXPECT_FALSE(sleep_slot_.IsRxSlot());
}

/**
 * @brief Test IsControlSlot method
 */
TEST_F(SlotAllocationTest, IsControlSlot) {
    SlotAllocation control_tx_slot(20, SlotAllocation::SlotType::CONTROL_TX);

    EXPECT_TRUE(control_rx_slot_.IsControlSlot());
    EXPECT_TRUE(control_tx_slot.IsControlSlot());

    EXPECT_FALSE(tx_slot_.IsControlSlot());
    EXPECT_FALSE(rx_slot_.IsControlSlot());
    EXPECT_FALSE(discovery_tx_slot_.IsControlSlot());
    EXPECT_FALSE(sleep_slot_.IsControlSlot());
}

/**
 * @brief Test IsDiscoverySlot method
 */
TEST_F(SlotAllocationTest, IsDiscoverySlot) {
    SlotAllocation discovery_rx_slot(21,
                                     SlotAllocation::SlotType::DISCOVERY_RX);

    EXPECT_TRUE(discovery_tx_slot_.IsDiscoverySlot());
    EXPECT_TRUE(discovery_rx_slot.IsDiscoverySlot());

    EXPECT_FALSE(tx_slot_.IsDiscoverySlot());
    EXPECT_FALSE(rx_slot_.IsDiscoverySlot());
    EXPECT_FALSE(control_rx_slot_.IsDiscoverySlot());
    EXPECT_FALSE(sleep_slot_.IsDiscoverySlot());
}

/**
 * @brief Test GetTypeString method
 */
TEST_F(SlotAllocationTest, GetTypeString) {
    EXPECT_EQ(tx_slot_.GetTypeString(), "TX");
    EXPECT_EQ(rx_slot_.GetTypeString(), "RX");
    EXPECT_EQ(sleep_slot_.GetTypeString(), "SLEEP");
    EXPECT_EQ(discovery_tx_slot_.GetTypeString(), "DISCOVERY_TX");
    EXPECT_EQ(control_rx_slot_.GetTypeString(), "CONTROL_RX");
}

/**
 * @brief Test serialization and deserialization
 */
TEST_F(SlotAllocationTest, SerializationDeserialization) {
    // Serialize the slot allocation
    std::vector<uint8_t> buffer(SlotAllocation::SerializedSize());
    utils::ByteSerializer serializer(buffer);

    Result result = tx_slot_.Serialize(serializer);
    ASSERT_TRUE(result.IsSuccess());

    // Deserialize the slot allocation
    utils::ByteDeserializer deserializer(buffer);
    auto deserialized_slot = SlotAllocation::Deserialize(deserializer);

    ASSERT_TRUE(deserialized_slot.has_value());

    // Compare original and deserialized slots
    EXPECT_EQ(tx_slot_, *deserialized_slot);
    EXPECT_EQ(tx_slot_.slot_number, deserialized_slot->slot_number);
    EXPECT_EQ(tx_slot_.type, deserialized_slot->type);
    EXPECT_EQ(tx_slot_.target_address, deserialized_slot->target_address);
}

/**
 * @brief Test deserialization with invalid slot type
 */
TEST_F(SlotAllocationTest, DeserializationWithInvalidSlotType) {
    // Create buffer with invalid slot type
    std::vector<uint8_t> buffer = {
        0x10, 0x00,  // slot_number = 16
        0xFF,        // invalid slot type
        0x34, 0x12   // target_address = 0x1234
    };

    utils::ByteDeserializer deserializer(buffer);
    auto result = SlotAllocation::Deserialize(deserializer);

    EXPECT_FALSE(result.has_value());
}

/**
 * @brief Test equality operators
 */
TEST_F(SlotAllocationTest, EqualityOperators) {
    SlotAllocation equal_slot(10, SlotAllocation::SlotType::TX, 0x1234);
    SlotAllocation different_slot(10, SlotAllocation::SlotType::RX, 0x1234);

    EXPECT_TRUE(tx_slot_ == equal_slot);
    EXPECT_FALSE(tx_slot_ != equal_slot);

    EXPECT_FALSE(tx_slot_ == different_slot);
    EXPECT_TRUE(tx_slot_ != different_slot);
}

/**
 * @brief Test less than operator for sorting
 */
TEST_F(SlotAllocationTest, LessThanOperator) {
    SlotAllocation earlier_slot(5, SlotAllocation::SlotType::TX);
    SlotAllocation later_slot(15, SlotAllocation::SlotType::RX);

    EXPECT_TRUE(earlier_slot < tx_slot_);   // 5 < 10
    EXPECT_FALSE(tx_slot_ < earlier_slot);  // 10 > 5
    EXPECT_TRUE(tx_slot_ < later_slot);     // 10 < 15
    EXPECT_FALSE(later_slot < tx_slot_);    // 15 > 10
}

/**
 * @brief Test utility functions
 */
TEST_F(SlotAllocationTest, UtilityFunctions) {
    // Test SlotTypeToString
    EXPECT_EQ(slot_utils::SlotTypeToString(SlotAllocation::SlotType::TX), "TX");
    EXPECT_EQ(slot_utils::SlotTypeToString(SlotAllocation::SlotType::RX), "RX");
    EXPECT_EQ(slot_utils::SlotTypeToString(SlotAllocation::SlotType::SLEEP),
              "SLEEP");

    // Test StringToSlotType
    auto tx_type = slot_utils::StringToSlotType("TX");
    ASSERT_TRUE(tx_type.has_value());
    EXPECT_EQ(*tx_type, SlotAllocation::SlotType::TX);

    auto invalid_type = slot_utils::StringToSlotType("INVALID");
    EXPECT_FALSE(invalid_type.has_value());

    // Test IsValidSlotType
    EXPECT_TRUE(slot_utils::IsValidSlotType(SlotAllocation::SlotType::TX));
    EXPECT_TRUE(
        slot_utils::IsValidSlotType(SlotAllocation::SlotType::CONTROL_RX));

    // Test with cast from invalid uint8_t value
    SlotAllocation::SlotType invalid_cast =
        static_cast<SlotAllocation::SlotType>(0xFF);
    EXPECT_FALSE(slot_utils::IsValidSlotType(invalid_cast));
}

/**
 * @brief Test SerializedSize constant
 */
TEST_F(SlotAllocationTest, SerializedSize) {
    // Test that the constant matches actual serialized size
    std::vector<uint8_t> buffer(100);  // Large enough buffer
    utils::ByteSerializer serializer(buffer);

    tx_slot_.Serialize(serializer);
    size_t actual_size = serializer.getOffset();

    EXPECT_EQ(SlotAllocation::SerializedSize(), actual_size);
}

}  // namespace test
}  // namespace lora_mesh
}  // namespace protocols
}  // namespace types
}  // namespace loramesher