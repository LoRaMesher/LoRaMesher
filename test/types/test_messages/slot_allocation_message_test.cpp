/**
 * @file slot_allocation_message_test.cpp
 * @brief Unit tests for SlotAllocationMessage class
 */

#include <gtest/gtest.h>

#include <memory>

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <sys/resource.h>
#endif

#include "types/messages/loramesher/slot_allocation_message.hpp"

namespace loramesher {
namespace test {

/**
  * @brief Test fixture for SlotAllocationMessage tests
  */
class SlotAllocationMessageTest : public ::testing::Test {
   protected:
    // Common test data
    static constexpr AddressType dest = 0x1234;
    static constexpr AddressType src = 0x5678;
    static constexpr uint16_t network_id = 0x9ABC;
    static constexpr uint8_t allocated_slots = 4;
    static constexpr uint8_t total_nodes = 10;

    std::unique_ptr<SlotAllocationMessage> msg_ptr;

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
        auto opt_msg = SlotAllocationMessage::Create(
            dest, src, network_id, allocated_slots, total_nodes);
        ASSERT_TRUE(opt_msg.has_value()) << "Failed to create test message";
        msg_ptr = std::make_unique<SlotAllocationMessage>(*opt_msg);
    }
};

/**
  * @brief Test creating a SlotAllocationMessage with valid parameters
  */
TEST_F(SlotAllocationMessageTest, CreationTest) {
    // Given: Test parameters
    const AddressType test_dest = 0xABCD;
    const AddressType test_src = 0xDCBA;
    const uint16_t test_network_id = 0x6789;
    const uint8_t test_slots = 7;
    const uint8_t test_nodes = 15;

    // When: Creating a message
    auto opt_msg = SlotAllocationMessage::Create(
        test_dest, test_src, test_network_id, test_slots, test_nodes);

    // Then: Message creation should succeed
    ASSERT_TRUE(opt_msg.has_value())
        << "Failed to create SlotAllocation message";

    // And: Message should have correct fields
    EXPECT_EQ(opt_msg->GetDestination(), test_dest);
    EXPECT_EQ(opt_msg->GetSource(), test_src);
    EXPECT_EQ(opt_msg->GetNetworkId(), test_network_id);
    EXPECT_EQ(opt_msg->GetAllocatedSlots(), test_slots);
    EXPECT_EQ(opt_msg->GetTotalNodes(), test_nodes);
}

/**
  * @brief Test serializing a SlotAllocationMessage
  */
TEST_F(SlotAllocationMessageTest, SerializationTest) {
    // Given: A valid message
    ASSERT_TRUE(msg_ptr != nullptr);

    // When: Serializing the message
    std::optional<std::vector<uint8_t>> opt_serialized = msg_ptr->Serialize();
    ASSERT_TRUE(opt_serialized.has_value()) << "Serialization failed";

    const std::vector<uint8_t>& serialized = *opt_serialized;

    // Then: Check size (4 bytes total: 2 for network_id, 1 for allocated_slots, 1 for total_nodes)
    ASSERT_EQ(serialized.size(), 4) << "Incorrect serialized size";

    // Check network ID (little endian)
    uint16_t extracted_network_id =
        (static_cast<uint16_t>(serialized[1]) << 8) | serialized[0];
    EXPECT_EQ(extracted_network_id, network_id);

    // Check allocated slots and total nodes
    EXPECT_EQ(serialized[2], allocated_slots);
    EXPECT_EQ(serialized[3], total_nodes);
}

/**
  * @brief Test deserializing a SlotAllocationMessage
  */
TEST_F(SlotAllocationMessageTest, DeserializationTest) {
    // Given: A serialized message
    auto opt_serialized = msg_ptr->Serialize();
    ASSERT_TRUE(opt_serialized.has_value()) << "Failed to serialize message";

    // When: Deserializing the message
    auto opt_deserialized =
        SlotAllocationMessage::CreateFromSerialized(*opt_serialized);
    ASSERT_TRUE(opt_deserialized.has_value())
        << "Failed to deserialize message";

    // Note: Source and destination are normally set by the BaseMessage handler
    // So we don't expect them to match here, but we can still check the other fields
    SlotAllocationMessage deserialized_msg = std::move(*opt_deserialized);

    // Then: Verify fields
    EXPECT_EQ(deserialized_msg.GetNetworkId(), network_id);
    EXPECT_EQ(deserialized_msg.GetAllocatedSlots(), allocated_slots);
    EXPECT_EQ(deserialized_msg.GetTotalNodes(), total_nodes);
}

/**
  * @brief Test deserializing a SlotAllocationMessage with invalid data
  */
TEST_F(SlotAllocationMessageTest, DeserializationFailureTest) {
    // Test: Empty data
    {
        std::vector<uint8_t> empty_data;
        auto result = SlotAllocationMessage::CreateFromSerialized(empty_data);
        EXPECT_FALSE(result.has_value()) << "Should fail with empty data";
    }

    // Test: Incomplete data
    {
        std::vector<uint8_t> incomplete_data{0x01, 0x02,
                                             0x03};  // Missing total_nodes
        auto result =
            SlotAllocationMessage::CreateFromSerialized(incomplete_data);
        EXPECT_FALSE(result.has_value()) << "Should fail with incomplete data";
    }
}

/**
  * @brief Test conversion to BaseMessage
  */
TEST_F(SlotAllocationMessageTest, ConversionToBaseMessageTest) {
    // Given: A SlotAllocation message
    ASSERT_TRUE(msg_ptr != nullptr);

    // When: Converting to BaseMessage
    BaseMessage base_msg = msg_ptr->ToBaseMessage();

    // Then: Base message should have correct header fields
    EXPECT_EQ(base_msg.GetHeader().GetDestination(), dest);
    EXPECT_EQ(base_msg.GetHeader().GetSource(), src);
    EXPECT_EQ(base_msg.GetHeader().GetType(), MessageType::SLOT_ALLOCATION);

    // And: Payload should contain slot allocation fields
    const std::vector<uint8_t>& payload = base_msg.GetPayload();

    // Payload should be 4 bytes
    ASSERT_EQ(payload.size(), 4);

    // Check network ID (little endian)
    uint16_t extracted_network_id =
        (static_cast<uint16_t>(payload[1]) << 8) | payload[0];
    EXPECT_EQ(extracted_network_id, network_id);

    // Check allocated slots and total nodes
    EXPECT_EQ(payload[2], allocated_slots);
    EXPECT_EQ(payload[3], total_nodes);
}

/**
  * @brief Test GetTotalSize() returns the correct value
  */
TEST_F(SlotAllocationMessageTest, GetTotalSizeTest) {
    // Given: A SlotAllocation message
    ASSERT_TRUE(msg_ptr != nullptr);

    // When: Getting the total size
    size_t total_size = msg_ptr->GetTotalSize();

    // Then: Size should be 4 bytes
    EXPECT_EQ(total_size, 4);
}

}  // namespace test
}  // namespace loramesher