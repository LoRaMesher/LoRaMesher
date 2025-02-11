// test/types/test_radio/radio_test.cpp
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mock_radio.hpp"

namespace loramesher {
namespace radio {
namespace test {

class RadioTest : public ::testing::Test {
   protected:
    void SetUp() override {
        mock_radio_ = std::make_unique<MockRadio>();
        message = std::make_unique<BaseMessage>(
            0x1234, 0x5678, MessageType::DATA,
            std::vector<uint8_t>{0x01, 0x02, 0x03});
    }

    void TearDown() override { mock_radio_.reset(); }

    std::unique_ptr<MockRadio> mock_radio_;
    std::unique_ptr<BaseMessage> message;
};

TEST_F(RadioTest, ConfigureSuccess) {
    RadioConfig config;  // Create a test configuration
    EXPECT_CALL(*mock_radio_, configure(::testing::_))
        .WillOnce(::testing::Return(Success()));

    Result result = mock_radio_->configure(config);
    EXPECT_TRUE(result.IsSuccess());
}

TEST_F(RadioTest, ConfigureFailure) {
    RadioConfig config;
    EXPECT_CALL(*mock_radio_, configure(::testing::_))
        .WillOnce(
            ::testing::Return(Error(RadioErrorCode::kConfigurationError)));

    Result result = mock_radio_->configure(config);
    EXPECT_FALSE(result.IsSuccess());
    EXPECT_EQ(result.GetErrorCode(), RadioErrorCode::kConfigurationError);
}

TEST_F(RadioTest, SendSuccess) {
    const uint8_t test_data[] = {0x01, 0x02, 0x03};
    EXPECT_CALL(*mock_radio_, send(::testing::_, 3))
        .WillOnce(::testing::Return(Success()));

    Result result = mock_radio_->send(test_data, 3);
    EXPECT_TRUE(result.IsSuccess());
}

TEST_F(RadioTest, SendFailure) {
    const uint8_t test_data[] = {0x01, 0x02, 0x03};
    EXPECT_CALL(*mock_radio_, send(::testing::_, 3))
        .WillOnce(::testing::Return(Error(RadioErrorCode::kTransmissionError)));

    Result result = mock_radio_->send(test_data, 3);
    EXPECT_FALSE(result.IsSuccess());
    EXPECT_EQ(result.GetErrorCode(), RadioErrorCode::kTransmissionError);
}

TEST_F(RadioTest, StartReceiveSuccess) {
    EXPECT_CALL(*mock_radio_, startReceive())
        .WillOnce(::testing::Return(Success()));

    Result result = mock_radio_->startReceive();
    EXPECT_TRUE(result.IsSuccess());
}

TEST_F(RadioTest, ReceiveCallback) {
    bool callback_called = false;
    auto callback = [&callback_called](RadioEvent& event) {
        callback_called = true;
        EXPECT_EQ(event.GetType(), RadioEventType::kReceived);
    };

    mock_radio_->setReceiveCallback(callback);

    // Simulate receiving data
    std::unique_ptr<RadioEvent> event =
        CreateReceivedEvent(std::move(message), -50, 10);

    // Call the callback directly to test
    callback(*event);
    EXPECT_TRUE(callback_called);
}

}  // namespace test
}  // namespace radio
}  // namespace loramesher