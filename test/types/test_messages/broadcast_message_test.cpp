/**
 * @file broadcast_message_test.cpp
 * @brief Unit tests for BroadcastMessage class
 */

#include <gtest/gtest.h>

#include "types/messages/loramesher/broadcast_message.hpp"

namespace loramesher {
namespace test {

class BroadcastMessageTest : public ::testing::Test {
   protected:
    static constexpr AddressType src = 0x5678;
    static constexpr uint8_t ttl = 5;
    static constexpr uint8_t seq_num = 42;
    std::vector<uint8_t> payload{0x11, 0x22, 0x33, 0x44};

    std::unique_ptr<BroadcastMessage> msg_ptr;

    void SetUp() override {
        auto opt_msg = BroadcastMessage::Create(src, ttl, seq_num, payload);
        ASSERT_TRUE(opt_msg.has_value());
        msg_ptr = std::make_unique<BroadcastMessage>(*opt_msg);
    }
};

TEST_F(BroadcastMessageTest, CreateFromBaseMessageSucceeds) {
    ASSERT_TRUE(msg_ptr != nullptr);

    BaseMessage base = msg_ptr->ToBaseMessage();
    ASSERT_EQ(base.GetType(), MessageType::DATA_BROADCAST);

    auto result = BroadcastMessage::CreateFromBaseMessage(base);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->GetSource(), src);
    EXPECT_EQ(result->GetTTL(), ttl);
    EXPECT_EQ(result->GetSeqNum(), seq_num);
    EXPECT_EQ(result->GetPayload(), payload);
}

TEST_F(BroadcastMessageTest, CreateFromBaseMessageWrongTypeReturnsNullopt) {
    std::array<uint8_t, 4> data{0x01, 0x02, 0x03, 0x04};
    BaseMessage wrong(0xFFFF, src, MessageType::DATA,
                      std::span<const uint8_t>(data));

    auto result = BroadcastMessage::CreateFromBaseMessage(wrong);
    EXPECT_FALSE(result.has_value());
}

}  // namespace test
}  // namespace loramesher
