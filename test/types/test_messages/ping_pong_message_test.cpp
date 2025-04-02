// test/types/test_messages/ping_pong_message_test.cpp
#include <gtest/gtest.h>

#include <memory>

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <sys/resource.h>
#endif

#include "types/messages/ping_pong/ping_pong_message.hpp"

namespace loramesher {
namespace test {

class PingPongMessageTest : public ::testing::Test {
   protected:
    // Common test data
    // Test constants
    static constexpr AddressType dest = 0x1234;
    static constexpr AddressType src = 0x5678;
    static constexpr PingPongSubtype subtype = PingPongSubtype::PING;
    static constexpr uint16_t sequence_number = 0x4321;
    static constexpr uint32_t timestamp = 0x87654321;

    std::unique_ptr<PingPongMessage> msg_ptr;

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
        auto opt_msg = PingPongMessage::Create(dest, src, subtype,
                                               sequence_number, timestamp);
        ASSERT_TRUE(opt_msg.has_value()) << "Failed to create test message";
        msg_ptr = std::make_unique<PingPongMessage>(*opt_msg);
    }
};

TEST_F(PingPongMessageTest, CreationTest) {
    // Given: Common test parameters
    const AddressType test_dest = 0xABCD;
    const AddressType test_src = 0xDCBA;
    const PingPongSubtype test_subtype = PingPongSubtype::PONG;
    const uint16_t test_seq = 0x1234;
    const uint32_t test_timestamp = 0x12345678;

    // When: Creating a message
    auto opt_msg = PingPongMessage::Create(test_dest, test_src, test_subtype,
                                           test_seq, test_timestamp);

    // Then: Message creation should succeed
    ASSERT_TRUE(opt_msg.has_value()) << "Failed to create PingPong message";

    // And: Message should have correct fields
    EXPECT_EQ(opt_msg->GetDestination(), test_dest);
    EXPECT_EQ(opt_msg->GetSource(), test_src);
    EXPECT_EQ(opt_msg->GetSubtype(), test_subtype);
    EXPECT_EQ(opt_msg->GetSequenceNumber(), test_seq);
    EXPECT_EQ(opt_msg->GetTimestamp(), test_timestamp);
}

TEST_F(PingPongMessageTest, InvalidCreationTest) {
    // Test with invalid subtype
    auto opt_msg =
        PingPongMessage::Create(dest, src, static_cast<PingPongSubtype>(0xFF),
                                sequence_number, timestamp);

    // Should fail with invalid subtype
    EXPECT_FALSE(opt_msg.has_value());
}

TEST_F(PingPongMessageTest, SerializationTest) {
    // Given: A valid message
    ASSERT_TRUE(msg_ptr != nullptr);

    // When: Serializing the message
    std::optional<std::vector<uint8_t>> opt_serialized = msg_ptr->Serialize();
    ASSERT_TRUE(opt_serialized.has_value()) << "Serialization failed";

    const std::vector<uint8_t>& serialized = *opt_serialized;

    // Then: Check size and structure (BaseHeader + PingPong fields)
    const size_t expected_size =
        BaseHeader::Size() + sizeof(uint16_t) + sizeof(uint32_t);
    ASSERT_EQ(serialized.size(), expected_size) << "Incorrect serialized size";

    // And: Verify header fields in memory
    const uint8_t* data = serialized.data();
    uint16_t stored_dest = (data[1] << 8) | data[0];
    uint16_t stored_src = (data[3] << 8) | data[2];

    EXPECT_EQ(stored_dest, dest) << "Incorrect destination in serialized data";
    EXPECT_EQ(stored_src, src) << "Incorrect source in serialized data";
    EXPECT_EQ(data[4], static_cast<uint8_t>(MessageType::PING))
        << "Incorrect message type in serialized data";

    // Verify PingPong fields
    // Offset to PingPong-specific fields
    const size_t offset = BaseHeader::Size();
    // Verify sequence number (16-bit, little endian)
    uint16_t stored_seq = (data[offset + 1] << 8) | data[offset];
    EXPECT_EQ(stored_seq, sequence_number)
        << "Incorrect sequence number in serialized data";

    // Verify timestamp (32-bit, little endian)
    uint32_t stored_timestamp =
        (static_cast<uint32_t>(data[offset + 5]) << 24) |
        (static_cast<uint32_t>(data[offset + 4]) << 16) |
        (static_cast<uint32_t>(data[offset + 3]) << 8) |
        static_cast<uint32_t>(data[offset + 2]);
    EXPECT_EQ(stored_timestamp, timestamp)
        << "Incorrect timestamp in serialized data";
}

TEST_F(PingPongMessageTest, DeserializationTest) {
    // Given: A serialized message
    auto opt_serialized = msg_ptr->Serialize();
    ASSERT_TRUE(opt_serialized.has_value()) << "Failed to serialize message";

    // When: Deserializing the message
    auto opt_deserialized =
        PingPongMessage::CreateFromSerialized(*opt_serialized);
    ASSERT_TRUE(opt_deserialized.has_value())
        << "Failed to deserialize message";

    PingPongMessage deserialized_msg = std::move(*opt_deserialized);

    // Then: Verify all fields
    EXPECT_EQ(deserialized_msg.GetDestination(), dest);
    EXPECT_EQ(deserialized_msg.GetSource(), src);
    EXPECT_EQ(deserialized_msg.GetSubtype(), subtype);
    EXPECT_EQ(deserialized_msg.GetSequenceNumber(), sequence_number);
    EXPECT_EQ(deserialized_msg.GetTimestamp(), timestamp);
}

TEST_F(PingPongMessageTest, DeserializationFailureTest) {
    // Test: Empty data
    {
        std::vector<uint8_t> empty_data;
        auto result = PingPongMessage::CreateFromSerialized(empty_data);
        EXPECT_FALSE(result.has_value()) << "Should fail with empty data";
    }

    // Test: Incomplete header
    {
        std::vector<uint8_t> incomplete_data{0x01, 0x02};
        auto result = PingPongMessage::CreateFromSerialized(incomplete_data);
        EXPECT_FALSE(result.has_value())
            << "Should fail with incomplete header";
    }

    // Test: Invalid message type
    {
        auto opt_serialized = msg_ptr->Serialize();
        ASSERT_TRUE(opt_serialized.has_value());
        std::vector<uint8_t> invalid_type = *opt_serialized;
        invalid_type[4] =
            static_cast<uint8_t>(MessageType::DATA_MSG);  // Wrong message type
        auto result = PingPongMessage::CreateFromSerialized(invalid_type);
        EXPECT_FALSE(result.has_value())
            << "Should fail with invalid message type";
    }

    // Test: Invalid PingPong subtype
    // {
    //     auto opt_serialized = msg_ptr->Serialize();
    //     ASSERT_TRUE(opt_serialized.has_value());
    //     std::vector<uint8_t> invalid_subtype = *opt_serialized;
    //     invalid_subtype[BaseHeader::Size()] = 0xFF;  // Invalid subtype
    //     auto result = PingPongMessage::CreateFromSerialized(invalid_subtype);
    //     EXPECT_FALSE(result.has_value())
    //         << "Should fail with invalid PingPong subtype";
    // }
}

TEST_F(PingPongMessageTest, ConversionToBaseMessageTest) {
    // Given: A PingPong message
    ASSERT_TRUE(msg_ptr != nullptr);

    // When: Converting to BaseMessage
    BaseMessage base_msg = msg_ptr->ToBaseMessage();

    // Then: Base message should have correct header fields
    EXPECT_EQ(base_msg.GetHeader().GetDestination(), dest);
    EXPECT_EQ(base_msg.GetHeader().GetSource(), src);
    EXPECT_EQ(base_msg.GetHeader().GetType(), MessageType::PING);

    // And: Payload should contain PingPong fields
    const std::vector<uint8_t>& payload = base_msg.GetPayload();
    ASSERT_EQ(payload.size(), PingPongHeader::PingPongFieldsSize());

    // Verify sequence number (little endian)
    uint16_t stored_seq = (payload[1] << 8) | payload[0];
    EXPECT_EQ(stored_seq, sequence_number);

    // Verify timestamp (little endian)
    uint32_t stored_timestamp = (static_cast<uint32_t>(payload[5]) << 24) |
                                (static_cast<uint32_t>(payload[4]) << 16) |
                                (static_cast<uint32_t>(payload[3]) << 8) |
                                static_cast<uint32_t>(payload[2]);
    EXPECT_EQ(stored_timestamp, timestamp);
}

TEST_F(PingPongMessageTest, CalculateRTTTest) {
    // Given: A PingPong message with timestamp
    ASSERT_TRUE(msg_ptr != nullptr);

    // When: Calculating RTT with a reference timestamp
    uint32_t reference_timestamp = 0x12345678;
    uint32_t rtt = msg_ptr->CalculateRTT(reference_timestamp);

    // Then: RTT should be the difference between timestamps
    EXPECT_EQ(rtt, timestamp - reference_timestamp);
}

TEST_F(PingPongMessageTest, SetInfoTest) {
    // Given: A PingPong message
    ASSERT_TRUE(msg_ptr != nullptr);

    // When: Setting new PingPong information
    PingPongSubtype new_subtype = PingPongSubtype::PONG;
    uint16_t new_seq = 0x9876;
    uint32_t new_timestamp = 0x98765432;

    Result result = msg_ptr->SetInfo(new_subtype, new_seq, new_timestamp);

    // Then: Setting should succeed
    EXPECT_TRUE(result.IsSuccess());

    // And: Message should have updated fields
    EXPECT_EQ(msg_ptr->GetSubtype(), new_subtype);
    EXPECT_EQ(msg_ptr->GetSequenceNumber(), new_seq);
    EXPECT_EQ(msg_ptr->GetTimestamp(), new_timestamp);

    // Test setting invalid subtype
    result = msg_ptr->SetInfo(static_cast<PingPongSubtype>(0xFF), new_seq,
                              new_timestamp);
    EXPECT_FALSE(result.IsSuccess());
}

}  // namespace test
}  // namespace loramesher