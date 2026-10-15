/**
 * @file reliable_messaging_test.cpp
 * @brief Unit tests for ReliableMessaging driven through a scripted host.
 */

#include <gtest/gtest.h>

#include <vector>

#include "protocols/lora_mesh/services/reliable_messaging.hpp"

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
        return host;
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
    const std::vector<uint8_t> payload_ = {1, 2, 3};
};

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

TEST_F(ReliableMessagingTest, IdleOnlyWithoutTrackedMessages) {
    EXPECT_TRUE(messaging_->IsIdle());
    messaging_->SendReliable(kPeer, payload_, 0, 0);
    EXPECT_FALSE(messaging_->IsIdle());
    messaging_->Reset();
    EXPECT_TRUE(messaging_->IsIdle());

    messaging_->SendGroupReliable(kGroup, payload_, 0, 5000);
    EXPECT_FALSE(messaging_->IsIdle());
    RunFor(20 * kSuperframeMs);
    EXPECT_TRUE(messaging_->IsIdle());
}

}  // namespace test
}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher
