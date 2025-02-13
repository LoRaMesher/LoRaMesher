// test/types/test_messages/routing_message_test.cpp
#include <gtest/gtest.h>

#include <memory>

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <sys/resource.h>
#endif

#include "../src/types/messages/routing_message.hpp"

namespace loramesher {

class RoutingMessageTest : public ::testing::Test {
   protected:
    // Common test data
    const AddressType dest = 0x1234;
    const AddressType src = 0x5678;
    const std::vector<uint8_t> payload{0x01, 0x02, 0x03};

    std::unique_ptr<RoutingMessage> msg_ptr;

#ifdef ARDUINO
    void SetUp() override { CreateMessage(); }
#else

    void SetUp() override {
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
        auto opt_msg = RoutingMessage::Create(dest, src, payload);
        ASSERT_TRUE(opt_msg.has_value()) << "Failed to create test message";
        msg_ptr = std::make_unique<RoutingMessage>(*opt_msg);
    }
};

TEST_F(RoutingMessageTest, RoutingSerializationTest) {
    msg_ptr->setRoutingInfo(0xABCD, 0x42, 0x0001);

    ASSERT_EQ(msg_ptr->getTotalSize(),
              RoutingHeader::size() + BaseHeader::size() + payload.size());

    // Serialize
    std::optional<std::vector<uint8_t>> opt_serialized = msg_ptr->Serialize();
    ASSERT_TRUE(opt_serialized.has_value());

    std::vector<uint8_t> serialized = *opt_serialized;

    // Expected memory layout:
    // [2B dest][2B src][1B type][1B size][2B nextHop][1B seqId][2B number][payload]
    ASSERT_EQ(serialized.size(),
              RoutingHeader::size() + BaseHeader::size() + payload.size());

    // Check header fields in memory
    const uint8_t* data = serialized.data();
    EXPECT_EQ((data[1] << 8) | data[0], 0x1234);  // destination
    EXPECT_EQ((data[3] << 8) | data[2], 0x5678);  // source
    EXPECT_EQ(data[4], static_cast<uint8_t>(MessageType::ROUTING_MSG));
    EXPECT_EQ(data[5], payload.size());
    EXPECT_EQ((data[7] << 8) | data[6], 0xABCD);   // nextHop
    EXPECT_EQ(data[8], 0x42);                      // sequenceId
    EXPECT_EQ((data[10] << 8) | data[9], 0x0001);  // number

    // Check payload
    EXPECT_EQ(memcmp(data + RoutingHeader::size() + BaseHeader::size(),
                     payload.data(), payload.size()),
              0);
}

TEST_F(RoutingMessageTest, RoutingDeserializationTest) {
    msg_ptr->setRoutingInfo(0xABCD, 0x42, 0x0001);

    // Serialize
    std::optional<std::vector<uint8_t>> opt_serialized = msg_ptr->Serialize();
    ASSERT_TRUE(opt_serialized.has_value());

    std::vector<uint8_t> serialized = *opt_serialized;

    // Deserialize
    std::optional<RoutingMessage> opt_deserialized =
        RoutingMessage::CreateFromSerialized(serialized);
    ASSERT_TRUE(opt_deserialized.has_value());

    RoutingMessage deserialized = *opt_deserialized;

    // Check header fields
    EXPECT_EQ(deserialized.getBaseHeader().destination, dest);
    EXPECT_EQ(deserialized.getBaseHeader().source, src);
    EXPECT_EQ(deserialized.getBaseHeader().type, MessageType::ROUTING_MSG);
    EXPECT_EQ(deserialized.getBaseHeader().payloadSize, payload.size());

    // Check routing header fields
    RoutingHeader routingHeader = deserialized.getRoutingHeader();
    EXPECT_EQ(routingHeader.next_hop, 0xABCD);
    EXPECT_EQ(routingHeader.sequence_id, 0x42);
    EXPECT_EQ(routingHeader.number, 0x0001);

    // Check payload
    EXPECT_EQ(deserialized.getPayload(), payload);
}

// Test for routing message memory management
TEST_F(RoutingMessageTest, RoutingRoutingMessageTest) {
    const AddressType next_hop = 0xABCD;
    const uint8_t seq_id = 0x42;
    const uint16_t number = 0x0001;

    {
        msg_ptr->setRoutingInfo(next_hop, seq_id, number);

        // Test copy
        RoutingMessage copy(*msg_ptr);
        EXPECT_EQ(copy.getRoutingHeader().next_hop, next_hop);
        EXPECT_NE(copy.getPayload().data(), msg_ptr->getPayload().data());

        // Test move
        RoutingMessage moved(std::move(copy));
        EXPECT_EQ(moved.getRoutingHeader().next_hop, next_hop);
        EXPECT_TRUE(copy.getPayload().empty());
    }
}

}  // namespace loramesher