// test/unit/message_test.cpp
#include <gtest/gtest.h>

#include "../src/types/messages/message.hpp"
#include "../src/types/messages/routing_message.hpp"

using namespace loramesher;

TEST(MessageTest, SerializationTest) {
    // Create message with known values
    AddressType dest = 0x1234;
    AddressType src = 0x5678;
    std::vector<uint8_t> payload = {0x01, 0x02, 0x03};

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

TEST(MessageTest, DeserializationTest) {
    // Create message with known values
    AddressType dest = 0x1234;
    AddressType src = 0x5678;
    std::vector<uint8_t> payload = {0x01, 0x02, 0x03};

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

TEST(MessageTest, RoutingSerializationTest) {
    // Create message with known values
    AddressType dest = 0x1234;
    AddressType src = 0x5678;
    std::vector<uint8_t> payload = {0x01, 0x02, 0x03};

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

TEST(MessageTest, RoutingDeserializationTest) {
    // Create message with known values
    AddressType dest = 0x1234;
    AddressType src = 0x5678;
    std::vector<uint8_t> payload = {0x01, 0x02, 0x03};

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

#if defined(ARDUINO)
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