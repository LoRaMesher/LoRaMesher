/**
 * @file reliable_deep_sleep_test.cpp
 * @brief Members deep-sleeping while a reliable message waits for its
 *        acknowledgement or a group acknowledgement window is open
 *
 * The sender is a member that deep-sleeps through the SLEEP run of every
 * superframe. The tests check that it keeps doing so with a message open,
 * that the message still ends exactly once with the right outcome, and that
 * retransmissions go out when they are due.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <set>

#include "node_reboot_fixture.hpp"
#include "types/storage/resume_snapshot_codec.hpp"

namespace loramesher {
namespace test {
namespace {

using protocols::reliability::DeliveryResult;
using protocols::reliability::MessageId;
using protocols::reliability::Outcome;

constexpr size_t kMessageTypeOffset = 4;
constexpr AddressType kGroup = 0x8001;

bool HasType(const std::vector<uint8_t>& frame, MessageType type) {
    return frame.size() > kMessageTypeOffset &&
           frame[kMessageTypeOffset] == static_cast<uint8_t>(type);
}

class ReliableDeepSleepTest : public NodeRebootFixture {
   protected:
    void TearDown() override {
        virtual_network_.SetDropFilter(nullptr);
        for (const auto& [address, count] : sleeps_without_beacon_) {
            ADD_FAILURE() << "Node 0x" << std::hex << address << std::dec
                          << " deep-slept " << count
                          << " times without a beacon in its superframe";
        }
        NodeRebootFixture::TearDown();
    }

    /// Form a three-node network whose members deep-sleep every superframe
    std::vector<TestNode*> FormNetwork(Topology topology,
                                       bool light_sleep = false) {
        power::DeepSleepPolicy policy;
        policy.enabled = true;
        policy.min_sleep_ms = 1000;
        policy.boot_time_ms = 100;
        policy.clock_drift_ppm = 2000;

        NetworkSpec spec;
        spec.topology = topology;
        spec.node_count = 3;
        spec.member_role = NodeRole::NODE_ONLY;
        spec.member_deep_sleep = policy;
        spec.member_light_sleep = light_sleep;
        auto nodes = BuildNetwork(spec);
        StartAndFormNetwork(nodes);
        return nodes;
    }

    static uint64_t VirtualNow() {
        return static_cast<os::RTOSMock*>(&GetRTOS())->getVirtualTime();
    }

    /// Run @p call with the test thread bound to @p node, as its application
    template <typename Fn>
    static auto AsNode(const TestNode& node, Fn&& call) {
        char addr_str[8];
        snprintf(addr_str, sizeof(addr_str), "0x%04X", node.address);
        GetRTOS().SetCurrentTaskNodeAddress(addr_str);
        auto result = call();
        GetRTOS().SetCurrentTaskNodeAddress("0xFFFF");
        return result;
    }

    /// Run until every member resumed once and @p sender may send to @p to
    ::testing::AssertionResult WaitUntilReady(
        const std::vector<TestNode*>& nodes, TestNode& sender, TestNode& to) {
        const size_t resumes = CountOf(resumed_boots_, sender.address);
        const uint32_t budget = superframe_ms_ * 8;
        const bool ready = AdvanceTime(budget, budget, kStepMs, 0, [&]() {
            return CountOf(resumed_boots_, sender.address) > resumes &&
                   sender.protocol != nullptr &&
                   sender.protocol->IsReadyToSend(to.address);
        });
        if (!ready) {
            return ::testing::AssertionFailure() << DescribeNetwork(nodes);
        }
        return ::testing::AssertionSuccess();
    }

    /// Send a reliable unicast as the application of @p from, and watch it
    MessageId SendReliable(TestNode& from, TestNode& to, uint8_t max_retries,
                           uint32_t timeout_ms,
                           const std::vector<uint8_t>& payload = Payload()) {
        sender_ = &from;
        sent_at_ms_ = VirtualNow();
        return AsNode(from, [&]() {
            return from.protocol->GetNetworkServiceForTest()->SendReliable(
                to.address, payload, max_retries, timeout_ms);
        });
    }

    static std::vector<uint8_t> Payload() { return {kDataMarker, 0xAC, 0x01}; }

    /// A payload that does not fit in the room LimitStore() leaves
    static std::vector<uint8_t> BigPayload() {
        return std::vector<uint8_t>(2 * kSpareStoreBytes, 0xB6);
    }

    /**
     * @brief Leave the store of @p node room for the state it last slept
     *        with and kSpareStoreBytes more
     */
    void LimitStore(const TestNode& node) {
        LimitedStateStore& store = *stores_.at(node.address);
        ASSERT_GT(store.GetLastSavedSize(), 0u);
        store.SetCapacity(store.GetLastSavedSize() + kSpareStoreBytes);
    }

    /// Lose the first @p count acknowledgements @p from sends to @p to
    void LoseAcks(const TestNode& from, const TestNode& to, size_t count) {
        acks_to_lose_ = count;
        acks_lost_ = 0;
        virtual_network_.SetDropFilter(
            [this, from = from.address, to = to.address](
                uint32_t source, uint32_t receiver,
                const std::vector<uint8_t>& frame) {
                if (source != from || receiver != to ||
                    !HasType(frame, MessageType::ACK) ||
                    acks_lost_ >= acks_to_lose_) {
                    return false;
                }
                ++acks_lost_;
                return true;
            });
    }

    /**
     * @brief Advance time until @p done, recording what the sender does
     *
     * Records when each transmission attempt of the watched message leaves
     * the sender, when each delivery outcome arrives, and the state of the
     * message in every snapshot the sender deep-sleeps with.
     */
    bool RunUntil(uint32_t budget_ms, const std::function<bool()>& done) {
        return AdvanceTime(budget_ms, budget_ms, kStepMs, 0, [&]() {
            Observe();
            return done();
        });
    }

    void Observe() {
        const uint64_t now = VirtualNow();
        const size_t attempts =
            virtual_network_
                .GetFilteredSentMessages(
                    sender_->address,
                    [this](const std::vector<uint8_t>& frame) {
                        return HasType(frame, attempt_type_);
                    })
                .size();
        while (attempt_times_ms_.size() + attempts_before_ < attempts) {
            attempt_times_ms_.push_back(now);
        }
        while (outcome_times_ms_.size() < sender_->delivery_outcomes.size()) {
            outcome_times_ms_.push_back(now);
        }
        const size_t sleeps =
            CountOf(sleeps_with_reliable_pending_, sender_->address);
        if (sleeps != observed_sleeps_) {
            observed_sleeps_ = sleeps;
            RecordSavedState();
        }
    }

    /// Keep what the sender's snapshot holds about its open messages
    void RecordSavedState() {
        auto blob = stores_.at(sender_->address)->Load();
        ASSERT_TRUE(blob.has_value());
        auto snapshot = storage::ResumeSnapshotCodec::Decode(*blob);
        ASSERT_TRUE(snapshot.has_value());
        ASSERT_EQ(snapshot->pending_reliable.size(), 1u);
        saved_states_.push_back(snapshot->pending_reliable.front());
    }

    /// Distinct retry budgets the sender slept with, in the order seen
    std::vector<uint8_t> SavedRetriesLeft() const {
        std::vector<uint8_t> retries;
        for (const auto& state : saved_states_) {
            if (retries.empty() || retries.back() != state.retries_left) {
                retries.push_back(state.retries_left);
            }
        }
        return retries;
    }

    size_t CountOutcomes(Outcome outcome) const {
        return static_cast<size_t>(
            std::count_if(sender_->delivery_outcomes.begin(),
                          sender_->delivery_outcomes.end(),
                          [outcome](const DeliveryResult& r) {
                              return r.outcome == outcome;
                          }));
    }

    /// Times the watched payload reached the application of @p node
    static size_t TimesReceived(
        const TestNode& node,
        const std::vector<uint8_t>& expected = Payload()) {
        size_t count = 0;
        for (const auto& msg : node.received_messages) {
            const auto payload = msg.GetPayload();
            count += std::equal(payload.begin(), payload.end(),
                                expected.begin(), expected.end());
        }
        return count;
    }

    /// Time from the send to attempt @p index leaving the sender
    uint32_t AttemptDelayMs(size_t index) const {
        return static_cast<uint32_t>(attempt_times_ms_.at(index) - sent_at_ms_);
    }

    /// Slack of a measured time: the observation step and the slot guards
    uint32_t SlackMs() const { return superframe_ms_ / 10; }

    /// Room LimitStore() leaves above the state without open messages
    static constexpr size_t kSpareStoreBytes = 40;

    TestNode* sender_ = nullptr;
    MessageType attempt_type_ = MessageType::DATA_RELIABLE;
    /// Frames of attempt_type_ the sender sent before the watched message
    size_t attempts_before_ = 0;
    uint64_t sent_at_ms_ = 0;
    std::vector<uint64_t> attempt_times_ms_;
    std::vector<uint64_t> outcome_times_ms_;
    size_t observed_sleeps_ = 0;
    std::vector<storage::PendingReliable> saved_states_;
    std::atomic<size_t> acks_to_lose_{0};
    std::atomic<size_t> acks_lost_{0};
};

TEST_F(ReliableDeepSleepTest, AcknowledgementArrivesAfterTheResume) {
    // Two hops: the acknowledgement comes back in a later superframe
    auto nodes = FormNetwork(Topology::kLine);
    ASSERT_FALSE(HasFatalFailure());
    TestNode& manager = *nodes.front();
    TestNode& member = *nodes.back();
    ASSERT_TRUE(WaitUntilReady(nodes, member, manager));

    const MessageId id = SendReliable(member, manager, 3, 0);
    ASSERT_NE(id.source, 0u);
    ASSERT_TRUE(RunUntil(superframe_ms_ * 8, [&]() {
        return !member.delivery_outcomes.empty();
    })) << DescribeNetwork(nodes);
    const auto rtt_upper_ms =
        static_cast<uint32_t>(outcome_times_ms_.front() - sent_at_ms_);

    // The member slept with the message open and matched the acknowledgement
    // of its only attempt after resuming
    EXPECT_GE(CountOf(sleeps_with_reliable_pending_, member.address), 1u);
    EXPECT_EQ(attempt_times_ms_.size(), 1u);
    const DeliveryResult outcome = member.delivery_outcomes.front();
    EXPECT_EQ(outcome.outcome, Outcome::Delivered);
    EXPECT_EQ(outcome.id, id);
    EXPECT_EQ(outcome.by, manager.address);
    EXPECT_GT(outcome.rtt_ms, superframe_ms_ / 2);
    EXPECT_LE(outcome.rtt_ms, rtt_upper_ms);
    EXPECT_GE(outcome.rtt_ms + 2 * kStepMs, rtt_upper_ms);
    ASSERT_FALSE(saved_states_.empty());
    EXPECT_EQ(saved_states_.front().destination, manager.address);
    EXPECT_EQ(saved_states_.front().sequence, id.seq);
    EXPECT_EQ(saved_states_.front().payload, Payload());
    EXPECT_EQ(saved_states_.front().retries_left, 3u);

    // Nothing more happens to the message, and the member keeps sleeping
    const size_t sleeps = CountOf(deep_sleeps_, member.address);
    RunUntil(superframe_ms_ * 4, []() { return false; });
    EXPECT_EQ(member.delivery_outcomes.size(), 1u);
    EXPECT_EQ(attempt_times_ms_.size(), 1u);
    EXPECT_EQ(TimesReceived(manager), 1u);
    EXPECT_GE(CountOf(deep_sleeps_, member.address), sleeps + 3);
    EXPECT_EQ(CountOf(fallback_boots_, member.address), 0u);
}

TEST_F(ReliableDeepSleepTest, LostAcknowledgementIsRetransmittedAfterAResume) {
    auto nodes = FormNetwork(Topology::kStar);
    ASSERT_FALSE(HasFatalFailure());
    TestNode& manager = *nodes.front();
    TestNode& member = *nodes.back();
    ASSERT_TRUE(WaitUntilReady(nodes, member, manager));

    LoseAcks(manager, member, 1);
    const uint32_t timeout_ms = 2 * superframe_ms_;
    const MessageId id = SendReliable(member, manager, 3, timeout_ms);
    ASSERT_NE(id.source, 0u);
    ASSERT_TRUE(RunUntil(superframe_ms_ * 8, [&]() {
        return !member.delivery_outcomes.empty();
    })) << DescribeNetwork(nodes);

    EXPECT_EQ(acks_lost_, 1u);
    EXPECT_GE(CountOf(sleeps_with_reliable_pending_, member.address), 1u);
    // One retransmission, in the first data slot after the timeout: the same
    // slot as the first attempt, a whole number of superframes later
    ASSERT_EQ(attempt_times_ms_.size(), 2u);
    EXPECT_GE(AttemptDelayMs(1), timeout_ms);
    EXPECT_LE(AttemptDelayMs(1), timeout_ms + superframe_ms_ + SlackMs());
    const uint32_t between = AttemptDelayMs(1) - AttemptDelayMs(0);
    const uint32_t phase = (between + SlackMs()) % superframe_ms_;
    EXPECT_LE(phase, 2 * SlackMs()) << between;
    // The member slept with its whole retry budget before retransmitting,
    // and with one retry less while the second acknowledgement was on its way
    ASSERT_FALSE(saved_states_.empty());
    EXPECT_EQ(SavedRetriesLeft().front(), 3u);
    EXPECT_GE(saved_states_.back().retries_left, 2u);
    EXPECT_EQ(saved_states_.front().current_timeout_ms, timeout_ms);

    ASSERT_EQ(member.delivery_outcomes.size(), 1u);
    const DeliveryResult outcome = member.delivery_outcomes.front();
    EXPECT_EQ(outcome.outcome, Outcome::Delivered);
    EXPECT_EQ(outcome.id, id);
    EXPECT_EQ(outcome.by, manager.address);
    // The round trip of the acknowledged attempt, the retransmission: from
    // its queueing to the acknowledgement, which the manager sends in its
    // own data slot
    EXPECT_GT(outcome.rtt_ms, 0u);
    EXPECT_LE(outcome.rtt_ms, 2 * superframe_ms_ + SlackMs());

    RunUntil(superframe_ms_ * 4, []() { return false; });
    EXPECT_EQ(member.delivery_outcomes.size(), 1u);
    EXPECT_EQ(attempt_times_ms_.size(), 2u);
    // The destination received both attempts and delivered the message once
    EXPECT_EQ(TimesReceived(manager), 1u);
    EXPECT_EQ(CountOf(fallback_boots_, member.address), 0u);
}

TEST_F(ReliableDeepSleepTest, RetriesAreExhaustedAcrossSleeps) {
    auto nodes = FormNetwork(Topology::kStar);
    ASSERT_FALSE(HasFatalFailure());
    TestNode& manager = *nodes.front();
    TestNode& member = *nodes.back();
    ASSERT_TRUE(WaitUntilReady(nodes, member, manager));

    // No acknowledgement ever arrives. The timeout of one hop is three
    // superframes and doubles with every retransmission
    LoseAcks(manager, member, SIZE_MAX);
    const uint32_t timeout_ms = 3 * superframe_ms_;
    const MessageId id = SendReliable(member, manager, 2, 0);
    ASSERT_NE(id.source, 0u);
    ASSERT_TRUE(RunUntil(superframe_ms_ * 26, [&]() {
        return !member.delivery_outcomes.empty();
    })) << DescribeNetwork(nodes);

    ASSERT_EQ(attempt_times_ms_.size(), 3u);
    EXPECT_GE(AttemptDelayMs(1), timeout_ms);
    EXPECT_LE(AttemptDelayMs(1), timeout_ms + superframe_ms_ + SlackMs());
    // A timeout runs from the moment its attempt was queued, which is at
    // most one superframe before the attempt left
    EXPECT_GE(AttemptDelayMs(2), 3 * timeout_ms);
    EXPECT_LE(AttemptDelayMs(2), 3 * timeout_ms + 2 * superframe_ms_);
    const auto failed_after_ms =
        static_cast<uint32_t>(outcome_times_ms_.front() - sent_at_ms_);
    EXPECT_GE(failed_after_ms, 7 * timeout_ms);
    EXPECT_LE(failed_after_ms, 7 * timeout_ms + 3 * superframe_ms_);

    // The member slept through it all, with each retry budget in turn and
    // the timeout doubling
    EXPECT_GE(CountOf(sleeps_with_reliable_pending_, member.address), 15u);
    EXPECT_EQ(SavedRetriesLeft(), (std::vector<uint8_t>{2, 1, 0}));
    ASSERT_FALSE(saved_states_.empty());
    EXPECT_EQ(saved_states_.front().current_timeout_ms, timeout_ms);
    EXPECT_EQ(saved_states_.back().current_timeout_ms, 4 * timeout_ms);

    ASSERT_EQ(member.delivery_outcomes.size(), 1u);
    EXPECT_EQ(member.delivery_outcomes.front().outcome, Outcome::Failed);
    EXPECT_EQ(member.delivery_outcomes.front().id, id);

    RunUntil(superframe_ms_ * 4, []() { return false; });
    EXPECT_EQ(member.delivery_outcomes.size(), 1u);
    EXPECT_EQ(attempt_times_ms_.size(), 3u);
    EXPECT_EQ(TimesReceived(manager), 1u);
    EXPECT_EQ(CountOf(fallback_boots_, member.address), 0u);
    EXPECT_TRUE(IsHealthy(nodes, manager.address, formed_network_id_))
        << DescribeNetwork(nodes);
}

TEST_F(ReliableDeepSleepTest, SleepClockErrorKeepsRetransmissionsOnTime) {
    auto nodes = FormNetwork(Topology::kStar);
    ASSERT_FALSE(HasFatalFailure());
    TestNode& manager = *nodes.front();
    TestNode& member = *nodes.back();

    // A clock that is 0.9 % fast and jumps by 20 ms on every sleep, before
    // the member has learned its error
    sleep_clock_ppm_ = 9000;
    sleep_clock_error_ms_ = 20;
    ASSERT_TRUE(WaitUntilReady(nodes, member, manager));

    LoseAcks(manager, member, 1);
    const uint32_t timeout_ms = 3 * superframe_ms_;
    const MessageId id = SendReliable(member, manager, 3, timeout_ms);
    ASSERT_NE(id.source, 0u);
    ASSERT_TRUE(RunUntil(superframe_ms_ * 8, [&]() {
        return !member.delivery_outcomes.empty();
    })) << DescribeNetwork(nodes);

    // The timeout is measured on the member's clock, which the sleeps put
    // off by at most 1 % of the time slept plus the jumps
    const uint32_t clock_error_ms = timeout_ms / 100 + 3 * 20;
    EXPECT_GE(CountOf(sleeps_with_reliable_pending_, member.address), 2u);
    ASSERT_EQ(attempt_times_ms_.size(), 2u);
    EXPECT_GE(AttemptDelayMs(1) + clock_error_ms, timeout_ms);
    EXPECT_LE(AttemptDelayMs(1),
              timeout_ms + superframe_ms_ + SlackMs() + clock_error_ms);

    ASSERT_EQ(member.delivery_outcomes.size(), 1u);
    const DeliveryResult outcome = member.delivery_outcomes.front();
    EXPECT_EQ(outcome.outcome, Outcome::Delivered);
    EXPECT_EQ(outcome.id, id);
    EXPECT_GT(outcome.rtt_ms, 0u);
    EXPECT_LE(outcome.rtt_ms, 2 * superframe_ms_ + SlackMs() + clock_error_ms);

    RunUntil(superframe_ms_ * 4, []() { return false; });
    EXPECT_EQ(member.delivery_outcomes.size(), 1u);
    EXPECT_EQ(attempt_times_ms_.size(), 2u);
    EXPECT_EQ(TimesReceived(manager), 1u);
    EXPECT_EQ(CountOf(fallback_boots_, member.address), 0u);
}

TEST_F(ReliableDeepSleepTest, LateWakeReportsTheOpenMessageAsFailed) {
    auto nodes = FormNetwork(Topology::kStar);
    ASSERT_FALSE(HasFatalFailure());
    TestNode& manager = *nodes.front();
    TestNode& member = *nodes.back();
    ASSERT_TRUE(WaitUntilReady(nodes, member, manager));

    LoseAcks(manager, member, SIZE_MAX);
    const MessageId id = SendReliable(member, manager, 3, 0);
    ASSERT_NE(id.source, 0u);

    // The member wakes more than a superframe late and has to rejoin
    extra_wake_delay_ms_ = superframe_ms_ * 2;
    ASSERT_TRUE(RunUntil(superframe_ms_ * 6, [&]() {
        return CountOf(fallback_boots_, member.address) > 0;
    })) << DescribeNetwork(nodes);
    extra_wake_delay_ms_ = 0;
    EXPECT_EQ(CountOf(sleeps_with_reliable_pending_, member.address), 1u);

    // It cannot continue the message, and says so once
    ASSERT_EQ(member.delivery_outcomes.size(), 1u);
    EXPECT_EQ(member.delivery_outcomes.front().outcome, Outcome::Failed);
    EXPECT_EQ(member.delivery_outcomes.front().id, id);

    ASSERT_TRUE(WaitForHealthyNetwork(
        nodes, manager.address, RecoveryBudgetMs(nodes), formed_network_id_))
        << DescribeNetwork(nodes);
    Observe();
    EXPECT_EQ(member.delivery_outcomes.size(), 1u);
    EXPECT_EQ(attempt_times_ms_.size(), 1u);
}

TEST_F(ReliableDeepSleepTest, GroupWindowStaysOpenAcrossSleeps) {
    auto nodes = FormNetwork(Topology::kStar);
    ASSERT_FALSE(HasFatalFailure());
    TestNode& manager = *nodes.front();
    TestNode& member = *nodes.back();
    ASSERT_TRUE(
        manager.protocol->GetNetworkServiceForTest()->JoinGroup(kGroup));
    ASSERT_TRUE(WaitUntilReady(nodes, member, manager));

    // One re-flood a superframe after the first, in a window of four
    // superframes: the retry, a round trip across one hop and one more
    sender_ = &member;
    attempt_type_ = MessageType::DATA_GROUP;
    attempts_before_ = 0;
    sent_at_ms_ = VirtualNow();
    const uint32_t window_ms = 4 * superframe_ms_;
    const auto payload = Payload();
    const MessageId id = AsNode(member, [&]() {
        return member.protocol->GetNetworkServiceForTest()->SendGroupReliable(
            kGroup, payload, 1, 0);
    });
    ASSERT_NE(id.source, 0u);
    ASSERT_TRUE(RunUntil(superframe_ms_ * 8, [&]() {
        return CountOutcomes(Outcome::GroupWindowClosed) > 0;
    })) << DescribeNetwork(nodes);

    EXPECT_GE(CountOf(sleeps_with_reliable_pending_, member.address), 3u);
    EXPECT_EQ(attempt_times_ms_.size(), 2u);
    // The manager acknowledged both floods and counts as one responder
    ASSERT_EQ(member.delivery_outcomes.size(), 2u);
    const DeliveryResult delivered = member.delivery_outcomes[0];
    EXPECT_EQ(delivered.outcome, Outcome::Delivered);
    EXPECT_EQ(delivered.id, id);
    EXPECT_EQ(delivered.by, manager.address);
    EXPECT_EQ(delivered.ack_count, 1u);
    const DeliveryResult closed = member.delivery_outcomes[1];
    EXPECT_EQ(closed.outcome, Outcome::GroupWindowClosed);
    EXPECT_EQ(closed.id, id);
    EXPECT_EQ(closed.ack_count, 1u);
    const auto closed_after_ms =
        static_cast<uint32_t>(outcome_times_ms_[1] - sent_at_ms_);
    EXPECT_GE(closed_after_ms, window_ms);
    EXPECT_LE(closed_after_ms, window_ms + superframe_ms_ + SlackMs());
    // The responder was saved with the window once it had answered
    ASSERT_FALSE(saved_states_.empty());
    EXPECT_EQ(saved_states_.back().responders,
              std::vector<AddressType>{manager.address});

    RunUntil(superframe_ms_ * 3, []() { return false; });
    EXPECT_EQ(member.delivery_outcomes.size(), 2u);
    EXPECT_EQ(TimesReceived(manager), 1u);
    EXPECT_EQ(CountOf(fallback_boots_, member.address), 0u);
}

TEST_F(ReliableDeepSleepTest, StateTooBigForTheStoreKeepsTheNodeUp) {
    auto nodes = FormNetwork(Topology::kStar);
    ASSERT_FALSE(HasFatalFailure());
    TestNode& manager = *nodes.front();
    TestNode& member = *nodes.back();
    ASSERT_TRUE(WaitUntilReady(nodes, member, manager));

    // The store has room for the member's state, but not with this message
    LimitedStateStore& store = *stores_.at(member.address);
    LimitStore(member);
    ASSERT_FALSE(HasFatalFailure());
    const size_t sleeps = CountOf(deep_sleeps_, member.address);

    // The message stays open for three superframes: its acknowledgement is
    // lost and the retransmission is acknowledged
    LoseAcks(manager, member, 1);
    const uint32_t timeout_ms = 3 * superframe_ms_;
    const MessageId id =
        SendReliable(member, manager, 3, timeout_ms, BigPayload());
    ASSERT_NE(id.source, 0u);
    bool slept = false;
    bool held_a_snapshot = false;
    ASSERT_TRUE(RunUntil(superframe_ms_ * 8, [&]() {
        slept = slept || IsSleeping(member);
        held_a_snapshot = held_a_snapshot || store.Load().has_value();
        return !member.delivery_outcomes.empty();
    })) << DescribeNetwork(nodes);

    // Every SLEEP run the save was refused and the member stayed up, with
    // nothing written to the store
    EXPECT_FALSE(slept);
    EXPECT_EQ(CountOf(deep_sleeps_, member.address), sleeps);
    EXPECT_EQ(CountOf(sleeps_with_reliable_pending_, member.address), 0u);
    EXPECT_GE(store.GetRejectedSaves(), 2u);
    EXPECT_GT(store.GetLastRejectedSize(), store.GetCapacity());
    EXPECT_FALSE(held_a_snapshot);

    // The message was not dropped: it was retransmitted when due and
    // delivered once
    ASSERT_EQ(attempt_times_ms_.size(), 2u);
    EXPECT_GE(AttemptDelayMs(1), timeout_ms);
    EXPECT_LE(AttemptDelayMs(1), timeout_ms + superframe_ms_ + SlackMs());
    ASSERT_EQ(member.delivery_outcomes.size(), 1u);
    EXPECT_EQ(member.delivery_outcomes.front().outcome, Outcome::Delivered);
    EXPECT_EQ(member.delivery_outcomes.front().id, id);
    EXPECT_EQ(member.delivery_outcomes.front().by, manager.address);

    // With the message closed the state fits again and the member goes back
    // to deep-sleeping every superframe
    const size_t rejected = store.GetRejectedSaves();
    RunUntil(superframe_ms_ * 4, []() { return false; });
    EXPECT_GE(CountOf(deep_sleeps_, member.address), sleeps + 3);
    EXPECT_EQ(store.GetRejectedSaves(), rejected);
    EXPECT_EQ(member.delivery_outcomes.size(), 1u);
    EXPECT_EQ(attempt_times_ms_.size(), 2u);
    EXPECT_EQ(TimesReceived(manager, BigPayload()), 1u);
    EXPECT_EQ(CountOf(fallback_boots_, member.address), 0u);
    EXPECT_TRUE(IsHealthy(nodes, manager.address, formed_network_id_))
        << DescribeNetwork(nodes);
}

TEST_F(ReliableDeepSleepTest, StateTooBigForTheStoreFallsBackToLightSleep) {
    // The members have a prepare-sleep callback, so they can light-sleep
    auto nodes = FormNetwork(Topology::kStar, true);
    ASSERT_FALSE(HasFatalFailure());
    TestNode& manager = *nodes.front();
    TestNode& member = *nodes.back();
    ASSERT_TRUE(WaitUntilReady(nodes, member, manager));

    // While the state fits, the callback is only asked for deep sleeps
    const auto requests_before = SleepRequestsOf(member);
    ASSERT_FALSE(requests_before.empty());
    EXPECT_EQ(std::count(requests_before.begin(), requests_before.end(),
                         power::PowerState::LIGHT_SLEEP),
              0);

    LimitedStateStore& store = *stores_.at(member.address);
    LimitStore(member);
    ASSERT_FALSE(HasFatalFailure());
    const size_t sleeps = CountOf(deep_sleeps_, member.address);

    LoseAcks(manager, member, 1);
    const uint32_t timeout_ms = 3 * superframe_ms_;
    const MessageId id =
        SendReliable(member, manager, 3, timeout_ms, BigPayload());
    ASSERT_NE(id.source, 0u);
    ASSERT_TRUE(RunUntil(superframe_ms_ * 8, [&]() {
        return !member.delivery_outcomes.empty();
    })) << DescribeNetwork(nodes);

    EXPECT_EQ(CountOf(deep_sleeps_, member.address), sleeps);
    EXPECT_EQ(CountOf(sleeps_with_reliable_pending_, member.address), 0u);
    const size_t rejected = store.GetRejectedSaves();
    EXPECT_GE(rejected, 2u);

    // The size is only known when the store refuses the save, after the
    // callback approved the deep sleep. So in each of those SLEEP runs the
    // callback is asked for a deep sleep that does not happen, and then for
    // the light sleep that replaces it
    auto requests = SleepRequestsOf(member);
    requests.erase(
        requests.begin(),
        requests.begin() + static_cast<std::ptrdiff_t>(requests_before.size()));
    ASSERT_EQ(requests.size(), 2 * rejected);
    for (size_t i = 0; i < requests.size(); i += 2) {
        EXPECT_EQ(requests[i], power::PowerState::DEEP_SLEEP) << i;
        EXPECT_EQ(requests[i + 1], power::PowerState::LIGHT_SLEEP) << i;
    }

    // Light-sleeping changed nothing for the message
    ASSERT_EQ(attempt_times_ms_.size(), 2u);
    EXPECT_GE(AttemptDelayMs(1), timeout_ms);
    EXPECT_LE(AttemptDelayMs(1), timeout_ms + superframe_ms_ + SlackMs());
    ASSERT_EQ(member.delivery_outcomes.size(), 1u);
    EXPECT_EQ(member.delivery_outcomes.front().outcome, Outcome::Delivered);
    EXPECT_EQ(member.delivery_outcomes.front().id, id);

    // Afterwards the member deep-sleeps again and is not asked for a light
    // sleep any more
    const size_t requests_after_hold = SleepRequestsOf(member).size();
    RunUntil(superframe_ms_ * 4, []() { return false; });
    EXPECT_GE(CountOf(deep_sleeps_, member.address), sleeps + 3);
    EXPECT_EQ(store.GetRejectedSaves(), rejected);
    auto later = SleepRequestsOf(member);
    later.erase(later.begin(), later.begin() + static_cast<std::ptrdiff_t>(
                                                   requests_after_hold));
    EXPECT_EQ(
        std::count(later.begin(), later.end(), power::PowerState::LIGHT_SLEEP),
        0);
    EXPECT_EQ(TimesReceived(manager, BigPayload()), 1u);
    EXPECT_EQ(CountOf(fallback_boots_, member.address), 0u);
    EXPECT_TRUE(IsHealthy(nodes, manager.address, formed_network_id_))
        << DescribeNetwork(nodes);
}

}  // namespace
}  // namespace test
}  // namespace loramesher
