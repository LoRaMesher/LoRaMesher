/**
 * @file nm_claim_message_test.cpp
 * @brief Unit tests for NMClaimMessage class
 */

#include <gtest/gtest.h>

#include "types/messages/loramesher/nm_claim_message.hpp"

namespace loramesher {
namespace test {

class NMClaimMessageTest : public ::testing::Test {
   protected:
    static constexpr AddressType kSrc = 0x1234;
    static constexpr uint8_t kPriority = 42;
    static constexpr uint8_t kBattery = 85;
    static constexpr uint8_t kNodeCount = 5;
    static constexpr uint16_t kNetworkId = 0xABCD;
};

TEST_F(NMClaimMessageTest, CreateMessage) {
    auto msg = NMClaimMessage::Create(kSrc, kPriority, kBattery, kNodeCount,
                                      kNetworkId);
    ASSERT_TRUE(msg.has_value());
}

TEST_F(NMClaimMessageTest, FieldsAreCorrect) {
    auto msg = NMClaimMessage::Create(kSrc, kPriority, kBattery, kNodeCount,
                                      kNetworkId);
    ASSERT_TRUE(msg.has_value());

    EXPECT_EQ(msg->GetSource(), kSrc);
    EXPECT_EQ(msg->GetPriority(), kPriority);
    EXPECT_EQ(msg->GetBatteryLevel(), kBattery);
    EXPECT_EQ(msg->GetNetworkNodeCount(), kNodeCount);
    EXPECT_EQ(msg->GetNetworkId(), kNetworkId);
}

TEST_F(NMClaimMessageTest, DestinationIsBroadcast) {
    auto msg = NMClaimMessage::Create(kSrc, kPriority, kBattery, kNodeCount,
                                      kNetworkId);
    ASSERT_TRUE(msg.has_value());

    BaseMessage base = msg->ToBaseMessage();
    EXPECT_EQ(base.GetDestination(), 0xFFFF);
    EXPECT_EQ(base.GetSource(), kSrc);
    EXPECT_EQ(base.GetType(), MessageType::NM_CLAIM);
}

TEST_F(NMClaimMessageTest, SerializationRoundTrip) {
    auto original = NMClaimMessage::Create(kSrc, kPriority, kBattery,
                                           kNodeCount, kNetworkId);
    ASSERT_TRUE(original.has_value());

    auto serialized = original->Serialize();
    ASSERT_TRUE(serialized.has_value());
    EXPECT_GT(serialized->size(), 0u);

    auto deserialized = NMClaimMessage::CreateFromSerialized(*serialized);
    ASSERT_TRUE(deserialized.has_value());

    EXPECT_EQ(deserialized->GetSource(), kSrc);
    EXPECT_EQ(deserialized->GetPriority(), kPriority);
    EXPECT_EQ(deserialized->GetBatteryLevel(), kBattery);
    EXPECT_EQ(deserialized->GetNetworkNodeCount(), kNodeCount);
    EXPECT_EQ(deserialized->GetNetworkId(), kNetworkId);
}

TEST_F(NMClaimMessageTest, ToBaseMessagePayloadSize) {
    auto msg = NMClaimMessage::Create(kSrc, kPriority, kBattery, kNodeCount,
                                      kNetworkId);
    ASSERT_TRUE(msg.has_value());

    BaseMessage base = msg->ToBaseMessage();
    EXPECT_EQ(base.GetPayload().size(), NMClaimHeader::NMClaimFieldsSize());
}

TEST_F(NMClaimMessageTest, GetHeaderReturnsCorrectData) {
    auto msg = NMClaimMessage::Create(kSrc, kPriority, kBattery, kNodeCount,
                                      kNetworkId);
    ASSERT_TRUE(msg.has_value());

    const auto& header = msg->GetHeader();
    EXPECT_EQ(header.GetPriority(), kPriority);
    EXPECT_EQ(header.GetBatteryLevel(), kBattery);
    EXPECT_EQ(header.GetNetworkNodeCount(), kNodeCount);
    EXPECT_EQ(header.GetNetworkId(), kNetworkId);
}

TEST_F(NMClaimMessageTest, GetTotalSizeMatchesHeader) {
    auto msg = NMClaimMessage::Create(kSrc, kPriority, kBattery, kNodeCount,
                                      kNetworkId);
    ASSERT_TRUE(msg.has_value());

    EXPECT_EQ(msg->GetTotalSize(), msg->GetHeader().GetSize());
    EXPECT_GT(msg->GetTotalSize(), 0u);
}

TEST_F(NMClaimMessageTest, DeserializeEmptyDataFails) {
    std::vector<uint8_t> empty;
    auto result = NMClaimMessage::CreateFromSerialized(empty);
    EXPECT_FALSE(result.has_value());
}

TEST_F(NMClaimMessageTest, DeserializeShortDataFails) {
    std::vector<uint8_t> short_data(3, 0x00);
    auto result = NMClaimMessage::CreateFromSerialized(short_data);
    EXPECT_FALSE(result.has_value());
}

TEST_F(NMClaimMessageTest, PriorityBoundaryValues) {
    // Min priority (highest priority in election)
    auto low = NMClaimMessage::Create(kSrc, 0x00, 100, 1, 1);
    ASSERT_TRUE(low.has_value());
    EXPECT_EQ(low->GetPriority(), 0x00);

    // Max priority (lowest priority)
    auto high = NMClaimMessage::Create(kSrc, 0xFF, 0, 0, 0xFFFF);
    ASSERT_TRUE(high.has_value());
    EXPECT_EQ(high->GetPriority(), 0xFF);
    EXPECT_EQ(high->GetNetworkId(), 0xFFFF);
}

TEST_F(NMClaimMessageTest, MultipleSerializeDeserializeCycles) {
    auto original = NMClaimMessage::Create(kSrc, kPriority, kBattery,
                                           kNodeCount, kNetworkId);
    ASSERT_TRUE(original.has_value());

    for (int i = 0; i < 3; ++i) {
        auto serialized = original->Serialize();
        ASSERT_TRUE(serialized.has_value());
        auto deserialized = NMClaimMessage::CreateFromSerialized(*serialized);
        ASSERT_TRUE(deserialized.has_value());
        EXPECT_EQ(deserialized->GetPriority(), kPriority);
        original = deserialized;
    }
}

TEST_F(NMClaimMessageTest, CreateFromBaseMessageSucceeds) {
    auto msg = NMClaimMessage::Create(kSrc, kPriority, kBattery, kNodeCount,
                                      kNetworkId);
    ASSERT_TRUE(msg.has_value());

    BaseMessage base = msg->ToBaseMessage();
    ASSERT_EQ(base.GetType(), MessageType::NM_CLAIM);

    auto result = NMClaimMessage::CreateFromBaseMessage(base);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->GetSource(), kSrc);
    EXPECT_EQ(result->GetPriority(), kPriority);
    EXPECT_EQ(result->GetBatteryLevel(), kBattery);
    EXPECT_EQ(result->GetNetworkNodeCount(), kNodeCount);
    EXPECT_EQ(result->GetNetworkId(), kNetworkId);
}

TEST_F(NMClaimMessageTest, CreateFromBaseMessageWrongTypeReturnsNullopt) {
    std::array<uint8_t, 4> payload{0x01, 0x02, 0x03, 0x04};
    BaseMessage wrong(0xFFFF, kSrc, MessageType::DATA,
                      std::span<const uint8_t>(payload));

    auto result = NMClaimMessage::CreateFromBaseMessage(wrong);
    EXPECT_FALSE(result.has_value());
}

}  // namespace test
}  // namespace loramesher
