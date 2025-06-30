/**
 * @file join_response_message_test.cpp
 * @brief Unit tests for JoinResponseMessage class
 */

#include <gtest/gtest.h>

#include <memory>

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <sys/resource.h>
#endif

#include "types/messages/loramesher/join_response_message.hpp"

namespace loramesher {
namespace test {

/**
 * @brief Test fixture for JoinResponseMessage tests
 */
class JoinResponseMessageTest : public ::testing::Test {
   protected:
    // Common test data
    static constexpr AddressType dest = 0x1234;
    static constexpr AddressType src = 0x5678;
    static constexpr uint16_t network_id = 0x9ABC;
    static constexpr uint8_t allocated_slots = 3;
    static constexpr JoinResponseHeader::ResponseStatus status =
        JoinResponseHeader::ResponseStatus::ACCEPTED;
    std::vector<uint8_t> superframe_info{0xAA, 0xBB, 0xCC, 0xDD};

    std::unique_ptr<JoinResponseMessage> msg_ptr;

#ifdef ARDUINO
    void SetUp() override { CreateMessage(); }
#else
    void SetUp() override {
        // Create message
        CreateMessage();

        // Record memory usage before test
        initial_memory_ = getCurrentMemoryUsage();
    }

    void TearDown() override {
        // Verify no memory leaks
        size_t final_memory = getCurrentMemoryUsage();
        EXPECT_EQ(final_memory, initial_memory_);
    }

   private:
    size_t getCurrentMemoryUsage() {
#ifdef _WIN32
        MEMORYSTATUSEX memStatus;
        memStatus.dwLength = sizeof(memStatus);
        GlobalMemoryStatusEx(&memStatus);
        return memStatus.ullTotalPhys;
#elif defined(__linux__)
        struct rusage usage;
        getrusage(RUSAGE_SELF, &usage);
        return usage.ru_maxrss;
#else
        return 0;
#endif
    }

    size_t initial_memory_;
#endif

    void CreateMessage() {
        auto opt_msg = JoinResponseMessage::Create(
            dest, src, network_id, allocated_slots, status, superframe_info);
        ASSERT_TRUE(opt_msg.has_value()) << "Failed to create test message";
        msg_ptr = std::make_unique<JoinResponseMessage>(*opt_msg);
    }
};

/**
 * @brief Test creating a JoinResponseMessage with valid parameters
 */
TEST_F(JoinResponseMessageTest, CreationTest) {
    // Given: Test parameters
    const AddressType test_dest = 0xABCD;
    const AddressType test_src = 0xDCBA;
    const uint16_t test_network_id = 0x6789;
    const uint8_t test_slots = 5;
    const JoinResponseHeader::ResponseStatus test_status =
        JoinResponseHeader::ResponseStatus::CAPACITY_EXCEEDED;
    const std::vector<uint8_t> test_info{0x11, 0x22, 0x33, 0x44, 0x55};

    // When: Creating a message
    auto opt_msg =
        JoinResponseMessage::Create(test_dest, test_src, test_network_id,
                                    test_slots, test_status, test_info);

    // Then: Message creation should succeed
    ASSERT_TRUE(opt_msg.has_value()) << "Failed to create JoinResponse message";

    // And: Message should have correct fields
    EXPECT_EQ(opt_msg->GetDestination(), test_dest);
    EXPECT_EQ(opt_msg->GetSource(), test_src);
    EXPECT_EQ(opt_msg->GetNetworkId(), test_network_id);
    EXPECT_EQ(opt_msg->GetAllocatedSlots(), test_slots);
    EXPECT_EQ(opt_msg->GetStatus(), test_status);
    EXPECT_EQ(opt_msg->GetSuperframeInfo(), test_info);
}

/**
 * @brief Test creating a JoinResponseMessage with no superframe info
 */
TEST_F(JoinResponseMessageTest, CreationWithoutSuperframeInfoTest) {
    // When: Creating a message without superframe info
    auto opt_msg = JoinResponseMessage::Create(dest, src, network_id,
                                               allocated_slots, status);

    // Then: Message creation should succeed
    ASSERT_TRUE(opt_msg.has_value())
        << "Failed to create JoinResponse message without superframe info";

    // And: Superframe info should be empty
    EXPECT_TRUE(opt_msg->GetSuperframeInfo().empty());
}

/**
 * @brief Test serializing a JoinResponseMessage
 */
TEST_F(JoinResponseMessageTest, SerializationTest) {
    // Given: A valid message
    ASSERT_TRUE(msg_ptr != nullptr);

    // When: Serializing the message
    std::optional<std::vector<uint8_t>> opt_serialized = msg_ptr->Serialize();
    ASSERT_TRUE(opt_serialized.has_value()) << "Serialization failed";

    const std::vector<uint8_t>& serialized = *opt_serialized;

    // Then: Check size (BaseHeader + JoinResponse fields + superframe info)
    const size_t expected_size = BaseHeader::Size() +
                                 JoinResponseHeader::JoinResponseFieldsSize() +
                                 superframe_info.size();
    ASSERT_EQ(serialized.size(), expected_size) << "Incorrect serialized size";
}

/**
 * @brief Test deserializing a JoinResponseMessage
 */
TEST_F(JoinResponseMessageTest, DeserializationTest) {
    // Given: A serialized message
    auto opt_serialized = msg_ptr->Serialize();
    ASSERT_TRUE(opt_serialized.has_value()) << "Failed to serialize message";
    ASSERT_EQ(opt_serialized->size(), msg_ptr->GetTotalSize())
        << "Serialized size does not match expected size";

    // When: Deserializing the message
    auto opt_deserialized =
        JoinResponseMessage::CreateFromSerialized(*opt_serialized);
    ASSERT_TRUE(opt_deserialized.has_value())
        << "Failed to deserialize message";

    JoinResponseMessage deserialized_msg = std::move(*opt_deserialized);

    // Then: Verify all fields
    EXPECT_EQ(deserialized_msg.GetDestination(), dest);
    EXPECT_EQ(deserialized_msg.GetSource(), src);
    EXPECT_EQ(deserialized_msg.GetNetworkId(), network_id);
    EXPECT_EQ(deserialized_msg.GetAllocatedSlots(), allocated_slots);
    EXPECT_EQ(deserialized_msg.GetStatus(), status);
    EXPECT_EQ(deserialized_msg.GetSuperframeInfo(), superframe_info);
}

/**
 * @brief Test deserializing a JoinResponseMessage with invalid data
 */
TEST_F(JoinResponseMessageTest, DeserializationFailureTest) {
    // Test: Empty data
    {
        std::vector<uint8_t> empty_data;
        auto result = JoinResponseMessage::CreateFromSerialized(empty_data);
        EXPECT_FALSE(result.has_value()) << "Should fail with empty data";
    }

    // Test: Incomplete header
    {
        std::vector<uint8_t> incomplete_data(BaseHeader::Size() - 1, 0);
        auto result =
            JoinResponseMessage::CreateFromSerialized(incomplete_data);
        EXPECT_FALSE(result.has_value())
            << "Should fail with incomplete header";
    }

    // Test: Wrong message type
    {
        auto opt_serialized = msg_ptr->Serialize();
        ASSERT_TRUE(opt_serialized.has_value());

        std::vector<uint8_t> invalid_type = *opt_serialized;
        // Change the message type byte (typically at index 4)
        invalid_type[4] = static_cast<uint8_t>(MessageType::DATA_MSG);

        auto result = JoinResponseMessage::CreateFromSerialized(invalid_type);
        EXPECT_FALSE(result.has_value())
            << "Should fail with invalid message type";
    }
}

/**
 * @brief Test conversion to BaseMessage
 */
TEST_F(JoinResponseMessageTest, ConversionToBaseMessageTest) {
    // Given: A JoinResponse message
    ASSERT_TRUE(msg_ptr != nullptr);

    // When: Converting to BaseMessage
    BaseMessage base_msg = msg_ptr->ToBaseMessage();

    // Then: Base message should have correct header fields
    EXPECT_EQ(base_msg.GetHeader().GetDestination(), dest);
    EXPECT_EQ(base_msg.GetHeader().GetSource(), src);
    EXPECT_EQ(base_msg.GetHeader().GetType(), MessageType::JOIN_RESPONSE);

    // And: Payload should contain JoinResponse fields + superframe info
    const std::vector<uint8_t>& payload = base_msg.GetPayload();

    // Payload should have 4 bytes (network_id, allocated_slots, status) plus superframe info
    ASSERT_EQ(payload.size(), 4 + superframe_info.size());

    // Network ID should be in the first two bytes (little endian)
    uint16_t extracted_network_id =
        (static_cast<uint16_t>(payload[1]) << 8) | payload[0];
    EXPECT_EQ(extracted_network_id, network_id);

    // Check allocated slots and status
    EXPECT_EQ(payload[2], allocated_slots);
    EXPECT_EQ(payload[3], static_cast<uint8_t>(status));

    // Check superframe info
    for (size_t i = 0; i < superframe_info.size(); i++) {
        EXPECT_EQ(payload[4 + i], superframe_info[i]);
    }
}

/**
 * @brief Test GetTotalSize() returns the correct value
 */
TEST_F(JoinResponseMessageTest, GetTotalSizeTest) {
    // Given: A JoinResponse message
    ASSERT_TRUE(msg_ptr != nullptr);

    // When: Getting the total size
    size_t total_size = msg_ptr->GetTotalSize();

    // Then: Size should be correct
    const size_t expected_size = JoinResponseHeader::JoinResponseFieldsSize() +
                                 BaseHeader::Size() + superframe_info.size();
    EXPECT_EQ(total_size, expected_size);
}

/**
 * @brief Test retrieving the header directly
 */
TEST_F(JoinResponseMessageTest, GetHeaderTest) {
    // Given: A JoinResponse message
    ASSERT_TRUE(msg_ptr != nullptr);

    // When: Getting the header
    const JoinResponseHeader& header = msg_ptr->GetHeader();

    // Then: Header should have correct values
    EXPECT_EQ(header.GetDestination(), dest);
    EXPECT_EQ(header.GetSource(), src);
    EXPECT_EQ(header.GetType(), MessageType::JOIN_RESPONSE);
    EXPECT_EQ(header.GetNetworkId(), network_id);
    EXPECT_EQ(header.GetAllocatedSlots(), allocated_slots);
    EXPECT_EQ(header.GetStatus(), status);
}

}  // namespace test
}  // namespace loramesher