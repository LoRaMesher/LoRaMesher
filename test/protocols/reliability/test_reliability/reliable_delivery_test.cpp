/**
 * @file reliable_delivery_test.cpp
 * @brief Isolation unit tests for the ReliableDelivery state machine.
 *
 * Drives the component with a fake Host (recording send_attempt calls and a
 * manual clock) and asserts the full state machine deterministically, with no
 * radio, mesh, or RTOS dependency.
 */

#include <gtest/gtest.h>

#include <vector>

#include "protocols/reliability/reliable_delivery.hpp"

namespace loramesher {
namespace protocols {
namespace reliability {
namespace test {

class ReliableDeliveryTest : public ::testing::Test {
   protected:
    struct SentAttempt {
        MessageId id;
        std::vector<uint8_t> payload;
    };

    void SetUp() override {
        Host host;
        host.now_ms = [this]() {
            return clock_ms_;
        };
        host.send_attempt = [this](const MessageId& id,
                                   std::span<const uint8_t> payload) {
            sent_.push_back(
                {id, std::vector<uint8_t>(payload.begin(), payload.end())});
            return Result::Success();
        };
        delivery_ = std::make_unique<ReliableDelivery>(
            std::move(host),
            [this](const DeliveryResult& r) { results_.push_back(r); });
    }

    MessageId Id(AddressType src, uint8_t seq) const { return {src, seq}; }

    static std::vector<uint8_t> Bytes(std::initializer_list<uint8_t> bytes) {
        return std::vector<uint8_t>(bytes);
    }

    uint32_t clock_ms_ = 0;
    std::vector<SentAttempt> sent_;
    std::vector<DeliveryResult> results_;
    std::unique_ptr<ReliableDelivery> delivery_;
};

TEST_F(ReliableDeliveryTest, TrackPerformsFirstAttempt) {
    const std::vector<uint8_t> payload = {1, 2, 3};
    ASSERT_TRUE(delivery_->Track(Id(0x10, 5), payload, {1000, 3, false}));

    ASSERT_EQ(sent_.size(), 1u);
    EXPECT_EQ(sent_[0].id, Id(0x10, 5));
    EXPECT_EQ(sent_[0].payload, payload);
    EXPECT_EQ(delivery_->PendingCount(), 1u);
    EXPECT_TRUE(results_.empty());
}

TEST_F(ReliableDeliveryTest, NoRetransmitBeforeTimeout) {
    const std::vector<uint8_t> payload = {7};
    delivery_->Track(Id(0x10, 1), payload, {1000, 3, false});

    clock_ms_ = 999;
    delivery_->Tick();
    EXPECT_EQ(sent_.size(), 1u);
}

TEST_F(ReliableDeliveryTest, RetransmitOnTimeoutDecrementsRetries) {
    const std::vector<uint8_t> payload = {7};
    delivery_->Track(Id(0x10, 1), payload, {1000, 2, false});

    clock_ms_ = 1000;
    delivery_->Tick();
    ASSERT_EQ(sent_.size(), 2u);  // attempt #2

    clock_ms_ = 2000;
    delivery_->Tick();
    ASSERT_EQ(sent_.size(), 3u);  // attempt #3 (retries exhausted now)

    // No more retransmits; next deadline fails out instead.
    clock_ms_ = 3000;
    delivery_->Tick();
    EXPECT_EQ(sent_.size(), 3u);
}

TEST_F(ReliableDeliveryTest, AckDeliversAndErases) {
    delivery_->Track(Id(0x10, 4), Bytes({1, 2}), {1000, 3, false});

    clock_ms_ = 250;  // RTT will be now - echo_ts
    EXPECT_TRUE(delivery_->OnAck(Id(0x10, 4), 0x20, /*echo_ts=*/100));

    ASSERT_EQ(results_.size(), 1u);
    EXPECT_EQ(results_[0].outcome, Outcome::Delivered);
    EXPECT_EQ(results_[0].by, 0x20);
    EXPECT_EQ(results_[0].rtt_ms, 150u);
    EXPECT_EQ(delivery_->PendingCount(), 0u);
}

TEST_F(ReliableDeliveryTest, UnmatchedAckReturnsFalse) {
    delivery_->Track(Id(0x10, 4), Bytes({1}), {1000, 3, false});
    EXPECT_FALSE(delivery_->OnAck(Id(0x10, 99), 0x20, 0));
    EXPECT_EQ(results_.size(), 0u);
    EXPECT_EQ(delivery_->PendingCount(), 1u);
}

TEST_F(ReliableDeliveryTest, FailsAfterMaxRetries) {
    delivery_->Track(Id(0x10, 1), Bytes({1}), {1000, 2, false});

    clock_ms_ = 1000;
    delivery_->Tick();  // attempt #2
    clock_ms_ = 2000;
    delivery_->Tick();  // attempt #3
    clock_ms_ = 3000;
    delivery_->Tick();  // exhausted -> Failed

    ASSERT_EQ(results_.size(), 1u);
    EXPECT_EQ(results_[0].outcome, Outcome::Failed);
    EXPECT_EQ(results_[0].id, Id(0x10, 1));
    EXPECT_EQ(delivery_->PendingCount(), 0u);  // no leak
}

TEST_F(ReliableDeliveryTest, CollectMultipleFiresPerDistinctResponder) {
    delivery_->Track(Id(0x10, 2), Bytes({1}),
                     {5000, 0, /*collect_multiple=*/true});

    clock_ms_ = 100;
    EXPECT_TRUE(delivery_->OnAck(Id(0x10, 2), 0x21, 0));
    EXPECT_TRUE(delivery_->OnAck(Id(0x10, 2), 0x22, 0));
    EXPECT_TRUE(delivery_->OnAck(Id(0x10, 2), 0x23, 0));
    // Duplicate responder must be ignored.
    EXPECT_FALSE(delivery_->OnAck(Id(0x10, 2), 0x22, 0));

    ASSERT_EQ(results_.size(), 3u);
    EXPECT_EQ(results_[0].by, 0x21);
    EXPECT_EQ(results_[1].by, 0x22);
    EXPECT_EQ(results_[2].by, 0x23);
    EXPECT_EQ(results_[2].ack_count, 3u);
    // Not erased while the window is open.
    EXPECT_EQ(delivery_->PendingCount(), 1u);
}

TEST_F(ReliableDeliveryTest, CollectMultipleNotFailedByTick) {
    delivery_->Track(Id(0x10, 2), Bytes({1}),
                     {1000, 3, /*collect_multiple=*/true});
    clock_ms_ = 100000;
    delivery_->Tick();
    EXPECT_TRUE(results_.empty());
    EXPECT_EQ(delivery_->PendingCount(), 1u);
}

TEST_F(ReliableDeliveryTest, CloseGroupReportsWindowClosed) {
    delivery_->Track(Id(0x10, 2), Bytes({1}), {5000, 0, true});
    delivery_->OnAck(Id(0x10, 2), 0x21, 0);
    delivery_->OnAck(Id(0x10, 2), 0x22, 0);
    results_.clear();

    delivery_->CloseGroup(Id(0x10, 2));

    ASSERT_EQ(results_.size(), 1u);
    EXPECT_EQ(results_[0].outcome, Outcome::GroupWindowClosed);
    EXPECT_EQ(results_[0].ack_count, 2u);
    EXPECT_EQ(delivery_->PendingCount(), 0u);
}

TEST_F(ReliableDeliveryTest, TrackFailsWhenTableFull) {
    for (size_t i = 0; i < ReliableDelivery::kMaxPending; ++i) {
        ASSERT_TRUE(delivery_->Track(Id(0x10, static_cast<uint8_t>(i)),
                                     Bytes({1}), {1000, 3, false}));
    }
    Result r = delivery_->Track(Id(0x10, 200), Bytes({1}), {1000, 3, false});
    EXPECT_FALSE(r.IsSuccess());
    EXPECT_EQ(r.getErrorCode(), LoraMesherErrorCode::kQueueFull);
}

TEST_F(ReliableDeliveryTest, TrackFailsWhenPayloadTooLarge) {
    std::vector<uint8_t> payload(ReliableDelivery::MaxReliablePayload() + 1, 0);
    Result r = delivery_->Track(Id(0x10, 1), payload, {1000, 3, false});
    EXPECT_FALSE(r.IsSuccess());
    EXPECT_EQ(r.getErrorCode(), LoraMesherErrorCode::kBufferOverflow);
    EXPECT_EQ(delivery_->PendingCount(), 0u);
}

}  // namespace test
}  // namespace reliability
}  // namespace protocols
}  // namespace loramesher

#if defined(ARDUINO)
#include <Arduino.h>

void setup() {
    ::testing::InitGoogleTest();
    if (RUN_ALL_TESTS()) {}
}

void loop() {}

#else
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    if (RUN_ALL_TESTS()) {}
    return 0;
}
#endif
