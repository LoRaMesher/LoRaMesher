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

/**
 * @brief Fixture of a message saved before a reset and tracked again after it
 *
 * The state machine after the reset is a new instance on the same clock, as
 * on a node whose protocol clock continues across a deep sleep.
 */
class ReliableDeliveryResumeTest : public ReliableDeliveryTest {
   protected:
    static constexpr AddressType kSource = 0x10;
    static constexpr AddressType kGroup = 0x8001;

    /// Replace the state machine by a new one holding the saved messages
    void SaveAndRestore() {
        const auto saved = delivery_->GetPending();
        SetUp();
        for (const auto& pending : saved) {
            ASSERT_TRUE(delivery_->Restore(kSource, pending));
        }
    }

    static Policy Backoff(uint32_t timeout_ms, uint8_t max_retries) {
        Policy policy;
        policy.timeout_ms = timeout_ms;
        policy.max_retries = max_retries;
        policy.exponential_backoff = true;
        policy.max_timeout_ms = 3000;
        return policy;
    }

    const std::vector<uint8_t> payload_ = {9, 8, 7, 6};
};

TEST_F(ReliableDeliveryResumeTest, SavedStateDescribesTheMessage) {
    clock_ms_ = 500;
    ASSERT_TRUE(Track(Id(kSource, 5), payload_, Backoff(1000, 3)));
    clock_ms_ = 1500;
    delivery_->Tick();

    const auto saved = delivery_->GetPending();
    ASSERT_EQ(saved.size(), 1u);
    const storage::PendingReliable& pending = saved.front();
    EXPECT_EQ(pending.destination, kDest);
    EXPECT_EQ(pending.sequence, 5u);
    EXPECT_EQ(pending.payload, payload_);
    EXPECT_EQ(pending.retries_left, 2u);
    EXPECT_EQ(pending.max_retries, 3u);
    EXPECT_EQ(pending.timeout_ms, 1000u);
    EXPECT_EQ(pending.current_timeout_ms, 2000u);
    EXPECT_EQ(pending.max_timeout_ms, 3000u);
    EXPECT_EQ(pending.next_deadline_ms, 3500u);
    EXPECT_TRUE(pending.exponential_backoff);
    EXPECT_FALSE(pending.requeue);
    EXPECT_TRUE(pending.responders.empty());
}

TEST_F(ReliableDeliveryResumeTest, RestoringSendsNothing) {
    ASSERT_TRUE(Track(Id(kSource, 5), payload_, {1000, 3, false}));
    SaveAndRestore();

    EXPECT_TRUE(sent_.size() == 1u);
    EXPECT_EQ(delivery_->PendingCount(), 1u);
    delivery_->Tick();
    EXPECT_EQ(sent_.size(), 1u);
    EXPECT_TRUE(results_.empty());
}

TEST_F(ReliableDeliveryResumeTest, AckAfterTheResetCompletesTheMessage) {
    clock_ms_ = 100;
    ASSERT_TRUE(Track(Id(kSource, 5), payload_, {5000, 3, false}));
    clock_ms_ = 400;
    SaveAndRestore();

    // The acknowledgement echoes the send time of the attempt it answers
    clock_ms_ = 2600;
    EXPECT_TRUE(delivery_->OnAck(Id(kSource, 5), kDest, 100));
    ASSERT_EQ(results_.size(), 1u);
    EXPECT_EQ(results_[0].outcome, Outcome::Delivered);
    EXPECT_EQ(results_[0].id, Id(kSource, 5));
    EXPECT_EQ(results_[0].by, kDest);
    EXPECT_EQ(results_[0].rtt_ms, 2500u);
    EXPECT_EQ(delivery_->PendingCount(), 0u);

    // A second acknowledgement finds nothing
    EXPECT_FALSE(delivery_->OnAck(Id(kSource, 5), kDest, 100));
    EXPECT_EQ(results_.size(), 1u);
}

TEST_F(ReliableDeliveryResumeTest, RetransmissionKeepsItsDeadline) {
    clock_ms_ = 100;
    ASSERT_TRUE(Track(Id(kSource, 5), payload_, {1000, 3, false}));
    clock_ms_ = 300;
    SaveAndRestore();

    clock_ms_ = 1099;
    delivery_->Tick();
    EXPECT_EQ(sent_.size(), 1u);
    clock_ms_ = 1100;
    delivery_->Tick();
    ASSERT_EQ(sent_.size(), 2u);
    EXPECT_EQ(sent_[1].id, Id(kSource, 5));
    EXPECT_EQ(sent_[1].payload, payload_);
}

TEST_F(ReliableDeliveryResumeTest, DeadlinePassedDuringTheResetIsServedAtOnce) {
    clock_ms_ = 100;
    ASSERT_TRUE(Track(Id(kSource, 5), payload_, {1000, 3, false}));
    SaveAndRestore();

    // The node was down while the deadline passed
    clock_ms_ = 4000;
    delivery_->Tick();
    ASSERT_EQ(sent_.size(), 2u);
    // One retransmission, however long the reset lasted, and the next
    // timeout runs from it
    delivery_->Tick();
    EXPECT_EQ(sent_.size(), 2u);
    const auto saved = delivery_->GetPending();
    ASSERT_EQ(saved.size(), 1u);
    EXPECT_EQ(saved[0].retries_left, 2u);
    EXPECT_EQ(saved[0].next_deadline_ms, 5000u);
}

TEST_F(ReliableDeliveryResumeTest, DeadlineSurvivesTheClockWrapping) {
    clock_ms_ = 0xFFFFFF00u;
    ASSERT_TRUE(Track(Id(kSource, 5), payload_, {1000, 3, false}));
    SaveAndRestore();

    clock_ms_ = 0xFFFFFFFFu;
    delivery_->Tick();
    EXPECT_EQ(sent_.size(), 1u);
    clock_ms_ = 1000 - 0x100;
    delivery_->Tick();
    EXPECT_EQ(sent_.size(), 2u);
}

TEST_F(ReliableDeliveryResumeTest, RetryBudgetAndBackoffSpanSeveralResets) {
    ASSERT_TRUE(Track(Id(kSource, 5), payload_, Backoff(1000, 2)));

    // Attempts at 0, 1000 and 3000. The timeout doubles up to its bound
    const std::vector<uint32_t> deadlines = {1000, 3000, 6000};
    for (size_t i = 0; i < deadlines.size(); ++i) {
        SCOPED_TRACE(i);
        SaveAndRestore();
        clock_ms_ = deadlines[i] - 1;
        delivery_->Tick();
        EXPECT_EQ(sent_.size(), 1u + i);
        EXPECT_TRUE(results_.empty());
        clock_ms_ = deadlines[i];
        delivery_->Tick();
    }

    EXPECT_EQ(sent_.size(), 3u);
    ASSERT_EQ(results_.size(), 1u);
    EXPECT_EQ(results_[0].outcome, Outcome::Failed);
    EXPECT_EQ(results_[0].id, Id(kSource, 5));
    EXPECT_EQ(delivery_->PendingCount(), 0u);
    SaveAndRestore();
    EXPECT_EQ(delivery_->PendingCount(), 0u);
}

TEST_F(ReliableDeliveryResumeTest, AttemptWaitingToBeQueuedIsRepeated) {
    Policy policy{1000, 1, false};
    policy.requeue_delay_ms = 200;
    policy.max_consecutive_requeues = 2;
    failing_sends_ = 1;
    ASSERT_TRUE(Track(Id(kSource, 5), payload_, policy));
    ASSERT_TRUE(sent_.empty());

    const auto saved = delivery_->GetPending();
    ASSERT_EQ(saved.size(), 1u);
    EXPECT_TRUE(saved[0].requeue);
    EXPECT_FALSE(saved[0].requeue_is_retry);
    EXPECT_EQ(saved[0].consecutive_requeues, 1u);
    EXPECT_EQ(saved[0].next_deadline_ms, 200u);
    SaveAndRestore();

    // The repeated first attempt does not spend the retry budget
    clock_ms_ = 200;
    delivery_->Tick();
    ASSERT_EQ(sent_.size(), 1u);
    EXPECT_EQ(delivery_->GetPending()[0].retries_left, 1u);
    EXPECT_EQ(delivery_->GetPending()[0].next_deadline_ms, 1200u);
}

TEST_F(ReliableDeliveryResumeTest, GroupWindowKeepsItsResponders) {
    const MessageId id{kSource, 7, kGroup};
    ASSERT_TRUE(Track(id, payload_, {1000, 1, /*collect_multiple=*/true}));
    ASSERT_TRUE(delivery_->OnAck(id, 0x21, 0));
    ASSERT_TRUE(delivery_->OnAck(id, 0x22, 0));
    results_.clear();

    const auto saved = delivery_->GetPending();
    ASSERT_EQ(saved.size(), 1u);
    EXPECT_EQ(saved[0].responders, (std::vector<AddressType>{0x21, 0x22}));
    SaveAndRestore();

    // A responder is reported once, also when it answers the re-flood
    clock_ms_ = 1000;
    delivery_->Tick();
    EXPECT_EQ(sent_.size(), 2u);
    EXPECT_FALSE(delivery_->OnAck(id, 0x21, 1000));
    EXPECT_TRUE(delivery_->OnAck(id, 0x23, 1000));
    ASSERT_EQ(results_.size(), 1u);
    EXPECT_EQ(results_[0].by, 0x23);
    EXPECT_EQ(results_[0].ack_count, 3u);

    // The window stays open until its owner closes it
    clock_ms_ = 10000;
    delivery_->Tick();
    EXPECT_EQ(delivery_->PendingCount(), 1u);
    delivery_->CloseGroup(id);
    ASSERT_EQ(results_.size(), 2u);
    EXPECT_EQ(results_[1].outcome, Outcome::GroupWindowClosed);
    EXPECT_EQ(results_[1].ack_count, 3u);
}

TEST_F(ReliableDeliveryResumeTest, EveryTrackedMessageIsSavedAndRestored) {
    for (uint8_t seq = 0; seq < ReliableDelivery::kMaxPending; ++seq) {
        const std::vector<uint8_t> payload(
            seq == 0 ? ReliableDelivery::MaxReliablePayload() : seq, seq);
        ASSERT_TRUE(Track(Id(kSource, seq), payload, {1000, 3, false}));
    }
    const auto saved = delivery_->GetPending();
    ASSERT_EQ(saved.size(), ReliableDelivery::kMaxPending);
    SaveAndRestore();

    EXPECT_EQ(delivery_->GetPending(), saved);
    // The table is full again
    storage::PendingReliable extra = saved.front();
    extra.sequence = 200;
    EXPECT_FALSE(delivery_->Restore(kSource, extra));
}

TEST_F(ReliableDeliveryResumeTest, InvalidSavedStateIsRefused) {
    ASSERT_TRUE(Track(Id(kSource, 5), payload_, {1000, 3, false}));
    const storage::PendingReliable saved = delivery_->GetPending().front();

    // Already tracked
    EXPECT_FALSE(delivery_->Restore(kSource, saved));

    storage::PendingReliable too_long = saved;
    too_long.sequence = 6;
    too_long.payload.assign(ReliableDelivery::MaxReliablePayload() + 1, 0);
    EXPECT_FALSE(delivery_->Restore(kSource, too_long));

    storage::PendingReliable crowded = saved;
    crowded.sequence = 7;
    crowded.destination = kGroup;
    crowded.responders.assign(ReliableDelivery::kMaxGroupResponders + 1, 0x21);
    EXPECT_FALSE(delivery_->Restore(kSource, crowded));
    EXPECT_EQ(delivery_->PendingCount(), 1u);
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
