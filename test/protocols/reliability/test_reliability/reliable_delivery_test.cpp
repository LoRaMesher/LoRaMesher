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
        AddressType dest;
    };

    void SetUp() override {
        Host host;
        host.now_ms = [this]() {
            return clock_ms_;
        };
        host.send_attempt = [this](const AttemptRequest& request) {
            if (failing_sends_ > 0) {
                failing_sends_--;
                return Result(LoraMesherErrorCode::kQueueFull, "queue full");
            }
            sent_.push_back({request.id,
                             std::vector<uint8_t>(request.payload.begin(),
                                                  request.payload.end()),
                             request.id.dest});
            return Result::Success();
        };
        delivery_ = std::make_unique<ReliableDelivery>(
            std::move(host),
            [this](const DeliveryResult& r) { results_.push_back(r); });
    }

    /// Identifier of a message from @p src addressed to kDest.
    MessageId Id(AddressType src, uint8_t seq) const {
        return {src, seq, kDest};
    }

    Result Track(MessageId id, std::span<const uint8_t> payload,
                 Policy policy) {
        return delivery_->Track(id, payload, policy);
    }

    static constexpr AddressType kDest = 0x20;

    static std::vector<uint8_t> Bytes(std::initializer_list<uint8_t> bytes) {
        return std::vector<uint8_t>(bytes);
    }

    uint32_t clock_ms_ = 0;
    int failing_sends_ = 0;  ///< Upcoming send attempts rejected as queue-full
    std::vector<SentAttempt> sent_;
    std::vector<DeliveryResult> results_;
    std::unique_ptr<ReliableDelivery> delivery_;
};

TEST_F(ReliableDeliveryTest, TrackPerformsFirstAttempt) {
    const std::vector<uint8_t> payload = {1, 2, 3};
    ASSERT_TRUE(Track(Id(0x10, 5), payload, {1000, 3, false}));

    ASSERT_EQ(sent_.size(), 1u);
    EXPECT_EQ(sent_[0].id, Id(0x10, 5));
    EXPECT_EQ(sent_[0].payload, payload);
    EXPECT_EQ(delivery_->PendingCount(), 1u);
    EXPECT_TRUE(results_.empty());
}

TEST_F(ReliableDeliveryTest, NoRetransmitBeforeTimeout) {
    const std::vector<uint8_t> payload = {7};
    Track(Id(0x10, 1), payload, {1000, 3, false});

    clock_ms_ = 999;
    delivery_->Tick();
    EXPECT_EQ(sent_.size(), 1u);
}

TEST_F(ReliableDeliveryTest, RetransmitOnTimeoutDecrementsRetries) {
    const std::vector<uint8_t> payload = {7};
    Track(Id(0x10, 1), payload, {1000, 2, false});

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
    Track(Id(0x10, 4), Bytes({1, 2}), {1000, 3, false});

    clock_ms_ = 250;  // RTT will be now - echo_ts
    EXPECT_TRUE(delivery_->OnAck(Id(0x10, 4), 0x20, /*echo_ts=*/100));

    ASSERT_EQ(results_.size(), 1u);
    EXPECT_EQ(results_[0].outcome, Outcome::Delivered);
    EXPECT_EQ(results_[0].by, 0x20);
    EXPECT_EQ(results_[0].rtt_ms, 150u);
    EXPECT_EQ(delivery_->PendingCount(), 0u);
}

TEST_F(ReliableDeliveryTest, UnmatchedAckReturnsFalse) {
    Track(Id(0x10, 4), Bytes({1}), {1000, 3, false});
    EXPECT_FALSE(delivery_->OnAck(Id(0x10, 99), 0x20, 0));
    EXPECT_EQ(results_.size(), 0u);
    EXPECT_EQ(delivery_->PendingCount(), 1u);
}

TEST_F(ReliableDeliveryTest, FailsAfterMaxRetries) {
    Track(Id(0x10, 1), Bytes({1}), {1000, 2, false});

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
    Track(Id(0x10, 2), Bytes({1}), {5000, 0, /*collect_multiple=*/true});

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
    Track(Id(0x10, 2), Bytes({1}), {1000, 3, /*collect_multiple=*/true});
    clock_ms_ = 100000;
    delivery_->Tick();
    EXPECT_TRUE(results_.empty());
    EXPECT_EQ(delivery_->PendingCount(), 1u);
}

TEST_F(ReliableDeliveryTest, CollectMultipleRetransmitsUntilRetriesExhausted) {
    ASSERT_TRUE(
        Track(Id(0x10, 2), Bytes({1}), {1000, 2, /*collect_multiple=*/true}));

    clock_ms_ = 1000;
    delivery_->Tick();
    clock_ms_ = 2000;
    delivery_->Tick();
    EXPECT_EQ(sent_.size(), 3u);

    // Retries exhausted: the window stays open until it is closed.
    clock_ms_ = 5000;
    delivery_->Tick();
    EXPECT_EQ(sent_.size(), 3u);
    EXPECT_TRUE(results_.empty());
    EXPECT_EQ(delivery_->PendingCount(), 1u);
}

TEST_F(ReliableDeliveryTest, CloseGroupReportsWindowClosed) {
    Track(Id(0x10, 2), Bytes({1}), {5000, 0, true});
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
        ASSERT_TRUE(Track(Id(0x10, static_cast<uint8_t>(i)), Bytes({1}),
                          {1000, 3, false}));
    }
    Result r = Track(Id(0x10, 200), Bytes({1}), {1000, 3, false});
    EXPECT_FALSE(r.IsSuccess());
    EXPECT_EQ(r.getErrorCode(), LoraMesherErrorCode::kQueueFull);
}

TEST_F(ReliableDeliveryTest, FailedEnqueueDoesNotConsumeAttempt) {
    Policy policy;
    policy.timeout_ms = 1000;
    policy.max_retries = 1;
    policy.requeue_delay_ms = 100;

    failing_sends_ = 1;
    ASSERT_TRUE(Track(Id(0x10, 1), Bytes({1}), policy));
    EXPECT_TRUE(sent_.empty());

    // The rejected first attempt is re-tried after the requeue delay, without
    // spending a retry.
    clock_ms_ = 100;
    delivery_->Tick();
    ASSERT_EQ(sent_.size(), 1u);

    // The single retry is still available one timeout later.
    clock_ms_ = 1100;
    delivery_->Tick();
    ASSERT_EQ(sent_.size(), 2u);

    clock_ms_ = 2100;
    delivery_->Tick();
    ASSERT_EQ(results_.size(), 1u);
    EXPECT_EQ(results_[0].outcome, Outcome::Failed);
}

TEST_F(ReliableDeliveryTest, BackoffDoublesTimeoutUpToMax) {
    Policy policy;
    policy.timeout_ms = 1000;
    policy.max_retries = 2;
    policy.max_timeout_ms = 3000;
    policy.exponential_backoff = true;

    ASSERT_TRUE(Track(Id(0x10, 1), Bytes({1}), policy));

    clock_ms_ = 1000;
    delivery_->Tick();  // attempt #2, next timeout 2000
    ASSERT_EQ(sent_.size(), 2u);

    clock_ms_ = 2999;
    delivery_->Tick();
    EXPECT_EQ(sent_.size(), 2u);

    clock_ms_ = 3000;
    delivery_->Tick();  // attempt #3, next timeout min(4000, 3000)
    ASSERT_EQ(sent_.size(), 3u);

    clock_ms_ = 5999;
    delivery_->Tick();
    EXPECT_TRUE(results_.empty());

    clock_ms_ = 6000;
    delivery_->Tick();
    ASSERT_EQ(results_.size(), 1u);
    EXPECT_EQ(results_[0].outcome, Outcome::Failed);
}

TEST_F(ReliableDeliveryTest,
       SameSeqToDifferentDestinationsIsTrackedSeparately) {
    constexpr AddressType kOther = kDest + 1;
    ASSERT_TRUE(Track({0x10, 5, kDest}, Bytes({1}), {1000, 3, false}));
    ASSERT_TRUE(Track({0x10, 5, kOther}, Bytes({2}), {1000, 3, false}));
    EXPECT_EQ(delivery_->PendingCount(), 2u);

    EXPECT_TRUE(delivery_->OnAck({0x10, 5, kOther}, kOther, 0));
    ASSERT_EQ(results_.size(), 1u);
    EXPECT_EQ(results_[0].id.dest, kOther);
    EXPECT_EQ(delivery_->PendingCount(), 1u);
}

TEST_F(ReliableDeliveryTest, UnicastAckFromAnotherNodeIsIgnored) {
    ASSERT_TRUE(Track(Id(0x10, 4), Bytes({1}), {1000, 3, false}));

    EXPECT_FALSE(delivery_->OnAck(Id(0x10, 4), kDest + 1, 0));
    EXPECT_TRUE(results_.empty());
    EXPECT_EQ(delivery_->PendingCount(), 1u);

    EXPECT_TRUE(delivery_->OnAck(Id(0x10, 4), kDest, 0));
    ASSERT_EQ(results_.size(), 1u);
    EXPECT_EQ(results_[0].by, kDest);
}

TEST_F(ReliableDeliveryTest, PendingSeqSpanMeasuresFromOldestOfTheStream) {
    constexpr AddressType kOther = kDest + 1;
    ASSERT_TRUE(Track({0x10, 250, kDest}, Bytes({1}), {1000, 3, false}));
    ASSERT_TRUE(Track({0x10, 2, kDest}, Bytes({1}), {1000, 3, false}));
    ASSERT_TRUE(Track({0x10, 100, kOther}, Bytes({1}), {1000, 3, false}));
    ASSERT_TRUE(Track({0x10, 7, 0x8001}, Bytes({1}), {1000, 0, true}));

    // Across the 8-bit wrap, 250 is the oldest message to kDest.
    EXPECT_EQ(delivery_->PendingSeqSpan(false, kDest, 5), 11u);
    EXPECT_EQ(delivery_->PendingSeqSpan(false, kOther, 101), 1u);
    EXPECT_EQ(delivery_->PendingSeqSpan(false, 0x30, 9), 0u);
    EXPECT_EQ(delivery_->PendingSeqSpan(true, 0, 9), 2u);
}

TEST_F(ReliableDeliveryTest, DeadlineAcrossClockWrapIsNotReachedEarly) {
    clock_ms_ = UINT32_MAX - 499;
    ASSERT_TRUE(Track(Id(0x10, 1), Bytes({1}), {1000, 1, false}));

    clock_ms_ = UINT32_MAX - 100;
    delivery_->Tick();
    EXPECT_EQ(sent_.size(), 1u);

    clock_ms_ = 500;  // 1000 ms after the first attempt, past the wrap
    delivery_->Tick();
    EXPECT_EQ(sent_.size(), 2u);
}

TEST_F(ReliableDeliveryTest, TrackRejectsIdAlreadyPending) {
    ASSERT_TRUE(Track(Id(0x10, 1), Bytes({1}), {1000, 3, false}));

    Result r = Track(Id(0x10, 1), Bytes({2}), {1000, 3, false});
    EXPECT_FALSE(r.IsSuccess());
    EXPECT_EQ(delivery_->PendingCount(), 1u);
    EXPECT_EQ(sent_.size(), 1u);
}

TEST_F(ReliableDeliveryTest, AttemptThatCanNeverBeQueuedEventuallyFails) {
    Policy policy;
    policy.timeout_ms = 1000;
    policy.max_retries = 1;
    policy.requeue_delay_ms = 100;

    failing_sends_ = 1000000;
    ASSERT_TRUE(Track(Id(0x10, 1), Bytes({1}), policy));

    for (int i = 0; i < 1000 && results_.empty(); ++i) {
        clock_ms_ += 100;
        delivery_->Tick();
    }

    ASSERT_EQ(results_.size(), 1u);
    EXPECT_EQ(results_[0].outcome, Outcome::Failed);
    EXPECT_EQ(delivery_->PendingCount(), 0u);
}

TEST_F(ReliableDeliveryTest, TrackFailsWhenPayloadTooLarge) {
    std::vector<uint8_t> payload(ReliableDelivery::MaxReliablePayload() + 1, 0);
    Result r = Track(Id(0x10, 1), payload, {1000, 3, false});
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
