// test/types/test_messages/message_test.cpp
#include <gtest/gtest.h>

#include <memory>

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <sys/resource.h>
#endif

#include "../src/types/messages/message.hpp"
#include "../src/types/messages/routing_message.hpp"

namespace loramesher {

class MessageMemoryTest : public ::testing::Test {
   protected:
    // Common test data
    const AddressType dest = 0x1234;
    const AddressType src = 0x5678;
    const std::vector<uint8_t> payload{0x01, 0x02, 0x03};

#ifndef ARDUINO
    void SetUp() override {
        // Record memory usage before test
        initial_memory = getCurrentMemoryUsage();
    }

    void TearDown() override {
        // Verify no memory leaks
        size_t final_memory = getCurrentMemoryUsage();
        EXPECT_EQ(final_memory, initial_memory);
    }

   private:
    size_t initial_memory;

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
#endif
};

TEST_F(MessageMemoryTest, SerializationTest) {
    BaseMessage msg(dest, src, MessageType::DATA, payload);

    // Serialize
    std::vector<uint8_t> serialized = msg.serialize();

    // Expected memory layout:
    // [2B dest][2B src][1B type][1B size][2B nextHop][1B seqId][2B number][payload]
    ASSERT_EQ(serialized.size(), BaseHeader::size() + payload.size());

    // Check header fields in memory
    const uint8_t* data = serialized.data();
    EXPECT_EQ((data[1] << 8) | data[0], 0x1234);  // destination
    EXPECT_EQ((data[3] << 8) | data[2], 0x5678);  // source
    EXPECT_EQ(data[4], static_cast<uint8_t>(MessageType::DATA));
    EXPECT_EQ(data[5], payload.size());
    EXPECT_EQ(memcmp(data + BaseHeader::size(), payload.data(), payload.size()),
              0);
}

TEST_F(MessageMemoryTest, DeserializationTest) {
    BaseMessage msg(dest, src, MessageType::DATA, payload);

    // Serialize
    std::vector<uint8_t> serialized = msg.serialize();

    // Deserialize
    auto deserialized = BaseMessage::deserialize(serialized);

    // Check header fields
    EXPECT_EQ(deserialized->getBaseHeader().destination, dest);
    EXPECT_EQ(deserialized->getBaseHeader().source, src);
    EXPECT_EQ(deserialized->getBaseHeader().type, MessageType::DATA);
    EXPECT_EQ(deserialized->getBaseHeader().payloadSize, payload.size());

    // Check payload
    EXPECT_EQ(deserialized->getPayload(), payload);
}

TEST_F(MessageMemoryTest, RoutingSerializationTest) {
    RoutingMessage msg(dest, src, payload);
    msg.setRoutingInfo(0xABCD, 0x42, 0x0001);

    ASSERT_EQ(msg.getTotalSize(),
              RoutingHeader::size() + BaseHeader::size() + payload.size());

    // Serialize
    std::vector<uint8_t> serialized = msg.serialize();

    // Expected memory layout:
    // [2B dest][2B src][1B type][1B size][2B nextHop][1B seqId][2B number][payload]
    ASSERT_EQ(serialized.size(),
              RoutingHeader::size() + BaseHeader::size() + payload.size());

    // Check header fields in memory
    const uint8_t* data = serialized.data();
    EXPECT_EQ((data[1] << 8) | data[0], 0x1234);  // destination
    EXPECT_EQ((data[3] << 8) | data[2], 0x5678);  // source
    EXPECT_EQ(data[4], static_cast<uint8_t>(MessageType::DATA));
    EXPECT_EQ(data[5], payload.size());
    EXPECT_EQ((data[7] << 8) | data[6], 0xABCD);   // nextHop
    EXPECT_EQ(data[8], 0x42);                      // sequenceId
    EXPECT_EQ((data[10] << 8) | data[9], 0x0001);  // number

    // Check payload
    EXPECT_EQ(memcmp(data + RoutingHeader::size() + BaseHeader::size(),
                     payload.data(), payload.size()),
              0);
}

TEST_F(MessageMemoryTest, RoutingDeserializationTest) {
    RoutingMessage msg(dest, src, payload);
    msg.setRoutingInfo(0xABCD, 0x42, 0x0001);

    // Serialize
    std::vector<uint8_t> serialized = msg.serialize();

    // Deserialize
    auto deserialized = RoutingMessage::deserialize(serialized);

    // Check header fields
    EXPECT_EQ(deserialized->getBaseHeader().destination, dest);
    EXPECT_EQ(deserialized->getBaseHeader().source, src);
    EXPECT_EQ(deserialized->getBaseHeader().type, MessageType::DATA);
    EXPECT_EQ(deserialized->getBaseHeader().payloadSize, payload.size());

    // Check routing header fields
    RoutingHeader routingHeader = deserialized->getRoutingHeader();
    EXPECT_EQ(routingHeader.nextHop, 0xABCD);
    EXPECT_EQ(routingHeader.sequenceId, 0x42);
    EXPECT_EQ(routingHeader.number, 0x0001);

    // Check payload
    EXPECT_EQ(deserialized->getPayload(), payload);
}

// Test copy constructor
TEST_F(MessageMemoryTest, CopyConstructorTest) {
    {
        BaseMessage original(dest, src, MessageType::DATA, payload);
        BaseMessage copy(original);

        // Verify independent copies
        EXPECT_EQ(copy.getPayload(), original.getPayload());
        EXPECT_NE(copy.getPayload().data(), original.getPayload().data());
    }
    // Both objects should be properly destroyed here
}

// Test copy assignment
TEST_F(MessageMemoryTest, CopyAssignmentTest) {
    BaseMessage original(dest, src, MessageType::DATA, payload);
    {
        BaseMessage copy(0x0000, 0x0000, MessageType::ACK,
                         std::vector<uint8_t>{0xFF});
        copy = original;

        // Verify independent copies
        EXPECT_EQ(copy.getPayload(), original.getPayload());
        EXPECT_NE(copy.getPayload().data(), original.getPayload().data());
    }
    // Copy should be destroyed, original should still be valid
    EXPECT_EQ(original.getPayload(), payload);
}

// Test move constructor
TEST_F(MessageMemoryTest, MoveConstructorTest) {
    const uint8_t* originalDataPtr = nullptr;
    {
        BaseMessage original(dest, src, MessageType::DATA, payload);
        originalDataPtr = original.getPayload().data();

        BaseMessage moved(std::move(original));

        // Verify moved data
        EXPECT_EQ(moved.getPayload().data(), originalDataPtr);
        EXPECT_EQ(moved.getPayload(), payload);

        // Original should be in valid but unspecified state
        EXPECT_TRUE(original.getPayload().empty());
    }
}

// Test move assignment
TEST_F(MessageMemoryTest, MoveAssignmentTest) {
    BaseMessage original(dest, src, MessageType::DATA, payload);
    const uint8_t* originalDataPtr = original.getPayload().data();

    {
        BaseMessage target(0x0000, 0x0000, MessageType::ACK,
                           std::vector<uint8_t>{0xFF});
        target = std::move(original);

        // Verify moved data
        EXPECT_EQ(target.getPayload().data(), originalDataPtr);
        EXPECT_EQ(target.getPayload(), payload);

        // Original should be in valid but unspecified state
        EXPECT_TRUE(original.getPayload().empty());
    }
}

// TODO:
// Test large payload handling
// TEST_F(MessageMemoryTest, LargePayloadTest) {
//     const size_t largeSize = 1024 * 1024;  // 1MB
//     std::vector<uint8_t> largePayload(largeSize, 0xFF);

//     {
//         BaseMessage msg(dest, src, MessageType::XL_DATA, largePayload);
//         EXPECT_EQ(msg.getPayload().size(), largeSize);

//         // Test copy with large payload
//         BaseMessage copy(msg);
//         EXPECT_EQ(copy.getPayload().size(), largeSize);
//         EXPECT_NE(copy.getPayload().data(), msg.getPayload().data());

//         // Test move with large payload
//         BaseMessage moved(std::move(copy));
//         EXPECT_EQ(moved.getPayload().size(), largeSize);
//         EXPECT_TRUE(copy.getPayload().empty());
//     }
// }

// Test exception safety
TEST_F(MessageMemoryTest, ExceptionSafetyTest) {
    BaseMessage original(dest, src, MessageType::DATA, payload);
    const auto originalPayload = original.getPayload();

    try {
        // Force exception by creating message with invalid data
        std::vector<uint8_t> invalidPayload;
        invalidPayload.resize(std::numeric_limits<uint8_t>::max() + 1);
        BaseMessage invalid(dest, src, MessageType::DATA, invalidPayload);
        FAIL() << "Expected exception not thrown";
    } catch (const std::exception& e) {
        // Original should remain unchanged
        EXPECT_EQ(original.getPayload(), originalPayload);
    }
}

// Test for routing message memory management
TEST_F(MessageMemoryTest, RoutingMessageMemoryTest) {
    const AddressType nextHop = 0xABCD;
    const uint8_t seqId = 0x42;
    const uint16_t number = 0x0001;

    {
        RoutingMessage original(dest, src, payload);
        original.setRoutingInfo(nextHop, seqId, number);

        // Test copy
        RoutingMessage copy(original);
        EXPECT_EQ(copy.getRoutingHeader().nextHop, nextHop);
        EXPECT_NE(copy.getPayload().data(), original.getPayload().data());

        // Test move
        RoutingMessage moved(std::move(copy));
        EXPECT_EQ(moved.getRoutingHeader().nextHop, nextHop);
        EXPECT_TRUE(copy.getPayload().empty());
    }
}

// Test chained operations
TEST_F(MessageMemoryTest, ChainedOperationsTest) {
    std::vector<std::unique_ptr<BaseMessage>> messages;

    // Create and move messages in a chain
    for (int i = 0; i < 10; ++i) {
        auto msg = std::make_unique<BaseMessage>(dest, src, MessageType::DATA,
                                                 payload);
        messages.push_back(std::move(msg));
        EXPECT_TRUE(msg ==
                    nullptr);  // Original pointer should be null after move
    }

    // Verify all messages are valid
    for (const auto& msg : messages) {
        EXPECT_EQ(msg->getPayload(), payload);
    }
}

// Test boundary conditions
TEST_F(MessageMemoryTest, BoundaryConditionsTest) {
    // Empty payload
    {
        BaseMessage msg(dest, src, MessageType::DATA, std::vector<uint8_t>{});
        EXPECT_EQ(msg.getPayload().size(), 0);
        EXPECT_EQ(msg.getTotalSize(), BaseHeader::size());
    }

    // Maximum size payload
    {
        std::vector<uint8_t> maxPayload(std::numeric_limits<uint8_t>::max());
        BaseMessage msg(dest, src, MessageType::DATA, maxPayload);
        EXPECT_EQ(msg.getPayload().size(), std::numeric_limits<uint8_t>::max());
    }
}

// Additional validation tests
TEST_F(MessageMemoryTest, PayloadSizeValidationTest) {
    // Test exactly at the limit
    std::vector<uint8_t> maxPayload(BaseMessage::MAX_PAYLOAD_SIZE, 0xFF);
    EXPECT_NO_THROW(
        { BaseMessage msg(dest, src, MessageType::DATA, maxPayload); });

    // Test one byte over the limit
    std::vector<uint8_t> tooLargePayload(BaseMessage::MAX_PAYLOAD_SIZE + 1,
                                         0xFF);
    EXPECT_THROW(
        { BaseMessage msg(dest, src, MessageType::DATA, tooLargePayload); },
        std::length_error);
}

// TODO:
// TEST_F(MessageMemoryTest, AddressValidationTest) {
//     EXPECT_THROW(
//         { BaseMessage msg(0, src, MessageType::DATA, payload); },
//         std::invalid_argument);

//     EXPECT_THROW(
//         { BaseMessage msg(dest, 0, MessageType::DATA, payload); },
//         std::invalid_argument);
// }

TEST_F(MessageMemoryTest, MessageTypeValidationTest) {
    EXPECT_THROW(
        {
            BaseMessage msg(dest, src, static_cast<MessageType>(0xFF), payload);
        },
        std::invalid_argument);
}

}  // namespace loramesher

#ifdef ARDUINO
#include <Arduino.h>

void setup() {
    // should be the same value as for the `test_speed` option in "platformio.ini"
    // default value is test_speed=115200
    Serial.begin(115200);

    ::testing::InitGoogleTest();
}

void loop() {
    // Run tests
    if (RUN_ALL_TESTS())
        ;

    // sleep 1 sec
    delay(1000);
}

#else
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    if (RUN_ALL_TESTS())
        ;
    // Always return zero-code and allow PlatformIO to parse results
    return 0;
}
#endif