/**
 * @file data_message_test.cpp
 * @brief Unit tests for DataMessage class
 */

#include <gtest/gtest.h>

#include <memory>

#include "types/messages/loramesher/data_message.hpp"

namespace loramesher {
namespace test {

/**
 * @brief Test fixture for DataMessage tests
 */
class DataMessageTest : public ::testing::Test {
   protected:
    // Common test data
    static constexpr AddressType dest = 0x1234;
    static constexpr AddressType src = 0x5678;
    static constexpr AddressType next_hop = 0xABCD;
    std::vector<uint8_t> payload{0x11, 0x22, 0x33, 0x44, 0x55};

    std::unique_ptr<DataMessage> msg_ptr;

    void SetUp() override { CreateMessage(); }

    void CreateMessage() {
        auto opt_msg = DataMessage::Create(dest, src, next_hop, payload);
        ASSERT_TRUE(opt_msg.has_value()) << "Failed to create test message";
        msg_ptr = std::make_unique<DataMessage>(*opt_msg);
    }
};

/**
 * @brief Test creating a DataMessage with valid parameters
 */
TEST_F(DataMessageTest, CreationTest) {
    // Given: Test parameters
    const AddressType test_dest = 0xDEAD;
    const AddressType test_src = 0xBEEF;
    const AddressType test_next_hop = 0xCAFE;
    const std::vector<uint8_t> test_payload{0xAA, 0xBB, 0xCC};

    // When: Creating a message
    auto opt_msg =
        DataMessage::Create(test_dest, test_src, test_next_hop, test_payload);

    // Then: Message creation should succeed
    ASSERT_TRUE(opt_msg.has_value()) << "Failed to create DataMessage";

    // And: Message should have correct fields
    EXPECT_EQ(opt_msg->GetDestination(), test_dest);
    EXPECT_EQ(opt_msg->GetSource(), test_src);
    EXPECT_EQ(opt_msg->GetNextHop(), test_next_hop);
    EXPECT_EQ(opt_msg->GetPayload(), test_payload);
}

/**
 * @brief Test creating a DataMessage with empty payload
 */
TEST_F(DataMessageTest, CreationWithEmptyPayloadTest) {
    // Given: Test parameters with empty payload
    const AddressType test_dest = 0x1111;
    const AddressType test_src = 0x2222;
    const AddressType test_next_hop = 0x3333;
    const std::vector<uint8_t> empty_payload;

    // When: Creating a message without payload
    auto opt_msg =
        DataMessage::Create(test_dest, test_src, test_next_hop, empty_payload);

    // Then: Message creation should succeed
    ASSERT_TRUE(opt_msg.has_value())
        << "Failed to create DataMessage with empty payload";

    // And: Payload should be empty
    EXPECT_TRUE(opt_msg->GetPayload().empty());
    EXPECT_EQ(opt_msg->GetDestination(), test_dest);
    EXPECT_EQ(opt_msg->GetSource(), test_src);
    EXPECT_EQ(opt_msg->GetNextHop(), test_next_hop);
}

/**
 * @brief Test serializing a DataMessage
 */
TEST_F(DataMessageTest, SerializationTest) {
    // Given: A valid message
    ASSERT_TRUE(msg_ptr != nullptr);

    // When: Serializing the message
    std::optional<std::vector<uint8_t>> opt_serialized = msg_ptr->Serialize();
    ASSERT_TRUE(opt_serialized.has_value()) << "Serialization failed";

    const std::vector<uint8_t>& serialized = *opt_serialized;

    // Then: Check size (BaseHeader + Data fields + payload)
    const size_t expected_size =
        BaseHeader::Size() + DataHeader::DataFieldsSize() + payload.size();
    ASSERT_EQ(serialized.size(), expected_size) << "Incorrect serialized size";
}

/**
 * @brief Test deserializing a DataMessage
 */
TEST_F(DataMessageTest, DeserializationTest) {
    // Given: A serialized message
    auto opt_serialized = msg_ptr->Serialize();
    ASSERT_TRUE(opt_serialized.has_value()) << "Failed to serialize message";

    // When: Deserializing the message
    auto opt_deserialized = DataMessage::CreateFromSerialized(*opt_serialized);
    ASSERT_TRUE(opt_deserialized.has_value())
        << "Failed to deserialize message";

    DataMessage deserialized_msg = std::move(*opt_deserialized);

    // Then: Verify all fields
    EXPECT_EQ(deserialized_msg.GetDestination(), dest);
    EXPECT_EQ(deserialized_msg.GetSource(), src);
    EXPECT_EQ(deserialized_msg.GetNextHop(), next_hop);
    EXPECT_EQ(deserialized_msg.GetPayload(), payload);
}

/**
 * @brief Test deserializing a DataMessage with invalid data
 */
TEST_F(DataMessageTest, DeserializationFailureTest) {
    // Test: Empty data
    {
        std::vector<uint8_t> empty_data;
        auto result = DataMessage::CreateFromSerialized(empty_data);
        EXPECT_FALSE(result.has_value()) << "Should fail with empty data";
    }

    // Test: Incomplete header
    {
        std::vector<uint8_t> incomplete_data(BaseHeader::Size() - 1, 0);
        auto result = DataMessage::CreateFromSerialized(incomplete_data);
        EXPECT_FALSE(result.has_value())
            << "Should fail with incomplete header";
    }

    // Test: Wrong message type
    {
        auto opt_serialized = msg_ptr->Serialize();
        ASSERT_TRUE(opt_serialized.has_value());

        std::vector<uint8_t> invalid_type = *opt_serialized;
        // Change the message type byte (typically at index 4)
        invalid_type[4] = static_cast<uint8_t>(MessageType::JOIN_REQUEST);

        auto result = DataMessage::CreateFromSerialized(invalid_type);
        EXPECT_FALSE(result.has_value())
            << "Should fail with invalid message type";
    }
}

/**
 * @brief Test conversion to BaseMessage
 */
TEST_F(DataMessageTest, ConversionToBaseMessageTest) {
    // Given: A Data message
    ASSERT_TRUE(msg_ptr != nullptr);

    // When: Converting to BaseMessage
    BaseMessage base_msg = msg_ptr->ToBaseMessage();

    // Then: Base message should have correct header fields
    EXPECT_EQ(base_msg.GetHeader().GetDestination(), dest);
    EXPECT_EQ(base_msg.GetHeader().GetSource(), src);
    EXPECT_EQ(base_msg.GetHeader().GetType(), MessageType::DATA);

    // And: Payload should contain Data fields + user payload
    auto base_payload = base_msg.GetPayload();

    ASSERT_EQ(base_payload.size(),
              DataHeader::DataFieldsSize() + payload.size());

    // Check next_hop in payload (little-endian)
    uint16_t payload_next_hop = base_payload[0] | (base_payload[1] << 8);
    EXPECT_EQ(payload_next_hop, next_hop);

    // Check user payload (starts after next_hop)
    for (size_t i = 0; i < payload.size(); i++) {
        EXPECT_EQ(base_payload[DataHeader::DataFieldsSize() + i], payload[i]);
    }
}

/**
 * @brief Test GetTotalSize() returns the correct value
 */
TEST_F(DataMessageTest, GetTotalSizeTest) {
    // Given: A Data message
    ASSERT_TRUE(msg_ptr != nullptr);

    // When: Getting the total size
    size_t total_size = msg_ptr->GetTotalSize();

    // Then: Size should be correct
    const size_t expected_size =
        DataHeader::DataFieldsSize() + BaseHeader::Size() + payload.size();
    EXPECT_EQ(total_size, expected_size);
}

/**
 * @brief Test retrieving the header directly
 */
TEST_F(DataMessageTest, GetHeaderTest) {
    // Given: A Data message
    ASSERT_TRUE(msg_ptr != nullptr);

    // When: Getting the header
    const DataHeader& header = msg_ptr->GetHeader();

    // Then: Header should have correct values
    EXPECT_EQ(header.GetDestination(), dest);
    EXPECT_EQ(header.GetSource(), src);
    EXPECT_EQ(header.GetType(), MessageType::DATA);
    EXPECT_EQ(header.GetNextHop(), next_hop);
}

/**
 * @brief Test SetNextHop() functionality
 */
TEST_F(DataMessageTest, SetNextHopTest) {
    // Given: A Data message
    ASSERT_TRUE(msg_ptr != nullptr);
    const AddressType original_next_hop = msg_ptr->GetNextHop();
    EXPECT_EQ(original_next_hop, next_hop);

    // When: Setting a new next hop
    const AddressType new_next_hop = 0xFFFF;
    Result result = msg_ptr->SetNextHop(new_next_hop);

    // Then: Setting should succeed
    ASSERT_TRUE(result.IsSuccess()) << "SetNextHop failed";

    // And: Next hop should be updated
    EXPECT_EQ(msg_ptr->GetNextHop(), new_next_hop);

    // And: Other fields should remain unchanged
    EXPECT_EQ(msg_ptr->GetDestination(), dest);
    EXPECT_EQ(msg_ptr->GetSource(), src);
    EXPECT_EQ(msg_ptr->GetPayload(), payload);
}

/**
 * @brief Test that next_hop is preserved during serialization/deserialization
 */
TEST_F(DataMessageTest, NextHopPreservedInSerializationTest) {
    // Given: A Data message with specific next_hop
    const AddressType test_next_hop = 0x9999;
    auto opt_msg = DataMessage::Create(dest, src, test_next_hop, payload);
    ASSERT_TRUE(opt_msg.has_value()) << "Failed to create message";

    DataMessage original_msg = std::move(*opt_msg);
    EXPECT_EQ(original_msg.GetNextHop(), test_next_hop);

    // When: Serializing and deserializing
    auto opt_serialized = original_msg.Serialize();
    ASSERT_TRUE(opt_serialized.has_value()) << "Failed to serialize";

    auto opt_deserialized = DataMessage::CreateFromSerialized(*opt_serialized);
    ASSERT_TRUE(opt_deserialized.has_value()) << "Failed to deserialize";

    // Then: Next hop should be preserved
    EXPECT_EQ(opt_deserialized->GetNextHop(), test_next_hop);

    // And: All other fields should also be preserved
    EXPECT_EQ(opt_deserialized->GetDestination(), dest);
    EXPECT_EQ(opt_deserialized->GetSource(), src);
    EXPECT_EQ(opt_deserialized->GetPayload(), payload);
}

/**
 * @brief Test creating message with next_hop equal to destination (direct delivery)
 */
TEST_F(DataMessageTest, DirectDeliveryTest) {
    // Given: next_hop equals destination (direct neighbor)
    const AddressType test_dest = 0x4444;
    const AddressType test_src = 0x5555;
    const AddressType test_next_hop = test_dest;  // Direct delivery
    const std::vector<uint8_t> test_payload{0x01, 0x02};

    // When: Creating a message for direct delivery
    auto opt_msg =
        DataMessage::Create(test_dest, test_src, test_next_hop, test_payload);

    // Then: Message creation should succeed
    ASSERT_TRUE(opt_msg.has_value())
        << "Failed to create direct delivery message";

    // And: next_hop should equal destination
    EXPECT_EQ(opt_msg->GetNextHop(), opt_msg->GetDestination());
}

/**
 * @brief Test GetMutableHeader allows modifying the header
 */
TEST_F(DataMessageTest, GetMutableHeaderTest) {
    // Given: A Data message
    ASSERT_TRUE(msg_ptr != nullptr);

    // When: Getting mutable header and modifying next_hop
    const AddressType new_next_hop = 0x7777;
    DataHeader& mutable_header = msg_ptr->GetMutableHeader();
    mutable_header.SetNextHop(new_next_hop);

    // Then: Next hop should be updated
    EXPECT_EQ(msg_ptr->GetNextHop(), new_next_hop);
    EXPECT_EQ(msg_ptr->GetHeader().GetNextHop(), new_next_hop);
}

TEST_F(DataMessageTest, CreateWithTTLAndSeqNum) {
    auto opt_msg = DataMessage::Create(dest, src, next_hop, payload, 10, 42);
    ASSERT_TRUE(opt_msg.has_value());
    EXPECT_EQ(opt_msg->GetTTL(), 10);
    EXPECT_EQ(opt_msg->GetSeqNum(), 42);
    EXPECT_EQ(opt_msg->GetPayload(), payload);
}

TEST_F(DataMessageTest, TTLAndSeqNumPreservedInSerialization) {
    auto opt_msg = DataMessage::Create(dest, src, next_hop, payload, 5, 77);
    ASSERT_TRUE(opt_msg.has_value());

    auto opt_serialized = opt_msg->Serialize();
    ASSERT_TRUE(opt_serialized.has_value());

    auto opt_deserialized = DataMessage::CreateFromSerialized(*opt_serialized);
    ASSERT_TRUE(opt_deserialized.has_value());
    EXPECT_EQ(opt_deserialized->GetTTL(), 5);
    EXPECT_EQ(opt_deserialized->GetSeqNum(), 77);
    EXPECT_EQ(opt_deserialized->GetPayload(), payload);
}

TEST_F(DataMessageTest, CreateForwardedDecrementsTTL) {
    auto opt_msg = DataMessage::Create(dest, src, next_hop, payload, 5, 10);
    ASSERT_TRUE(opt_msg.has_value());

    const AddressType new_hop = 0x9999;
    auto forwarded = DataMessage::CreateForwarded(*opt_msg, new_hop);
    ASSERT_TRUE(forwarded.has_value());
    EXPECT_EQ(forwarded->GetTTL(), 4);
    EXPECT_EQ(forwarded->GetSeqNum(), 10);
    EXPECT_EQ(forwarded->GetNextHop(), new_hop);
    EXPECT_EQ(forwarded->GetSource(), src);
    EXPECT_EQ(forwarded->GetDestination(), dest);
}

TEST_F(DataMessageTest, CreateForwardedReturnsNulloptAtTTLOne) {
    auto opt_msg = DataMessage::Create(dest, src, next_hop, payload, 1, 10);
    ASSERT_TRUE(opt_msg.has_value());

    auto forwarded = DataMessage::CreateForwarded(*opt_msg, 0x9999);
    EXPECT_FALSE(forwarded.has_value());
}

TEST_F(DataMessageTest, CreateForwardedReturnsNulloptAtTTLZero) {
    auto opt_msg = DataMessage::Create(dest, src, next_hop, payload, 0, 10);
    ASSERT_TRUE(opt_msg.has_value());

    auto forwarded = DataMessage::CreateForwarded(*opt_msg, 0x9999);
    EXPECT_FALSE(forwarded.has_value());
}

TEST_F(DataMessageTest, CreateFromBaseMessageSucceeds) {
    ASSERT_TRUE(msg_ptr != nullptr);

    BaseMessage base = msg_ptr->ToBaseMessage();
    ASSERT_EQ(base.GetType(), MessageType::DATA);

    auto result = DataMessage::CreateFromBaseMessage(base);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->GetDestination(), dest);
    EXPECT_EQ(result->GetSource(), src);
    EXPECT_EQ(result->GetNextHop(), next_hop);
    EXPECT_EQ(result->GetPayload(), payload);
}

TEST_F(DataMessageTest, CreateFromBaseMessageWithTTLAndSeqNum) {
    auto opt_msg = DataMessage::Create(dest, src, next_hop, payload, 5, 42);
    ASSERT_TRUE(opt_msg.has_value());

    BaseMessage base = opt_msg->ToBaseMessage();
    auto result = DataMessage::CreateFromBaseMessage(base);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->GetTTL(), 5);
    EXPECT_EQ(result->GetSeqNum(), 42);
}

TEST_F(DataMessageTest, CreateFromBaseMessageWrongTypeReturnsNullopt) {
    std::array<uint8_t, 4> payload_arr{0x01, 0x02, 0x03, 0x04};
    BaseMessage wrong(dest, src, MessageType::JOIN_REQUEST,
                      std::span<const uint8_t>(payload_arr));

    auto result = DataMessage::CreateFromBaseMessage(wrong);
    EXPECT_FALSE(result.has_value());
}

}  // namespace test
}  // namespace loramesher
