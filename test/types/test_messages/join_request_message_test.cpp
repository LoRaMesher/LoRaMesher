/**
 * @file join_request_message_test.cpp
 * @brief Unit tests for JoinRequestMessage class
 */

#include <gtest/gtest.h>

#include <memory>

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <sys/resource.h>
#endif

#include "types/messages/loramesher/join_request_message.hpp"

namespace loramesher {
namespace test {

/**
 * @brief Test fixture for JoinRequestMessage tests
 */
class JoinRequestMessageTest : public ::testing::Test {
   protected:
    // Common test data
    static constexpr AddressType dest = 0x1234;
    static constexpr AddressType src = 0x5678;
    static constexpr uint8_t capabilities =
        JoinRequestHeader::NodeCapabilities::ROUTER |
        JoinRequestHeader::NodeCapabilities::SENSOR_NODE;
    static constexpr uint8_t battery_level = 75;
    static constexpr uint8_t requested_slots = 3;
    std::vector<uint8_t> additional_info{0xAA, 0xBB, 0xCC};

    std::unique_ptr<JoinRequestMessage> msg_ptr;

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
        auto opt_msg =
            JoinRequestMessage::Create(dest, src, capabilities, battery_level,
                                       requested_slots, additional_info);
        ASSERT_TRUE(opt_msg.has_value()) << "Failed to create test message";
        msg_ptr = std::make_unique<JoinRequestMessage>(*opt_msg);
    }
};

/**
 * @brief Test creating a JoinRequestMessage with valid parameters
 */
TEST_F(JoinRequestMessageTest, CreationTest) {
    // Given: Test parameters
    const AddressType test_dest = 0xABCD;
    const AddressType test_src = 0xDCBA;
    const uint8_t test_capabilities =
        JoinRequestHeader::NodeCapabilities::GATEWAY |
        JoinRequestHeader::NodeCapabilities::BATTERY_POWERED;
    const uint8_t test_battery = 90;
    const uint8_t test_slots = 2;
    const std::vector<uint8_t> test_info{0x11, 0x22, 0x33, 0x44};

    // When: Creating a message
    auto opt_msg =
        JoinRequestMessage::Create(test_dest, test_src, test_capabilities,
                                   test_battery, test_slots, test_info);

    // Then: Message creation should succeed
    ASSERT_TRUE(opt_msg.has_value()) << "Failed to create JoinRequest message";

    // And: Message should have correct fields
    EXPECT_EQ(opt_msg->GetDestination(), test_dest);
    EXPECT_EQ(opt_msg->GetSource(), test_src);
    EXPECT_EQ(opt_msg->GetCapabilities(), test_capabilities);
    EXPECT_EQ(opt_msg->GetBatteryLevel(), test_battery);
    EXPECT_EQ(opt_msg->GetRequestedSlots(), test_slots);
    EXPECT_EQ(opt_msg->GetAdditionalInfo(), test_info);
}

/**
 * @brief Test creating a JoinRequestMessage with sponsor address
 */
TEST_F(JoinRequestMessageTest, CreationWithSponsorTest) {
    // Given: Test parameters with sponsor address
    const AddressType test_dest = 0xABCD;
    const AddressType test_src = 0xDCBA;
    const AddressType test_sponsor = 0xEF01;
    const uint8_t test_capabilities =
        JoinRequestHeader::NodeCapabilities::GATEWAY |
        JoinRequestHeader::NodeCapabilities::BATTERY_POWERED;
    const uint8_t test_battery = 90;
    const uint8_t test_slots = 2;
    const std::vector<uint8_t> test_info{0x11, 0x22, 0x33, 0x44};

    // When: Creating a message with sponsor address
    auto opt_msg = JoinRequestMessage::Create(
        test_dest, test_src, test_capabilities, test_battery, test_slots,
        test_info, 0, test_sponsor);

    // Then: Message creation should succeed
    ASSERT_TRUE(opt_msg.has_value())
        << "Failed to create JoinRequest message with sponsor";

    // And: Message should have correct fields including sponsor address
    EXPECT_EQ(opt_msg->GetDestination(), test_dest);
    EXPECT_EQ(opt_msg->GetSource(), test_src);
    EXPECT_EQ(opt_msg->GetCapabilities(), test_capabilities);
    EXPECT_EQ(opt_msg->GetBatteryLevel(), test_battery);
    EXPECT_EQ(opt_msg->GetRequestedSlots(), test_slots);
    EXPECT_EQ(opt_msg->GetAdditionalInfo(), test_info);
    EXPECT_EQ(opt_msg->GetHeader().GetSponsorAddress(), test_sponsor);
}

/**
 * @brief Test creating a JoinRequestMessage without sponsor address (default 0)
 */
TEST_F(JoinRequestMessageTest, CreationWithoutSponsorTest) {
    // Given: Test parameters without explicit sponsor address
    const AddressType test_dest = 0xABCD;
    const AddressType test_src = 0xDCBA;
    const uint8_t test_capabilities =
        JoinRequestHeader::NodeCapabilities::GATEWAY;
    const uint8_t test_battery = 90;
    const uint8_t test_slots = 2;

    // When: Creating a message without sponsor address
    auto opt_msg = JoinRequestMessage::Create(
        test_dest, test_src, test_capabilities, test_battery, test_slots);

    // Then: Message creation should succeed
    ASSERT_TRUE(opt_msg.has_value())
        << "Failed to create JoinRequest message without sponsor";

    // And: Sponsor address should default to 0
    EXPECT_EQ(opt_msg->GetHeader().GetSponsorAddress(), 0);
}

/**
 * @brief Test failing to create a JoinRequestMessage with invalid battery level
 */
TEST_F(JoinRequestMessageTest, InvalidCreationTest) {
    // Battery level should be between 0-100
    auto opt_msg = JoinRequestMessage::Create(dest, src, capabilities, 101,
                                              requested_slots, additional_info);

    // Should fail with invalid battery level
    EXPECT_FALSE(opt_msg.has_value());
}

/**
 * @brief Test creating a JoinRequestMessage with no additional info
 */
TEST_F(JoinRequestMessageTest, CreationWithoutAdditionalInfoTest) {
    // When: Creating a message without additional info
    auto opt_msg = JoinRequestMessage::Create(dest, src, capabilities,
                                              battery_level, requested_slots);

    // Then: Message creation should succeed
    ASSERT_TRUE(opt_msg.has_value())
        << "Failed to create JoinRequest message without additional info";

    // And: Additional info should be empty
    EXPECT_TRUE(opt_msg->GetAdditionalInfo().empty());
}

/**
 * @brief Test serializing a JoinRequestMessage
 */
TEST_F(JoinRequestMessageTest, SerializationTest) {
    // Given: A valid message
    ASSERT_TRUE(msg_ptr != nullptr);

    // When: Serializing the message
    std::optional<std::vector<uint8_t>> opt_serialized = msg_ptr->Serialize();
    ASSERT_TRUE(opt_serialized.has_value()) << "Serialization failed";

    const std::vector<uint8_t>& serialized = *opt_serialized;

    // Then: Check size (BaseHeader + JoinRequest fields + additional info)
    const size_t expected_size = BaseHeader::Size() +
                                 JoinRequestHeader::JoinRequestFieldsSize() +
                                 additional_info.size();
    ASSERT_EQ(serialized.size(), expected_size) << "Incorrect serialized size";

    // We can't easily test exact byte values because of the serialization format,
    // but we can verify the size matches what we expect
}

/**
 * @brief Test deserializing a JoinRequestMessage
 */
TEST_F(JoinRequestMessageTest, DeserializationTest) {
    // Given: A serialized message
    auto opt_serialized = msg_ptr->Serialize();
    ASSERT_TRUE(opt_serialized.has_value()) << "Failed to serialize message";

    // When: Deserializing the message
    auto opt_deserialized =
        JoinRequestMessage::CreateFromSerialized(*opt_serialized);
    ASSERT_TRUE(opt_deserialized.has_value())
        << "Failed to deserialize message";

    JoinRequestMessage deserialized_msg = std::move(*opt_deserialized);

    // Then: Verify all fields
    EXPECT_EQ(deserialized_msg.GetDestination(), dest);
    EXPECT_EQ(deserialized_msg.GetSource(), src);
    EXPECT_EQ(deserialized_msg.GetCapabilities(), capabilities);
    EXPECT_EQ(deserialized_msg.GetBatteryLevel(), battery_level);
    EXPECT_EQ(deserialized_msg.GetRequestedSlots(), requested_slots);
    EXPECT_EQ(deserialized_msg.GetAdditionalInfo(), additional_info);
    EXPECT_EQ(deserialized_msg.GetHeader().GetSponsorAddress(),
              0);  // Default sponsor address
}

/**
 * @brief Test serialization/deserialization with sponsor address
 */
TEST_F(JoinRequestMessageTest, SponsorSerializationDeserializationTest) {
    // Given: A message with sponsor address
    const AddressType sponsor_address = 0xCAFE;
    auto opt_msg = JoinRequestMessage::Create(
        dest, src, capabilities, battery_level, requested_slots,
        additional_info, 0, sponsor_address);
    ASSERT_TRUE(opt_msg.has_value()) << "Failed to create message with sponsor";

    JoinRequestMessage sponsor_msg = std::move(*opt_msg);

    // When: Serializing and then deserializing
    auto opt_serialized = sponsor_msg.Serialize();
    ASSERT_TRUE(opt_serialized.has_value())
        << "Failed to serialize message with sponsor";

    auto opt_deserialized =
        JoinRequestMessage::CreateFromSerialized(*opt_serialized);
    ASSERT_TRUE(opt_deserialized.has_value())
        << "Failed to deserialize message with sponsor";

    // Then: Sponsor address should be preserved
    EXPECT_EQ(opt_deserialized->GetHeader().GetSponsorAddress(),
              sponsor_address);

    // And: All other fields should also be preserved
    EXPECT_EQ(opt_deserialized->GetDestination(), dest);
    EXPECT_EQ(opt_deserialized->GetSource(), src);
    EXPECT_EQ(opt_deserialized->GetCapabilities(), capabilities);
    EXPECT_EQ(opt_deserialized->GetBatteryLevel(), battery_level);
    EXPECT_EQ(opt_deserialized->GetRequestedSlots(), requested_slots);
    EXPECT_EQ(opt_deserialized->GetAdditionalInfo(), additional_info);
}

/**
 * @brief Test deserializing a JoinRequestMessage with invalid data
 */
TEST_F(JoinRequestMessageTest, DeserializationFailureTest) {
    // Test: Empty data
    {
        std::vector<uint8_t> empty_data;
        auto result = JoinRequestMessage::CreateFromSerialized(empty_data);
        EXPECT_FALSE(result.has_value()) << "Should fail with empty data";
    }

    // Test: Incomplete header
    {
        std::vector<uint8_t> incomplete_data(BaseHeader::Size() - 1, 0);
        auto result = JoinRequestMessage::CreateFromSerialized(incomplete_data);
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

        auto result = JoinRequestMessage::CreateFromSerialized(invalid_type);
        EXPECT_FALSE(result.has_value())
            << "Should fail with invalid message type";
    }
}

/**
 * @brief Test conversion to BaseMessage
 */
TEST_F(JoinRequestMessageTest, ConversionToBaseMessageTest) {
    // Given: A JoinRequest message
    ASSERT_TRUE(msg_ptr != nullptr);

    // When: Converting to BaseMessage
    BaseMessage base_msg = msg_ptr->ToBaseMessage();

    // Then: Base message should have correct header fields
    EXPECT_EQ(base_msg.GetHeader().GetDestination(), dest);
    EXPECT_EQ(base_msg.GetHeader().GetSource(), src);
    EXPECT_EQ(base_msg.GetHeader().GetType(), MessageType::JOIN_REQUEST);

    // And: Payload should contain JoinRequest fields + additional info
    const std::vector<uint8_t>& payload = base_msg.GetPayload();

    ASSERT_EQ(payload.size(), JoinRequestHeader::JoinRequestFieldsSize() +
                                  additional_info.size());

    // Check capabilities, battery level, slots
    EXPECT_EQ(payload[0], capabilities);
    EXPECT_EQ(payload[1], battery_level);
    EXPECT_EQ(payload[2], requested_slots);

    // Check additional info
    for (size_t i = 0; i < additional_info.size(); i++) {
        EXPECT_EQ(payload[JoinRequestHeader::JoinRequestFieldsSize() + i],
                  additional_info[i]);
    }
}

/**
 * @brief Test GetTotalSize() returns the correct value
 */
TEST_F(JoinRequestMessageTest, GetTotalSizeTest) {
    // Given: A JoinRequest message
    ASSERT_TRUE(msg_ptr != nullptr);

    // When: Getting the total size
    size_t total_size = msg_ptr->GetTotalSize();

    // Then: Size should be correct
    const size_t expected_size = JoinRequestHeader::JoinRequestFieldsSize() +
                                 BaseHeader::Size() + additional_info.size();
    EXPECT_EQ(total_size, expected_size);
}

/**
 * @brief Test retrieving the header directly
 */
TEST_F(JoinRequestMessageTest, GetHeaderTest) {
    // Given: A JoinRequest message
    ASSERT_TRUE(msg_ptr != nullptr);

    // When: Getting the header
    const JoinRequestHeader& header = msg_ptr->GetHeader();

    // Then: Header should have correct values
    EXPECT_EQ(header.GetDestination(), dest);
    EXPECT_EQ(header.GetSource(), src);
    EXPECT_EQ(header.GetType(), MessageType::JOIN_REQUEST);
    EXPECT_EQ(header.GetCapabilities(), capabilities);
    EXPECT_EQ(header.GetBatteryLevel(), battery_level);
    EXPECT_EQ(header.GetRequestedSlots(), requested_slots);
    EXPECT_EQ(header.GetSponsorAddress(), 0);  // Default sponsor address
}

}  // namespace test
}  // namespace loramesher