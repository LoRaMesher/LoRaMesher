// test/types/test_radio/radio_event_test.cpp
#include <gtest/gtest.h>

#include "types/radio/radio_event.hpp"

namespace loramesher {
namespace radio {
namespace test {

class RadioEventTest : public ::testing::Test {
   protected:
    void SetUp() override {
        auto base_message =
            BaseMessage::Create(0x1234, 0x5678, MessageType::DATA,
                                std::vector<uint8_t>{0x01, 0x02, 0x03});
        ASSERT_TRUE(base_message.has_value())
            << "Failed to create test message";
        message = std::make_unique<BaseMessage>(std::move(*base_message));
    }
    void TearDown() override {}

    std::unique_ptr<BaseMessage> message;
};

TEST_F(RadioEventTest, CreateReceivedEventTest) {
    const int8_t test_rssi = -70;
    const int8_t test_snr = 5;

    auto event = CreateReceivedEvent(std::move(message), test_rssi, test_snr);

    EXPECT_EQ(event->getType(), RadioEventType::kReceived);
    EXPECT_EQ(event->getRssi(), test_rssi);
    EXPECT_EQ(event->getSnr(), test_snr);
    EXPECT_TRUE(event->HasMessage());
}

TEST_F(RadioEventTest, CreateTransmittedEventTest) {
    auto event = CreateTransmittedEvent(std::move(message));

    EXPECT_EQ(event->getType(), RadioEventType::kTransmitted);
    EXPECT_TRUE(event->HasMessage());
}

TEST_F(RadioEventTest, CreateTimeoutEventTest) {
    auto event = CreateTimeoutEvent();

    EXPECT_EQ(event->getType(), RadioEventType::kTimeout);
    EXPECT_FALSE(event->HasMessage());
}

TEST_F(RadioEventTest, EventTypeToStringTest) {
    EXPECT_STREQ(RadioEvent::EventTypeToString(RadioEventType::kReceived),
                 "Received");
    EXPECT_STREQ(RadioEvent::EventTypeToString(RadioEventType::kTransmitted),
                 "Transmitted");
    EXPECT_STREQ(RadioEvent::EventTypeToString(RadioEventType::kTimeout),
                 "Timeout");
    EXPECT_STREQ(RadioEvent::EventTypeToString(RadioEventType::kCrcError),
                 "CRC Error");
}

}  // namespace test
}  // namespace radio
}  // namespace loramesher