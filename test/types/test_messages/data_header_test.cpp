/**
 * @file data_header_test.cpp
 * @brief Unit tests for DataHeader class
 */

#include <gtest/gtest.h>

#include "types/messages/loramesher/data_header.hpp"

namespace loramesher {
namespace test {

class DataHeaderTest : public ::testing::Test {
   protected:
    static constexpr AddressType kDest = 0x1234;
    static constexpr AddressType kSrc = 0x5678;
    static constexpr AddressType kNextHop = 0xABCD;
    static constexpr uint8_t kPayloadSize = 10;
};

TEST_F(DataHeaderTest, ConstructorSetsFields) {
    DataHeader header(kDest, kSrc, kNextHop, kPayloadSize);

    EXPECT_EQ(header.GetDestination(), kDest);
    EXPECT_EQ(header.GetSource(), kSrc);
    EXPECT_EQ(header.GetNextHop(), kNextHop);
    EXPECT_EQ(header.GetType(), MessageType::DATA);
    EXPECT_EQ(header.GetTTL(), 0);
    EXPECT_EQ(header.GetSeqNum(), 0);
}

TEST_F(DataHeaderTest, ConstructorWithTTLAndSeqNum) {
    DataHeader header(kDest, kSrc, kNextHop, kPayloadSize, 5, 42);

    EXPECT_EQ(header.GetNextHop(), kNextHop);
    EXPECT_EQ(header.GetTTL(), 5);
    EXPECT_EQ(header.GetSeqNum(), 42);
}

TEST_F(DataHeaderTest, SetNextHopUpdatesValue) {
    DataHeader header(kDest, kSrc, kNextHop, kPayloadSize);

    Result result = header.SetNextHop(0xFFFF);
    EXPECT_TRUE(result.IsSuccess());
    EXPECT_EQ(header.GetNextHop(), 0xFFFF);
}

TEST_F(DataHeaderTest, SerializeAndDeserializeRoundTrip) {
    DataHeader original(kDest, kSrc, kNextHop, kPayloadSize, 7, 99);

    // Serialize
    std::vector<uint8_t> buffer(original.GetSize());
    utils::ByteSerializer serializer(buffer);
    Result result = original.Serialize(serializer);
    ASSERT_TRUE(result.IsSuccess());

    // Deserialize
    utils::ByteDeserializer deserializer(buffer);
    auto deserialized = DataHeader::Deserialize(deserializer);

    ASSERT_TRUE(deserialized.has_value());
    EXPECT_EQ(deserialized->GetDestination(), kDest);
    EXPECT_EQ(deserialized->GetSource(), kSrc);
    EXPECT_EQ(deserialized->GetNextHop(), kNextHop);
    EXPECT_EQ(deserialized->GetType(), MessageType::DATA);
    EXPECT_EQ(deserialized->GetTTL(), 7);
    EXPECT_EQ(deserialized->GetSeqNum(), 99);
}

TEST_F(DataHeaderTest, DeserializeFailsWithEmptyBuffer) {
    std::vector<uint8_t> empty;
    utils::ByteDeserializer deserializer(empty);
    auto result = DataHeader::Deserialize(deserializer);
    EXPECT_FALSE(result.has_value());
}

TEST_F(DataHeaderTest, DeserializeFailsWithWrongMessageType) {
    // Build a valid base header with PING type, then append next_hop
    BaseHeader base(kDest, kSrc, MessageType::PING,
                    static_cast<uint8_t>(DataHeader::DataFieldsSize()));
    std::vector<uint8_t> buffer(BaseHeader::Size() +
                                DataHeader::DataFieldsSize());
    utils::ByteSerializer serializer(buffer);
    base.Serialize(serializer);
    serializer.WriteUint16(kNextHop);

    utils::ByteDeserializer deserializer(buffer);
    auto result = DataHeader::Deserialize(deserializer);
    EXPECT_FALSE(result.has_value());
}

TEST_F(DataHeaderTest, DeserializeFailsWithTruncatedNextHop) {
    // Build a valid base header for DATA type but omit the next_hop bytes
    BaseHeader base(kDest, kSrc, MessageType::DATA,
                    static_cast<uint8_t>(DataHeader::DataFieldsSize()));
    std::vector<uint8_t> buffer(BaseHeader::Size());
    utils::ByteSerializer serializer(buffer);
    base.Serialize(serializer);

    utils::ByteDeserializer deserializer(buffer);
    auto result = DataHeader::Deserialize(deserializer);
    EXPECT_FALSE(result.has_value());
}

TEST_F(DataHeaderTest, DeserializePayloadSizeCalculation) {
    // DataHeader with payload_size > DataFieldsSize: actual = payload_size - DataFieldsSize
    uint8_t total_payload =
        static_cast<uint8_t>(DataHeader::DataFieldsSize() + 5);
    DataHeader original(kDest, kSrc, kNextHop, 5);

    std::vector<uint8_t> buffer(original.GetSize());
    utils::ByteSerializer serializer(buffer);
    original.Serialize(serializer);

    utils::ByteDeserializer deserializer(buffer);
    auto result = DataHeader::Deserialize(deserializer);
    ASSERT_TRUE(result.has_value());
    // payload_size in the base header = DataFieldsSize() + 5
    // actual_payload_size = (DataFieldsSize() + 5) - DataFieldsSize() = 5
    EXPECT_EQ(result->GetPayloadSize(),
              static_cast<uint8_t>(DataHeader::DataFieldsSize() + 5));
}

TEST_F(DataHeaderTest, DataFieldsSizeIncludesTTLAndSeqNum) {
    EXPECT_EQ(DataHeader::DataFieldsSize(),
              sizeof(AddressType) + sizeof(uint8_t) + sizeof(uint8_t));
}

TEST_F(DataHeaderTest, GetSizeIncludesBaseAndDataFields) {
    DataHeader header(kDest, kSrc, kNextHop, kPayloadSize);
    EXPECT_EQ(header.GetSize(),
              BaseHeader::Size() + DataHeader::DataFieldsSize());
}

TEST_F(DataHeaderTest, DefaultConstructor) {
    DataHeader header;
    EXPECT_EQ(header.GetNextHop(), 0);
    EXPECT_EQ(header.GetTTL(), 0);
    EXPECT_EQ(header.GetSeqNum(), 0);
}

}  // namespace test
}  // namespace loramesher
