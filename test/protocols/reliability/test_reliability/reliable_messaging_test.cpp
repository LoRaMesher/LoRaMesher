/**
 * @file reliable_messaging_test.cpp
 * @brief Unit tests for ReliableMessaging driven through a scripted host.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "protocols/lora_mesh/services/reliable_messaging.hpp"
#include "types/messages/loramesher/ack_payload.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {
namespace test {

namespace {

constexpr AddressType kLocal = 0x0001;
constexpr AddressType kPeer = 0x0002;
constexpr AddressType kGroup = 0x8001;
constexpr uint32_t kSuperframeMs = 1000;

}  // namespace

class ReliableMessagingTest : public ::testing::Test {
   protected:
    void SetUp() override {
        messaging_ =
            std::make_unique<ReliableMessaging>(message_cache_, MakeHost());
        messaging_->SetDeliveryCallback(
            [this](const reliability::DeliveryResult& result) {
                outcomes_.push_back(result);
            });
    }

    ReliableMessaging::Host MakeHost() {
        ReliableMessaging::Host host;
        host.node_address = kLocal;
        host.now_ms = [this]() {
            return now_ms_;
        };
        host.enqueue =
            [this](types::protocols::lora_mesh::SlotAllocation::SlotType,
                   std::unique_ptr<BaseMessage>) {
                if (!queue_accepts_) {
                    return Result(LoraMesherErrorCode::kQueueFull,
                                  "Queue full");
                }
                ++enqueued_;
                return Result::Success();
            };
        host.find_next_hop = [](AddressType dest) {
            return dest;
        };
        host.forward_data_message = [](const DataMessage&) {
            return Result::Success();
        };
        host.deliver_to_app = [](AddressType, uint8_t, AddressType, uint8_t,
                                 std::span<const uint8_t>) {
        };
        host.in_operational_state = []() {
            return true;
        };
        host.max_hops = []() -> uint8_t {
            return 4;
        };
        host.max_packet_size = []() -> uint16_t {
            return 255;
        };
        host.superframe_duration = []() -> uint32_t {
            return kSuperframeMs;
        };
        host.network_depth = [this]() -> uint8_t {
            return network_depth_;
        };
        return host;
    }

    /// Number of GroupWindowClosed outcomes reported so far.
    size_t ClosedWindows() const {
        return static_cast<size_t>(
            std::count_if(outcomes_.begin(), outcomes_.end(),
                          [](const reliability::DeliveryResult& result) {
                              return result.outcome ==
                                     reliability::Outcome::GroupWindowClosed;
                          }));
    }

    /// Advance the clock superframe by superframe, running the timers.
    void RunFor(uint32_t duration_ms) {
        for (uint32_t elapsed = 0; elapsed < duration_ms;
             elapsed += kSuperframeMs) {
            now_ms_ += kSuperframeMs;
            messaging_->ProcessReliableTimers();
        }
    }

    MessageCache message_cache_;
    std::unique_ptr<ReliableMessaging> messaging_;
    std::vector<reliability::DeliveryResult> outcomes_;
    uint32_t now_ms_ = 1000;
    bool queue_accepts_ = true;
    int enqueued_ = 0;
    uint8_t network_depth_ = 1;
    const std::vector<uint8_t> payload_ = {1, 2, 3};
};

/**
 * @brief A zero window is derived from the network: a round trip across its
 *        depth plus one superframe.
 */
TEST_F(ReliableMessagingTest, DerivedGroupWindowCoversARoundTrip) {
    network_depth_ = 1;
    ASSERT_NE(messaging_->SendGroupReliable(kGroup, payload_, 0, 0).source, 0u);

    RunFor(2 * kSuperframeMs);
    EXPECT_EQ(ClosedWindows(), 0u);
    RunFor(kSuperframeMs);
    EXPECT_EQ(ClosedWindows(), 1u);
}

TEST_F(ReliableMessagingTest, DerivedGroupWindowScalesWithNetworkDepth) {
    network_depth_ = 3;
    ASSERT_NE(messaging_->SendGroupReliable(kGroup, payload_, 0, 0).source, 0u);

    RunFor(6 * kSuperframeMs);
    EXPECT_EQ(ClosedWindows(), 0u);
    RunFor(kSuperframeMs);
    EXPECT_EQ(ClosedWindows(), 1u);
}

TEST_F(ReliableMessagingTest, DerivedGroupWindowTreatsUnknownDepthAsOneHop) {
    network_depth_ = 0;
    ASSERT_NE(messaging_->SendGroupReliable(kGroup, payload_, 0, 0).source, 0u);

    RunFor(2 * kSuperframeMs);
    EXPECT_EQ(ClosedWindows(), 0u);
    RunFor(kSuperframeMs);
    EXPECT_EQ(ClosedWindows(), 1u);
}

/**
 * @brief Each retry adds one superframe, so the last attempt still has a
 *        full round trip before the window closes.
 */
TEST_F(ReliableMessagingTest, DerivedGroupWindowAddsASuperframePerRetry) {
    network_depth_ = 1;
    ASSERT_NE(messaging_->SendGroupReliable(kGroup, payload_, 2, 0).source, 0u);

    RunFor(4 * kSuperframeMs);
    EXPECT_EQ(ClosedWindows(), 0u);
    EXPECT_EQ(enqueued_, 3);
    RunFor(kSuperframeMs);
    EXPECT_EQ(ClosedWindows(), 1u);
}

TEST_F(ReliableMessagingTest, ExplicitGroupWindowIsRespected) {
    network_depth_ = 1;
    ASSERT_NE(messaging_->SendGroupReliable(kGroup, payload_, 0, 5000).source,
              0u);

    RunFor(4 * kSuperframeMs);
    EXPECT_EQ(ClosedWindows(), 0u);
    RunFor(kSuperframeMs);
    EXPECT_EQ(ClosedWindows(), 1u);
}

/**
 * @brief A group send whose attempts can never be queued fails and releases
 *        its acknowledgement window, so later group sends still close.
 */
TEST_F(ReliableMessagingTest, FailedGroupSendReleasesItsWindow) {
    constexpr uint32_t kWindowMs = 60000;
    queue_accepts_ = false;
    for (size_t i = 0; i < reliability::ReliableDelivery::kMaxPending; ++i) {
        ASSERT_NE(messaging_->SendGroupReliable(kGroup, payload_, 0, kWindowMs)
                      .source,
                  0u);
    }
    RunFor(20 * kSuperframeMs);
    ASSERT_EQ(messaging_->GetReliablePendingCount(), 0u);

    queue_accepts_ = true;
    outcomes_.clear();
    const auto id = messaging_->SendGroupReliable(kGroup, payload_, 0, 5000);
    ASSERT_NE(id.source, 0u);
    RunFor(10 * kSuperframeMs);

    EXPECT_EQ(messaging_->GetReliablePendingCount(), 0u);
    ASSERT_EQ(outcomes_.size(), 1u);
    EXPECT_EQ(outcomes_[0].outcome, reliability::Outcome::GroupWindowClosed);
}

TEST_F(ReliableMessagingTest, SendReliableRejectsNonUnicastDestinations) {
    for (AddressType destination :
         {kGroup, kBroadcastAddress, AddressType{0}}) {
        EXPECT_EQ(messaging_->SendReliable(destination, payload_, 0, 0).source,
                  0u)
            << std::hex << destination;
    }
    EXPECT_EQ(messaging_->GetReliablePendingCount(), 0u);
    EXPECT_EQ(enqueued_, 0);
}

TEST_F(ReliableMessagingTest, ResumedNodeContinuesItsSequenceStreams) {
    const uint8_t first = messaging_->SendReliable(kPeer, payload_, 0, 0).seq;
    const uint8_t group_first =
        messaging_->SendGroupReliable(kGroup, payload_, 0, 5000).seq;
    ASSERT_TRUE(messaging_->AcceptReliable(
        kPeer, reliability::StreamKind::kUnicast, 40, 1234));

    storage::ResumeSnapshot snapshot;
    messaging_->CaptureResumeState(snapshot);
    ASSERT_EQ(snapshot.sequence_streams.size(), 1u);
    EXPECT_EQ(
        snapshot.sequence_streams[0],
        (storage::SequenceStream{kPeer, static_cast<uint8_t>(first + 1)}));
    EXPECT_EQ(snapshot.group_next_sequence,
              static_cast<uint8_t>(group_first + 1));
    ASSERT_EQ(snapshot.delivery_streams.size(), 1u);

    MessageCache cache;
    ReliableMessaging resumed(cache, MakeHost());
    ASSERT_TRUE(resumed.ApplyResumeState(snapshot));

    EXPECT_EQ(resumed.SendReliable(kPeer, payload_, 0, 0).seq,
              static_cast<uint8_t>(first + 1));
    EXPECT_EQ(resumed.SendGroupReliable(kGroup, payload_, 0, 5000).seq,
              static_cast<uint8_t>(group_first + 1));
    EXPECT_FALSE(resumed.AcceptReliable(
        kPeer, reliability::StreamKind::kUnicast, 40, 1234));
    EXPECT_TRUE(resumed.AcceptReliable(kPeer, reliability::StreamKind::kUnicast,
                                       41, 1300));
}

TEST_F(ReliableMessagingTest, FreshNodeHasNoGroupStream) {
    storage::ResumeSnapshot snapshot;
    messaging_->CaptureResumeState(snapshot);
    EXPECT_TRUE(snapshot.sequence_streams.empty());
    EXPECT_FALSE(snapshot.group_next_sequence.has_value());
    EXPECT_TRUE(snapshot.delivery_streams.empty());
}

TEST_F(ReliableMessagingTest, InvalidResumeStateIsRefused) {
    storage::ResumeSnapshot snapshot;
    snapshot.sequence_streams = {{kPeer, 1}, {kPeer, 2}};
    EXPECT_FALSE(messaging_->ApplyResumeState(snapshot));

    snapshot.sequence_streams = {{kBroadcastAddress, 1}};
    EXPECT_FALSE(messaging_->ApplyResumeState(snapshot));

    snapshot.sequence_streams.clear();
    snapshot.delivery_streams = {{kPeer, false, 1, 0, 0}};
    EXPECT_FALSE(messaging_->ApplyResumeState(snapshot));
}

TEST_F(ReliableMessagingTest, OpenMessagesAreSavedWithTheirWindows) {
    const auto unicast = messaging_->SendReliable(kPeer, payload_, 2, 0);
    now_ms_ += 100;
    const auto group = messaging_->SendGroupReliable(kGroup, payload_, 1, 5000);
    ASSERT_NE(unicast.source, 0u);
    ASSERT_NE(group.source, 0u);

    storage::ResumeSnapshot snapshot;
    messaging_->CaptureResumeState(snapshot);
    ASSERT_EQ(snapshot.pending_reliable.size(), 2u);
    const auto& saved_unicast = snapshot.pending_reliable[0];
    EXPECT_EQ(saved_unicast.destination, kPeer);
    EXPECT_EQ(saved_unicast.sequence, unicast.seq);
    EXPECT_EQ(saved_unicast.payload, payload_);
    EXPECT_EQ(saved_unicast.retries_left, 2u);
    EXPECT_EQ(saved_unicast.window_deadline_ms, 0u);
    const auto& saved_group = snapshot.pending_reliable[1];
    EXPECT_EQ(saved_group.destination, kGroup);
    EXPECT_EQ(saved_group.sequence, group.seq);
    EXPECT_EQ(saved_group.window_deadline_ms, now_ms_ + 5000);
}

TEST_F(ReliableMessagingTest, ResumedNodeCompletesItsOpenMessages) {
    const auto unicast = messaging_->SendReliable(kPeer, payload_, 2, 3000);
    const auto group = messaging_->SendGroupReliable(kGroup, payload_, 0, 5000);
    const uint32_t sent_at_ms = now_ms_;
    storage::ResumeSnapshot snapshot;
    messaging_->CaptureResumeState(snapshot);
    const int enqueued_before = enqueued_;

    // The node resets and its clock continues
    now_ms_ += 2000;
    outcomes_.clear();
    messaging_ =
        std::make_unique<ReliableMessaging>(message_cache_, MakeHost());
    messaging_->SetDeliveryCallback(
        [this](const reliability::DeliveryResult& result) {
            outcomes_.push_back(result);
        });
    ASSERT_TRUE(messaging_->ApplyResumeState(snapshot));
    messaging_->ResumeOpenMessages(snapshot);
    EXPECT_EQ(messaging_->GetReliablePendingCount(), 2u);
    messaging_->ProcessReliableTimers();
    EXPECT_EQ(enqueued_, enqueued_before);
    EXPECT_TRUE(outcomes_.empty());

    // The acknowledgements of both messages arrive after the reset
    for (const bool was_group : {false, true}) {
        AckPayload ack;
        ack.acked_seq = was_group ? group.seq : unicast.seq;
        ack.flags = was_group ? AckPayload::kFlagWasGroup : 0;
        ack.echo_timestamp = sent_at_ms;
        const auto bytes = ack.Serialize();
        auto message = DataMessage::Create(
            kLocal, kPeer, kLocal,
            std::vector<uint8_t>(bytes.begin(), bytes.end()), 5, 0,
            MessageType::ACK);
        ASSERT_TRUE(message.has_value());
        ASSERT_TRUE(messaging_->ProcessAckMessage(message->ToBaseMessage()));
    }
    ASSERT_EQ(outcomes_.size(), 2u);
    for (const auto& outcome : outcomes_) {
        EXPECT_EQ(outcome.outcome, reliability::Outcome::Delivered);
        EXPECT_EQ(outcome.by, kPeer);
        EXPECT_EQ(outcome.rtt_ms, 2000u);
    }
    EXPECT_EQ(outcomes_[0].id, unicast);
    EXPECT_EQ(outcomes_[1].id, group);

    // The group window closes at the deadline it had before the reset
    outcomes_.clear();
    RunFor(2 * kSuperframeMs);
    EXPECT_TRUE(outcomes_.empty());
    RunFor(kSuperframeMs);
    ASSERT_EQ(outcomes_.size(), 1u);
    EXPECT_EQ(outcomes_[0].outcome, reliability::Outcome::GroupWindowClosed);
    EXPECT_EQ(outcomes_[0].ack_count, 1u);
    EXPECT_EQ(messaging_->GetReliablePendingCount(), 0u);

    // The window is free again
    EXPECT_NE(messaging_->SendGroupReliable(kGroup, payload_, 0, 5000).source,
              0u);
}

TEST_F(ReliableMessagingTest, ResumedNodeRetransmitsWhenDue) {
    ASSERT_NE(messaging_->SendReliable(kPeer, payload_, 1, 3000).source, 0u);
    storage::ResumeSnapshot snapshot;
    messaging_->CaptureResumeState(snapshot);

    messaging_ =
        std::make_unique<ReliableMessaging>(message_cache_, MakeHost());
    messaging_->SetDeliveryCallback(
        [this](const reliability::DeliveryResult& result) {
            outcomes_.push_back(result);
        });
    ASSERT_TRUE(messaging_->ApplyResumeState(snapshot));
    messaging_->ResumeOpenMessages(snapshot);

    RunFor(2 * kSuperframeMs);
    EXPECT_EQ(enqueued_, 1);
    RunFor(kSuperframeMs);
    EXPECT_EQ(enqueued_, 2);
    EXPECT_TRUE(outcomes_.empty());
    RunFor(3 * kSuperframeMs);
    EXPECT_EQ(enqueued_, 2);
    ASSERT_EQ(outcomes_.size(), 1u);
    EXPECT_EQ(outcomes_[0].outcome, reliability::Outcome::Failed);
}

TEST_F(ReliableMessagingTest, AbandonedOpenMessagesAreReportedOnce) {
    const auto unicast = messaging_->SendReliable(kPeer, payload_, 2, 0);
    const auto group = messaging_->SendGroupReliable(kGroup, payload_, 0, 5000);
    storage::ResumeSnapshot snapshot;
    messaging_->CaptureResumeState(snapshot);
    snapshot.pending_reliable[1].responders = {kPeer, 0x0003};

    outcomes_.clear();
    messaging_ =
        std::make_unique<ReliableMessaging>(message_cache_, MakeHost());
    messaging_->SetDeliveryCallback(
        [this](const reliability::DeliveryResult& result) {
            outcomes_.push_back(result);
        });
    messaging_->AbandonOpenMessages(snapshot);

    ASSERT_EQ(outcomes_.size(), 2u);
    EXPECT_EQ(outcomes_[0].id, unicast);
    EXPECT_EQ(outcomes_[0].outcome, reliability::Outcome::Failed);
    EXPECT_EQ(outcomes_[1].id, group);
    EXPECT_EQ(outcomes_[1].outcome, reliability::Outcome::GroupWindowClosed);
    EXPECT_EQ(outcomes_[1].ack_count, 2u);
    EXPECT_EQ(messaging_->GetReliablePendingCount(), 0u);
    RunFor(10 * kSuperframeMs);
    EXPECT_EQ(outcomes_.size(), 2u);
}

TEST_F(ReliableMessagingTest, OpenMessageThatCannotBeTrackedIsReported) {
    const auto first = messaging_->SendReliable(kPeer, payload_, 2, 0);
    ASSERT_NE(first.source, 0u);
    storage::ResumeSnapshot snapshot;
    messaging_->CaptureResumeState(snapshot);
    outcomes_.clear();

    // The message is still tracked: restoring it again reports the copy
    messaging_->ResumeOpenMessages(snapshot);
    ASSERT_EQ(outcomes_.size(), 1u);
    EXPECT_EQ(outcomes_[0].outcome, reliability::Outcome::Failed);
    EXPECT_EQ(messaging_->GetReliablePendingCount(), 1u);
}

}  // namespace test
}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher
