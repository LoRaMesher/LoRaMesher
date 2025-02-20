// test/types/test_messages/message_test.cpp
#include <gtest/gtest.h>

#include <memory>

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <sys/resource.h>
#endif

#include "../src/types/messages/message.hpp"

namespace loramesher {
namespace test {

class MessageMemoryTest : public ::testing::Test {
   protected:
    // Common test data
    // Test constants
    static constexpr AddressType dest = 0x1234;
    static constexpr AddressType src = 0x5678;
    static const inline std::vector<uint8_t> payload{0x01, 0x02, 0x03};

    std::unique_ptr<BaseMessage> msg_ptr;

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
            BaseMessage::Create(dest, src, MessageType::DATA, payload);
        ASSERT_TRUE(opt_msg.has_value()) << "Failed to create test message";
        msg_ptr = std::make_unique<BaseMessage>(*opt_msg);
    }
};

TEST_F(MessageMemoryTest, SerializationTest) {
    // Given: A valid message
    ASSERT_TRUE(msg_ptr != nullptr);

    // When: Serializing the message
    std::optional<std::vector<uint8_t>> opt_serialized = msg_ptr->Serialize();
    ASSERT_TRUE(opt_serialized.has_value()) << "Serialization failed";

    const std::vector<uint8_t>& serialized = *opt_serialized;

    // Then: Check size and structure
    ASSERT_EQ(serialized.size(), BaseHeader::size() + payload.size())
        << "Incorrect serialized size";

    // And: Verify header fields in memory
    const uint8_t* data = serialized.data();
    uint16_t stored_dest = (data[1] << 8) | data[0];
    uint16_t stored_src = (data[3] << 8) | data[2];

    EXPECT_EQ(stored_dest, dest) << "Incorrect destination in serialized data";
    EXPECT_EQ(stored_src, src) << "Incorrect source in serialized data";
    EXPECT_EQ(data[4], static_cast<uint8_t>(MessageType::DATA))
        << "Incorrect message type in serialized data";
    EXPECT_EQ(data[5], payload.size())
        << "Incorrect payload size in serialized data";

    // And: Verify payload
    EXPECT_EQ(memcmp(data + BaseHeader::size(), payload.data(), payload.size()),
              0)
        << "Payload mismatch in serialized data";
}

/**
 * @brief Test deserialization functionality and error cases
 */
TEST_F(MessageMemoryTest, DeserializationTest) {
    // Given: A serialized message
    auto opt_serialized = msg_ptr->Serialize();
    ASSERT_TRUE(opt_serialized.has_value()) << "Failed to serialize message";

    // When: Deserializing the message
    auto opt_deserialized = BaseMessage::CreateFromSerialized(*opt_serialized);
    ASSERT_TRUE(opt_deserialized.has_value())
        << "Failed to deserialize message";

    BaseMessage deserialized_msg = std::move(*opt_deserialized);

    // Then: Verify header fields
    const auto& header = deserialized_msg.getBaseHeader();
    EXPECT_EQ(header.destination, dest) << "Incorrect deserialized destination";
    EXPECT_EQ(header.source, src) << "Incorrect deserialized source";
    EXPECT_EQ(header.type, MessageType::DATA)
        << "Incorrect deserialized message type";
    EXPECT_EQ(header.payloadSize, payload.size())
        << "Incorrect deserialized payload size";

    // And: Verify payload
    EXPECT_EQ(deserialized_msg.getPayload(), payload)
        << "Incorrect deserialized payload";
}

/**
 * @brief Test deserialization error handling
 */
TEST_F(MessageMemoryTest, DeserializationFailureTest) {
    // Test: Empty data
    {
        std::vector<uint8_t> empty_data;
        auto result = BaseMessage::CreateFromSerialized(empty_data);
        EXPECT_FALSE(result.has_value()) << "Should fail with empty data";
    }

    // Test: Incomplete header
    {
        std::vector<uint8_t> incomplete_data{0x01, 0x02, 0x03};
        auto result = BaseMessage::CreateFromSerialized(incomplete_data);
        EXPECT_FALSE(result.has_value())
            << "Should fail with incomplete header";
    }

    // Test: Invalid message type
    {
        auto opt_serialized = msg_ptr->Serialize();
        ASSERT_TRUE(opt_serialized.has_value());
        std::vector<uint8_t> invalid_type = *opt_serialized;
        invalid_type[4] = 0xFF;  // Invalid message type
        auto result = BaseMessage::CreateFromSerialized(invalid_type);
        EXPECT_FALSE(result.has_value())
            << "Should fail with invalid message type";
    }
}

// Test copy constructor
TEST_F(MessageMemoryTest, CopyConstructorTest) {
    {
        BaseMessage msg = *msg_ptr;
        BaseMessage copy(msg);

        // Verify independent copies
        EXPECT_EQ(copy.getPayload(), msg_ptr->getPayload());
        EXPECT_NE(copy.getPayload().data(), msg_ptr->getPayload().data());
    }
    // Both objects should be properly destroyed here
}

// Test copy assignment
TEST_F(MessageMemoryTest, CopyAssignmentTest) {
    {
        std::optional<loramesher::BaseMessage> opt_copy = BaseMessage::Create(
            0x0000, 0x0000, MessageType::ACK, std::vector<uint8_t>{0xFF});
        if (!opt_copy) {
            FAIL() << "Failed to create message";
        }

        BaseMessage copy = *opt_copy;
        BaseMessage msg = *msg_ptr;
        copy = msg;

        // Verify independent copies
        EXPECT_EQ(copy.getPayload(), msg_ptr->getPayload());
        EXPECT_NE(copy.getPayload().data(), msg_ptr->getPayload().data());
    }
    // Copy should be destroyed, original should still be valid
    EXPECT_EQ(msg_ptr->getPayload(), payload);
}

// Test move constructor
TEST_F(MessageMemoryTest, MoveConstructorTest) {
    const uint8_t* originalDataPtr = nullptr;
    {
        originalDataPtr = msg_ptr->getPayload().data();
        BaseMessage moved(std::move(*msg_ptr));

        // Verify moved data
        EXPECT_EQ(moved.getPayload().data(), originalDataPtr);
        EXPECT_EQ(moved.getPayload(), payload);

        // Original should be in valid but unspecified state
        EXPECT_TRUE(msg_ptr->getPayload().empty());
    }
}

// Test move assignment
TEST_F(MessageMemoryTest, MoveAssignmentTest) {
    // Given: A source message with known payload
    BaseMessage source_msg = *msg_ptr;

    // Create target with different content
    auto opt_target = BaseMessage::Create(0x0000, 0x0000, MessageType::ACK,
                                          std::vector<uint8_t>{0xFF});
    ASSERT_TRUE(opt_target.has_value()) << "Failed to create target message";
    BaseMessage target_msg = std::move(*opt_target);

    // Store the original payload for comparison
    const std::vector<uint8_t> original_payload = source_msg.getPayload();

    // When: Moving source to target
    target_msg = std::move(source_msg);

    // Then: Target should have the original payload
    EXPECT_EQ(target_msg.getPayload(), original_payload)
        << "Target payload doesn't match original";

    // And: Source should be empty but valid
    EXPECT_TRUE(source_msg.getPayload().empty())
        << "Source message not empty after move";

    // And: Source and target should have different payload storage
    EXPECT_NE(source_msg.getPayload().data(), target_msg.getPayload().data())
        << "Source and target point to same storage after move";
}

// Test error safety
TEST_F(MessageMemoryTest, CreateErrorTest) {
    const auto originalPayload = msg_ptr->getPayload();

    // Force exception by creating message with invalid data
    std::vector<uint8_t> invalidPayload;
    invalidPayload.resize(std::numeric_limits<uint8_t>::max() + 1);
    std::optional<loramesher::BaseMessage> opt_msg =
        BaseMessage::Create(0, 0, MessageType::DATA, invalidPayload);

    if (opt_msg) {
        FAIL() << "Expected optional to be empty";
    }

    // Original should remain unchanged
    EXPECT_EQ(msg_ptr->getPayload(), originalPayload);
}

// Test chained operations
TEST_F(MessageMemoryTest, ChainedOperationsTest) {
    std::vector<std::unique_ptr<BaseMessage>> messages;

    // Create and move messages in a chain
    for (int i = 0; i < 10; ++i) {
        auto opt_msg =
            BaseMessage::Create(dest, src, MessageType::DATA, payload);
        if (!opt_msg) {
            FAIL() << "Failed to create message";
        }
        auto ptr_msg = std::make_unique<BaseMessage>(*opt_msg);

        messages.push_back(std::move(ptr_msg));
        EXPECT_TRUE(ptr_msg ==
                    nullptr);  // Original pointer should be null after move
    }

    // Verify all messages are valid
    for (const auto& msg : messages) {
        EXPECT_EQ(msg_ptr->getPayload(), payload);
    }
}

// Test boundary conditions
TEST_F(MessageMemoryTest, BoundaryConditionsTest) {
    // Empty payload
    {
        Result result =
            msg_ptr->setBaseHeader(dest, src, MessageType::DATA, {});
        EXPECT_TRUE(result.IsSuccess());

        EXPECT_EQ(msg_ptr->getPayload().size(), 0);
        EXPECT_EQ(msg_ptr->getTotalSize(), BaseHeader::size());
    }

    // Maximum size payload
    {
        uint8_t max_payload_size = std::numeric_limits<uint8_t>::max();
        std::vector<uint8_t> max_payload(max_payload_size);
        Result result =
            msg_ptr->setBaseHeader(dest, src, MessageType::DATA, max_payload);
        EXPECT_TRUE(result.IsSuccess());
        EXPECT_EQ(msg_ptr->getPayload().size(),
                  std::numeric_limits<uint8_t>::max());
    }
}

// Additional validation tests
TEST_F(MessageMemoryTest, PayloadSizeValidationTest) {
    // Test exactly at the limit
    std::vector<uint8_t> maxPayload(BaseMessage::MAX_PAYLOAD_SIZE, 0xFF);

    Result result =
        msg_ptr->setBaseHeader(dest, src, MessageType::DATA, maxPayload);
    EXPECT_TRUE(result.IsSuccess());

    // Test one byte over the limit
    std::vector<uint8_t> tooLargePayload(BaseMessage::MAX_PAYLOAD_SIZE + 1,
                                         0xFF);
    result =
        msg_ptr->setBaseHeader(dest, src, MessageType::DATA, tooLargePayload);
    EXPECT_FALSE(result.IsSuccess());
}

TEST_F(MessageMemoryTest, MessageTypeValidationTest) {
    // Test valid message type
    Result result = msg_ptr->setBaseHeader(
        dest, src, static_cast<MessageType>(0xFF), payload);
    EXPECT_FALSE(result.IsSuccess());
}

}  // namespace test
}  // namespace loramesher