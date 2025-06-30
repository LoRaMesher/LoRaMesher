/**
 * @file slot_request_message_test.cpp
 * @brief Unit tests for SlotRequestMessage class
 */

#include <gtest/gtest.h>

#include <memory>

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <sys/resource.h>
#endif

#include "types/messages/loramesher/slot_request_message.hpp"

namespace loramesher {
namespace test {

/**
 * @brief Test fixture for SlotRequestMessage tests
 */
class SlotRequestMessageTest : public ::testing::Test {
   protected:
    // Common test data
    static constexpr AddressType dest = 0x1234;
    static constexpr AddressType src = 0x5678;
    static constexpr uint8_t requested_slots = 5;

    std::unique_ptr<SlotRequestMessage> msg_ptr;

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
        auto opt_msg = SlotRequestMessage::Create(dest, src, requested_slots);
        ASSERT_TRUE(opt_msg.has_value()) << "Failed to create test message";
        msg_ptr = std::make_unique<SlotRequestMessage>(*opt_msg);
    }
};

/**
 * @brief Test creating a SlotRequestMessage with valid parameters
 */
TEST_F(SlotRequestMessageTest, CreationTest) {
    // Given: Test parameters
    const AddressType test_dest = 0xABCD;
    const AddressType test_src = 0xDCBA;
    const uint8_t test_slots = 10;

    // When: Creating a message
    auto opt_msg = SlotRequestMessage::Create(test_dest, test_src, test_slots);

    // Then: Message creation should succeed
    ASSERT_TRUE(opt_msg.has_value()) << "Failed to create SlotRequest message";

    // And: Message should have correct fields
    EXPECT_EQ(opt_msg->GetDestination(), test_dest);
    EXPECT_EQ(opt_msg->GetSource(), test_src);
    EXPECT_EQ(opt_msg->GetRequestedSlots(), test_slots);
}

/**
 * @brief Test serializing and deserializing a SlotRequestMessage
 */
TEST_F(SlotRequestMessageTest, SerializationTest) {
    // Given: A valid message
    ASSERT_TRUE(msg_ptr != nullptr);

    // When: Serializing the message
    std::optional<std::vector<uint8_t>> opt_serialized = msg_ptr->Serialize();
    ASSERT_TRUE(opt_serialized.has_value()) << "Serialization failed";

    const std::vector<uint8_t>& serialized = *opt_serialized;

    // Then: Check size (should just be 1 byte for requested_slots)
    ASSERT_EQ(serialized.size(), 1) << "Incorrect serialized size";

    // And: Verify data
    EXPECT_EQ(serialized[0], requested_slots)
        << "Incorrect requested slots in serialized data";
}

/**
 * @brief Test deserializing a SlotRequestMessage
 */
TEST_F(SlotRequestMessageTest, DeserializationTest) {
    // Given: A serialized message
    auto opt_serialized = msg_ptr->Serialize();
    ASSERT_TRUE(opt_serialized.has_value()) << "Failed to serialize message";

    // When: Deserializing the message
    auto opt_deserialized =
        SlotRequestMessage::CreateFromSerialized(*opt_serialized);
    ASSERT_TRUE(opt_deserialized.has_value())
        << "Failed to deserialize message";

    // Note: The source and destination addresses would normally be set by the BaseMessage
    // that contains this as payload, so we don't check those here.

    // Then: Check the requested slots field
    EXPECT_EQ(opt_deserialized->GetRequestedSlots(), requested_slots);
}

/**
 * @brief Test deserializing a SlotRequestMessage with invalid data
 */
TEST_F(SlotRequestMessageTest, DeserializationFailureTest) {
    // Test: Empty data
    {
        std::vector<uint8_t> empty_data;
        auto result = SlotRequestMessage::CreateFromSerialized(empty_data);
        EXPECT_FALSE(result.has_value()) << "Should fail with empty data";
    }
}

/**
 * @brief Test conversion to BaseMessage
 */
TEST_F(SlotRequestMessageTest, ConversionToBaseMessageTest) {
    // Given: A SlotRequest message
    ASSERT_TRUE(msg_ptr != nullptr);

    // When: Converting to BaseMessage
    BaseMessage base_msg = msg_ptr->ToBaseMessage();

    // Then: Base message should have correct header fields
    EXPECT_EQ(base_msg.GetHeader().GetDestination(), dest);
    EXPECT_EQ(base_msg.GetHeader().GetSource(), src);
    EXPECT_EQ(base_msg.GetHeader().GetType(), MessageType::SLOT_REQUEST);

    // And: Payload should contain requested slots field
    const std::vector<uint8_t>& payload = base_msg.GetPayload();
    ASSERT_EQ(payload.size(), 1);
    EXPECT_EQ(payload[0], requested_slots);
}

/**
 * @brief Test that GetTotalSize() returns the correct value
 */
TEST_F(SlotRequestMessageTest, GetTotalSizeTest) {
    // Given: A SlotRequest message
    ASSERT_TRUE(msg_ptr != nullptr);

    // When: Getting the total size
    size_t total_size = msg_ptr->GetTotalSize();

    // Then: Size should be correct (1 byte for the requested slots)
    EXPECT_EQ(total_size, 1);
}

}  // namespace test
}  // namespace loramesher