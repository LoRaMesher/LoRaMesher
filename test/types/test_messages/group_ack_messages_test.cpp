/**
 * @file group_ack_messages_test.cpp
 * @brief Round-trip tests for AckPayload, typed DataMessage, and GroupMessage.
 */

#include <gtest/gtest.h>

#include "types/messages/loramesher/ack_payload.hpp"
#include "types/messages/loramesher/data_message.hpp"
#include "types/messages/loramesher/group_message.hpp"

namespace loramesher {
namespace test {

// ---- AckPayload ----

TEST(AckPayloadTest, RoundTripPreservesFields) {
    AckPayload ack{/*acked_seq=*/42, /*flags=*/AckPayload::kFlagWasGroup,
                   /*echo_timestamp=*/0x12345678};
    auto bytes = ack.Serialize();
    EXPECT_EQ(bytes.size(), AckPayload::kSize);

    auto parsed = AckPayload::Deserialize(bytes);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->acked_seq, 42u);
    EXPECT_EQ(parsed->echo_timestamp, 0x12345678u);
    EXPECT_TRUE(parsed->WasGroup());
}

TEST(AckPayloadTest, UnicastFlagNotGroup) {
    AckPayload ack{/*acked_seq=*/7, /*flags=*/0, /*echo_timestamp=*/100};
    auto parsed = AckPayload::Deserialize(ack.Serialize());
    ASSERT_TRUE(parsed.has_value());
    EXPECT_FALSE(parsed->WasGroup());
}

TEST(AckPayloadTest, DeserializeRejectsShortBuffer) {
    std::vector<uint8_t> too_short(AckPayload::kSize - 1, 0);
    EXPECT_FALSE(AckPayload::Deserialize(too_short).has_value());
}

// ---- Typed DataMessage (DATA_RELIABLE / ACK) ----

TEST(TypedDataMessageTest, ReliableTypeRoundTrips) {
    std::vector<uint8_t> payload{1, 2, 3, 4};
    auto msg = DataMessage::Create(0x0002, 0x0001, 0x0003, payload, /*ttl=*/4,
                                   /*seq=*/9, MessageType::DATA_RELIABLE);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->GetType(), MessageType::DATA_RELIABLE);

    BaseMessage base = msg->ToBaseMessage();
    EXPECT_EQ(base.GetType(), MessageType::DATA_RELIABLE);

    auto parsed = DataMessage::CreateFromBaseMessage(base);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->GetType(), MessageType::DATA_RELIABLE);
    EXPECT_EQ(parsed->GetDestination(), 0x0002);
    EXPECT_EQ(parsed->GetSource(), 0x0001);
    EXPECT_EQ(parsed->GetNextHop(), 0x0003);
    EXPECT_EQ(parsed->GetSeqNum(), 9u);
    EXPECT_EQ(parsed->GetPayload(), payload);
}

TEST(TypedDataMessageTest, AckTypeRoundTripsWithAckPayload) {
    AckPayload ack{/*acked_seq=*/5, /*flags=*/0, /*echo_timestamp=*/0xABCD};
    auto ack_bytes = ack.Serialize();
    std::vector<uint8_t> payload(ack_bytes.begin(), ack_bytes.end());

    auto msg = DataMessage::Create(0x0001, 0x0002, 0x0001, payload, /*ttl=*/4,
                                   /*seq=*/3, MessageType::ACK);
    ASSERT_TRUE(msg.has_value());

    auto parsed = DataMessage::CreateFromBaseMessage(msg->ToBaseMessage());
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->GetType(), MessageType::ACK);

    auto parsed_ack = AckPayload::Deserialize(parsed->GetPayload());
    ASSERT_TRUE(parsed_ack.has_value());
    EXPECT_EQ(parsed_ack->acked_seq, 5u);
    EXPECT_EQ(parsed_ack->echo_timestamp, 0xABCDu);
}

TEST(TypedDataMessageTest, ForwardPreservesType) {
    std::vector<uint8_t> payload{9};
    auto msg = DataMessage::Create(0x0002, 0x0001, 0x0003, payload, /*ttl=*/4,
                                   /*seq=*/1, MessageType::DATA_RELIABLE);
    ASSERT_TRUE(msg.has_value());

    auto forwarded = DataMessage::CreateForwarded(*msg, 0x0004);
    ASSERT_TRUE(forwarded.has_value());
    EXPECT_EQ(forwarded->GetType(), MessageType::DATA_RELIABLE);
    EXPECT_EQ(forwarded->GetTTL(), 3u);
    EXPECT_EQ(forwarded->GetNextHop(), 0x0004);
}

TEST(TypedDataMessageTest, PlainDataDefaultsToDataType) {
    std::vector<uint8_t> payload{1};
    auto msg = DataMessage::Create(0x0002, 0x0001, 0x0003, payload);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->GetType(), MessageType::DATA);
    EXPECT_EQ(msg->ToBaseMessage().GetType(), MessageType::DATA);
}

// ---- GroupMessage ----

TEST(GroupMessageTest, RoundTripPreservesGroupAndFlags) {
    std::vector<uint8_t> payload{0xAA, 0xBB};
    auto msg = GroupMessage::Create(0x8001, 0x0001, /*ttl=*/4,
                                    GroupMessage::kFlagRequestAcks, /*seq=*/7,
                                    payload);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->GetDestination(), 0x8001);
    EXPECT_TRUE(msg->RequestAcks());

    BaseMessage base = msg->ToBaseMessage();
    EXPECT_EQ(base.GetType(), MessageType::DATA_GROUP);
    EXPECT_EQ(base.GetDestination(), 0x8001);

    auto parsed = GroupMessage::CreateFromBaseMessage(base);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->GetGroup(), 0x8001);
    EXPECT_EQ(parsed->GetSource(), 0x0001);
    EXPECT_EQ(parsed->GetSeqNum(), 7u);
    EXPECT_TRUE(parsed->RequestAcks());
    EXPECT_EQ(parsed->GetPayload(), payload);
}

TEST(GroupMessageTest, RejectsNonGroupDestination) {
    std::vector<uint8_t> payload{1};
    EXPECT_FALSE(
        GroupMessage::Create(0x0001, 0x0001, 4, 0, 1, payload).has_value());
}

TEST(GroupMessageTest, ForwardDecrementsTtl) {
    std::vector<uint8_t> payload{1};
    auto msg = GroupMessage::Create(0x8001, 0x0001, /*ttl=*/3, 0, 1, payload);
    ASSERT_TRUE(msg.has_value());

    auto forwarded = GroupMessage::CreateForwarded(*msg);
    ASSERT_TRUE(forwarded.has_value());
    EXPECT_EQ(forwarded->GetTTL(), 2u);
    EXPECT_EQ(forwarded->GetGroup(), 0x8001);

    auto msg_ttl1 =
        GroupMessage::Create(0x8001, 0x0001, /*ttl=*/1, 0, 1, payload);
    ASSERT_TRUE(msg_ttl1.has_value());
    EXPECT_FALSE(GroupMessage::CreateForwarded(*msg_ttl1).has_value());
}

}  // namespace test
}  // namespace loramesher
