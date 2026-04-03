/**
 * @file routing_table_message_test.cpp
 * @brief Unit tests for RoutingTableMessage class
 */

#include <gtest/gtest.h>

#include <memory>

#include "types/messages/base_header.hpp"
#include "types/messages/loramesher/routing_table_header.hpp"
#include "types/messages/loramesher/routing_table_message.hpp"
#include "utils/byte_operations.h"

namespace loramesher {
namespace test {

/**
 * @brief Test fixture for RoutingTableMessage tests
 */
class RoutingTableMessageTest : public ::testing::Test {
   protected:
    // Common test data
    static constexpr AddressType dest = 0x1234;
    static constexpr AddressType src = 0x5678;
    static constexpr uint16_t network_id = 0x9ABC;
    static constexpr uint8_t table_version = 3;

    // Test routing entries
    std::vector<RoutingTableEntry> entries;

    std::unique_ptr<RoutingTableMessage> msg_ptr;

    void SetUp() override {
        SetupEntries();
        CreateMessage();
    }

    void SetupEntries() {
        // Create a few test routing entries
        RoutingTableEntry direct(0x1111, 1, 90, 2);
        direct.reception_quality = 90;  // Direct neighbor has reception quality
        entries.push_back(direct);
        entries.push_back(
            RoutingTableEntry(0x2222, 2, 70, 3));  // 2 hops, good quality
        entries.push_back(
            RoutingTableEntry(0x3333, 3, 50, 1));  // 3 hops, medium quality
    }

    void CreateMessage() {
        auto opt_msg = RoutingTableMessage::Create(dest, src, network_id,
                                                   table_version, entries);
        ASSERT_TRUE(opt_msg.has_value()) << "Failed to create test message";
        msg_ptr = std::make_unique<RoutingTableMessage>(*opt_msg);
    }
};

/**
 * @brief Test creating a RoutingTableMessage with valid parameters
 */
TEST_F(RoutingTableMessageTest, CreationTest) {
    // Given: Test parameters
    const AddressType test_dest = 0xABCD;
    const AddressType test_src = 0xDCBA;
    const uint16_t test_network_id = 0x6789;
    const uint8_t test_version = 10;

    std::vector<RoutingTableEntry> test_entries;
    test_entries.push_back(RoutingTableEntry(0x4444, 1, 85, 2));
    test_entries.push_back(RoutingTableEntry(0x5555, 2, 75, 1));

    // When: Creating a message
    auto opt_msg = RoutingTableMessage::Create(
        test_dest, test_src, test_network_id, test_version, test_entries);

    // Then: Message creation should succeed
    ASSERT_TRUE(opt_msg.has_value()) << "Failed to create RoutingTable message";

    // And: Message should have correct fields
    EXPECT_EQ(opt_msg->GetDestination(), test_dest);
    EXPECT_EQ(opt_msg->GetSource(), test_src);
    EXPECT_EQ(opt_msg->GetNetworkManager(), test_network_id);
    EXPECT_EQ(opt_msg->GetTableVersion(), test_version);

    // Check entries
    const auto& result_entries = opt_msg->GetEntries();
    ASSERT_EQ(result_entries.size(), test_entries.size());

    for (size_t i = 0; i < test_entries.size(); i++) {
        EXPECT_EQ(result_entries[i].destination, test_entries[i].destination);
        EXPECT_EQ(result_entries[i].hop_count, test_entries[i].hop_count);
        EXPECT_EQ(result_entries[i].link_quality, test_entries[i].link_quality);
        EXPECT_EQ(result_entries[i].allocated_data_slots,
                  test_entries[i].allocated_data_slots);
    }
}

/**
 * @brief Test creation failure with too many entries
 *
 * kMaxRoutingEntries is 30. Creating a message with 31 entries must return
 * nullopt (exercises routing_table_message.cpp lines 58-62).
 */
TEST_F(RoutingTableMessageTest, TooManyEntriesTest) {
    // Build a vector with one more entry than the maximum
    std::vector<RoutingTableEntry> too_many;
    for (int i = 0;
         i <= static_cast<int>(RoutingTableMessage::kMaxRoutingEntries); ++i) {
        too_many.push_back(
            RoutingTableEntry(static_cast<AddressType>(0x1000 + i), 1, 80, 1));
    }
    ASSERT_GT(too_many.size(),
              static_cast<size_t>(RoutingTableMessage::kMaxRoutingEntries));

    auto opt_msg = RoutingTableMessage::Create(dest, src, network_id,
                                               table_version, too_many);
    EXPECT_FALSE(opt_msg.has_value()) << "Create() should return nullopt when "
                                         "entries exceed kMaxRoutingEntries";
}

/**
 * @brief Test creating a RoutingTableMessage with empty entries
 */
TEST_F(RoutingTableMessageTest, EmptyEntriesTest) {
    // When: Creating a message with no entries
    std::vector<RoutingTableEntry> empty_entries;
    auto opt_msg = RoutingTableMessage::Create(dest, src, network_id,
                                               table_version, empty_entries);

    // Then: Message creation should succeed
    ASSERT_TRUE(opt_msg.has_value())
        << "Failed to create RoutingTable message with empty entries";

    // And: Entries should be empty
    EXPECT_TRUE(opt_msg->GetEntries().empty());
}

/**
 * @brief Test serializing a RoutingTableMessage
 */
TEST_F(RoutingTableMessageTest, SerializationTest) {
    // Given: A valid message
    ASSERT_TRUE(msg_ptr != nullptr);

    // When: Serializing the message
    std::optional<std::vector<uint8_t>> opt_serialized = msg_ptr->Serialize();
    ASSERT_TRUE(opt_serialized.has_value()) << "Serialization failed";

    const std::vector<uint8_t>& serialized = *opt_serialized;

    // Then: Check size (BaseHeader + RoutingTable fields + entries)
    const size_t expected_size = BaseHeader::Size() +
                                 RoutingTableHeader::RoutingTableFieldsSize() +
                                 (entries.size() * RoutingTableEntry::Size());
    ASSERT_EQ(serialized.size(), expected_size) << "Incorrect serialized size";
}

/**
 * @brief Test deserializing a RoutingTableMessage
 */
TEST_F(RoutingTableMessageTest, DeserializationTest) {
    // Given: A serialized message
    auto opt_serialized = msg_ptr->Serialize();
    ASSERT_TRUE(opt_serialized.has_value()) << "Failed to serialize message";

    // When: Deserializing the message
    auto opt_deserialized =
        RoutingTableMessage::CreateFromSerialized(*opt_serialized);
    ASSERT_TRUE(opt_deserialized.has_value())
        << "Failed to deserialize message";

    RoutingTableMessage deserialized_msg = std::move(*opt_deserialized);

    // Then: Verify header fields
    EXPECT_EQ(deserialized_msg.GetDestination(), dest);
    EXPECT_EQ(deserialized_msg.GetSource(), src);
    EXPECT_EQ(deserialized_msg.GetNetworkManager(), network_id);
    EXPECT_EQ(deserialized_msg.GetTableVersion(), table_version);

    // And: Verify entries
    const auto& result_entries = deserialized_msg.GetEntries();
    ASSERT_EQ(result_entries.size(), entries.size());

    for (size_t i = 0; i < entries.size(); i++) {
        EXPECT_EQ(result_entries[i].destination, entries[i].destination);
        EXPECT_EQ(result_entries[i].hop_count, entries[i].hop_count);
        EXPECT_EQ(result_entries[i].link_quality, entries[i].link_quality);
        EXPECT_EQ(result_entries[i].allocated_data_slots,
                  entries[i].allocated_data_slots);
    }
}

/**
 * @brief Test deserializing a RoutingTableMessage with invalid data
 */
TEST_F(RoutingTableMessageTest, DeserializationFailureTest) {
    // Test: Empty data
    {
        std::vector<uint8_t> empty_data;
        auto result = RoutingTableMessage::CreateFromSerialized(empty_data);
        EXPECT_FALSE(result.has_value()) << "Should fail with empty data";
    }

    // Test: Incomplete header
    {
        std::vector<uint8_t> incomplete_data(BaseHeader::Size() - 1, 0);
        auto result =
            RoutingTableMessage::CreateFromSerialized(incomplete_data);
        EXPECT_FALSE(result.has_value())
            << "Should fail with incomplete header";
    }

    // Test: Missing entries
    {
        auto opt_serialized = msg_ptr->Serialize();
        ASSERT_TRUE(opt_serialized.has_value());

        // Truncate the data to include header but not all entries
        std::vector<uint8_t> truncated_data(
            opt_serialized->begin(),
            opt_serialized->begin() + RoutingTableHeader::Size() + 2);

        auto result = RoutingTableMessage::CreateFromSerialized(truncated_data);
        EXPECT_FALSE(result.has_value()) << "Should fail with missing entries";
    }

    // Test: Wrong message type
    {
        auto opt_serialized = msg_ptr->Serialize();
        ASSERT_TRUE(opt_serialized.has_value());

        std::vector<uint8_t> invalid_type = *opt_serialized;
        // Change the message type byte (typically at index 4)
        invalid_type[4] = static_cast<uint8_t>(MessageType::DATA_MSG);

        auto result = RoutingTableMessage::CreateFromSerialized(invalid_type);
        EXPECT_FALSE(result.has_value())
            << "Should fail with invalid message type";
    }
}

/**
 * @brief Test conversion to BaseMessage
 */
TEST_F(RoutingTableMessageTest, ConversionToBaseMessageTest) {
    // Given: A RoutingTable message
    ASSERT_TRUE(msg_ptr != nullptr);

    // When: Converting to BaseMessage
    BaseMessage base_msg = msg_ptr->ToBaseMessage();

    // Then: Base message should have correct header fields
    EXPECT_EQ(base_msg.GetHeader().GetDestination(), dest);
    EXPECT_EQ(base_msg.GetHeader().GetSource(), src);
    EXPECT_EQ(base_msg.GetHeader().GetType(), MessageType::ROUTE_TABLE);

    // And: Payload should contain RoutingTable fields + entries
    auto payload = base_msg.GetPayload();

    // Payload should have 4 bytes header (network_id, version, entry_count) plus entry data
    const size_t expected_payload_size = msg_ptr->GetTotalPayloadSize();
    ASSERT_EQ(payload.size(), expected_payload_size);

    // Network ID should be in the first two bytes (assuming little endian)
    uint16_t extracted_network_id =
        (static_cast<uint16_t>(payload[1]) << 8) | payload[0];
    EXPECT_EQ(extracted_network_id, network_id);

    // Version is the third byte
    EXPECT_EQ(payload[2], table_version);

    // Entry count is the fourth byte
    EXPECT_EQ(payload[3], static_cast<uint8_t>(entries.size()));
}

/**
 * @brief Test GetTotalSize() returns the correct value
 */
TEST_F(RoutingTableMessageTest, GetTotalPayloadSizeTest) {
    // Given: A RoutingTable message
    ASSERT_TRUE(msg_ptr != nullptr);

    // When: Getting the total size
    size_t total_size = msg_ptr->GetTotalPayloadSize();

    // Then: Size should be correct
    const size_t expected_size = RoutingTableHeader::RoutingTableFieldsSize() +
                                 (entries.size() * RoutingTableEntry::Size());
    EXPECT_EQ(total_size, expected_size);
}

/**
 * @brief Test retrieving the header directly
 */
TEST_F(RoutingTableMessageTest, GetHeaderTest) {
    // Given: A RoutingTable message
    ASSERT_TRUE(msg_ptr != nullptr);

    // When: Getting the header
    const RoutingTableHeader& header = msg_ptr->GetHeader();

    // Then: Header should have correct values
    EXPECT_EQ(header.GetDestination(), dest);
    EXPECT_EQ(header.GetSource(), src);
    EXPECT_EQ(header.GetType(), MessageType::ROUTE_TABLE);
    EXPECT_EQ(header.GetNetworkManager(), network_id);
    EXPECT_EQ(header.GetTableVersion(), table_version);
    EXPECT_EQ(header.GetEntryCount(), static_cast<uint8_t>(entries.size()));
}

/**
 * @brief Test deserialization failure when entry_count in header exceeds max
 *
 * Manually patches the entry_count byte in a valid serialized buffer to a
 * value larger than kMaxRoutingEntries (30), then verifies that
 * CreateFromSerialized returns nullopt
 * (exercises routing_table_message.cpp lines 105-109).
 */
TEST_F(RoutingTableMessageTest, DeserializeTooManyEntriesTest) {
    // Start from a valid serialized message
    auto opt_serialized = msg_ptr->Serialize();
    ASSERT_TRUE(opt_serialized.has_value())
        << "Setup: serialization must succeed";

    std::vector<uint8_t> bad_data = *opt_serialized;

    // The entry_count field sits at:
    //   BaseHeader::Size() (6) + sizeof(AddressType) (2) + sizeof(uint8_t version) (1) = index 9
    const size_t entry_count_offset =
        BaseHeader::Size() + sizeof(AddressType) + sizeof(uint8_t);
    ASSERT_LT(entry_count_offset, bad_data.size());

    // Write a value that exceeds kMaxRoutingEntries
    bad_data[entry_count_offset] =
        static_cast<uint8_t>(RoutingTableMessage::kMaxRoutingEntries + 1);

    auto result = RoutingTableMessage::CreateFromSerialized(bad_data);
    EXPECT_FALSE(result.has_value())
        << "CreateFromSerialized() must return nullopt when entry_count "
           "exceeds kMaxRoutingEntries";
}

// ---------------------------------------------------------------------------
// RoutingTableHeader-specific coverage
// (src/types/messages/loramesher/routing_table_header.cpp)
// ---------------------------------------------------------------------------

/**
 * @brief RoutingTableHeader::SetRoutingTableInfo() updates fields.
 *
 * Exercises routing_table_header.cpp lines 26-34.
 */
TEST_F(RoutingTableMessageTest, SetRoutingTableInfo) {
    ASSERT_NE(msg_ptr, nullptr);

    // Grab a mutable header copy via the message
    RoutingTableHeader hdr(dest, src, network_id, table_version,
                           static_cast<uint8_t>(entries.size()));

    const AddressType new_nm = 0xDEAD;
    const uint8_t new_version = 99;
    const uint8_t new_count = 7;
    Result r = hdr.SetRoutingTableInfo(new_nm, new_version, new_count);
    EXPECT_TRUE(r.IsSuccess());
    EXPECT_EQ(hdr.GetNetworkManager(), new_nm);
    EXPECT_EQ(hdr.GetTableVersion(), new_version);
    EXPECT_EQ(hdr.GetEntryCount(), new_count);
}

/**
 * @brief RoutingTableHeader getters for source capabilities and
 *        source_allocated_data_slots — lines covered when accessed.
 */
TEST_F(RoutingTableMessageTest, SourceCapabilitiesAndSlots) {
    const uint8_t caps = 0x42;
    const uint8_t slots = 3;
    RoutingTableHeader hdr(dest, src, network_id, table_version,
                           static_cast<uint8_t>(entries.size()), caps, slots);

    EXPECT_EQ(hdr.GetSourceCapabilities(), caps);
    EXPECT_EQ(hdr.GetSourceAllocatedDataSlots(), slots);
}

/**
 * @brief RoutingTableHeader::Deserialize() success path via serialized buffer.
 *
 * Exercises routing_table_header.cpp Deserialize() lines 53-91.
 */
TEST_F(RoutingTableMessageTest, HeaderDeserializeSuccess) {
    // Build a valid buffer: serialize the header directly
    RoutingTableHeader hdr(dest, src, network_id, table_version,
                           static_cast<uint8_t>(entries.size()), 0x10, 2);

    std::vector<uint8_t> buf(hdr.GetSize(), 0);
    utils::ByteSerializer ser(buf);
    Result r = hdr.Serialize(ser);
    ASSERT_TRUE(r.IsSuccess());

    // Now deserialize
    utils::ByteDeserializer deser(
        std::span<const uint8_t>(buf.data(), buf.size()));
    auto opt = RoutingTableHeader::Deserialize(deser);
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->GetNetworkManager(), network_id);
    EXPECT_EQ(opt->GetTableVersion(), table_version);
    EXPECT_EQ(opt->GetEntryCount(), static_cast<uint8_t>(entries.size()));
    EXPECT_EQ(opt->GetSourceCapabilities(), 0x10u);
    EXPECT_EQ(opt->GetSourceAllocatedDataSlots(), 2u);
}

/**
 * @brief RoutingTableHeader::Deserialize() fails on wrong message type.
 *
 * Exercises routing_table_header.cpp lines 64-68 (type-mismatch branch).
 */
TEST_F(RoutingTableMessageTest, HeaderDeserializeWrongType) {
    // Serialize a PING header (wrong type)
    BaseHeader wrong_type_hdr(dest, src, MessageType::PING, 0);
    std::vector<uint8_t> buf(BaseHeader::Size(), 0);
    utils::ByteSerializer ser(buf);
    wrong_type_hdr.Serialize(ser);

    utils::ByteDeserializer deser(
        std::span<const uint8_t>(buf.data(), buf.size()));
    auto opt = RoutingTableHeader::Deserialize(deser);
    EXPECT_FALSE(opt.has_value());
}

/**
 * @brief RoutingTableHeader::Deserialize() fails on truncated buffer
 *        (missing fields after base header).
 *
 * Exercises routing_table_header.cpp lines 77-82 (missing fields branch).
 */
TEST_F(RoutingTableMessageTest, HeaderDeserializeTruncated) {
    // Valid base header bytes but no routing-table-specific fields
    BaseHeader base_hdr(dest, src, MessageType::ROUTE_TABLE, 0);
    std::vector<uint8_t> buf(BaseHeader::Size(), 0);
    utils::ByteSerializer ser(buf);
    base_hdr.Serialize(ser);

    utils::ByteDeserializer deser(
        std::span<const uint8_t>(buf.data(), buf.size()));
    auto opt = RoutingTableHeader::Deserialize(deser);
    EXPECT_FALSE(opt.has_value());
}

/**
 * @brief RoutingTableHeader::GetSize() returns the correct value.
 */
TEST_F(RoutingTableMessageTest, GetSize) {
    RoutingTableHeader hdr(dest, src, network_id, table_version, 0);
    EXPECT_EQ(hdr.GetSize(), BaseHeader::Size() +
                                 RoutingTableHeader::RoutingTableFieldsSize());
}

// =============================================================================
// CreateFromBaseMessage — factory method (replaces throwing constructor)
// =============================================================================

TEST_F(RoutingTableMessageTest, CreateFromBaseMessageSucceeds) {
    ASSERT_TRUE(msg_ptr != nullptr);

    BaseMessage base = msg_ptr->ToBaseMessage();
    ASSERT_EQ(base.GetType(), MessageType::ROUTE_TABLE);

    auto result = RoutingTableMessage::CreateFromBaseMessage(base);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->GetDestination(), dest);
    EXPECT_EQ(result->GetSource(), src);
    EXPECT_EQ(result->GetEntries().size(), entries.size());
    EXPECT_EQ(result->GetNetworkManager(), network_id);
    EXPECT_EQ(result->GetTableVersion(), table_version);
}

TEST_F(RoutingTableMessageTest, CreateFromBaseMessageWrongTypeReturnsNullopt) {
    std::array<uint8_t, 4> payload{0x01, 0x02, 0x03, 0x04};
    BaseMessage wrong_type_msg(dest, src, MessageType::DATA,
                               std::span<const uint8_t>(payload));

    auto result = RoutingTableMessage::CreateFromBaseMessage(wrong_type_msg);
    EXPECT_FALSE(result.has_value());
}

TEST_F(RoutingTableMessageTest, CreateFromBaseMessageTruncatedReturnsNullopt) {
    ASSERT_TRUE(msg_ptr != nullptr);

    // Serialize the message, then truncate so only 1 of 3 entries fits
    auto serialized = msg_ptr->Serialize();
    ASSERT_TRUE(serialized.has_value());

    size_t truncated_size = BaseHeader::Size() +
                            RoutingTableHeader::RoutingTableFieldsSize() +
                            1 * RoutingTableEntry::Size();
    ASSERT_LT(truncated_size, serialized->size());

    std::vector<uint8_t> truncated(serialized->begin(),
                                   serialized->begin() + truncated_size);

    // Reconstruct a BaseMessage from truncated data
    auto truncated_base =
        BaseMessage::CreateFromSerialized(std::span<const uint8_t>(truncated));
    // BaseMessage::CreateFromSerialized checks payload_size vs buffer, so this
    // will fail because the header's payload_size field claims more bytes than
    // available.  Build the BaseMessage manually instead.
    // Manually construct: use the truncated payload with correct payload_size.
    size_t truncated_payload_size = truncated_size - BaseHeader::Size();
    BaseMessage manual_msg(
        dest, src, MessageType::ROUTE_TABLE,
        std::span<const uint8_t>(truncated.data() + BaseHeader::Size(),
                                 truncated_payload_size));

    auto result = RoutingTableMessage::CreateFromBaseMessage(manual_msg);
    EXPECT_FALSE(result.has_value());
}

TEST_F(RoutingTableMessageTest, CreateFromSerializedEntryCountExceedsBuffer) {
    ASSERT_TRUE(msg_ptr != nullptr);

    // Serialize the full message (3 entries), then truncate to fit only 1 entry
    auto serialized = msg_ptr->Serialize();
    ASSERT_TRUE(serialized.has_value());

    size_t truncated_size = BaseHeader::Size() +
                            RoutingTableHeader::RoutingTableFieldsSize() +
                            1 * RoutingTableEntry::Size();
    ASSERT_LT(truncated_size, serialized->size());

    std::vector<uint8_t> truncated(serialized->begin(),
                                   serialized->begin() + truncated_size);

    // entry_count in the header still says 3, but only 1 entry of data exists
    auto result = RoutingTableMessage::CreateFromSerialized(truncated);
    EXPECT_FALSE(result.has_value());
}

// =============================================================================
// GetReceptionQualityFor — hop_count filtering (unidirectional link detection)
// =============================================================================

/**
 * @brief Test GetReceptionQualityFor returns reception quality for direct
 * neighbor (hop_count==1)
 */
TEST_F(RoutingTableMessageTest,
       GetReceptionQualityForDirectNeighborReturnsQuality) {
    ASSERT_TRUE(msg_ptr != nullptr);

    // 0x1111 has hop_count=1 with reception_quality=90 in fixture entries
    EXPECT_EQ(msg_ptr->GetReceptionQualityFor(0x1111), 90);
}

/**
 * @brief Test GetReceptionQualityFor returns 0 for multi-hop entries without
 * reception data
 */
TEST_F(RoutingTableMessageTest,
       GetReceptionQualityForMultiHopNoReceptionReturnsZero) {
    ASSERT_TRUE(msg_ptr != nullptr);

    // 0x2222 has hop_count=2, 0x3333 has hop_count=3, both with
    // reception_quality=0 (no direct reception data)
    EXPECT_EQ(msg_ptr->GetReceptionQualityFor(0x2222), 0);
    EXPECT_EQ(msg_ptr->GetReceptionQualityFor(0x3333), 0);
}

/**
 * @brief Test GetReceptionQualityFor returns quality for multi-hop entries
 * that have direct reception data (reception_quality > 0)
 *
 * A node can be routed via multi-hop but still heard directly (lossy link).
 */
TEST_F(RoutingTableMessageTest,
       GetReceptionQualityForMultiHopWithReceptionReturnsQuality) {
    // Create a hop=2 entry with reception_quality set (lossy but heard)
    std::vector<RoutingTableEntry> test_entries;
    RoutingTableEntry lossy_entry(0x4444, 2, 200, 1);
    lossy_entry.reception_quality = 48;  // Sender hears this node at 48
    test_entries.push_back(lossy_entry);

    auto opt_msg = RoutingTableMessage::Create(dest, src, network_id,
                                               table_version, test_entries);
    ASSERT_TRUE(opt_msg.has_value());

    EXPECT_EQ(opt_msg->GetReceptionQualityFor(0x4444), 48);
}

/**
 * @brief Test GetReceptionQualityFor returns 0 for missing node
 */
TEST_F(RoutingTableMessageTest, GetReceptionQualityForMissingNodeReturnsZero) {
    ASSERT_TRUE(msg_ptr != nullptr);

    EXPECT_EQ(msg_ptr->GetReceptionQualityFor(0xDEAD), 0);
}

}  // namespace test
}  // namespace loramesher