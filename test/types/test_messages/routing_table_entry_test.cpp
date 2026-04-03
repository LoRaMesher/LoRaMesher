/**
 * @file routing_table_entry_test.cpp
 * @brief Unit tests for RoutingTableEntry struct
 */

#include <gtest/gtest.h>

#include "types/messages/loramesher/routing_table_entry.hpp"
#include "utils/byte_operations.h"

namespace loramesher {
namespace test {

class RoutingTableEntryTest : public ::testing::Test {
   protected:
    static constexpr AddressType kDest = 0x1234;
    static constexpr uint8_t kHops = 3;
    static constexpr uint8_t kQuality = 200;
    static constexpr uint8_t kDataSlots = 4;
    static constexpr uint8_t kCaps = 0x07;
    static constexpr uint8_t kCtrlSlot = 2;
};

TEST_F(RoutingTableEntryTest, DefaultConstructor) {
    RoutingTableEntry entry;
    EXPECT_EQ(entry.destination, 0u);
    EXPECT_EQ(entry.hop_count, 0u);
    EXPECT_EQ(entry.link_quality, 0u);
    EXPECT_EQ(entry.allocated_data_slots, 0u);
    EXPECT_EQ(entry.capabilities, 0u);
    EXPECT_EQ(entry.control_slot_index, 0xFF);
    EXPECT_EQ(entry.reception_quality, 0u);
    EXPECT_EQ(entry.next_hop, 0u);
}

TEST_F(RoutingTableEntryTest, ParameterizedConstructor) {
    RoutingTableEntry entry(kDest, kHops, kQuality, kDataSlots, kCaps,
                            kCtrlSlot);
    EXPECT_EQ(entry.destination, kDest);
    EXPECT_EQ(entry.hop_count, kHops);
    EXPECT_EQ(entry.link_quality, kQuality);
    EXPECT_EQ(entry.allocated_data_slots, kDataSlots);
    EXPECT_EQ(entry.capabilities, kCaps);
    EXPECT_EQ(entry.control_slot_index, kCtrlSlot);
}

TEST_F(RoutingTableEntryTest, ConstructorDefaultCaps) {
    RoutingTableEntry entry(kDest, kHops, kQuality, kDataSlots);
    EXPECT_EQ(entry.capabilities, 0u);
    EXPECT_EQ(entry.control_slot_index, 0xFF);
}

TEST_F(RoutingTableEntryTest, SizeConstant) {
    EXPECT_EQ(RoutingTableEntry::Size(), 10u);  // 2 + 1 + 1 + 1 + 1 + 1 + 1 + 2
}

TEST_F(RoutingTableEntryTest, SerializeDeserializeRoundTrip) {
    RoutingTableEntry original(kDest, kHops, kQuality, kDataSlots, kCaps,
                               kCtrlSlot);
    original.reception_quality = 42;
    original.next_hop = 0x5678;

    std::vector<uint8_t> buf(RoutingTableEntry::Size());
    utils::ByteSerializer ser(buf);
    Result result = original.Serialize(ser);
    ASSERT_TRUE(result.IsSuccess());

    utils::ByteDeserializer deser(buf);
    auto deserialized = RoutingTableEntry::Deserialize(deser);
    ASSERT_TRUE(deserialized.has_value());

    EXPECT_EQ(deserialized->destination, kDest);
    EXPECT_EQ(deserialized->hop_count, kHops);
    EXPECT_EQ(deserialized->link_quality, kQuality);
    EXPECT_EQ(deserialized->allocated_data_slots, kDataSlots);
    EXPECT_EQ(deserialized->capabilities, kCaps);
    EXPECT_EQ(deserialized->control_slot_index, kCtrlSlot);
    EXPECT_EQ(deserialized->reception_quality, 42u);
    EXPECT_EQ(deserialized->next_hop, 0x5678u);
}

TEST_F(RoutingTableEntryTest, DeserializeInsufficientData) {
    std::vector<uint8_t> short_buf(3);
    utils::ByteDeserializer deser(short_buf);
    auto result = RoutingTableEntry::Deserialize(deser);
    EXPECT_FALSE(result.has_value());
}

TEST_F(RoutingTableEntryTest, EdgeCaseMaxValues) {
    RoutingTableEntry entry(0xFFFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);
    entry.reception_quality = 0xFF;
    entry.next_hop = 0xFFFF;

    std::vector<uint8_t> buf(RoutingTableEntry::Size());
    utils::ByteSerializer ser(buf);
    ASSERT_TRUE(entry.Serialize(ser).IsSuccess());

    utils::ByteDeserializer deser(buf);
    auto result = RoutingTableEntry::Deserialize(deser);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->destination, 0xFFFF);
    EXPECT_EQ(result->hop_count, 0xFF);
    EXPECT_EQ(result->link_quality, 0xFF);
    EXPECT_EQ(result->reception_quality, 0xFF);
    EXPECT_EQ(result->next_hop, 0xFFFF);
}

TEST_F(RoutingTableEntryTest, SerializedSizeMatchesActual) {
    RoutingTableEntry entry(kDest, kHops, kQuality, kDataSlots, kCaps);
    std::vector<uint8_t> buf(100);
    utils::ByteSerializer ser(buf);
    entry.Serialize(ser);
    EXPECT_EQ(ser.getOffset(), RoutingTableEntry::Size());
}

TEST_F(RoutingTableEntryTest, UnassignedControlSlotDefault) {
    RoutingTableEntry entry(kDest, kHops, kQuality, kDataSlots);
    // Default control slot index is 0xFF (unassigned)
    EXPECT_EQ(entry.control_slot_index, 0xFF);

    std::vector<uint8_t> buf(RoutingTableEntry::Size());
    utils::ByteSerializer ser(buf);
    entry.Serialize(ser);

    utils::ByteDeserializer deser(buf);
    auto result = RoutingTableEntry::Deserialize(deser);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->control_slot_index, 0xFF);
}

}  // namespace test
}  // namespace loramesher
